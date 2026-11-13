// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineGripEditing.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieScene.h"
#include "ScopedTransaction.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineFocusEditing.h"
#include "UEcineMove.h"
#include "UEcineOperator.h"
#include "UEcineOptics.h"
#include "UEcineRig.h"
#include "UEcineSequenceUtils.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

using namespace UEcine::SequenceUtils;

namespace
{
	/** Where to stand a rig, and how to set it, so its mount is where a camera already is. */
	struct FRigSeat
	{
		FTransform RigTransform;
		FUEcineRigPose Pose;
	};

	/**
	 * The dolly zooms of a rig's move, worked out one after another: each starts on the focal
	 * length the one before left the lens at.
	 */
	struct FDollyZoomSolver
	{
		const AUEcineRig& Rig;
		/** What the camera is focused on: the thing that keeps its size. */
		FVector Subject = FVector::ZeroVector;
		/** The focal length the lens is on as the next move starts. */
		double FocalLengthMm = 0.0;
		double LensWidestMm = 0.0;
		double LensLongestMm = 0.0;

		FDollyZoomSolver(const AUEcineRig& InRig, const ACineCameraActor& Camera)
			: Rig(InRig)
		{
			const UCineCameraComponent* Lens = Camera.GetCineCameraComponent();
			const UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(&Camera);
			// The package's focal length, not the lens's: Sequencer may be holding the lens somewhere in an earlier bake.
			FocalLengthMm = Package != nullptr ? Package->Package.FocalLengthMm : Lens->CurrentFocalLength;
			LensWidestMm = Lens->LensSettings.MinFocalLength;
			LensLongestMm = Lens->LensSettings.MaxFocalLength;
			Subject = UEcine::FocusEditing::GetFocusPoint(&Camera);
		}

		/** How far the camera is from the subject with the rig in a pose. */
		double DistanceAt(const FUEcineRigPose& Pose) const
		{
			return FVector::Dist((Rig.SolveMount(Pose) * Rig.GetActorTransform()).GetLocation(), Subject);
		}

		/** What the move into a mark asks of the lens at a time from the move's start. */
		double FocalLengthAt(int32 MarkIndex, double TimeSeconds) const
		{
			const FUEcineRigMark& From = Rig.Marks[MarkIndex - 1];
			const FUEcineRigMark& To = Rig.Marks[MarkIndex];
			const double StartDistance = DistanceAt(From.Pose);
			return StartDistance > UE_KINDA_SMALL_NUMBER
				? UEcine::Optics::DollyZoomFocalLengthMm(FocalLengthMm, StartDistance, DistanceAt(UEcine::Move::Evaluate(From.Pose, To.Pose, To.Arrival, TimeSeconds)))
				: FocalLengthMm;
		}

		double ClampToLens(double Asked) const
		{
			return FMath::Clamp(Asked, LensWidestMm, LensLongestMm);
		}

		/** Moves on past the move into a mark: the lens is left where that move put it. */
		void FinishMove(int32 MarkIndex)
		{
			if (Rig.Marks[MarkIndex].bDollyZoom)
			{
				FocalLengthMm = ClampToLens(FocalLengthAt(MarkIndex, Rig.Marks[MarkIndex].Arrival.Normalized().DurationSeconds));
			}
		}
	};

	/** Rigs whose lift axis is the camera's height straight above the floor. */
	bool StandsUnderCamera(EUEcineRigType Type)
	{
		return Type != EUEcineRigType::Jib && Type != EUEcineRigType::Crane;
	}

