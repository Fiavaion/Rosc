#include "UEcineShotAssistant.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineEditorSettings.h"
#include "UEcineExposure.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineOptics.h"
#include "UEcineRig.h"
#include "UEcineShotEditing.h"

#define LOCTEXT_NAMESPACE "UEcineShotAssistant"

namespace
{
	/** How far from the camera something can be and still be told to the assistant. */
	constexpr double SubjectReachCm = 15000.0;
	/** Things larger than this are the set, not something in it: floors, skies, landscapes. */
	constexpr double SubjectLargestCm = 5000.0;
	constexpr int32 SubjectsTold = 40;

	/** Something in the level a shot can be about. */
	struct FShotSubject
	{
		AActor* Actor = nullptr;
		FString Name;
		/** The middle of it. */
		FVector Centre = FVector::ZeroVector;
		FVector Size = FVector::ZeroVector;
		double DistanceCm = 0.0;
	};

	/** The things near the camera a shot can be about, nearest first. */
	TArray<FShotSubject> FindShotSubjects(const ACineCameraActor* Camera)
	{
		TArray<FShotSubject> Subjects;
		const FVector CameraLocation = Camera->GetCineCameraComponent()->GetComponentLocation();
		for (TActorIterator<AActor> It(Camera->GetWorld()); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor == Camera || Actor->IsA<AUEcineRig>() || Actor->IsA<ACameraActor>() || Actor->IsHiddenEd() || Actor->IsEditorOnly())
			{
				continue;
			}
			// What can be seen has a shape; lights, volumes and the like have none worth the name.
			const FBox Bounds = Actor->GetComponentsBoundingBox(false);
			if (!Bounds.IsValid || Bounds.GetSize().GetMax() < 1.0 || Bounds.GetSize().GetMax() > SubjectLargestCm)
			{
				continue;
			}
			FShotSubject Subject;
			Subject.Actor = Actor;
			Subject.Name = Actor->GetActorLabel();
			Subject.Centre = Bounds.GetCenter();
			Subject.Size = Bounds.GetSize();
			Subject.DistanceCm = FVector::Dist(CameraLocation, Subject.Centre);
			if (Subject.DistanceCm <= SubjectReachCm)
			{
				Subjects.Add(Subject);
			}
		}
		Subjects.Sort([](const FShotSubject& A, const FShotSubject& B) { return A.DistanceCm < B.DistanceCm; });
		Subjects.SetNum(FMath::Min(Subjects.Num(), SubjectsTold));
		return Subjects;
	}

	/** The nearest thing of that name, or null. */
	const FShotSubject* FindShotSubject(TConstArrayView<FShotSubject> Subjects, const FString& Name)
	{
		return Subjects.FindByPredicate([&Name](const FShotSubject& Subject) { return Subject.Name.Equals(Name, ESearchCase::IgnoreCase); });
	}

	/** Metres to two decimals, as a crew would say them. */
	TSharedRef<FJsonValue> ToMetres(double Centimetres)
	{
		return UEcine::ShotPlan::MakeNumber(Centimetres / 100.0, 2);
	}

	/**
	 * Makes a camera or exposure step's change to a package.
	 * @return false with the reason in OutProblem when the step names equipment that is not there.
	 */
	bool EditPackageForStep(const FUEcineShotStep& Step, FUEcineCameraPackage& Package, FText& OutProblem)
	{
		const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();
		switch (Step.Kind)
		{
		case EUEcineShotStep::ChooseCamera:
		{
			const FUEcineCameraBody* Body = Database.FindBody(Step.BodyId);
			if (Body == nullptr)
			{
				OutProblem = FText::Format(LOCTEXT("NoBody", "The camera database has no body \"{0}\"."), FText::FromString(Step.BodyId));
				return false;
			}
			if (Body->FindFormat(Step.FormatId) == nullptr)
			{
				OutProblem = FText::Format(LOCTEXT("NoFormat", "{0} has no recording format \"{1}\"."), FText::FromString(Body->GetDisplayName()), FText::FromString(Step.FormatId));
				return false;
			}
			if (Package.BodyId != Body->Id)
			{
				Package.SelectBody(*Body);
			}
			Package.FormatId = Step.FormatId;
			return true;
		}
		case EUEcineShotStep::ChooseLens:
		{
			const FUEcineLensKit* Kit = Database.FindLensKit(Step.LensKitId);
			if (Kit == nullptr || Kit->Lenses.IsEmpty())
			{
				OutProblem = FText::Format(LOCTEXT("NoKit", "The camera database has no lens kit \"{0}\"."), FText::FromString(Step.LensKitId));
				return false;
			}
			Package.FocalLengthMm = static_cast<float>(Step.FocalLengthMm);
			Package.SelectLensKit(*Kit);
			return true;
		}
		case EUEcineShotStep::SetExposure:
			Package.TStop = static_cast<float>(Step.TStop);
			Package.Iso = Step.Iso;
			Package.ShutterAngleDeg = static_cast<float>(Step.ShutterAngleDeg);
			// A stop of neutral density is 0.3 of optical density, as filters are sold.
			Package.NdDensity = static_cast<float>(Step.NdStops * 0.3);
			// The lens that is on holds the stop to what its iris can do.
			if (const FUEcineLensKit* Kit = Database.FindLensKit(Package.LensKitId); Kit != nullptr && Kit->Lenses.IsValidIndex(Package.LensIndex))
			{
				Package.SelectLens(*Kit, Package.LensIndex);
			}
			return true;
		default:
			checkNoEntry();
			return false;
		}
	}

	/** Where a move takes the rig: the change added on the axes the support has. */
	FUEcineRigPose AddMove(const FUEcineRigSpec& Spec, const FUEcineRigPose& From, const FUEcineRigPose& Change)
	{
		FUEcineRigPose To = From;
		for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
		{
			const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
			if (Spec.GetAxis(Axis).bUsed)
			{
				To.Set(Axis, From.Get(Axis) + Change.Get(Axis));
			}
		}
		return To;
	}

	/** "+1.5 m", "-12°": how far an axis goes, with its direction. */
	FText FormatAxisChange(EUEcineRigAxis Axis, double Change)
	{
		const FText Amount = UEcine::Rig::FormatAxisValue(Axis, FMath::Abs(Change), UUEcineEditorSettings::GetDistanceUnits());
		return FText::Format(Change < 0.0 ? LOCTEXT("ChangeDown", "-{0}") : LOCTEXT("ChangeUp", "+{0}"), Amount);
	}

	FText FormatSeconds(double Seconds)
	{
		return FText::Format(LOCTEXT("Seconds", "{0} s"), UEcine::Units::FormatNumber(Seconds, 1));
	}

	/** Where things stand part of the way through a plan, worked out without changing anything. */
	struct FPlanProgress
	{
		bool bHasPackage = false;
		FUEcineCameraPackage Package;
		bool bOnSupport = false;
		FUEcineRigSpec Spec;
		FUEcineRigPose Pose;
		int32 Marks = 0;
		bool bMoved = false;
	};

	FPlanProgress GetPlanStart(const ACineCameraActor* Camera)
	{
		FPlanProgress Progress;
		if (const UUEcineCameraPackageComponent* Component = UUEcineCameraPackageComponent::Find(Camera))
		{
			Progress.bHasPackage = true;
			Progress.Package = Component->Package;
		}
		else
		{
			const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
			Progress.Package = UEcine::CameraEditing::MakeDefaultPackage(Lens->CurrentFocalLength, Lens->CurrentAperture);
		}
		if (const AUEcineRig* Rig = UEcine::GripEditing::FindRig(Camera))
		{
			Progress.bOnSupport = true;
			Progress.Spec = Rig->GetSpec();
			Progress.Pose = Rig->GetPose();
			Progress.Marks = Rig->Marks.Num();
		}
		return Progress;
	}

	/** Reads one step against where things stand, and moves on to where they stand after it. */
	UEcine::ShotAssistant::FPlanLine ReviewStep(const FUEcineShotStep& Step, const ACineCameraActor* Camera, TConstArrayView<FShotSubject> Subjects, FPlanProgress& Progress)
	{
		using namespace UEcine;
		const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();
		const EUEcineDistanceUnits DistanceUnits = UUEcineEditorSettings::GetDistanceUnits();
		ShotAssistant::FPlanLine Line;

		// A camera step is tried on a copy of the package. What the package warns of after the step
		// and did not before is the step's doing, and is told on its line.
		const auto TryPackage = [&]
		{
			FUEcineResolvedCamera Before;
			FText NotResolved;
			ResolveCameraPackage(Database, Progress.Package, Before, NotResolved);

			FUEcineCameraPackage Edited = Progress.Package;
			FUEcineResolvedCamera Resolved;
			if (EditPackageForStep(Step, Edited, Line.Problem) && ResolveCameraPackage(Database, Edited, Resolved, Line.Problem))
			{
				Progress.Package = Edited;
				for (const FText& Warning : Resolved.Warnings)
				{
					if (!Before.Warnings.ContainsByPredicate([&Warning](const FText& Old) { return Old.EqualTo(Warning); }))
					{
						Line.Warnings.Add(Warning);
					}
				}
			}
			return Resolved;
		};

		switch (Step.Kind)
		{
		case EUEcineShotStep::ChooseCamera:
		{
			TryPackage();
			const FUEcineCameraBody* Body = Database.FindBody(Step.BodyId);
			const FUEcineRecordingFormat* Format = Body != nullptr ? Body->FindFormat(Step.FormatId) : nullptr;
			Line.Text = Format != nullptr
				? FText::Format(LOCTEXT("LineCamera", "Camera: {0}, {1}"), FText::FromString(Body->GetDisplayName()), FText::FromString(Format->Name))
				: LOCTEXT("LineCameraUnknown", "Camera: not in the camera database");
			break;
		}

		case EUEcineShotStep::ChooseLens:
		{
			const FUEcineResolvedCamera Resolved = TryPackage();
			const FUEcineLensKit* Kit = Database.FindLensKit(Step.LensKitId);
			if (Kit == nullptr || !Line.Problem.IsEmpty())
			{
				Line.Text = LOCTEXT("LineLensUnknown", "Lens: not in the camera database");
				break;
			}
			Line.Text = FText::Format(LOCTEXT("LineLens", "Lens: {0} {1}"), FText::FromString(Kit->GetDisplayName()), FText::FromString(Kit->Lenses[Progress.Package.LensIndex].GetDisplayName()));
			if (!FMath::IsNearlyEqual(Resolved.FocalLengthMm, Step.FocalLengthMm, Step.FocalLengthMm * 0.02))
			{
				Line.Warnings.Add(FText::Format(LOCTEXT("NearestLens", "The kit has no {0} mm. The {1} mm is the nearest."),
					Units::FormatNumber(Step.FocalLengthMm, 1), Units::FormatNumber(Resolved.FocalLengthMm, 1)));
			}
			break;
		}

		case EUEcineShotStep::SetExposure:
		{
			const FUEcineResolvedCamera Resolved = TryPackage();
			Line.Text = FText::Format(LOCTEXT("LineExposure", "Exposure: T{0}, ISO {1}, {2}° shutter, {3}"),
				Units::FormatNumber(Step.TStop, 1), FText::AsNumber(Step.Iso, &FNumberFormattingOptions::DefaultNoGrouping()), Units::FormatNumber(Step.ShutterAngleDeg, 1),
				Step.NdStops > 0.0 ? FText::Format(LOCTEXT("NdOn", "ND {0}"), Units::FormatNumber(Step.NdStops * 0.3, 1)) : LOCTEXT("NdOff", "no ND"));
			if (Line.Problem.IsEmpty() && !Exposure::IsSameSetting(Resolved.TStop, Step.TStop))
			{
				Line.Warnings.Add(FText::Format(LOCTEXT("TStopLimited", "This lens cannot be set to T{0}. It goes to T{1}."), Units::FormatNumber(Step.TStop, 1), Units::FormatNumber(Resolved.TStop, 1)));
			}
			break;
		}

		case EUEcineShotStep::PutOnSupport:
		{
			const FUEcineRigPreset* Preset = Rig::FindPreset(Step.SupportId);
			if (Preset == nullptr)
			{
				Line.Text = LOCTEXT("LineSupportUnknown", "Support: not one UEcine has");
				Line.Problem = FText::Format(LOCTEXT("NoSupport", "There is no support \"{0}\"."), FText::FromString(Step.SupportId));
				break;
			}
			Line.Text = FText::Format(LOCTEXT("LineSupport", "Support: {0}"), Preset->Name);
			// Marks are positions of the old rig's axes and are dropped with it.
			if (!Progress.bOnSupport || Progress.Spec.Type != Preset->Spec.Type)
			{
				Progress.Marks = 0;
			}
			Progress.bOnSupport = true;
			Progress.Spec = Preset->Spec;
			Progress.Pose = GripEditing::GetPoseOnRig(Camera, *Preset);
			break;
		}

		case EUEcineShotStep::MoveToMark:
		{
			if (!Progress.bOnSupport)
			{
				Line.Text = FText::Format(LOCTEXT("LineMoveNoSupport", "Move over {0}"), FormatSeconds(Step.Timing.DurationSeconds));
				Line.Problem = LOCTEXT("MoveNeedsSupport", "The camera is on no support to move. Put it on one first.");
				break;
			}
			TArray<FText> Parts;
			TArray<FText> AxesNotThere;
			for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
			{
				const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
				const double Change = Step.Change.Get(Axis);
				if (FMath::IsNearlyZero(Change, 0.01))
				{
					continue;
				}
				if (Progress.Spec.GetAxis(Axis).bUsed)
				{
					Parts.Add(FText::Format(LOCTEXT("MovePart", "{0} {1}"), Rig::GetAxisName(Progress.Spec.Type, Axis), FormatAxisChange(Axis, Change)));
				}
				else
				{
					AxesNotThere.Add(Rig::GetAxisName(Progress.Spec.Type, Axis));
				}
			}
			Line.Text = Parts.IsEmpty()
				? FText::Format(LOCTEXT("LineHold", "Hold for {0}"), FormatSeconds(Step.Timing.DurationSeconds))
				: FText::Format(LOCTEXT("LineMove", "Move over {0}: {1}"), FormatSeconds(Step.Timing.DurationSeconds), FText::Join(LOCTEXT("PartsComma", ", "), Parts));
			if (!AxesNotThere.IsEmpty())
			{
				Line.Warnings.Add(FText::Format(LOCTEXT("AxesNotThere", "The support on the camera ({0}) has no {1} axis. That part of the move is left out."),
					Rig::GetTypeName(Progress.Spec.Type), FText::Join(LOCTEXT("AxesOr", " or "), AxesNotThere)));
			}

			const FUEcineRigPose Target = AddMove(Progress.Spec, Progress.Pose, Step.Change);
			for (const FUEcineMoveFinding& Finding : Move::MostLimiting(Move::Check(Progress.Spec, Progress.Pose, Target, Step.Timing)))
			{
				Line.Warnings.Add(Move::Describe(Finding, Progress.Spec.Type, DistanceUnits));
			}
			// The first move of a plan starts a new set of marks from where the rig stands.
			Progress.Marks = Progress.bMoved ? Progress.Marks + 1 : 2;
			Progress.bMoved = true;
			Progress.Pose = Target;
			break;
		}

		case EUEcineShotStep::FocusOn:
		{
			// Named as it is in the level, whatever the case it was asked for in.
			const FShotSubject* Subject = FindShotSubject(Subjects, Step.Subject);
			Line.Text = FText::Format(Step.bFollow ? LOCTEXT("LineFollow", "Focus: follow {0}") : LOCTEXT("LineFocusOn", "Focus: on {0}"), FText::FromString(Subject != nullptr ? Subject->Name : Step.Subject));
			if (Subject == nullptr)
			{
				Line.Problem = FText::Format(LOCTEXT("NoSubject", "Nothing near the camera is called \"{0}\"."), FText::FromString(Step.Subject));
			}
			break;
		}

		case EUEcineShotStep::FocusAt:
			Line.Text = FText::Format(LOCTEXT("LineFocusAt", "Focus: {0}"), Units::FormatDistance(Step.FocusDistanceCm * 10.0, DistanceUnits));
			break;

		case EUEcineShotStep::AddShot:
		{
			TArray<FText> Parts;
			if (!Step.Scene.IsEmpty())
			{
				Parts.Add(FText::Format(LOCTEXT("ShotScene", "scene {0}"), FText::FromString(Step.Scene)));
			}
			if (Step.Size != EUEcineShotSize::Unset)
			{
				Parts.Add(Shots::GetShotSizeName(Step.Size).ToLower());
			}
			if (!Step.Move.IsEmpty())
			{
				Parts.Add(FText::FromString(Step.Move));
			}
			Line.Text = Parts.IsEmpty() ? LOCTEXT("LineShotBare", "New shot") : FText::Format(LOCTEXT("LineShot", "New shot: {0}"), FText::Join(LOCTEXT("ShotComma", ", "), Parts));
			break;
		}

		case EUEcineShotStep::BakeMove:
			Line.Text = LOCTEXT("LineBake", "Bake the move into the sequence");
			if (!Progress.bOnSupport || Progress.Marks < 2)
			{
				Line.Problem = LOCTEXT("NothingToBake", "There is no move to bake: the rig needs two marks first.");
			}
			break;
		}
		return Line;
	}

	TSharedRef<FJsonObject> DescribeCamera(const ACineCameraActor* Camera)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Camera->GetActorLabel());

		const FPlanProgress Start = GetPlanStart(Camera);
		const FUEcineCameraPackage& Package = Start.Package;
		Object->SetStringField(TEXT("bodyId"), Package.BodyId);
		Object->SetStringField(TEXT("formatId"), Package.FormatId);
		Object->SetStringField(TEXT("lensKitId"), Package.LensKitId);
		FUEcineResolvedCamera Resolved;
		FText Error;
		if (UEcine::ResolveCameraPackage(FUEcineCameraDatabase::Get(), Package, Resolved, Error))
		{
			Object->SetStringField(TEXT("lens"), FUEcineCameraDatabase::Get().FindLensKit(Package.LensKitId)->Lenses[Package.LensIndex].GetDisplayName());
			Object->SetField(TEXT("focalLengthMm"), UEcine::ShotPlan::MakeNumber(Resolved.FocalLengthMm, 1));
			Object->SetField(TEXT("horizontalFieldOfViewDeg"), UEcine::ShotPlan::MakeNumber(UEcine::Optics::HorizontalFieldOfViewDeg(Resolved.SensorWidthMm, Resolved.FocalLengthMm, Resolved.Squeeze), 1));
		}
		Object->SetField(TEXT("tStop"), UEcine::ShotPlan::MakeNumber(Package.TStop, 1));
		Object->SetNumberField(TEXT("iso"), Package.Iso);
		Object->SetField(TEXT("shutterAngleDeg"), UEcine::ShotPlan::MakeNumber(Package.ShutterAngleDeg, 1));
		Object->SetField(TEXT("ndStops"), UEcine::ShotPlan::MakeNumber(UEcine::Exposure::NdStops(Package.NdDensity), 1));
		Object->SetField(TEXT("framesPerSecond"), UEcine::ShotPlan::MakeNumber(Package.FramesPerSecond, 3));
		Object->SetField(TEXT("tiltDeg"), UEcine::ShotPlan::MakeNumber(Camera->GetActorRotation().Pitch, 1));

		// How high the lens is, when there is a floor under it to measure from.
		const FVector Location = Camera->GetCineCameraComponent()->GetComponentLocation();
		FHitResult Floor;
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(UEcineAssistantFloor), true, Camera);
		if (Camera->GetWorld()->LineTraceSingleByChannel(Floor, Location, Location - FVector(0.0, 0.0, 2000.0), ECC_Visibility, Query))
		{
			Object->SetField(TEXT("heightAboveFloorM"), ToMetres(Location.Z - Floor.ImpactPoint.Z));
		}

		const TSharedRef<FJsonObject> Focus = MakeShared<FJsonObject>();
		Focus->SetField(TEXT("distanceM"), ToMetres(UEcine::FocusEditing::GetFocusDistance(Camera)));
		if (const AActor* Followed = UEcine::FocusEditing::GetFollowedActor(Camera))
		{
			Focus->SetStringField(TEXT("following"), Followed->GetActorLabel());
		}
		Object->SetObjectField(TEXT("focus"), Focus);
		return Object;
	}

	TSharedRef<FJsonObject> DescribeSupport(const AUEcineRig* Rig)
	{
		const FUEcineRigSpec Spec = Rig->GetSpec();
		const FUEcineRigPose Pose = Rig->GetPose();
		const TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
		for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
		{
			const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
			if (Spec.GetAxis(Axis).bUsed)
			{
				Position->SetField(UEcine::ShotPlan::GetAxisId(Axis), UEcine::ShotPlan::MakeNumber(UEcine::ShotPlan::ToPlanUnits(Axis, Pose.Get(Axis)), 2));
			}
		}
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Spec.PresetId);
		Object->SetStringField(TEXT("kind"), UEcine::Rig::GetTypeName(Spec.Type).ToString());
		Object->SetObjectField(TEXT("position"), Position);
		Object->SetNumberField(TEXT("marks"), Rig->Marks.Num());
		if (Spec.Type == EUEcineRigType::Dolly)
		{
			Object->SetField(TEXT("trackLengthM"), ToMetres(Rig->GetTrackLength()));
		}
		return Object;
	}
}

