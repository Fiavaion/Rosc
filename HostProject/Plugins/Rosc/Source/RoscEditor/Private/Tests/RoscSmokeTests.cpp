// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneToolsProjectSettings.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/RoscDeliverChoicesBefore.h"
#include "Tests/RoscRenderedFiles.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscDeliverEditing.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscHandoff.h"
#include "RoscMonitorSubsystem.h"
#include "RoscMove.h"
#include "RoscRenderSubsystem.h"
#include "RoscRig.h"
#include "RoscShotEditing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Half a second of shot keeps the two renders short. */
	constexpr int32 SmokeShotFrames = 12;
	constexpr double SmokeRenderTimeoutSeconds = 600.0;

	/** What the smoke test holds on to while its renders run over the editor's frames. */
	struct FSmokeState
	{
		TWeakObjectPtr<ACineCameraActor> Camera;
		TWeakObjectPtr<AActor> Hero;
		TArray<TWeakObjectPtr<ULevelSequence>> CreatedSequences;
		FString ShotName;
		/** The frame of the sequence after the shot's last: the last frame of a movie with a slate. */
		int32 FrameAfterShot = 0;
		FString OutputDirectory;

		/** The user's own choices and settings, to put back. */
		FRoscDeliverChoicesBefore ChoicesBefore;
		FRoscMonitorSettings MonitorBefore;
		float ShotSecondsBefore = 0.0f;

		/** Which of the test's renders is being waited for. */
		int32 RendersStarted = 0;
		double StartSeconds = 0.0;
		/** The furthest frame of the sequence at which the render being waited for was seen to look through a camera. */
		int32 LastFrameThroughCamera = MIN_int32;
		Rosc::DeliverEditing::FDelivery Delivery;
	};

	void CleanUpSmokeTest(FAutomationTestBase& Test, FSmokeState& State)
	{
		State.ChoicesBefore.PutBack();
		URoscMonitorSubsystem::Get()->SetSettings(State.MonitorBefore);
		GetMutableDefault<UMovieSceneToolsProjectSettings>()->DefaultDuration = State.ShotSecondsBefore;

		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		GEditor->SelectNone(false, true, false);
		if (ACineCameraActor* Camera = State.Camera.Get())
		{
			Rosc::GripEditing::TakeCameraOffRig(Camera);
			Camera->GetWorld()->EditorDestroyActor(Camera, false);
		}
		if (AActor* Hero = State.Hero.Get())
		{
			Hero->GetWorld()->EditorDestroyActor(Hero, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc smoke test")));

		// The handoff copies the VFX handoff made go first: they play the sequences they were made from.
		TArray<ULevelSequence*> ToRemove;
		for (const TWeakObjectPtr<ULevelSequence>& Sequence : State.CreatedSequences)
		{
			if (Sequence.IsValid())
			{
				const FString CopyPackage = Rosc::Handoff::GetHandoffPackageName(Sequence.Get());
				if (ULevelSequence* Copy = FindObject<ULevelSequence>(nullptr, *(CopyPackage + TEXT(".") + FPackageName::GetLongPackageAssetName(CopyPackage))))
				{
					ToRemove.Add(Copy);
				}
			}
		}
		for (const TWeakObjectPtr<ULevelSequence>& Sequence : State.CreatedSequences)
		{
			if (Sequence.IsValid())
			{
				ToRemove.Add(Sequence.Get());
			}
		}
		for (ULevelSequence* Sequence : ToRemove)
		{
			Test.TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
		}
	}

	/** Moves Sequencer's playhead to a frame of the open sequence and gives back where the camera then is. */
	FVector FindSmokeCameraAt(const ACineCameraActor& Camera, int32 Frame)
	{
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
		return Camera.GetCineCameraComponent()->GetComponentLocation();
	}

	/** The frame of its sequence the render is at, when the render is looking through a cine camera just now. */
	TOptional<int32> FindSmokeRenderFrameThroughCamera()
	{
		const UWorld* PlayWorld = GEditor->PlayWorld;
		const APlayerController* Player = PlayWorld != nullptr ? PlayWorld->GetFirstPlayerController() : nullptr;
		if (Player != nullptr && Cast<ACineCameraActor>(Player->GetViewTarget()) != nullptr)
		{
			for (TActorIterator<ALevelSequenceActor> It(PlayWorld); It; ++It)
			{
				return It->GetSequencePlayer()->GetCurrentTime().Time.FloorToFrame().Value;
			}
		}
		return {};
	}

	/** The share of a picture that is clearly not the same as in another picture of the same view. */
	float MeasureSmokeDifference(const TArray<FColor>& First, const TArray<FColor>& Second)
	{
		if (First.Num() != Second.Num() || First.IsEmpty())
		{
			return 0.0f;
		}
		int32 Different = 0;
		for (int32 Index = 0; Index < First.Num(); ++Index)
		{
			const FColor& A = First[Index];
			const FColor& B = Second[Index];
			Different += (FMath::Abs(A.R - B.R) > 24 || FMath::Abs(A.G - B.G) > 24 || FMath::Abs(A.B - B.B) > 24) ? 1 : 0;
		}
		return static_cast<float>(Different) / First.Num();
	}

	/** Looks through the camera in a level viewport with the monitor's tools, as the DOP would to judge exposure and focus. */
	void CheckSmokeMonitor(FAutomationTestBase& Test, ACineCameraActor* Camera)
	{
		FLevelEditorViewportClient* ViewportClient = nullptr;
		for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
		{
			if (Candidate != nullptr && Candidate->Viewport != nullptr && Candidate->IsPerspective() && Candidate->Viewport->GetSizeXY().GetMin() >= 200)
			{
				ViewportClient = Candidate;
				break;
			}
		}
		ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (!Test.TestNotNull(TEXT("There is a perspective level viewport to look through the camera in"), ViewportClient) || !Test.TestNotNull(TEXT("The level editor is there"), LevelEditor))
		{
			return;
		}

		URoscMonitorSubsystem* Monitor = URoscMonitorSubsystem::Get();
		LevelEditor->PilotLevelActor(Camera);
		// Game view hides the grid and gizmos, which the editor draws over the picture after the monitor's tools.
		const bool bWasGameView = ViewportClient->IsInGameView();
		ViewportClient->SetGameView(true);

		const auto Shoot = [Monitor, ViewportClient](const FRoscMonitorSettings& Settings)
		{
			Monitor->SetSettings(Settings);
			Monitor->UpdateCameraOverlays();
			TArray<FColor> Pixels;
			ViewportClient->Viewport->Draw(false);
			ViewportClient->Viewport->ReadPixels(Pixels);
			return Pixels;
		};
		FRoscMonitorSettings Exposure;
		Exposure.bFalseColour = true;
		FRoscMonitorSettings Focus;
		Focus.bPeaking = true;

		const TArray<FColor> Plain = Shoot(FRoscMonitorSettings());
		const float FalseColourShare = MeasureSmokeDifference(Plain, Shoot(Exposure));
		const float PeakingShare = MeasureSmokeDifference(Plain, Shoot(Focus));
		const float PlainAgainShare = MeasureSmokeDifference(Plain, Shoot(FRoscMonitorSettings()));
		Test.AddInfo(FString::Printf(TEXT("Monitor through the camera: false colour changes %.1f %% of the picture, peaking %.2f %%, nothing %.2f %%"),
			FalseColourShare * 100.0f, PeakingShare * 100.0f, PlainAgainShare * 100.0f));
		Test.TestTrue(TEXT("The picture through the camera is read back"), !Plain.IsEmpty());
		Test.TestTrue(TEXT("False colour redraws the picture to show exposure"), FalseColourShare > 0.3f);
		Test.TestTrue(TEXT("Peaking marks what is in focus"), PeakingShare > PlainAgainShare && PeakingShare > 0.0005f);
		Test.TestTrue(TEXT("With the tools off the picture is plain again"), PlainAgainShare < 0.01f);

		ViewportClient->SetGameView(bWasGameView);
		LevelEditor->EjectPilotLevelActor();
	}

	void CheckSmokeReview(FAutomationTestBase& Test, const FSmokeState& State)
	{
		const FRoscRenderResult& Result = GEditor->GetEditorSubsystem<URoscRenderSubsystem>()->GetLastResult();
		Test.TestTrue(TEXT("Review: the render succeeded"), Result.bSuccess);
		if (Test.TestEqual(TEXT("Review: one movie is written"), Result.Files.Num(), 1))
		{
			Test.AddInfo(FString::Printf(TEXT("Review: %s"), *Result.Files[0]));
			Test.TestTrue(TEXT("Review: the movie is in the review folder of the delivery"), FPaths::IsUnderDirectory(Result.Files[0], State.Delivery.Directory) && Result.Files[0].EndsWith(TEXT(".mov")));
			Test.TestEqual(TEXT("Review: the movie holds the slate, the shot and the tail frame"), Rosc::RenderedFiles::CountMovFrames(Result.Files[0]), SmokeShotFrames + 2);
			// The shot ends where the sequence does, and the renderer would play the tail frame after the sequence had stopped.
			Test.TestEqual(TEXT("Review: the tail frame is seen through the camera like the rest"), State.LastFrameThroughCamera, State.FrameAfterShot);
		}
	}

	void CheckSmokeVfxHandoff(FAutomationTestBase& Test, const FSmokeState& State)
	{
		using namespace Rosc::RenderedFiles;

		const FRoscRenderResult& Result = GEditor->GetEditorSubsystem<URoscRenderSubsystem>()->GetLastResult();
		Test.TestTrue(TEXT("VFX handoff: the render succeeded"), Result.bSuccess);
		if (!Test.TestEqual(TEXT("VFX handoff: one file a frame is written"), Result.Files.Num(), SmokeShotFrames))
		{
			return;
		}

		FExrHeader Header;
		if (Test.TestTrue(TEXT("VFX handoff: a frame's header is read"), ReadExrHeader(Result.Files[0], Header)))
		{
			const TArray<FString> Layers = GetExrLayers(Header);
			Test.AddInfo(FString::Printf(TEXT("VFX handoff: %s, %d x %d, layers: %s"), *Result.Files[0], Header.Size.X, Header.Size.Y, *FString::Join(Layers, TEXT(" | "))));
			for (const TCHAR* Layer : { TEXT("FinalImage"), TEXT("Deferred_depth"), TEXT("Deferred_normal"), TEXT("Deferred_motion") })
			{
				Test.TestTrue(*FString::Printf(TEXT("VFX handoff: the frame has the layer %s"), Layer), Layers.Contains(Layer));
			}
			Test.TestTrue(TEXT("VFX handoff: the frame has mattes"), Layers.ContainsByPredicate([](const FString& Layer) { return Layer.Contains(TEXT("ObjectID")); }));
			Test.TestEqual(TEXT("VFX handoff: the frame says which shot it is"), Header.Text.FindRef(TEXT("rosc/shot")), State.ShotName);
			Test.TestTrue(TEXT("VFX handoff: the frame says which camera body shot it"), Header.Text.FindRef(TEXT("rosc/camera/body")).Contains(TEXT("ALEXA 35")));
			Test.TestTrue(TEXT("VFX handoff: the frame says which lens kit was on it"), Header.Text.FindRef(TEXT("rosc/lens/kit")).Contains(TEXT("Signature Prime")));
			Test.TestTrue(TEXT("VFX handoff: the frame says the camera was on a crane"), Header.Text.FindRef(TEXT("rosc/camera/support")).Contains(TEXT("crane"), ESearchCase::IgnoreCase));
		}

		const FString* Sidecar = State.Delivery.Files.FindByPredicate([](const FString& File) { return File.EndsWith(TEXT(".json")); });
		const FString* CameraFile = State.Delivery.Files.FindByPredicate([](const FString& File) { return File.EndsWith(TEXT(".fbx")); });
		if (!Test.TestTrue(TEXT("VFX handoff: a sidecar file and a camera file go with the frames"), Sidecar != nullptr && CameraFile != nullptr))
		{
			return;
		}
		Test.TestTrue(TEXT("VFX handoff: the camera file is written"), IFileManager::Get().FileSize(**CameraFile) > 1000);

		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (Test.TestTrue(TEXT("VFX handoff: the sidecar is JSON"), FFileHelper::LoadFileToString(Json, **Sidecar) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
		{
			const TArray<TSharedPtr<FJsonValue>>& CameraFrames = Root->GetArrayField(TEXT("camera"));
			if (Test.TestEqual(TEXT("VFX handoff: the sidecar has the camera at every frame"), CameraFrames.Num(), SmokeShotFrames))
			{
				const auto ReadLocation = [](const TSharedPtr<FJsonValue>& Frame)
				{
					const TArray<TSharedPtr<FJsonValue>>& Location = Frame->AsObject()->GetArrayField(TEXT("location"));
					return Location.Num() == 3 ? FVector(Location[0]->AsNumber(), Location[1]->AsNumber(), Location[2]->AsNumber()) : FVector::ZeroVector;
				};
				Test.TestTrue(TEXT("VFX handoff: the crane move is in the camera data handed over"), FVector::Dist(ReadLocation(CameraFrames[0]), ReadLocation(CameraFrames.Last())) > 5.0);
				Test.TestTrue(TEXT("VFX handoff: the focus distance is in the camera data handed over"), CameraFrames[0]->AsObject()->GetNumberField(TEXT("focusDistance")) > 100.0);
			}
		}
	}
}

/**
 * The Success Metric, walked the way the panel does it: a camera package of a real body,
 * recording format and lens; the camera put on a crane and a move blocked and baked into a
 * shot; focus kept on the subject; exposure and focus judged with the monitor's tools through
 * the camera; then a review movie and a VFX handoff rendered and read back. Every step is the
 * operation the panel's controls call. The pictures and renders need a renderer
 * (tools/test.ps1 -Rendering), which is how the release gate runs it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscSmokeFullShotTest, "Rosc.Smoke.FullShot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscSmokeFullShotTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::DeliverEditing;

	Rosc::RenderedFiles::ExpectRenderLogNoise(*this);

	const TSharedRef<FSmokeState> State = MakeShared<FSmokeState>();
	State->MonitorBefore = URoscMonitorSubsystem::Get()->GetSettings();
	UMovieSceneToolsProjectSettings* SequencerSettings = GetMutableDefault<UMovieSceneToolsProjectSettings>();
	State->ShotSecondsBefore = SequencerSettings->DefaultDuration;
	SequencerSettings->DefaultDuration = SmokeShotFrames / 24.0f;
	State->OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Rosc/Renders/Smoke"));
	IFileManager::Get().DeleteDirectory(*State->OutputDirectory, false, true);
	EditChoices([&State](URoscEditorSettings& Choices)
	{
		Choices.DeliverFolder.Path = State->OutputDirectory;
		Choices.DeliverNamePattern = Rosc::Deliver::DefaultNamePattern;
		Choices.bDeliverSlate = true;
		Choices.bDeliverBurnIn = true;
	});

	// Camera: a new camera, then a real body, recording format and lens, and how it is exposed.
	UWorld* World = GEditor->GetEditorWorldContext().World();
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	ACineCameraActor* Camera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FRotator(-12.0, 0.0, 0.0), FVector(-600.0, 0.0, 220.0)));
	AStaticMeshActor* Hero = World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, 60.0), FRotator(0.0, 30.0, 0.0));
	State->Camera = Camera;
	State->Hero = Hero;
	const FRoscCameraDatabase& Database = FRoscCameraDatabase::Get();
	const FRoscCameraBody* Body = Database.FindBody(TEXT("arri-alexa-35"));
	const FRoscLensKit* Kit = Database.FindLensKit(TEXT("arri-signature-prime"));
	URoscCameraPackageComponent* Package = Camera != nullptr ? URoscCameraPackageComponent::Find(Camera) : nullptr;
	if (!TestNotNull(TEXT("A camera"), Camera) || !TestNotNull(TEXT("A subject"), Hero) || !TestNotNull(TEXT("The camera has a camera package"), Package)
		|| !TestNotNull(TEXT("The ALEXA 35 is in the database"), Body) || !TestNotNull(TEXT("The Signature Primes are in the database"), Kit))
	{
		CleanUpSmokeTest(*this, *State);
		return false;
	}
	Hero->SetActorLabel(TEXT("Hero"));
	Hero->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));

	const int32 LensIndex = Kit->Lenses.IndexOfByPredicate([](const FRoscLens& Lens) { return Lens.FocalLengthMm == 21.0; });
	FText Error;
	const bool bPackaged = TestTrue(TEXT("The kit has a 21 mm"), LensIndex != INDEX_NONE)
		&& Rosc::CameraEditing::EditPackage(Package, FText::FromString(TEXT("Build Camera Package")), [Body, Kit, LensIndex](FRoscCameraPackage& Edited)
		{
			Edited.SelectBody(*Body);
			Edited.FormatId = TEXT("hd-16x9-s16");
			Edited.SelectLensKit(*Kit);
			Edited.SelectLens(*Kit, LensIndex);
			Edited.TStop = 2.8f;
			Edited.ShutterAngleDeg = 172.8f;
			Edited.NdDensity = 0.6f;
			Edited.DeliveryAspect = 2.39f;
		}, Error);
	const FRoscRecordingFormat* Format = Body->FindFormat(TEXT("hd-16x9-s16"));
	if (!TestTrue(*FString::Printf(TEXT("The camera package is built (%s)"), *Error.ToString()), bPackaged) || !TestNotNull(TEXT("The body records HD"), Format))
	{
		CleanUpSmokeTest(*this, *State);
		return false;
	}
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	TestEqual(TEXT("The camera's sensor is the recording format's"), Lens->Filmback.SensorWidth, static_cast<float>(Format->SensorWidthMm), 0.001f);
	TestEqual(TEXT("The camera has the 21 mm on"), Lens->CurrentFocalLength, 21.0f, 0.001f);
	TestEqual(TEXT("The camera is at T2.8"), Lens->CurrentAperture, 2.8f, 0.001f);

	// Grip: onto a crane, two marks, a move between them that the crane can make.
	ARoscRig* Rig = Rosc::GripEditing::PutCameraOnRig(Camera, *Rosc::Rig::FindPreset(TEXT("crane-30ft")));
	if (!TestNotNull(TEXT("The camera is put on a crane"), Rig))
	{
		CleanUpSmokeTest(*this, *State);
		return false;
	}
	const FRoscRigPose First = Rig->GetPose();
	FRoscRigPose Second = First;
	// A small move: the shot is half a second, and a crane of this size does not hurry.
	Second.Swing += 1.0;
	Second.Boom += 1.0;
	Rosc::GripEditing::AddMark(Rig);
	Rosc::GripEditing::EditRig(Rig, FText::FromString(TEXT("Move Rig")), [&Second](ARoscRig& Edited) { Edited.SetPose(Second); });
	Rosc::GripEditing::AddMark(Rig);
	Rosc::GripEditing::EditRig(Rig, FText::FromString(TEXT("Time Move")), [](ARoscRig& Edited) { Edited.Marks[1].Arrival.DurationSeconds = SmokeShotFrames / 24.0; });
	TestEqual(TEXT("The move is within what the crane can do"), Rosc::Move::Check(Rig->GetSpec(), First, Second, Rig->Marks[1].Arrival).Num(), 0);
	Rosc::GripEditing::GoToMark(Rig, 0);

	// Shots: a shot through the camera, and the move baked into it.
	if (!TestTrue(TEXT("A shot is added"), Rosc::ShotEditing::AddShot(Camera, Error)))
	{
		AddError(Error.ToString());
		CleanUpSmokeTest(*this, *State);
		return false;
	}
	const TArray<Rosc::ShotEditing::FShot> Shots = Rosc::ShotEditing::GetShots();
	State->CreatedSequences.Add(ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence());
	for (const Rosc::ShotEditing::FShot& Shot : Shots)
	{
		State->CreatedSequences.AddUnique(Shot.Sequence);
	}
	if (!TestEqual(TEXT("The sequence has the one shot"), Shots.Num(), 1))
	{
		CleanUpSmokeTest(*this, *State);
		return false;
	}
	State->ShotName = Shots[0].Name;
	State->FrameAfterShot = Shots[0].StartFrame.Value + SmokeShotFrames;
	Rosc::ShotEditing::GoToShot(Shots[0]);
	TestTrue(TEXT("The crane move is baked into the shot"), Rosc::GripEditing::BakeMoveToSequencer(Rig, Error) == Shots[0].Sequence);
	const FVector CameraAtStart = FindSmokeCameraAt(*Camera, Shots[0].StartFrame.Value);
	const FVector CameraAtEnd = FindSmokeCameraAt(*Camera, Shots[0].StartFrame.Value + SmokeShotFrames - 1);
	AddInfo(FString::Printf(TEXT("The crane carries the camera %.1f cm over the shot"), FVector::Dist(CameraAtStart, CameraAtEnd)));
	TestTrue(TEXT("Played from Sequencer, the crane carries the camera"), FVector::Dist(CameraAtStart, CameraAtEnd) > 5.0);
	FindSmokeCameraAt(*Camera, Shots[0].StartFrame.Value);

	// Focus: kept on the subject.
	Rosc::FocusEditing::FollowActor(Camera, Hero);
	const float SubjectDistance = static_cast<float>(FVector::Dist(Lens->GetComponentLocation(), Hero->GetActorLocation()));
	TestEqual(TEXT("The focus is on the subject"), Rosc::FocusEditing::GetFocusDistance(Camera), SubjectDistance, 0.2f * SubjectDistance);

	// Deliver: preflight finds nothing that stops either render.
	for (const ERoscDeliverIntent Intent : { ERoscDeliverIntent::Review, ERoscDeliverIntent::VfxHandoff })
	{
		for (const FRoscPreflightFinding& Finding : CheckDelivery(Intent).Findings)
		{
			AddInfo(FString::Printf(TEXT("Preflight, %s: %s (%s)"), *Rosc::Deliver::GetIntentName(Intent).ToString(), *Finding.Title.ToString(), Finding.bStopsRender ? TEXT("stops the render") : TEXT("worth knowing")));
			TestFalse(*FString::Printf(TEXT("Preflight finds nothing that stops the %s"), *Rosc::Deliver::GetIntentName(Intent).ToString()), Finding.bStopsRender);
		}
	}

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Monitor pictures and renders skipped: they need a renderer (run tools/test.ps1 with -Rendering)."));
		CleanUpSmokeTest(*this, *State);
		return true;
	}

	// Monitor: exposure and focus judged through the camera.
	CheckSmokeMonitor(*this, Camera);

	// Deliver: a review movie, then a VFX handoff, each read back when its render is over.
	State->StartSeconds = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]
	{
		URoscRenderSubsystem* Render = GEditor->GetEditorSubsystem<URoscRenderSubsystem>();
		if (Render->IsRendering())
		{
			State->LastFrameThroughCamera = FMath::Max(State->LastFrameThroughCamera, FindSmokeRenderFrameThroughCamera().Get(MIN_int32));
			if (FPlatformTime::Seconds() - State->StartSeconds < SmokeRenderTimeoutSeconds)
			{
				return false;
			}
			AddError(FString::Printf(TEXT("Render %d did not finish in %.0f seconds"), State->RendersStarted, SmokeRenderTimeoutSeconds));
			Render->CancelRender();
			CleanUpSmokeTest(*this, *State);
			return true;
		}

		static const ERoscDeliverIntent Deliveries[] = { ERoscDeliverIntent::Review, ERoscDeliverIntent::VfxHandoff };
		if (State->RendersStarted == 1)
		{
			CheckSmokeReview(*this, *State);
		}
		else if (State->RendersStarted == 2)
		{
			CheckSmokeVfxHandoff(*this, *State);
		}

		FText StartError;
		if (State->RendersStarted < UE_ARRAY_COUNT(Deliveries))
		{
			State->StartSeconds = FPlatformTime::Seconds();
			State->LastFrameThroughCamera = MIN_int32;
			if (StartDelivery(Deliveries[State->RendersStarted++], State->Delivery, StartError))
			{
				return false;
			}
			AddError(FString::Printf(TEXT("Delivery %d did not start: %s"), State->RendersStarted, *StartError.ToString()));
		}
		CleanUpSmokeTest(*this, *State);
		return true;
	}));
	return true;
}

#endif
