// Copyright 2026 Fiavaion. All rights reserved.

#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "MovieSceneSequencePlayer.h"
#include "ObjectTools.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscFocus.h"
#include "RoscFocusEditing.h"
#include "RoscSequenceUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The float channel Sequencer plays for a camera's focus distance, or null when it has no focus track. */
	const FMovieSceneFloatChannel* FindFocusChannel(ULevelSequence* Sequence, ACineCameraActor* Camera)
	{
		using namespace Rosc::SequenceUtils;
		const UMovieSceneFloatTrack* Track = FindFloatTrack(Sequence->GetMovieScene(), FindBinding(Sequence, Camera->GetCineCameraComponent()), TEXT("ManualFocusDistance"));
		return Track != nullptr && !Track->GetAllSections().IsEmpty() ? &CastChecked<UMovieSceneFloatSection>(Track->GetAllSections()[0])->GetChannel() : nullptr;
	}

	/** Where the engine itself focuses the camera: what its depth of field is rendered with. */
	float GetRenderedFocusDistance(ACineCameraActor* Camera)
	{
		FMinimalViewInfo View;
		Camera->GetCineCameraComponent()->GetCameraView(0.0f, View);
		return View.PostProcessSettings.DepthOfFieldFocalDistance;
	}
}

/**
 * The focus puller's workflow: set focus, focus on a point, follow a subject, mark focus, then
 * bake racks and scrub them in the real Sequencer to see where the lens actually is.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscFocusPullTest, "Rosc.Editor.Focus.Pull", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscFocusPullTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::FocusEditing;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	// At the origin, looking along X.
	ACineCameraActor* Camera = Rosc::CameraEditing::SpawnCamera(World, FTransform::Identity);
	if (!TestNotNull(TEXT("A camera to pull focus on"), Camera))
	{
		return false;
	}
	UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	URoscCameraPackageComponent* Package = URoscCameraPackageComponent::Find(Camera);
	const float CloseFocus = GetCloseFocus(Camera);
	TestTrue(TEXT("The lens has a close focus under a metre"), CloseFocus > 1.0f && CloseFocus < 100.0f);

	// Focus set by hand, in one undoable step.
	const float DistanceBefore = GetFocusDistance(Camera);
	SetFocusDistance(Camera, 300.0f);
	TestEqual(TEXT("The focus is set to 3 m"), GetFocusDistance(Camera), 300.0f, 0.01f);
	TestEqual(TEXT("The engine renders with the focus at 3 m"), GetRenderedFocusDistance(Camera), 300.0f, 0.01f);
	TestTrue(TEXT("Setting focus can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("After undo the focus is where it was"), GetFocusDistance(Camera), DistanceBefore, 0.01f);
	TestTrue(TEXT("It can be redone"), GEditor->RedoTransaction());
	TestEqual(TEXT("After redo the focus is at 3 m"), GetFocusDistance(Camera), 300.0f, 0.01f);

	SetFocusDistance(Camera, CloseFocus * 0.5f);
	TestEqual(TEXT("Focus nearer than the lens can go stops at its close focus"), GetFocusDistance(Camera), CloseFocus, 0.01f);

	// Focus on a point: the distance along the lens axis, not the straight-line distance.
	FText Error;
	TestTrue(TEXT("Focus on a point in front of the camera"), FocusOnPoint(Camera, FVector(500.0, 120.0, -40.0), Error));
	TestEqual(TEXT("The focus is the point's distance along the lens axis"), GetFocusDistance(Camera), 500.0f, 0.01f);
	TestFalse(TEXT("A point behind the camera is refused"), FocusOnPoint(Camera, FVector(-100.0, 0.0, 0.0), Error));
	TestFalse(TEXT("The refusal says why"), Error.IsEmpty());
	TestEqual(TEXT("A refused point leaves the focus alone"), GetFocusDistance(Camera), 500.0f, 0.01f);

	// Click-to-focus waits for a click in the viewport and can be called off.
	BeginPickFocus(Camera);
	TestTrue(TEXT("The camera is waiting for a click to focus on"), IsPickingFocus(Camera));
	EndPickFocus();
	TestFalse(TEXT("Picking is called off"), IsPickingFocus(Camera));

	// The first click of a fresh editor, where no viewport is current yet, focuses: the view looks
	// along the camera at a wall that fills it, wherever the click lands. The picker looks through
	// the viewport's picture, so this needs a renderer (tools/test.ps1 -Rendering).
	const TSharedPtr<ILevelEditor> LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstLevelEditor();
	const TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor.IsValid() ? LevelEditor->GetActiveViewportInterface() : nullptr;
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped the first click in the viewport: it needs a renderer (tools/test.ps1 -Rendering)."));
	}
	else if (TestTrue(TEXT("The level editor has an active viewport"), ActiveViewport.IsValid()))
	{
		FLevelEditorViewportClient& View = ActiveViewport->GetLevelViewportClient();
		const FVector ViewLocationBefore = View.GetViewLocation();
		const FRotator ViewRotationBefore = View.GetViewRotation();
		View.SetViewLocation(FVector::ZeroVector);
		View.SetViewRotation(FRotator::ZeroRotator);
		AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(FVector(700.0, 0.0, 0.0), FRotator::ZeroRotator);
		Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Wall->SetActorScale3D(FVector(0.2, 60.0, 60.0));
		{
			TGuardValue<FLevelEditorViewportClient*> FreshEditor(GCurrentLevelEditingViewportClient, nullptr);
			BeginPickFocus(Camera);
			TestTrue(TEXT("Picking makes the active viewport the current one"), GCurrentLevelEditingViewportClient == &View);
			GLevelEditorModeTools().InputKey(&View, View.Viewport, EKeys::LeftMouseButton, IE_Pressed);
		}
		TestEqual(TEXT("The first click focuses on the wall's face"), GetFocusDistance(Camera), 690.0f, 1.0f);
		EndPickFocus();
		World->EditorDestroyActor(Wall, false);
		View.SetViewLocation(ViewLocationBefore);
		View.SetViewRotation(ViewRotationBefore);
	}

	// Follow focus: the focus stays on a subject as it moves.
	AStaticMeshActor* Subject = World->SpawnActor<AStaticMeshActor>(FVector(800.0, 200.0, 0.0), FRotator::ZeroRotator);
	Subject->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	TestNull(TEXT("A camera focused by hand follows nothing"), GetFollowedActor(Camera));
	FollowActor(Camera, Subject);
	TestTrue(TEXT("The camera follows the subject"), GetFollowedActor(Camera) == Subject);
	TestEqual(TEXT("The focus is on the subject"), GetFocusDistance(Camera), 800.0f, 0.01f);
	Subject->SetActorLocation(FVector(1200.0, -300.0, 50.0));
	TestEqual(TEXT("The focus follows the subject as it moves"), GetFocusDistance(Camera), 1200.0f, 0.01f);
	TestEqual(TEXT("The engine renders with the focus on the subject"), GetRenderedFocusDistance(Camera), 1200.0f, 0.01f);
	Subject->SetActorLocation(FVector(10.0, 0.0, 0.0));
	TestTrue(TEXT("A subject nearer than the lens can focus is reported as it is"), GetFocusDistance(Camera) < CloseFocus);
	TestEqual(TEXT("The engine holds the lens at its close focus"), GetRenderedFocusDistance(Camera), CloseFocus, 0.01f);
	Subject->SetActorLocation(FVector(900.0, 0.0, 0.0));
	StopFollowing(Camera);
	TestNull(TEXT("The camera stops following"), GetFollowedActor(Camera));
	TestEqual(TEXT("The focus stays where the subject was"), GetFocusDistance(Camera), 900.0f, 0.01f);
	TestTrue(TEXT("Stopping can be undone"), GEditor->UndoTransaction());
	TestTrue(TEXT("After undo the camera follows the subject again"), GetFollowedActor(Camera) == Subject);
	StopFollowing(Camera);

	// Focus marks, kept with the camera package.
	SetFocusDistance(Camera, 150.0f);
	TestEqual(TEXT("First focus mark"), AddFocusMark(Camera), 0);
	SetFocusDistance(Camera, 600.0f);
	TestEqual(TEXT("Second focus mark"), AddFocusMark(Camera), 1);
	TestEqual(TEXT("The first mark is at 1.5 m"), Package->FocusMarks[0].DistanceCm, 150.0f, 0.01f);
	TestEqual(TEXT("The second mark is at 6 m"), Package->FocusMarks[1].DistanceCm, 600.0f, 0.01f);
	TestTrue(TEXT("Marking can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("After undo there is one mark"), Package->FocusMarks.Num(), 1);
	TestTrue(TEXT("It can be redone"), GEditor->RedoTransaction());
	GoToFocusMark(Camera, 0);
	TestEqual(TEXT("Go to mark puts the focus on it"), GetFocusDistance(Camera), 150.0f, 0.01f);
	SetFocusDistance(Camera, 450.0f);
	UpdateFocusMark(Camera, 1);
	TestEqual(TEXT("Re-marking moves the mark"), Package->FocusMarks[1].DistanceCm, 450.0f, 0.01f);
	TestEqual(TEXT("A third mark"), AddFocusMark(Camera), 2);
	RemoveFocusMark(Camera, 1);
	TestEqual(TEXT("Removing a mark leaves the others"), Package->FocusMarks.Num(), 2);
	TestEqual(TEXT("The marks after it move up"), Package->FocusMarks[1].DistanceCm, 450.0f, 0.01f);
	SetFocusDistance(Camera, 600.0f);
	UpdateFocusMark(Camera, 1);
	SetRackTime(Camera, 2.0f);
	TestEqual(TEXT("The rack time is kept"), Package->RackSeconds, 2.0f, 1.e-4f);
	SetRackTime(Camera, 0.0f);
	TestEqual(TEXT("A rack cannot take no time"), Package->RackSeconds, 0.1f, 1.e-4f);
	SetRackTime(Camera, 2.0f);

	// Bake a two-second rack from 1.5 m to 6 m, starting one second in.
	GoToFocusMark(Camera, 0);
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional);
	Sequence->Initialize();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate DisplayRate(24, 1);
	MovieScene->SetDisplayRate(DisplayRate);
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const auto ToTime = [&DisplayRate, &TickResolution](int32 Frame) { return ConvertFrameTime(FFrameTime(Frame), DisplayRate, TickResolution); };
	const FRoscMoveTiming Timing = Rosc::Focus::MakeRackTiming(2.0);
	const int32 StartFrame = 24;
	const int32 EndFrame = StartFrame + 48;

	TestTrue(TEXT("The rack bakes"), BakeRack(Camera, 600.0f, 2.0, Sequence, StartFrame, Error));
	TestTrue(TEXT("The playback range takes in the rack"), MovieScene->GetPlaybackRange().Contains(ToTime(EndFrame).RoundToFrame()));
	const FMovieSceneFloatChannel* Channel = FindFocusChannel(Sequence, Camera);
	if (!TestNotNull(TEXT("The camera's focus distance has a track"), Channel))
	{
		return false;
	}
	TestEqual(TEXT("The rack has a key on every frame, both ends included"), Channel->GetNumKeys(), 49);
	for (int32 Frame = StartFrame - 12; Frame <= EndFrame + 12; ++Frame)
	{
		float Keyed = 0.0f;
		Channel->Evaluate(ToTime(Frame), Keyed);
		const double Expected = Rosc::Focus::RackDistance(150.0, 600.0, Timing, (Frame - StartFrame) / 24.0);
		TestEqual(*FString::Printf(TEXT("Focus distance at frame %d"), Frame), static_cast<double>(Keyed), Expected, 0.01);
	}
	float HalfWay = 0.0f;
	Channel->Evaluate(ToTime(StartFrame + 24), HalfWay);
	TestEqual(TEXT("Half way through, the lens is at 2.4 m: half way round the focus ring"), HalfWay, 240.0f, 0.01f);

	// Baking twice replaces the keys rather than piling them up.
	GoToFocusMark(Camera, 0);
	TestTrue(TEXT("The rack bakes again"), BakeRack(Camera, 600.0f, 2.0, Sequence, StartFrame, Error));
	TestEqual(TEXT("Rebaking replaces the keys"), Channel->GetNumKeys(), 49);

	// A second rack later in the shot leaves from where the first one left the focus, and keeps the first.
	TestTrue(TEXT("A rack back bakes"), BakeRack(Camera, 150.0f, 1.0, Sequence, 96, Error));
	TestEqual(TEXT("Both racks are in the track"), Channel->GetNumKeys(), 49 + 25);
	float RackBackStart = 0.0f;
	float RackBackEnd = 0.0f;
	Channel->Evaluate(ToTime(96), RackBackStart);
	Channel->Evaluate(ToTime(120), RackBackEnd);
	TestEqual(TEXT("The rack back leaves from 6 m"), RackBackStart, 600.0f, 0.01f);
	TestEqual(TEXT("The rack back arrives at 1.5 m"), RackBackEnd, 150.0f, 0.01f);

	// The real thing: open the sequence in Sequencer, scrub, and see where the lens actually is.
	if (TestTrue(TEXT("The sequence opens in Sequencer"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence)))
	{
		for (int32 Frame = StartFrame; Frame <= EndFrame; Frame += 6)
		{
			ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
			const float Expected = static_cast<float>(Rosc::Focus::RackDistance(150.0, 600.0, Timing, (Frame - StartFrame) / 24.0));
			TestEqual(*FString::Printf(TEXT("Scrubbed to frame %d: the camera's focus"), Frame), GetFocusDistance(Camera), Expected, 0.01f);
			TestEqual(*FString::Printf(TEXT("Scrubbed to frame %d: the focus the engine renders with"), Frame), GetRenderedFocusDistance(Camera), Expected, 0.01f);
		}

		// What the panel's rack button does with a sequence open: rack from the playhead.
		const int32 PlayheadFrame = 144;
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(PlayheadFrame), EUpdatePositionMethod::Scrub));
		TestTrue(TEXT("Racking to a mark uses the open sequence"), RackToFocusMark(Camera, 1, Error) == Sequence);
		float AtPlayhead = 0.0f;
		float AfterRack = 0.0f;
		Channel->Evaluate(ToTime(PlayheadFrame), AtPlayhead);
		Channel->Evaluate(ToTime(PlayheadFrame + 48), AfterRack);
		TestEqual(TEXT("Racked at the playhead, the focus leaves from where it was"), AtPlayhead, 150.0f, 0.01f);
		TestEqual(TEXT("It reaches the mark after the camera's rack time"), AfterRack, 600.0f, 0.01f);

		// A rack on a camera that is following a subject: it leaves from the subject and the camera stops following.
		FollowActor(Camera, Subject);
		TestTrue(TEXT("A rack bakes from follow focus"), BakeRack(Camera, 150.0f, 1.0, Sequence, 240, Error));
		TestNull(TEXT("Keyed focus is set by hand, so the camera stops following"), GetFollowedActor(Camera));
		float FromSubject = 0.0f;
		Channel->Evaluate(ToTime(240), FromSubject);
		TestEqual(TEXT("The rack leaves from where the sequence has the focus on that frame"), FromSubject, 600.0f, 0.01f);

		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	}

	// With nothing open, a rack makes a sequence at the camera's frame rate and opens it.
	TestNull(TEXT("No sequence is open"), ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence());
	GoToFocusMark(Camera, 0);
	ULevelSequence* NewSequence = RackToFocusMark(Camera, 1, Error);
	if (TestNotNull(TEXT("Racking with nothing open makes a sequence"), NewSequence))
	{
		TestTrue(TEXT("The new sequence is an asset under the Rosc folder"), NewSequence->GetPathName().StartsWith(TEXT("/Game/Rosc/Sequences/")));
		TestTrue(TEXT("The new sequence is named after the camera and what it holds"), NewSequence->GetName().EndsWith(TEXT("_Focus")));
		TestTrue(TEXT("The new sequence is open"), ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence() == NewSequence);
		const FMovieSceneFloatChannel* NewChannel = FindFocusChannel(NewSequence, Camera);
		if (TestNotNull(TEXT("The new sequence has the rack"), NewChannel))
		{
			float First = 0.0f;
			float Last = 0.0f;
			NewChannel->Evaluate(ConvertFrameTime(FFrameTime(0), DisplayRate, NewSequence->GetMovieScene()->GetTickResolution()), First);
			NewChannel->Evaluate(ConvertFrameTime(FFrameTime(48), DisplayRate, NewSequence->GetMovieScene()->GetTickResolution()), Last);
			TestEqual(TEXT("In the new sequence the rack starts on the first frame"), First, 150.0f, 0.01f);
			TestEqual(TEXT("In the new sequence the rack reaches the mark"), Last, 600.0f, 0.01f);
		}
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(NewSequence, false));
	}

	// A rack to a mark that is not there is refused, with the reason, and makes nothing.
	TestNull(TEXT("There is no ninth mark to rack to"), RackToFocusMark(Camera, 8, Error));
	TestFalse(TEXT("The refusal says why"), Error.IsEmpty());
	TestNull(TEXT("A refused rack makes no sequence"), ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence());

	GEditor->SelectNone(false, true, false);
	World->EditorDestroyActor(Subject, false);
	World->EditorDestroyActor(Camera, false);
	GEditor->ResetTransaction(FText::FromString(TEXT("Rosc focus test")));
	return true;
}

/**
 * Focus on a point: pick and hold a point on a subject, mark it, go back to it, and rack to it
 * in a shot where the camera travels, reading the frames in the real Sequencer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscFocusPointsTest, "Rosc.Editor.Focus.Points", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscFocusPointsTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::FocusEditing;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	// At the origin, looking along X.
	ACineCameraActor* Camera = Rosc::CameraEditing::SpawnCamera(World, FTransform::Identity);
	AStaticMeshActor* Subject = World->SpawnActor<AStaticMeshActor>(FVector(600.0, 0.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("A camera to pull focus on"), Camera) || !TestNotNull(TEXT("A subject"), Subject))
	{
		return false;
	}
	Subject->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Subject->SetActorLabel(TEXT("Anna"));
	URoscCameraPackageComponent* Package = URoscCameraPackageComponent::Find(Camera);
	FText Error;

	// Hold a point on the subject: an eye, 40 cm nearer the camera than the subject's middle.
	SetFocusDistance(Camera, 150.0f);
	TestFalse(TEXT("A camera focused by hand holds nothing"), IsFollowing(Camera));
	TestTrue(TEXT("A point on the subject is held"), HoldPoint(Camera, Subject, FVector(560.0, 30.0, 20.0), Error));
	TestTrue(TEXT("The focus is held on a point"), IsFollowing(Camera) && IsHoldingPoint(Camera));
	TestTrue(TEXT("The point is on the subject"), GetFollowedActor(Camera) == Subject);
	TestEqual(TEXT("The focus is at the point, not the subject's middle"), GetFocusDistance(Camera), 560.0f, 0.01f);
	TestEqual(TEXT("The engine renders with the focus at the point"), GetRenderedFocusDistance(Camera), 560.0f, 0.01f);
	Subject->SetActorLocation(FVector(700.0, 0.0, 0.0));
	TestEqual(TEXT("The point moves with the subject"), GetFocusDistance(Camera), 660.0f, 0.01f);
	Camera->SetActorLocation(FVector(100.0, 0.0, 0.0));
	TestEqual(TEXT("And stays in focus when the camera moves"), GetRenderedFocusDistance(Camera), 560.0f, 0.01f);
	Camera->SetActorLocation(FVector::ZeroVector);
	Subject->SetActorLocation(FVector(600.0, 0.0, 0.0));

	FollowActor(Camera, Subject);
	TestTrue(TEXT("Following a subject as a whole is not holding a point"), IsFollowing(Camera) && !IsHoldingPoint(Camera));
	TestFalse(TEXT("A point behind the camera is refused"), HoldPoint(Camera, Subject, FVector(-200.0, 0.0, 0.0), Error));
	TestTrue(TEXT("A refused point leaves the focus where it was"), !IsHoldingPoint(Camera) && GetFollowedActor(Camera) == Subject);
	TestTrue(TEXT("A place in the level can be held with no subject"), HoldPoint(Camera, nullptr, FVector(400.0, 0.0, 0.0), Error));
	TestTrue(TEXT("That is holding a point, on no subject"), IsHoldingPoint(Camera) && GetFollowedActor(Camera) == nullptr);
	TestEqual(TEXT("The focus is at the place"), GetFocusDistance(Camera), 400.0f, 0.01f);
	TestTrue(TEXT("Holding a point can be undone"), GEditor->UndoTransaction());
	TestTrue(TEXT("After undo the focus follows the subject again"), !IsHoldingPoint(Camera) && GetFollowedActor(Camera) == Subject);

	// Pick and hold waits for its own click.
	BeginPickFocus(Camera, true);
	TestTrue(TEXT("The camera is waiting for a click to hold"), IsPickingFocus(Camera, true) && !IsPickingFocus(Camera, false));
	EndPickFocus();
	TestFalse(TEXT("Picking is called off"), IsPickingFocus(Camera, true));

	// Marks: one at a distance, one on the point.
	SetFocusDistance(Camera, 150.0f);
	TestEqual(TEXT("A mark at a distance"), AddFocusMark(Camera), 0);
	HoldPoint(Camera, Subject, FVector(560.0, 30.0, 20.0), Error);
	TestEqual(TEXT("A mark on the point"), AddFocusMark(Camera), 1);
	TestFalse(TEXT("The first mark is a distance"), Package->FocusMarks[0].bPoint);
	TestTrue(TEXT("The second mark is the point on the subject"), Package->FocusMarks[1].bPoint && Package->FocusMarks[1].Subject == Subject);
	Subject->SetActorLocation(FVector(800.0, 0.0, 0.0));
	TestEqual(TEXT("A mark at a distance stays where it was"), GetFocusMarkDistance(Camera, 0), 150.0f, 0.01f);
	TestEqual(TEXT("A mark on a point is measured to where the point is now"), GetFocusMarkDistance(Camera, 1), 760.0f, 0.01f);
	Subject->SetActorLocation(FVector(600.0, 0.0, 0.0));
	GoToFocusMark(Camera, 0);
	TestTrue(TEXT("Going to a distance mark sets the focus by hand"), !IsFollowing(Camera) && FMath::IsNearlyEqual(GetFocusDistance(Camera), 150.0f, 0.01f));
	GoToFocusMark(Camera, 1);
	TestTrue(TEXT("Going to a point mark holds the point"), IsHoldingPoint(Camera) && GetFollowedActor(Camera) == Subject);
	GoToFocusMark(Camera, 0);

	// A shot in which the camera travels 2 m towards the subject over its first two seconds.
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional);
	Sequence->Initialize();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate DisplayRate(24, 1);
	MovieScene->SetDisplayRate(DisplayRate);
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const auto ToTime = [&DisplayRate, &TickResolution](int32 Frame) { return ConvertFrameTime(FFrameTime(Frame), DisplayRate, TickResolution); };
	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(ToTime(0).RoundToFrame(), ToTime(72).RoundToFrame()));
	UMovieScene3DTransformTrack* Travel = MovieScene->AddTrack<UMovieScene3DTransformTrack>(Rosc::SequenceUtils::FindOrAddBinding(Sequence, Camera));
	UMovieScene3DTransformSection* TravelSection = CastChecked<UMovieScene3DTransformSection>(Travel->CreateNewSection());
	TravelSection->SetRange(TRange<FFrameNumber>::All());
	Travel->AddSection(*TravelSection);
	FMovieSceneDoubleChannel* TravelX = TravelSection->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(0);
	TravelX->AddLinearKey(ToTime(0).RoundToFrame(), 0.0);
	TravelX->AddLinearKey(ToTime(48).RoundToFrame(), 200.0);

	if (TestTrue(TEXT("The shot opens in Sequencer"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence)))
	{
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(0), EUpdatePositionMethod::Scrub));
		TestTrue(TEXT("Racking to the point mark uses the open shot"), RackToFocusMark(Camera, 1, Error) == Sequence);
		const FMovieSceneFloatChannel* Channel = FindFocusChannel(Sequence, Camera);
		if (TestNotNull(TEXT("The camera's focus distance has a track"), Channel))
		{
			TestEqual(TEXT("A key on every frame the shot plays: the rack, then the hold"), Channel->GetNumKeys(), 72);
			const auto Keyed = [Channel, &ToTime](int32 Frame)
			{
				float Value = 0.0f;
				Channel->Evaluate(ToTime(Frame), Value);
				return Value;
			};
			TestEqual(TEXT("The rack leaves from where the focus was"), Keyed(0), 150.0f, 0.01f);
			// The rack takes the camera's one second. By then the camera has come 1 m closer.
			TestEqual(TEXT("At the end of the rack the focus is on the point, measured from where the camera is by then"), Keyed(24), 460.0f, 0.5f);
			TestEqual(TEXT("While the camera keeps travelling the point stays sharp"), Keyed(36), 410.0f, 0.5f);
			TestEqual(TEXT("And when the camera has stopped"), Keyed(60), 360.0f, 0.5f);
			TestEqual(TEXT("To the last frame the shot plays"), Keyed(71), 360.0f, 0.5f);
		}
		TestEqual(TEXT("The playhead is back where it was"), ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition().Frame.FloorToFrame().Value, 0);

		// The real thing: scrub, and compare the engine's focus with the distance to the point.
		for (int32 Frame = 24; Frame <= 71; Frame += 12)
		{
			ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
			const float ToPoint = 560.0f - static_cast<float>(Camera->GetActorLocation().X);
			TestEqual(*FString::Printf(TEXT("Scrubbed to frame %d: the engine focuses on the point"), Frame), GetRenderedFocusDistance(Camera), ToPoint, 0.5f);
		}
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	}

	GEditor->SelectNone(false, true, false);
	World->EditorDestroyActor(Subject, false);
	World->EditorDestroyActor(Camera, false);
	GEditor->ResetTransaction(FText::FromString(TEXT("Rosc focus points test")));
	return true;
}

#endif