FString UEcine::ShotAssistant::DescribeSet(const ACineCameraActor* Camera)
{
	const TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
	if (Camera != nullptr)
	{
		Set->SetObjectField(TEXT("camera"), DescribeCamera(Camera));
		if (const AUEcineRig* Rig = GripEditing::FindRig(Camera))
		{
			Set->SetObjectField(TEXT("support"), DescribeSupport(Rig));
		}

		// Where each thing is as the camera sees it: ahead along the lens, to its right, above it.
		const FTransform CameraTransform = Camera->GetCineCameraComponent()->GetComponentTransform();
		TArray<TSharedPtr<FJsonValue>> SubjectValues;
		for (const FShotSubject& Subject : FindShotSubjects(Camera))
		{
			const FVector Seen = CameraTransform.InverseTransformPositionNoScale(Subject.Centre);
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("name"), Subject.Name);
			Object->SetField(TEXT("aheadM"), ToMetres(Seen.X));
			Object->SetField(TEXT("rightM"), ToMetres(Seen.Y));
			Object->SetField(TEXT("upM"), ToMetres(Seen.Z));
			Object->SetField(TEXT("heightM"), ToMetres(Subject.Size.Z));
			Object->SetField(TEXT("widthM"), ToMetres(FMath::Max(Subject.Size.X, Subject.Size.Y)));
			SubjectValues.Add(MakeShared<FJsonValueObject>(Object));
		}
		Set->SetArrayField(TEXT("subjects"), SubjectValues);
	}

	if (const ULevelSequence* Sequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())
	{
		TArray<TSharedPtr<FJsonValue>> ShotNames;
		for (const ShotEditing::FShot& Shot : ShotEditing::GetShots())
		{
			ShotNames.Add(MakeShared<FJsonValueString>(Shot.Name));
		}
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Sequence->GetName());
		Object->SetArrayField(TEXT("shots"), ShotNames);
		Set->SetObjectField(TEXT("sequence"), Object);
	}

	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Set, Writer);
	return Text;
}

