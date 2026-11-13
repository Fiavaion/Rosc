// Copyright 2026 Fiavaion. All rights reserved.

#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/SplineComponent.h"
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
#include "UEcineCameraDatabase.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineMove.h"
#include "UEcineOperator.h"
#include "UEcineRig.h"
#include "UEcineSequenceUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FUEcineMoveTiming MakeTiming(double Duration, double EaseIn, double EaseOut)
	{
		FUEcineMoveTiming Timing;
		Timing.DurationSeconds = Duration;
		Timing.EaseInFraction = EaseIn;
		Timing.EaseOutFraction = EaseOut;
		return Timing;
	}

	/** The float channel Sequencer plays for one rig axis, or null when the axis has no track. */
	const FMovieSceneFloatChannel* FindAxisChannel(ULevelSequence* Sequence, AUEcineRig* Rig, EUEcineRigAxis Axis)
	{
		const UMovieScene* MovieScene = Sequence->GetMovieScene();
		const FMovieSceneBinding* Binding = MovieScene->FindBinding(UEcine::SequenceUtils::FindBinding(Sequence, Rig));
		if (Binding == nullptr)
		{
			return nullptr;
		}
		for (const UMovieSceneTrack* Track : Binding->GetTracks())
		{
			const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
			if (FloatTrack != nullptr && FloatTrack->GetPropertyName() == AUEcineRig::GetAxisPropertyName(Axis) && !FloatTrack->GetAllSections().IsEmpty())
			{
				return &CastChecked<UMovieSceneFloatSection>(FloatTrack->GetAllSections()[0])->GetChannel();
			}
		}
		return nullptr;
	}
}