	FRigSeat SeatRigUnderCamera(const ACineCameraActor* Camera, const FUEcineRigPreset& Preset)
	{
		const FUEcineRigSpec& Spec = Preset.Spec;
		const FVector CameraLocation = Camera->GetActorLocation();
		const FRotator CameraRotation = Camera->GetActorRotation();

		FRigSeat Seat;
		Seat.Pose = Preset.DefaultPose;

		// Keep the camera's tilt and roll on the head, as far as the head allows.
		const FUEcineAxisLimits& TiltLimits = Spec.GetAxis(EUEcineRigAxis::Tilt);
		if (TiltLimits.bUsed)
		{
			Seat.Pose.Tilt = FMath::Clamp(CameraRotation.Pitch, TiltLimits.Min, TiltLimits.Max);
		}
		const FUEcineAxisLimits& RollLimits = Spec.GetAxis(EUEcineRigAxis::Roll);
		if (RollLimits.bUsed)
		{
			Seat.Pose.Roll = FMath::Clamp(CameraRotation.Roll, RollLimits.Min, RollLimits.Max);
		}

		// Stand the rig on the floor under the camera when there is a floor within its reach.
		const FUEcineAxisLimits& LiftLimits = Spec.GetAxis(EUEcineRigAxis::Lift);
		if (StandsUnderCamera(Spec.Type) && LiftLimits.bUsed)
		{
			FHitResult Floor;
			const FVector Below = CameraLocation - FVector(0.0, 0.0, LiftLimits.Max + 100.0);
			const FCollisionQueryParams Query(SCENE_QUERY_STAT(UEcineFindFloor), true, Camera);
			if (Camera->GetWorld()->LineTraceSingleByChannel(Floor, CameraLocation, Below, ECC_Visibility, Query))
			{
				Seat.Pose.Lift = FMath::Clamp(CameraLocation.Z - Floor.ImpactPoint.Z, LiftLimits.Min, LiftLimits.Max);
			}
		}

		// The rig faces the way the camera looks; the head starts with no pan.
		const FTransform MountOnRig = UEcine::Rig::SolveMount(Spec.Type, Seat.Pose, UEcine::Rig::StraightTrackPoint(Seat.Pose.Travel));
		const FQuat RigRotation = FRotator(0.0, CameraRotation.Yaw, 0.0).Quaternion();
		Seat.RigTransform = FTransform(RigRotation, CameraLocation - RigRotation.RotateVector(MountOnRig.GetLocation()));
		return Seat;
	}

	FString MakeRigLabel(const ACineCameraActor* Camera, EUEcineRigType Type)
	{
		return FString::Printf(TEXT("%s %s"), *Camera->GetActorLabel(), *UEcine::Rig::GetTypeName(Type).ToString());
	}

	bool HasMoveToBake(const AUEcineRig* Rig, FText& OutError)
	{
		if (Rig == nullptr)
		{
			OutError = LOCTEXT("BakeNoRig", "There is no rig to bake a move from.");
			return false;
		}
		if (Rig->Marks.Num() < 2)
		{
			OutError = LOCTEXT("BakeNeedsMarks", "Mark at least two positions before baking a move.");
			return false;
		}
		return true;
	}
}

AUEcineRig* UEcine::GripEditing::FindRig(const ACineCameraActor* Camera)
{
	if (Camera == nullptr)
	{
		return nullptr;
	}
	AUEcineRig* Rig = Cast<AUEcineRig>(Camera->GetAttachParentActor());
	return Rig != nullptr && Rig->GetMountedCamera() == Camera ? Rig : nullptr;
}

FUEcineRigPose UEcine::GripEditing::GetPoseOnRig(const ACineCameraActor* Camera, const FUEcineRigPreset& Preset)
{
	return Camera != nullptr ? SeatRigUnderCamera(Camera, Preset).Pose : Preset.DefaultPose;
}

AUEcineRig* UEcine::GripEditing::PutCameraOnRig(ACineCameraActor* Camera, const FUEcineRigPreset& Preset)
{
	if (Camera == nullptr)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("PutCameraOnRig", "Put Camera on {0}"), Preset.Name));
	const FRigSeat Seat = SeatRigUnderCamera(Camera, Preset);

	AUEcineRig* Rig = FindRig(Camera);
	if (Rig == nullptr)
	{
		Rig = Cast<AUEcineRig>(GEditor->AddActor(Camera->GetLevel(), AUEcineRig::StaticClass(), Seat.RigTransform, true, RF_Transactional, false));
		if (Rig == nullptr)
		{
			return nullptr;
		}
		Rig->SetSpec(Preset.Spec, Seat.Pose);
		Rig->Operator.Style = UEcine::Operator::GetDefaultStyle(Preset.Spec.Type);
		Camera->Modify();
		Rig->MountCamera(Camera);
		// The support holds the camera: looking through it and flying the viewport, or dragging
		// it, would take it off its mount and leave the support's axes describing nothing.
		Camera->SetLockLocation(true);
	}
	else
	{
		Rig->Modify();
		const bool bTypeChanged = Rig->GetSpec().Type != Preset.Spec.Type;
		Rig->SetActorTransform(Seat.RigTransform);
		Rig->SetSpec(Preset.Spec, Seat.Pose);
		if (bTypeChanged)
		{
			// Marks are positions of the old rig's axes and mean nothing on a different kind of rig.
			Rig->Marks.Reset();
			Rig->Operator.Style = UEcine::Operator::GetDefaultStyle(Preset.Spec.Type);
		}
	}
	Rig->SetActorLabel(MakeRigLabel(Camera, Preset.Spec.Type));

	GEditor->RedrawLevelEditingViewports();
	return Rig;
}

