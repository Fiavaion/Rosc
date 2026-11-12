// Copyright 2026 Fiavaion. All rights reserved.

#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "MovieSceneSequencePlayer.h"
#include "ObjectTools.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineFocus.h"
#include "UEcineFocusEditing.h"
#include "UEcineSequenceUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The float channel Sequencer plays for a camera's focus distance, or null when it has no focus track. */
	const FMovieSceneFloatChannel* FindFocusChannel(ULevelSequence* Sequence, ACineCameraActor* Camera)
	{
		using namespace UEcine::SequenceUtils;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineFocusPullTest, "UEcine.Editor.Focus.Pull", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineFocusPullTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::FocusEditing;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	// At the origin, looking along X.
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform::Identity);
	if (!TestNotNull(TEXT("A camera to pull focus on"), Camera))
	{
		return false;
	}
	UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(Camera);
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
	TestEqual(TEXT("The first mark is at 1.5 m"), Package->FocusMarksCm[0], 150.0f, 0.01f);
	TestEqual(TEXT("The second mark is at 6 m"), Package->FocusMarksCm[1], 600.0f, 0.01f);
	TestTrue(TEXT("Marking can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("After undo there is one mark"), Package->FocusMarksCm.Num(), 1);
	TestTrue(TEXT("It can be redone"), GEditor->RedoTransaction());
	GoToFocusMark(Camera, 0);
	TestEqual(TEXT("Go to mark puts the focus on it"), GetFocusDistance(Camera), 150.0f, 0.01f);
	SetFocusDistance(Camera, 450.0f);
	UpdateFocusMark(Camera, 1);
	TestEqual(TEXT("Re-marking moves the mark"), Package->FocusMarksCm[1], 450.0f, 0.01f);
	TestEqual(TEXT("A third mark"), AddFocusMark(Camera), 2);
	RemoveFocusMark(Camera, 1);
	TestEqual(TEXT("Removing a mark leaves the others"), Package->FocusMarksCm.Num(), 2);
	TestEqual(TEXT("The marks after it move up"), Package->FocusMarksCm[1], 450.0f, 0.01f);
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
	const FUEcineMoveTiming Timing = UEcine::Focus::MakeRackTiming(2.0);
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
		const double Expected = UEcine::Focus::RackDistance(150.0, 600.0, Timing, (Frame - StartFrame) / 24.0);
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
			const float Expected = static_cast<float>(UEcine::Focus::RackDistance(150.0, 600.0, Timing, (Frame - StartFrame) / 24.0));
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
		TestTrue(TEXT("The new sequence is an asset under the UEcine folder"), NewSequence->GetPathName().StartsWith(TEXT("/Game/UEcine/Sequences/")));
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
	GEditor->ResetTransaction(FText::FromString(TEXT("UEcine focus test")));
	return true;
}

#endif