/**
 * The Phase 2 workflow: put a camera on a crane, mark three positions, bake the move, then play
 * it in the real Sequencer and compare where the camera is with where the solver says it should be.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineGripCraneMoveTest, "UEcine.Editor.Grip.CraneMove", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineGripCraneMoveTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::GripEditing;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	const FTransform CameraStart(FRotator(-5.0, 30.0, 0.0), FVector(200.0, -300.0, 380.0));
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, CameraStart);
	if (!TestNotNull(TEXT("A camera to put on the crane"), Camera))
	{
		return false;
	}

	// Put the camera on a crane. The camera must not move; the crane is built under it.
	const FUEcineRigPreset& Crane = *UEcine::Rig::FindPreset(TEXT("crane-30ft"));
	TestNull(TEXT("A new camera is on no rig"), FindRig(Camera));
	TestFalse(TEXT("A free camera can be moved in the viewport"), Camera->IsLockLocation());
	AUEcineRig* Rig = PutCameraOnRig(Camera, Crane);
	if (!TestNotNull(TEXT("The camera is put on a crane"), Rig))
	{
		return false;
	}
	TestTrue(TEXT("On a support the camera is held: the viewport cannot move it off its mount"), Camera->IsLockLocation());
	TestTrue(TEXT("The rig is found from the camera"), FindRig(Camera) == Rig);
	TestTrue(TEXT("The camera did not move"), Camera->GetActorLocation().Equals(CameraStart.GetLocation(), 0.05));
	TestTrue(TEXT("The camera did not turn"), Camera->GetActorQuat().Equals(CameraStart.GetRotation(), 1.e-4));
	TestTrue(TEXT("The rig is named after its camera"), Rig->GetActorLabel().EndsWith(TEXT("Crane")));
	TestTrue(TEXT("A crane comes with a remote head operator"), Rig->Operator.Style == EUEcineOperatorStyle::RemoteHead);

	// One undo takes the camera off again, one redo puts it back.
	TestTrue(TEXT("Putting the camera on a rig can be undone"), GEditor->UndoTransaction());
	TestNull(TEXT("After undo the camera is on no rig"), FindRig(Camera));
	TestTrue(TEXT("After undo the camera is where it was"), Camera->GetActorLocation().Equals(CameraStart.GetLocation(), 0.05));
	TestTrue(TEXT("It can be redone"), GEditor->RedoTransaction());
	Rig = FindRig(Camera);
	if (!TestNotNull(TEXT("After redo the camera is on the crane"), Rig))
	{
		return false;
	}

	// Block three marks. Locked off, so the camera path is exactly the designed move.
	const FUEcineRigPose First = Rig->GetPose();
	FUEcineRigPose Second = First;
	Second.Swing = 35.0;
	Second.Boom = 25.0;
	Second.Extension = First.Extension + 250.0;
	Second.Pan = -40.0;
	Second.Tilt = -20.0;
	FUEcineRigPose Third = Second;
	Third.Swing = 10.0;
	Third.Extension = First.Extension - 100.0;
	Third.Roll = 8.0;

	EditRig(Rig, FText::FromString(TEXT("Lock Off")), [](AUEcineRig& EditedRig) { EditedRig.Operator.Style = EUEcineOperatorStyle::LockedOff; });
	TestEqual(TEXT("First mark"), AddMark(Rig), 0);
	EditRig(Rig, FText::FromString(TEXT("Move Rig")), [&Second](AUEcineRig& EditedRig) { EditedRig.SetPose(Second); });
	TestEqual(TEXT("Second mark"), AddMark(Rig), 1);
	EditRig(Rig, FText::FromString(TEXT("Move Rig")), [&Third](AUEcineRig& EditedRig) { EditedRig.SetPose(Third); });
	TestEqual(TEXT("Third mark"), AddMark(Rig), 2);
	EditRig(Rig, FText::FromString(TEXT("Time Moves")), [](AUEcineRig& EditedRig)
	{
		EditedRig.Marks[1].Arrival = MakeTiming(6.0, 0.25, 0.25);
		EditedRig.Marks[2].Arrival = MakeTiming(4.0, 0.5, 0.5);
	});
	TestEqual(TEXT("Both moves are within the crane's limits"),
		UEcine::Move::Check(Rig->GetSpec(), First, Second, Rig->Marks[1].Arrival).Num() + UEcine::Move::Check(Rig->GetSpec(), Second, Third, Rig->Marks[2].Arrival).Num(), 0);
	TestTrue(TEXT("The same first move in one second is beyond the crane"), UEcine::Move::Check(Rig->GetSpec(), First, Second, MakeTiming(1.0, 0.25, 0.25)).Num() > 0);

	GoToMark(Rig, 0);
	TestEqual(TEXT("Go to mark puts the rig back"), Rig->GetPose().Swing, First.Swing, 1.e-4);
	TestTrue(TEXT("Back on the first mark the camera is where it started"), Camera->GetActorLocation().Equals(CameraStart.GetLocation(), 0.05));

	// Bake into a sequence, starting one second in.
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional);
	Sequence->Initialize();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate DisplayRate(24, 1);
	MovieScene->SetDisplayRate(DisplayRate);
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const int32 StartFrame = 24;
	const int32 EndFrame = StartFrame + 240;

	FText Error;
	TestFalse(TEXT("Before any bake there is no baked move to be out of date"), Rig->HasChangedSinceBake());
	TestTrue(TEXT("The move bakes"), BakeMove(Rig, Sequence, StartFrame, Error));

	// The rig keeps what was baked, so that the tab can say when the marks have moved on since.
	TestFalse(TEXT("Fresh from the bake, nothing has changed since"), Rig->HasChangedSinceBake());
	const double BakedSeconds = Rig->Marks[1].Arrival.DurationSeconds;
	Rig->Marks[1].Arrival.DurationSeconds = BakedSeconds + 1.0;
	TestTrue(TEXT("A move retimed after the bake is a change since the bake"), Rig->HasChangedSinceBake());
	Rig->Marks[1].Arrival.DurationSeconds = BakedSeconds;
	TestFalse(TEXT("Put back as it was baked, it is no change"), Rig->HasChangedSinceBake());
	TestTrue(TEXT("The playback range takes in the move"), MovieScene->GetPlaybackRange().Contains(ConvertFrameTime(FFrameTime(EndFrame), DisplayRate, TickResolution).RoundToFrame()));

	// The keys: every axis the crane has is on a track, and each curve matches the solver.
	const FUEcineRigSpec Spec = Rig->GetSpec();
	for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
	{
		const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
		const FMovieSceneFloatChannel* Channel = FindAxisChannel(Sequence, Rig, Axis);
		if (!Spec.Axes[AxisIndex].bUsed)
		{
			TestNull(*FString::Printf(TEXT("Axis %d the crane does not have gets no track"), AxisIndex), Channel);
			continue;
		}
		if (!TestNotNull(*FString::Printf(TEXT("Axis %d has a track"), AxisIndex), Channel))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("Axis %d uses a handful of keys, not one per frame"), AxisIndex), Channel->GetNumKeys() <= 6);
		for (int32 Frame = StartFrame - 12; Frame <= EndFrame + 12; Frame += 3)
		{
			float Keyed = 0.0f;
			Channel->Evaluate(ConvertFrameTime(FFrameTime(Frame), DisplayRate, TickResolution), Keyed);
			const double Solved = UEcine::Move::EvaluateMarks(Rig->Marks, (Frame - StartFrame) / 24.0).Get(Axis);
			TestEqual(*FString::Printf(TEXT("Axis %d at frame %d"), AxisIndex, Frame), static_cast<double>(Keyed), Solved, 0.01);
		}
	}

	// Baking twice replaces the keys rather than piling them up.
	const int32 KeysAfterFirstBake = FindAxisChannel(Sequence, Rig, EUEcineRigAxis::Swing)->GetNumKeys();
	TestTrue(TEXT("The move bakes again"), BakeMove(Rig, Sequence, StartFrame, Error));
	TestEqual(TEXT("Rebaking replaces the keys"), FindAxisChannel(Sequence, Rig, EUEcineRigAxis::Swing)->GetNumKeys(), KeysAfterFirstBake);

	// The real thing: open the sequence in Sequencer, scrub, and see where the camera actually is.
	const FTransform RigTransform = Rig->GetActorTransform();
	const auto CheckPlayback = [&](const TCHAR* What)
	{
		for (int32 Frame = StartFrame; Frame <= EndFrame; Frame += 8)
		{
			ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
			const double Time = (Frame - StartFrame) / 24.0;
			const FTransform Expected = UEcine::Operator::Offset(Rig->Operator, Time) * Rig->SolveMount(UEcine::Move::EvaluateMarks(Rig->Marks, Time)) * RigTransform;
			const FTransform Actual = Camera->GetActorTransform();
			TestTrue(*FString::Printf(TEXT("%s: camera position at frame %d (off by %.3f cm)"), What, Frame, FVector::Dist(Actual.GetLocation(), Expected.GetLocation())),
				Actual.GetLocation().Equals(Expected.GetLocation(), 0.05));
			TestTrue(*FString::Printf(TEXT("%s: camera direction at frame %d"), What, Frame), Actual.GetRotation().Equals(Expected.GetRotation(), 1.e-3));
		}
	};

	if (TestTrue(TEXT("The sequence opens in Sequencer"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence)))
	{
		CheckPlayback(TEXT("Locked off"));

		// The same move with a handheld operator on top: repeatable, so still exactly predictable.
		EditRig(Rig, FText::FromString(TEXT("Go Handheld")), [](AUEcineRig& EditedRig)
		{
			EditedRig.Operator.Style = EUEcineOperatorStyle::Handheld;
			EditedRig.Operator.Seed = 42;
		});
		TestTrue(TEXT("The move bakes with an operator"), BakeMove(Rig, Sequence, StartFrame, Error));
		CheckPlayback(TEXT("Handheld"));

		// What the panel's bake button does with a sequence open: bake at the playhead.
		const int32 PlayheadFrame = 48;
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(PlayheadFrame), EUpdatePositionMethod::Scrub));
		TestTrue(TEXT("Baking to Sequencer uses the open sequence"), BakeMoveToSequencer(Rig, Error) == Sequence);
		float SwingOnSecondMark = 0.0f;
		FindAxisChannel(Sequence, Rig, EUEcineRigAxis::Swing)->Evaluate(ConvertFrameTime(FFrameTime(PlayheadFrame + 144), DisplayRate, TickResolution), SwingOnSecondMark);
		TestEqual(TEXT("Baked at the playhead, the rig reaches the second mark six seconds after it"), static_cast<double>(SwingOnSecondMark), Second.Swing, 0.01);

		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	}

	// With nothing open, the bake button makes a sequence at the camera's frame rate and opens it.
	TestNull(TEXT("No sequence is open"), ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence());
	ULevelSequence* NewSequence = BakeMoveToSequencer(Rig, Error);
	if (TestNotNull(TEXT("Baking to Sequencer with nothing open makes a sequence"), NewSequence))
	{
		TestTrue(TEXT("The new sequence is an asset under the UEcine folder"), NewSequence->GetPathName().StartsWith(TEXT("/Game/UEcine/Sequences/")));
		TestTrue(TEXT("The new sequence is open"), ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence() == NewSequence);
		TestTrue(TEXT("The new sequence runs at the camera's frame rate"), NewSequence->GetMovieScene()->GetDisplayRate() == FFrameRate(24, 1));
		float SwingOnSecondMark = 0.0f;
		const FMovieSceneFloatChannel* Swing = FindAxisChannel(NewSequence, Rig, EUEcineRigAxis::Swing);
		if (TestNotNull(TEXT("The new sequence has the move"), Swing))
		{
			Swing->Evaluate(ConvertFrameTime(FFrameTime(144), DisplayRate, NewSequence->GetMovieScene()->GetTickResolution()), SwingOnSecondMark);
			TestEqual(TEXT("In the new sequence the move starts on the first frame"), static_cast<double>(SwingOnSecondMark), Second.Swing, 0.01);
		}

		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(NewSequence, false));
	}

	// Without marks there is nothing to bake, and the reason is given.
	EditRig(Rig, FText::FromString(TEXT("Clear Marks")), [](AUEcineRig& EditedRig) { EditedRig.Marks.SetNum(1); });
	TestFalse(TEXT("One mark is not a move"), BakeMove(Rig, Sequence, StartFrame, Error));
	TestFalse(TEXT("The refusal says why"), Error.IsEmpty());
	TestNull(TEXT("The bake button refuses too"), BakeMoveToSequencer(Rig, Error));
	TestNull(TEXT("A refused bake makes no sequence"), ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence());

	// Taking the camera off leaves it where it is and removes the rig.
	const FVector LocationOnRig = Camera->GetActorLocation();
	GEditor->SelectNone(false, true, false);
	TakeCameraOffRig(Camera);
	TestNull(TEXT("The camera is off the rig"), FindRig(Camera));
	TestFalse(TEXT("Off the support the camera is free again"), Camera->IsLockLocation());
	TestTrue(TEXT("The camera stays where it was"), Camera->GetActorLocation().Equals(LocationOnRig, 0.05));

	World->EditorDestroyActor(Camera, false);
	GEditor->ResetTransaction(FText::FromString(TEXT("UEcine grip test")));
	return true;
}

/** Laying dolly track from the panel: the travel range follows the track, and it can be undone. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineGripDollyTrackTest, "UEcine.Editor.Grip.DollyTrack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineGripDollyTrackTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::GripEditing;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 150.0)));
	AUEcineRig* Rig = PutCameraOnRig(Camera, UEcine::Rig::GetDefaultPreset(EUEcineRigType::Dolly));
	if (!TestNotNull(TEXT("The camera is put on a dolly"), Rig))
	{
		return false;
	}
	TestTrue(TEXT("The camera did not move"), Camera->GetActorLocation().Equals(FVector(0.0, 0.0, 150.0), 0.05));
	TestEqual(TEXT("A dolly comes with six metres of track"), Rig->GetTrackLength(), 600.0f, 0.1f);

	EditRig(Rig, FText::FromString(TEXT("Lay Track")), [](AUEcineRig& EditedRig)
	{
		EditedRig.GetTrack()->Modify();
		EditedRig.SetStraightTrack(900.0f);
	});
	TestEqual(TEXT("More track is laid"), Rig->GetTrackLength(), 900.0f, 0.1f);
	TestEqual(TEXT("The dolly can travel the new length"), Rig->GetSpec().GetAxis(EUEcineRigAxis::Travel).Max, 900.0, 0.1);

	TestTrue(TEXT("Laying track can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("After undo the track is its old length"), Rig->GetTrackLength(), 600.0f, 0.1f);

	// A support deleted by hand in the level lets go of its camera.
	GEditor->SelectNone(false, true, false);
	TestTrue(TEXT("On the dolly the camera is held in place"), Camera->IsLockLocation());
	World->EditorDestroyActor(Rig, false);
	TestNull(TEXT("With its support deleted the camera is on nothing"), FindRig(Camera));
	TestFalse(TEXT("With its support deleted the camera is free again"), Camera->IsLockLocation());
	World->EditorDestroyActor(Camera, false);
	GEditor->ResetTransaction(FText::FromString(TEXT("UEcine grip test")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineSequenceFrameRateTest, "UEcine.Editor.Sequence.FrameRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineSequenceFrameRateTest::RunTest(const FString& Parameters)
{
	using UEcine::SequenceUtils::GetFrameRate;
	TestTrue(TEXT("24 fps"), GetFrameRate(24.0) == FFrameRate(24, 1));
	TestTrue(TEXT("An unusual rate is kept to a thousandth"), GetFrameRate(22.5) == FFrameRate(22500, 1000));
	TestTrue(TEXT("23.976 fps is 24000/1001"), GetFrameRate(23.976) == FFrameRate(24000, 1001));
	TestTrue(TEXT("29.97 fps is 30000/1001"), GetFrameRate(29.97) == FFrameRate(30000, 1001));
	TestTrue(TEXT("59.94 fps is 60000/1001"), GetFrameRate(59.94) == FFrameRate(60000, 1001));
	TestEqual(TEXT("25 fps plays 25 frames a second"), GetFrameRate(25.0).AsDecimal(), 25.0, 1.e-9);
	return true;
}

/**
 * A dolly zoom: a zoom on a dolly pushes in on a subject, the move is baked, and scrubbed in the
 * real Sequencer the subject keeps its size in the frame while the lens widens.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineGripDollyZoomTest, "UEcine.Editor.Grip.DollyZoom", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineGripDollyZoomTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::GripEditing;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 150.0)));
	AStaticMeshActor* Subject = World->SpawnActor<AStaticMeshActor>(FVector(500.0, 0.0, 150.0), FRotator::ZeroRotator);
	UUEcineCameraPackageComponent* Package = Camera != nullptr ? UUEcineCameraPackageComponent::Find(Camera) : nullptr;
	const FUEcineLensKit* Zoom = FUEcineCameraDatabase::Get().FindLensKit(TEXT("angenieux-optimo-24-290"));
	if (!TestNotNull(TEXT("A camera with a package"), Package) || !TestNotNull(TEXT("A subject"), Subject) || !TestNotNull(TEXT("The 24-290 zoom is in the database"), Zoom))
	{
		return false;
	}
	UCineCameraComponent* Lens = Camera->GetCineCameraComponent();

	// On a crane there is no dolly zoom to switch on.
	AUEcineRig* Rig = PutCameraOnRig(Camera, UEcine::Rig::GetDefaultPreset(EUEcineRigType::Crane));
	TestFalse(TEXT("A crane does not offer a dolly zoom"), CanDollyZoom(Rig));
	Rig = PutCameraOnRig(Camera, UEcine::Rig::GetDefaultPreset(EUEcineRigType::Slider));
	TestTrue(TEXT("A slider offers a dolly zoom"), CanDollyZoom(Rig));
	Rig = PutCameraOnRig(Camera, UEcine::Rig::GetDefaultPreset(EUEcineRigType::Dolly));
	if (!TestNotNull(TEXT("The camera is put on a dolly"), Rig) || !TestTrue(TEXT("A dolly offers a dolly zoom"), CanDollyZoom(Rig)))
	{
		return false;
	}

	// Two marks: where the camera is, and 2 m further along the track, towards the subject.
	UEcine::FocusEditing::FollowActor(Camera, Subject);
	AddMark(Rig);
	EditRig(Rig, FText::FromString(TEXT("Move Rig")), [](AUEcineRig& Edited)
	{
		FUEcineRigPose Pose = Edited.GetPose();
		Pose.Travel += 200.0;
		Edited.SetPose(Pose);
	});
	AddMark(Rig);
	const double EndDistance = FVector::Dist(Lens->GetComponentLocation(), Subject->GetActorLocation());
	GoToMark(Rig, 0);
	const double StartDistance = FVector::Dist(Lens->GetComponentLocation(), Subject->GetActorLocation());
	TestEqual(TEXT("The second mark is 2 m nearer the subject"), StartDistance - EndDistance, 200.0, 0.5);
	EditRig(Rig, FText::FromString(TEXT("Time Move")), [](AUEcineRig& Edited) { Edited.Marks[1].Arrival = MakeTiming(2.0, 0.25, 0.25); });

	FDollyZoom Plan;
	TestFalse(TEXT("A move is not a dolly zoom until it is made one"), PlanDollyZoom(Rig, 1, Plan));
	EditRig(Rig, FText::FromString(TEXT("Change Dolly Zoom")), [](AUEcineRig& Edited) { Edited.Marks[1].bDollyZoom = true; });

	// On a prime the plan says the lens cannot do it.
	if (TestTrue(TEXT("The dolly zoom is worked out"), PlanDollyZoom(Rig, 1, Plan)))
	{
		TestFalse(TEXT("A prime is not a zoom lens"), Plan.IsZoomLens());
		TestTrue(TEXT("The focus is held on the subject"), Plan.bFocusHeld);
	}

	// On the 24-290, starting on 50 mm.
	FText Error;
	TestTrue(TEXT("The zoom goes on"), UEcine::CameraEditing::EditPackage(Package, FText::FromString(TEXT("Change Lens")), [Zoom](FUEcineCameraPackage& Edited)
	{
		Edited.SelectLensKit(*Zoom);
		Edited.FocalLengthMm = 50.0f;
	}, Error));
	TestEqual(TEXT("The zoom is on 50 mm"), Lens->CurrentFocalLength, 50.0f, 0.01f);
	const double ExpectedEnd = 50.0 * EndDistance / StartDistance;
	if (TestTrue(TEXT("The dolly zoom is worked out for the zoom"), PlanDollyZoom(Rig, 1, Plan)))
	{
		TestTrue(TEXT("The 24-290 is a zoom and can do this one"), Plan.IsZoomLens() && Plan.IsWithinLens());
		TestEqual(TEXT("It starts on 50 mm"), Plan.StartFocalLengthMm, 50.0, 0.01);
		TestEqual(TEXT("Pushing in, the lens widens in step with the distance"), Plan.EndFocalLengthMm, ExpectedEnd, 0.01);
		AddInfo(FString::Printf(TEXT("From %.0f cm to %.0f cm the lens zooms from %.1f to %.1f mm"), StartDistance, EndDistance, Plan.StartFocalLengthMm, Plan.EndFocalLengthMm));
	}

	// With the focus set by hand the plan says the subject will go soft.
	UEcine::FocusEditing::StopFollowing(Camera);
	TestTrue(TEXT("The plan is worked out with the focus set by hand"), PlanDollyZoom(Rig, 1, Plan) && !Plan.bFocusHeld);
	TestEqual(TEXT("And the subject is what the lens is focused on"), Plan.EndFocalLengthMm, ExpectedEnd, 0.01);
	UEcine::FocusEditing::FollowActor(Camera, Subject);

	// Bake it and play it in the real Sequencer.
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional);
	Sequence->Initialize();
	Sequence->GetMovieScene()->SetDisplayRate(FFrameRate(24, 1));
	TestTrue(TEXT("The move bakes"), BakeMove(Rig, Sequence, 0, Error));
	const UMovieSceneFloatTrack* ZoomTrack = UEcine::SequenceUtils::FindFloatTrack(Sequence->GetMovieScene(), UEcine::SequenceUtils::FindBinding(Sequence, Lens), TEXT("CurrentFocalLength"));
	if (TestNotNull(TEXT("The lens's focal length has a track"), ZoomTrack) && TestTrue(TEXT("The shot opens in Sequencer"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence)))
	{
		TestEqual(TEXT("A key on every frame of the two-second move"), CastChecked<UMovieSceneFloatSection>(ZoomTrack->GetAllSections()[0])->GetChannel().GetNumKeys(), 49);
		float WidestSeen = 50.0f;
		for (int32 Frame = 0; Frame <= 48; Frame += 6)
		{
			ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
			const double Distance = FVector::Dist(Lens->GetComponentLocation(), Subject->GetActorLocation());
			// The subject's size in the frame goes with focal length over distance.
			TestEqual(*FString::Printf(TEXT("Scrubbed to frame %d: the subject is the size it started"), Frame), Lens->CurrentFocalLength / Distance, 50.0 / StartDistance, 1.e-4);
			WidestSeen = FMath::Min(WidestSeen, Lens->CurrentFocalLength);
		}
		TestEqual(TEXT("At the end of the move the camera is on its second mark"), FVector::Dist(Lens->GetComponentLocation(), Subject->GetActorLocation()), EndDistance, 0.5);
		TestEqual(TEXT("And the lens is as wide as the plan said"), static_cast<double>(WidestSeen), ExpectedEnd, 0.05);

		// Switched off and baked again, the zoom's keys go.
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(0), EUpdatePositionMethod::Scrub));
		EditRig(Rig, FText::FromString(TEXT("Change Dolly Zoom")), [](AUEcineRig& Edited) { Edited.Marks[1].bDollyZoom = false; });
		TestTrue(TEXT("The move bakes again"), BakeMove(Rig, Sequence, 0, Error));
		TestEqual(TEXT("Without the dolly zoom the lens has no keys"), CastChecked<UMovieSceneFloatSection>(ZoomTrack->GetAllSections()[0])->GetChannel().GetNumKeys(), 0);
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	}

	// A travel the lens cannot follow all the way: the plan says so and the keys stop at the lens's limit.
	TestTrue(TEXT("The zoom is put on its wide end"), UEcine::CameraEditing::EditPackage(Package, FText::FromString(TEXT("Zoom")), [](FUEcineCameraPackage& Edited) { Edited.FocalLengthMm = 30.0f; }, Error));
	EditRig(Rig, FText::FromString(TEXT("Change Dolly Zoom")), [](AUEcineRig& Edited) { Edited.Marks[1].bDollyZoom = true; });
	if (TestTrue(TEXT("The plan is worked out from 30 mm"), PlanDollyZoom(Rig, 1, Plan)))
	{
		TestTrue(TEXT("Pushing in from 30 mm asks for wider than the zoom goes"), Plan.WidestMm < 24.0 && !Plan.IsWithinLens());
	}
	ULevelSequence* Short = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional);
	Short->Initialize();
	Short->GetMovieScene()->SetDisplayRate(FFrameRate(24, 1));
	TestTrue(TEXT("The move still bakes"), BakeMove(Rig, Short, 0, Error));
	const UMovieSceneFloatTrack* ShortTrack = UEcine::SequenceUtils::FindFloatTrack(Short->GetMovieScene(), UEcine::SequenceUtils::FindBinding(Short, Lens), TEXT("CurrentFocalLength"));
	if (TestNotNull(TEXT("The lens's focal length has a track"), ShortTrack))
	{
		float LastKey = 0.0f;
		const FMovieSceneFloatChannel& Channel = CastChecked<UMovieSceneFloatSection>(ShortTrack->GetAllSections()[0])->GetChannel();
		Channel.Evaluate(ConvertFrameTime(FFrameTime(48), FFrameRate(24, 1), Short->GetMovieScene()->GetTickResolution()), LastKey);
		TestEqual(TEXT("The lens stops at its wide end"), LastKey, 24.0f, 0.01f);
	}

	GEditor->SelectNone(false, true, false);
	World->EditorDestroyActor(Rig, false);
	World->EditorDestroyActor(Subject, false);
	World->EditorDestroyActor(Camera, false);
	GEditor->ResetTransaction(FText::FromString(TEXT("UEcine dolly zoom test")));
	return true;
}

#endif