void UEcine::GripEditing::TakeCameraOffRig(ACineCameraActor* Camera)
{
	AUEcineRig* Rig = FindRig(Camera);
	if (Rig == nullptr)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("TakeCameraOffRig", "Take Camera off Support"));
	Camera->Modify();
	Camera->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Camera->SetLockLocation(false);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->DestroyActor(Rig);
	GEditor->RedrawLevelEditingViewports();
}

void UEcine::GripEditing::EditRig(AUEcineRig* Rig, const FText& Description, TFunctionRef<void(AUEcineRig&)> Edit)
{
	if (Rig == nullptr)
	{
		return;
	}
	const FScopedTransaction Transaction(Description);
	Rig->Modify();
	Edit(*Rig);
	GEditor->RedrawLevelEditingViewports();
}

int32 UEcine::GripEditing::AddMark(AUEcineRig* Rig)
{
	if (Rig == nullptr)
	{
		return INDEX_NONE;
	}
	EditRig(Rig, LOCTEXT("AddMark", "Mark Position"), [](AUEcineRig& EditedRig)
	{
		EditedRig.Marks.AddDefaulted_GetRef().Pose = EditedRig.GetPose();
	});
	return Rig->Marks.Num() - 1;
}

void UEcine::GripEditing::UpdateMark(AUEcineRig* Rig, int32 MarkIndex)
{
	if (Rig != nullptr && Rig->Marks.IsValidIndex(MarkIndex))
	{
		EditRig(Rig, LOCTEXT("UpdateMark", "Move Mark Here"), [MarkIndex](AUEcineRig& EditedRig) { EditedRig.Marks[MarkIndex].Pose = EditedRig.GetPose(); });
	}
}

void UEcine::GripEditing::RemoveMark(AUEcineRig* Rig, int32 MarkIndex)
{
	if (Rig != nullptr && Rig->Marks.IsValidIndex(MarkIndex))
	{
		EditRig(Rig, LOCTEXT("RemoveMark", "Remove Mark"), [MarkIndex](AUEcineRig& EditedRig) { EditedRig.Marks.RemoveAt(MarkIndex); });
	}
}

void UEcine::GripEditing::GoToMark(AUEcineRig* Rig, int32 MarkIndex)
{
	if (Rig != nullptr && Rig->Marks.IsValidIndex(MarkIndex))
	{
		EditRig(Rig, LOCTEXT("GoToMark", "Go to Mark"), [MarkIndex](AUEcineRig& EditedRig) { EditedRig.SetPose(EditedRig.Marks[MarkIndex].Pose); });
	}
}

bool UEcine::GripEditing::CanDollyZoom(const AUEcineRig* Rig)
{
	if (Rig == nullptr)
	{
		return false;
	}
	const EUEcineRigType Type = Rig->GetSpec().Type;
	return Type == EUEcineRigType::Dolly || Type == EUEcineRigType::Slider;
}

bool UEcine::GripEditing::PlanDollyZoom(const AUEcineRig* Rig, int32 MarkIndex, FDollyZoom& OutPlan)
{
	const ACineCameraActor* Camera = Rig != nullptr ? Rig->GetMountedCamera() : nullptr;
	if (Camera == nullptr || MarkIndex < 1 || !Rig->Marks.IsValidIndex(MarkIndex) || !Rig->Marks[MarkIndex].bDollyZoom || !CanDollyZoom(Rig))
	{
		return false;
	}

	FDollyZoomSolver Solver(*Rig, *Camera);
	for (int32 Earlier = 1; Earlier < MarkIndex; ++Earlier)
	{
		Solver.FinishMove(Earlier);
	}
	const double DurationSeconds = Rig->Marks[MarkIndex].Arrival.Normalized().DurationSeconds;
	OutPlan.StartFocalLengthMm = Solver.FocalLengthMm;
	OutPlan.EndFocalLengthMm = Solver.FocalLengthAt(MarkIndex, DurationSeconds);
	OutPlan.WidestMm = OutPlan.LongestMm = OutPlan.StartFocalLengthMm;
	const int32 Steps = 32;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const double Asked = Solver.FocalLengthAt(MarkIndex, DurationSeconds * Step / Steps);
		OutPlan.WidestMm = FMath::Min(OutPlan.WidestMm, Asked);
		OutPlan.LongestMm = FMath::Max(OutPlan.LongestMm, Asked);
	}
	OutPlan.LensWidestMm = Solver.LensWidestMm;
	OutPlan.LensLongestMm = Solver.LensLongestMm;
	OutPlan.bFocusHeld = UEcine::FocusEditing::IsFollowing(Camera);
	return true;
}

