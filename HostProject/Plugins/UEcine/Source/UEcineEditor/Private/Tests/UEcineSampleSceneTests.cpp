// Copyright 2026 Fiavaion. All rights reserved.

#include "AssetRegistry/AssetRegistryModule.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/LightComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "ImageUtils.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationCommon.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineMonitorSubsystem.h"
#include "UEcineMove.h"
#include "UEcineRig.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* const SampleStagePackage = TEXT("/UEcine/Sample/UEcineSampleStage");
	const TCHAR* const SampleCameraLabel = TEXT("A Cam");
	const TCHAR* const SampleSubjectLabel = TEXT("Anna");
	const TCHAR* const SampleForegroundLabel = TEXT("Lamp");

	/** Full sun on a clear day, in lux: the camera in the sample is exposed like a real one. */
	constexpr float SampleSunLux = 100000.0f;

	AStaticMeshActor* AddSamplePiece(UWorld* World, const TCHAR* Label, const TCHAR* Shape, const FVector& Location, const FVector& Scale)
	{
		AStaticMeshActor* Piece = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		Piece->SetActorLabel(Label);
		Piece->SetActorScale3D(Scale);
		Piece->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
		return Piece;
	}

	template <typename ActorType>
	ActorType* FindSampleActor(const UWorld* World, const TCHAR* Label)
	{
		for (AActor* Actor : World->PersistentLevel->Actors)
		{
			ActorType* Typed = Cast<ActorType>(Actor);
			if (Typed != nullptr && (Label == nullptr || Typed->GetActorLabel() == Label))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	/** The angle between where the camera looks and where the subject is. */
	double MeasureSampleAimErrorDeg(const ACineCameraActor& Camera, const FVector& Subject)
	{
		const USceneComponent* Lens = Camera.GetCineCameraComponent();
		const FVector ToSubject = (Subject - Lens->GetComponentLocation()).GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Lens->GetForwardVector(), ToSubject)));
	}

	/** Turns the rig's head until the camera looks at a point. */
	void AimSampleRig(AUEcineRig& Rig, const ACineCameraActor& Camera, const FVector& Subject)
	{
		const USceneComponent* Lens = Camera.GetCineCameraComponent();
		const FRotator Wanted = (Subject - Lens->GetComponentLocation()).Rotation();
		const FRotator Now = Lens->GetComponentRotation();
		FUEcineRigPose Pose = Rig.GetPose();
		Pose.Pan += FRotator::NormalizeAxis(Wanted.Yaw - Now.Yaw);
		Pose.Tilt += FRotator::NormalizeAxis(Wanted.Pitch - Now.Pitch);
		Rig.SetPose(Pose);
	}

	/**
	 * Builds the sample stage in a new level and saves it into the plugin: a set in daylight, a
	 * subject, and a camera package on a crane with a move blocked between two marks.
	 */
	bool BuildSampleStage(FAutomationTestBase& Test)
	{
		UWorld* World = UEditorLoadingAndSavingUtils::NewMapFromTemplate(TEXT("/Engine/Maps/Templates/Template_Default"), false);
		ADirectionalLight* Sun = World != nullptr ? FindSampleActor<ADirectionalLight>(World, nullptr) : nullptr;
		if (!Test.TestNotNull(TEXT("A new level is made from the engine's daylight template"), World) || !Test.TestNotNull(TEXT("The template has a sun"), Sun))
		{
			return false;
		}
		Sun->GetLightComponent()->SetIntensity(SampleSunLux);
		// Over the camera's left shoulder, so the subject is lit from the front and side.
		Sun->SetActorRotation(FRotator(-42.0, 35.0, 0.0));

		AddSamplePiece(World, TEXT("Table"), TEXT("Cube"), FVector(0.0, 0.0, 37.5), FVector(1.6, 0.9, 0.75));
		AStaticMeshActor* Subject = AddSamplePiece(World, SampleSubjectLabel, TEXT("Cylinder"), FVector(130.0, 0.0, 75.0), FVector(0.45, 0.45, 1.5));
		AStaticMeshActor* Head = AddSamplePiece(World, TEXT("Anna head"), TEXT("Sphere"), FVector(130.0, 0.0, 163.0), FVector(0.24, 0.24, 0.26));
		Head->AttachToActor(Subject, FAttachmentTransformRules::KeepWorldTransform);
		AddSamplePiece(World, TEXT("Pillar left"), TEXT("Cube"), FVector(420.0, -320.0, 200.0), FVector(0.5, 0.5, 4.0));
		AddSamplePiece(World, TEXT("Pillar right"), TEXT("Cube"), FVector(420.0, 320.0, 200.0), FVector(0.5, 0.5, 4.0));
		AddSamplePiece(World, TEXT("Back wall"), TEXT("Cube"), FVector(760.0, 0.0, 175.0), FVector(0.3, 11.0, 3.5));
		AddSamplePiece(World, TEXT("Crate"), TEXT("Cube"), FVector(-150.0, 190.0, 30.0), FVector(0.6, 0.6, 0.6));
		// A floor lamp between the camera and the subject: something near to rack the focus to.
		AStaticMeshActor* Lamp = AddSamplePiece(World, SampleForegroundLabel, TEXT("Cylinder"), FVector(-143.0, -45.0, 75.0), FVector(0.08, 0.08, 1.5));
		AStaticMeshActor* Shade = AddSamplePiece(World, TEXT("Lamp shade"), TEXT("Cone"), FVector(-143.0, -45.0, 162.0), FVector(0.3, 0.3, 0.25));
		Shade->AttachToActor(Lamp, FAttachmentTransformRules::KeepWorldTransform);

		// Camera: a real body and lens, exposed for full sun the way a camera would be on the day.
		// A long lens, open and close to the subject: what is sharp is a narrow slice of the set.
		const FVector SubjectFace(130.0, 0.0, 150.0);
		const FVector CameraLocation(-330.0, -120.0, 150.0);
		ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform((SubjectFace - CameraLocation).Rotation(), CameraLocation));
		const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();
		const FUEcineCameraBody* Body = Database.FindBody(TEXT("arri-alexa-35"));
		const FUEcineLensKit* Kit = Database.FindLensKit(TEXT("arri-signature-prime"));
		UUEcineCameraPackageComponent* Package = Camera != nullptr ? UUEcineCameraPackageComponent::Find(Camera) : nullptr;
		if (!Test.TestNotNull(TEXT("A camera with a camera package"), Package) || !Test.TestNotNull(TEXT("The ALEXA 35 is in the database"), Body) || !Test.TestNotNull(TEXT("The Signature Primes are in the database"), Kit))
		{
			return false;
		}
		Camera->SetActorLabel(SampleCameraLabel);
		const int32 LensIndex = Kit->Lenses.IndexOfByPredicate([](const FUEcineLens& Lens) { return Lens.FocalLengthMm == 75.0; });
		FText Error;
		const bool bPackaged = Test.TestTrue(TEXT("The kit has a 75 mm"), LensIndex != INDEX_NONE)
			&& UEcine::CameraEditing::EditPackage(Package, FText::FromString(TEXT("Build Camera Package")), [Body, Kit, LensIndex](FUEcineCameraPackage& Edited)
			{
				Edited.SelectBody(*Body);
				Edited.SelectLensKit(*Kit);
				Edited.SelectLens(*Kit, LensIndex);
				Edited.TStop = 2.8f;
				Edited.Iso = 400;
				Edited.ShutterAngleDeg = 180.0f;
				Edited.NdDensity = 2.1f;
				Edited.DeliveryAspect = 2.39f;
				Edited.bPhysicalExposure = true;
			}, Error);
		if (!Test.TestTrue(*FString::Printf(TEXT("The camera package is built (%s)"), *Error.ToString()), bPackaged))
		{
			return false;
		}

		// Grip: a 30 ft crane, a first mark where the camera is, a second with the arm up and round.
		AUEcineRig* Rig = UEcine::GripEditing::PutCameraOnRig(Camera, *UEcine::Rig::FindPreset(TEXT("crane-30ft")));
		if (!Test.TestNotNull(TEXT("The camera is put on a crane"), Rig))
		{
			return false;
		}
		UEcine::GripEditing::AddMark(Rig);
		UEcine::GripEditing::EditRig(Rig, FText::FromString(TEXT("Move Rig")), [Camera, &SubjectFace](AUEcineRig& Edited)
		{
			FUEcineRigPose Raised = Edited.GetPose();
			Raised.Boom += 16.0;
			Raised.Swing += 12.0;
			Edited.SetPose(Raised);
			AimSampleRig(Edited, *Camera, SubjectFace);
		});
		UEcine::GripEditing::AddMark(Rig);
		UEcine::GripEditing::EditRig(Rig, FText::FromString(TEXT("Time Move")), [](AUEcineRig& Edited) { Edited.Marks[1].Arrival.DurationSeconds = 4.0; });
		Test.TestTrue(TEXT("On the second mark the camera still looks at the subject"), MeasureSampleAimErrorDeg(*Camera, SubjectFace) < 1.0);
		UEcine::GripEditing::GoToMark(Rig, 0);

		UEcine::FocusEditing::FollowActor(Camera, Subject);

		GEditor->SelectNone(false, true, false);
		return Test.TestTrue(TEXT("The sample stage is saved into the plugin"), UEditorLoadingAndSavingUtils::SaveMap(World, SampleStagePackage));
	}

	/** What the user guide says is in the sample stage. */
	void CheckSampleStage(FAutomationTestBase& Test, const UWorld* World)
	{
		Test.TestNotNull(TEXT("The stage has a sun"), FindSampleActor<ADirectionalLight>(World, nullptr));
		Test.TestNotNull(TEXT("The stage has a table"), FindSampleActor<AStaticMeshActor>(World, TEXT("Table")));
		const AActor* Subject = FindSampleActor<AStaticMeshActor>(World, SampleSubjectLabel);
		const ACineCameraActor* Camera = FindSampleActor<ACineCameraActor>(World, SampleCameraLabel);
		const UUEcineCameraPackageComponent* Package = Camera != nullptr ? UUEcineCameraPackageComponent::Find(Camera) : nullptr;
		if (!Test.TestNotNull(TEXT("The stage has the subject"), Subject) || !Test.TestNotNull(TEXT("The stage has the camera, with a camera package"), Package))
		{
			return;
		}
		Test.TestEqual(TEXT("The camera is an ALEXA 35"), Package->Package.BodyId, FString(TEXT("arri-alexa-35")));
		Test.TestEqual(TEXT("The camera has the 75 mm on"), Camera->GetCineCameraComponent()->CurrentFocalLength, 75.0f, 0.001f);
		Test.TestNotNull(TEXT("The stage has something in the foreground to rack to"), FindSampleActor<AStaticMeshActor>(World, SampleForegroundLabel));
		Test.TestTrue(TEXT("The camera is exposed like a real one"), Package->Package.bPhysicalExposure);
		Test.TestTrue(TEXT("The focus follows the subject"), UEcine::FocusEditing::GetFollowedActor(Camera) == Subject);

		const AUEcineRig* Rig = UEcine::GripEditing::FindRig(Camera);
		if (!Test.TestNotNull(TEXT("The camera is on a support"), Rig))
		{
			return;
		}
		const FUEcineRigSpec Spec = Rig->GetSpec();
		Test.TestTrue(TEXT("The support is a crane"), Spec.Type == EUEcineRigType::Crane);
		Test.TestTrue(TEXT("The crane holds the camera in place"), Camera->IsLockLocation());
		if (Test.TestEqual(TEXT("A move is blocked between two marks"), Rig->Marks.Num(), 2))
		{
			Test.TestEqual(TEXT("The move is one the crane can make"), UEcine::Move::Check(Spec, Rig->Marks[0].Pose, Rig->Marks[1].Pose, Rig->Marks[1].Arrival).Num(), 0);
		}

		// A sample in a plugin must stand on the engine's and the plugin's own content alone.
		TArray<FName> Dependencies;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetDependencies(FName(SampleStagePackage), Dependencies);
		Test.TestTrue(TEXT("The stage's dependencies are read"), !Dependencies.IsEmpty());
		for (const FName& Dependency : Dependencies)
		{
			const FString Name = Dependency.ToString();
			Test.TestTrue(*FString::Printf(TEXT("The stage needs nothing outside the engine and the plugin (%s)"), *Name),
				Name.StartsWith(TEXT("/Engine/")) || Name.StartsWith(TEXT("/Script/")) || Name.StartsWith(TEXT("/UEcine/")));
		}
	}

	/** Looks through the stage's camera in a level viewport and saves the picture, to be looked at. */
	void ShootSampleStage(FAutomationTestBase& Test, UWorld* World)
	{
		ACineCameraActor* Camera = FindSampleActor<ACineCameraActor>(World, SampleCameraLabel);
		ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		FLevelEditorViewportClient* ViewportClient = nullptr;
		for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
		{
			if (Candidate != nullptr && Candidate->Viewport != nullptr && Candidate->IsPerspective() && Candidate->Viewport->GetSizeXY().GetMin() >= 200)
			{
				ViewportClient = Candidate;
				break;
			}
		}
		if (!Test.TestNotNull(TEXT("The stage's camera"), Camera) || !Test.TestNotNull(TEXT("A perspective level viewport"), ViewportClient))
		{
			return;
		}
		LevelEditor->PilotLevelActor(Camera);
		ViewportClient->SetGameView(true);
		// The viewport takes the camera's exposure and focus when it ticks; the pictures are taken before it has.
		ViewportClient->UpdateViewForLockedActor();
		TArray<FColor> Pixels;
		// The sky's light settles over a few frames.
		for (int32 Frame = 0; Frame < 8; ++Frame)
		{
			ViewportClient->Viewport->Draw(false);
		}
		ViewportClient->Viewport->ReadPixels(Pixels);

		// The same picture in false colour, as the Monitor tab shows it.
		UUEcineMonitorSubsystem* Monitor = UUEcineMonitorSubsystem::Get();
		const FUEcineMonitorSettings MonitorBefore = Monitor->GetSettings();
		FUEcineMonitorSettings Exposure;
		Exposure.bFalseColour = true;
		Monitor->SetSettings(Exposure);
		Monitor->UpdateCameraOverlays();
		TArray<FColor> FalseColour;
		ViewportClient->Viewport->Draw(false);
		ViewportClient->Viewport->ReadPixels(FalseColour);

		// And with focus peaking: the colour belongs on the subject, which is what is sharp.
		FUEcineMonitorSettings Sharpness;
		Sharpness.bPeaking = true;
		Monitor->SetSettings(Sharpness);
		Monitor->UpdateCameraOverlays();
		TArray<FColor> Peaking;
		ViewportClient->Viewport->Draw(false);
		ViewportClient->Viewport->ReadPixels(Peaking);
		Monitor->SetSettings(MonitorBefore);
		LevelEditor->EjectPilotLevelActor();

		if (!Test.TestTrue(TEXT("The picture through the camera is read back"), !Pixels.IsEmpty()))
		{
			return;
		}
		const FIntPoint Size = ViewportClient->Viewport->GetSizeXY();
		// The camera's picture sits in the middle of a wider viewport, with black to either side.
		const FCameraFilmbackSettings& Filmback = Camera->GetCineCameraComponent()->Filmback;
		const int32 PictureWidth = FMath::Min(Size.X, FMath::RoundToInt32(Size.Y * Filmback.SensorWidth / Filmback.SensorHeight));
		const int32 PictureLeft = (Size.X - PictureWidth) / 2;
		double Level = 0.0;
		int32 Clipped = 0;
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = PictureLeft; X < PictureLeft + PictureWidth; ++X)
			{
				const FColor& Pixel = Pixels[Y * Size.X + X];
				const double Luma = (0.2126 * Pixel.R + 0.7152 * Pixel.G + 0.0722 * Pixel.B) / 255.0;
				Level += Luma;
				Clipped += Luma > 0.98 ? 1 : 0;
			}
		}
		Level /= PictureWidth * Size.Y;
		const double ClippedShare = static_cast<double>(Clipped) / (PictureWidth * Size.Y);
		Test.AddInfo(FString::Printf(TEXT("Through the stage's camera the picture averages %.2f of full signal, with %.1f %% of it clipped"), Level, ClippedShare * 100.0));
		Test.TestTrue(TEXT("The stage is exposed: neither dark nor burnt out"), Level > 0.2 && Level < 0.7 && ClippedShare < 0.1);

		const auto Save = [&Test, Size](TArray<FColor>& Picture, const TCHAR* Name)
		{
			for (FColor& Pixel : Picture)
			{
				Pixel.A = 255;
			}
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Picture.GetData(), Picture.Num()), Png);
			Test.TestTrue(*FString::Printf(TEXT("The picture %s was saved"), Name), FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("UEcine/Screenshots") / Name)));
		};
		// Where peaking drew: on the subject in the middle of the frame, or on the rest of the set.
		if (Test.TestEqual(TEXT("The peaking picture is read back"), Peaking.Num(), Pixels.Num()))
		{
			int32 OnSubject = 0;
			int32 Elsewhere = 0;
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X)
				{
					const FColor& Plain = Pixels[Y * Size.X + X];
					const FColor& Peaked = Peaking[Y * Size.X + X];
					if (FMath::Abs(Plain.R - Peaked.R) + FMath::Abs(Plain.G - Peaked.G) + FMath::Abs(Plain.B - Peaked.B) > 60)
					{
						const bool bMiddle = FMath::Abs(X - Size.X / 2) < PictureWidth / 4;
						(bMiddle ? OnSubject : Elsewhere) += 1;
					}
				}
			}
			Test.AddInfo(FString::Printf(TEXT("Focus peaking marks %d pixels on the subject and %d on the rest of the set"), OnSubject, Elsewhere));
			Test.TestTrue(TEXT("Peaking marks the subject, which is in focus"), OnSubject > 200);
			Test.TestTrue(TEXT("Peaking leaves the soft background and foreground alone"), Elsewhere * 10 < OnSubject);
			Save(Peaking, TEXT("sample-stage-peaking.png"));
		}
		Save(Pixels, TEXT("sample-stage.png"));
		if (Test.TestEqual(TEXT("The false colour picture is read back"), FalseColour.Num(), Pixels.Num()))
		{
			Save(FalseColour, TEXT("sample-stage-false-colour.png"));
		}
	}
}

/**
 * The sample stage that ships in the plugin's content. Every run checks the saved level against
 * what the user guide says is in it. Started with -UEcineBuildSample the test first builds the
 * level again and saves it over the old one, and with a renderer it also saves a picture through
 * the stage's camera:
 * tools/test.ps1 -Filter UEcine.Editor.SampleScene -Rendering -EditorArgs "-UEcineBuildSample"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineSampleSceneTest, "UEcine.Editor.SampleScene", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineSampleSceneTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("UEcineBuildSample")))
	{
		const UWorld* World = LoadObject<UWorld>(nullptr, *FString::Printf(TEXT("%s.UEcineSampleStage"), SampleStagePackage));
		if (TestNotNull(TEXT("The sample stage is in the plugin's content"), World))
		{
			CheckSampleStage(*this, World);
		}
		return true;
	}

	if (!BuildSampleStage(*this))
	{
		return false;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	CheckSampleStage(*this, World);
	if (FApp::CanEverRender())
	{
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, World] { ShootSampleStage(*this, World); }, 2.0f));
	}
	return true;
}

#endif