TArray<UEcine::ShotAssistant::FPlanLine> UEcine::ShotAssistant::Review(const FUEcineShotPlan& Plan, const ACineCameraActor* Camera)
{
	TArray<FPlanLine> Lines;
	if (Camera == nullptr)
	{
		return Lines;
	}
	const TArray<FShotSubject> Subjects = FindShotSubjects(Camera);
	FPlanProgress Progress = GetPlanStart(Camera);
	for (const FUEcineShotStep& Step : Plan.Steps)
	{
		Lines.Add(ReviewStep(Step, Camera, Subjects, Progress));
	}
	return Lines;
}

bool UEcine::ShotAssistant::CanApply(TConstArrayView<FPlanLine> Lines)
{
	return !Lines.IsEmpty() && !Lines.ContainsByPredicate([](const FPlanLine& Line) { return !Line.Problem.IsEmpty(); });
}

bool UEcine::ShotAssistant::Apply(const FUEcineShotPlan& Plan, ACineCameraActor* Camera, FText& OutError)
{
	if (Camera == nullptr)
	{
		OutError = LOCTEXT("NoCamera", "There is no camera to carry the plan out for.");
		return false;
	}
	if (Plan.Steps.IsEmpty())
	{
		OutError = LOCTEXT("NoSteps", "The plan has no steps to carry out.");
		return false;
	}
	const TArray<FPlanLine> Lines = Review(Plan, Camera);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		if (!Lines[Index].Problem.IsEmpty())
		{
			OutError = FText::Format(LOCTEXT("StepProblem", "Step {0} cannot be carried out. {1}"), FText::AsNumber(Index + 1), Lines[Index].Problem);
			return false;
		}
	}

	// Each step is an undoable change of its own inside this one, so one undo takes the plan back.
	const FScopedTransaction Transaction(LOCTEXT("ApplyShotPlan", "Apply Shot Plan"));
	const TArray<FShotSubject> Subjects = FindShotSubjects(Camera);
	bool bMoved = false;
	for (int32 Index = 0; Index < Plan.Steps.Num(); ++Index)
	{
		const FUEcineShotStep& Step = Plan.Steps[Index];
		FText Problem;
		switch (Step.Kind)
		{
		case EUEcineShotStep::ChooseCamera:
		case EUEcineShotStep::ChooseLens:
		case EUEcineShotStep::SetExposure:
			CameraEditing::EditPackage(CameraEditing::AddCameraPackage(Camera), Lines[Index].Text, [&Step, &Problem](FUEcineCameraPackage& Package)
			{
				EditPackageForStep(Step, Package, Problem);
			}, Problem);
			break;

		case EUEcineShotStep::PutOnSupport:
			if (GripEditing::PutCameraOnRig(Camera, *Rig::FindPreset(Step.SupportId)) == nullptr)
			{
				Problem = LOCTEXT("SupportNotStood", "The support could not be stood up in the level.");
			}
			break;

		case EUEcineShotStep::MoveToMark:
			GripEditing::EditRig(GripEditing::FindRig(Camera), LOCTEXT("BlockMove", "Block Move"), [&Step, bMoved](AUEcineRig& Rig)
			{
				if (!bMoved)
				{
					Rig.Marks.Reset();
					Rig.Marks.AddDefaulted_GetRef().Pose = Rig.GetPose();
				}
				FUEcineRigMark& Mark = Rig.Marks.AddDefaulted_GetRef();
				Mark.Pose = AddMove(Rig.GetSpec(), Rig.GetPose(), Step.Change);
				Mark.Arrival = Step.Timing;
				Rig.SetPose(Mark.Pose);
			});
			bMoved = true;
			break;

		case EUEcineShotStep::FocusOn:
		{
			const FShotSubject* Subject = FindShotSubject(Subjects, Step.Subject);
			if (Step.bFollow)
			{
				FocusEditing::FollowActor(Camera, Subject->Actor);
			}
			else
			{
				FocusEditing::FocusOnPoint(Camera, Subject->Centre, Problem);
			}
			break;
		}

		case EUEcineShotStep::FocusAt:
			FocusEditing::SetFocusDistance(Camera, static_cast<float>(Step.FocusDistanceCm));
			break;

		case EUEcineShotStep::AddShot:
			if (ShotEditing::AddShot(Camera, Problem))
			{
				// Straight into the new shot, the last one, so that a move baked next is written into it.
				const TArray<ShotEditing::FShot> Shots = ShotEditing::GetShots();
				if (!Shots.IsEmpty())
				{
					ULevelSequence* ShotSequence = Shots.Last().Sequence;
					ShotEditing::GoToShot(Shots.Last());
					ShotEditing::SetScene(ShotSequence, Step.Scene);
					ShotEditing::SetSize(ShotSequence, Step.Size);
					ShotEditing::SetMove(ShotSequence, Step.Move);
					ShotEditing::SetNotes(ShotSequence, Step.Notes);
				}
			}
			break;

		case EUEcineShotStep::BakeMove:
			GripEditing::BakeMoveToSequencer(GripEditing::FindRig(Camera), Problem);
			break;
		}

		if (!Problem.IsEmpty())
		{
			OutError = Index == 0
				? FText::Format(LOCTEXT("FirstStepFailed", "Step 1 could not be carried out. {0}"), Problem)
				: FText::Format(LOCTEXT("StepFailed", "Step {0} could not be carried out. {1} The steps before it were; Undo takes them back."), FText::AsNumber(Index + 1), Problem);
			return false;
		}
	}

	// The camera is left where the shot starts.
	if (bMoved)
	{
		GripEditing::GoToMark(GripEditing::FindRig(Camera), 0);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