bool UEcine::GripEditing::BakeMove(AUEcineRig* Rig, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError)
{
	if (!HasMoveToBake(Rig, OutError))
	{
		return false;
	}
	if (Sequence == nullptr || Sequence->GetMovieScene() == nullptr)
	{
		OutError = LOCTEXT("BakeNoSequence", "There is no sequence to bake the move into.");
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const double TicksPerSecond = TickResolution.AsDecimal();
	const double DurationSeconds = UEcine::Move::GetTotalDuration(Rig->Marks);

	const FFrameNumber StartTick = ConvertFrameTime(FFrameTime(StartFrame), DisplayRate, TickResolution).RoundToFrame();
	const FFrameNumber EndTick = StartTick + FMath::RoundToInt32(DurationSeconds * TicksPerSecond);
	const TRange<FFrameNumber> MoveRange = TRange<FFrameNumber>::Inclusive(StartTick, EndTick);

	const FScopedTransaction Transaction(LOCTEXT("BakeMove", "Bake Move to Sequencer"));
	Sequence->Modify();
	MovieScene->Modify();

	// The designed move: a few keys per axis, with the slopes that draw the eases exactly.
	const FUEcineRigSpec Spec = Rig->GetSpec();
	const FGuid RigBinding = FindOrAddBinding(Sequence, Rig);
	for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
	{
		const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
		if (!Spec.Axes[AxisIndex].bUsed)
		{
			continue;
		}

		UMovieSceneFloatTrack* Track = FindOrAddFloatTrack(MovieScene, RigBinding, AUEcineRig::GetAxisPropertyName(Axis), AUEcineRig::GetAxisPropertyName(Axis).ToString(), UEcine::Rig::GetAxisName(Spec.Type, Axis));
		Track->Modify();
		UMovieSceneFloatSection* Section = FindOrAddSection<UMovieSceneFloatSection>(Track);
		Section->Modify();

		FMovieSceneFloatChannel& Channel = Section->GetChannel();
		DeleteKeysInRange(Channel, MoveRange);
		for (const FUEcineMoveKey& Key : UEcine::Move::MakeKeysForMarks(Rig->Marks, Axis))
		{
			// Sequencer stores slopes per tick.
			FMovieSceneTangentData Tangent;
			Tangent.ArriveTangent = static_cast<float>(Key.ArriveSlopePerSecond / TicksPerSecond);
			Tangent.LeaveTangent = static_cast<float>(Key.LeaveSlopePerSecond / TicksPerSecond);
			const FFrameNumber KeyTick = StartTick + FMath::RoundToInt32(Key.TimeSeconds * TicksPerSecond);
			Channel.AddCubicKey(KeyTick, static_cast<float>(Key.Value), RCTM_Break, Tangent);
		}
		Section->MarkAsChanged();
	}

	// The operator's hand: a key on every frame of the camera's transform relative to the mount.
	if (ACineCameraActor* Camera = Rig->GetMountedCamera())
	{
		const FGuid CameraBinding = FindOrAddBinding(Sequence, Camera);
		UMovieScene3DTransformTrack* Track = MovieScene->FindTrack<UMovieScene3DTransformTrack>(CameraBinding);
		if (Track == nullptr && !Rig->Operator.IsStill())
		{
			Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(CameraBinding);
		}
		if (Track != nullptr)
		{
			Track->Modify();
			UMovieScene3DTransformSection* Section = FindOrAddSection<UMovieScene3DTransformSection>(Track);
			Section->Modify();

			// Channels are location X Y Z, rotation roll pitch yaw, then scale X Y Z.
			const TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			for (int32 ChannelIndex = 0; ChannelIndex < 6; ++ChannelIndex)
			{
				DeleteKeysInRange(*Channels[ChannelIndex], MoveRange);
			}

			if (!Rig->Operator.IsStill())
			{
				const FFrameNumber EndFrame = ConvertFrameTime(FFrameTime(EndTick), TickResolution, DisplayRate).CeilToFrame();
				for (FFrameNumber Frame = StartFrame; Frame <= EndFrame; ++Frame)
				{
					const FFrameNumber Tick = ConvertFrameTime(FFrameTime(Frame), DisplayRate, TickResolution).RoundToFrame();
					const FTransform Offset = UEcine::Operator::Offset(Rig->Operator, DisplayRate.AsSeconds(FFrameTime(Frame - StartFrame)));
					const FVector Location = Offset.GetLocation();
					const FRotator Rotation = Offset.Rotator();
					Channels[0]->AddLinearKey(Tick, Location.X);
					Channels[1]->AddLinearKey(Tick, Location.Y);
					Channels[2]->AddLinearKey(Tick, Location.Z);
					Channels[3]->AddLinearKey(Tick, Rotation.Roll);
					Channels[4]->AddLinearKey(Tick, Rotation.Pitch);
					Channels[5]->AddLinearKey(Tick, Rotation.Yaw);
				}
			}
			Section->MarkAsChanged();
		}
	}

	// A dolly zoom: a key on every frame of the lens's focal length while the camera travels.
	// A move baked earlier with a dolly zoom that has since been switched off loses its keys.
	if (ACineCameraActor* Camera = Rig->GetMountedCamera())
	{
		UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
		const FName FocalLengthName = GET_MEMBER_NAME_CHECKED(UCineCameraComponent, CurrentFocalLength);
		const bool bAnyDollyZoom = CanDollyZoom(Rig) && Rig->Marks.ContainsByPredicate([](const FUEcineRigMark& Mark) { return Mark.bDollyZoom; });
		UMovieSceneFloatTrack* Track = bAnyDollyZoom
			? FindOrAddFloatTrack(MovieScene, FindOrAddComponentBinding(Sequence, Lens), FocalLengthName, FocalLengthName.ToString(), LOCTEXT("ZoomTrackName", "Focal Length"))
			: FindFloatTrack(MovieScene, FindBinding(Sequence, Lens), FocalLengthName);
		if (Track != nullptr)
		{
			Track->Modify();
			UMovieSceneFloatSection* Section = FindOrAddSection<UMovieSceneFloatSection>(Track);
			Section->Modify();
			FMovieSceneFloatChannel& Channel = Section->GetChannel();
			DeleteKeysInRange(Channel, MoveRange);

			FDollyZoomSolver Solver(*Rig, *Camera);
			const double FramesPerSecond = DisplayRate.AsDecimal();
			double MoveStartSeconds = 0.0;
			for (int32 MarkIndex = 1; bAnyDollyZoom && MarkIndex < Rig->Marks.Num(); ++MarkIndex)
			{
				const double MoveSeconds = Rig->Marks[MarkIndex].Arrival.Normalized().DurationSeconds;
				if (Rig->Marks[MarkIndex].bDollyZoom)
				{
					const FFrameNumber FirstFrame = StartFrame + FMath::CeilToInt32(MoveStartSeconds * FramesPerSecond - 1.e-6);
					const FFrameNumber LastFrame = StartFrame + FMath::CeilToInt32((MoveStartSeconds + MoveSeconds) * FramesPerSecond - 1.e-6);
					for (FFrameNumber Frame = FirstFrame; Frame <= LastFrame; ++Frame)
					{
						const double IntoMove = FMath::Clamp(DisplayRate.AsSeconds(FFrameTime(Frame - StartFrame)) - MoveStartSeconds, 0.0, MoveSeconds);
						const FFrameNumber Tick = ConvertFrameTime(FFrameTime(Frame), DisplayRate, TickResolution).RoundToFrame();
						Channel.AddLinearKey(Tick, static_cast<float>(Solver.ClampToLens(Solver.FocalLengthAt(MarkIndex, IntoMove))));
					}
				}
				Solver.FinishMove(MarkIndex);
				MoveStartSeconds += MoveSeconds;
			}
			Section->MarkAsChanged();
		}
	}

	// What was baked, so that the Grip tab can say when the marks have moved on since.
	Rig->Modify();
	Rig->BakedMarks = Rig->Marks;
	Rig->BakedOperator = Rig->Operator;

	IncludeInPlaybackRange(MovieScene, MoveRange);
	ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	return true;
}

ULevelSequence* UEcine::GripEditing::BakeMoveToSequencer(AUEcineRig* Rig, FText& OutError)
{
	// Checked first so that a refused bake does not leave a new, empty sequence behind.
	if (!HasMoveToBake(Rig, OutError))
	{
		return nullptr;
	}

	const ACineCameraActor* Camera = Rig->GetMountedCamera();
	const FBakeTarget Target = FindOrCreateBakeTarget(Camera != nullptr ? static_cast<const AActor*>(Camera) : Rig, TEXT("_Move"), Camera);
	if (Target.Sequence == nullptr)
	{
		OutError = LOCTEXT("BakeNoNewSequence", "A new sequence could not be made for the move.");
		return nullptr;
	}
	return BakeMove(Rig, Target.Sequence, Target.StartFrame, OutError) ? Target.Sequence : nullptr;
}

#undef LOCTEXT_NAMESPACE
