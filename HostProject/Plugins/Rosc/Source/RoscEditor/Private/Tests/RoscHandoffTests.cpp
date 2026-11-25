// Copyright 2026 Fiavaion. All rights reserved.

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequencePlayer.h"
#include "ObjectTools.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscHandoff.h"
#include "RoscRig.h"
#include "RoscShotEditing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** What a camera shows at one moment: where its lens is and how the lens is set. */
	struct FHandoffTestView
	{
		FVector Location = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		float FocalLength = 0.0f;
		float Aperture = 0.0f;
		float FocusDistance = 0.0f;
	};

	void ScrubToHandoffFrame(int32 Frame)
	{
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
	}

	FHandoffTestView ReadHandoffView(const ACineCameraActor* Camera)
	{
		const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
		return { Lens->GetComponentLocation(), Lens->GetComponentQuat(), Lens->CurrentFocalLength, Lens->CurrentAperture, Rosc::FocusEditing::GetLensDistance(Camera) };
	}

	/** The Rosc classes a sequence asset holds or is bound to: none, for one that plays without the plugin. */
	TArray<FString> FindRoscClasses(ULevelSequence* Sequence)
	{
		TArray<UObject*> Inside;
		FReferenceFinder Finder(Inside, Sequence->GetOutermost(), false, true, true, true);
		Finder.FindReferences(Sequence);

		TArray<const UClass*> Classes;
		for (const UObject* Object : Inside)
		{
			Classes.AddUnique(Object->GetClass());
		}
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
		{
			if (const UClass* Class = MovieScene->GetPossessable(Index).GetPossessedObjectClass())
			{
				Classes.AddUnique(Class);
			}
		}

		TArray<FString> Found;
		for (const UClass* Class : Classes)
		{
			if (Class->GetOutermost()->GetName().StartsWith(TEXT("/Script/Rosc")))
			{
				Found.Add(Class->GetName());
			}
		}
		return Found;
	}

	/** How many keys the first transform track in a sequence has on its first channel. */
	int32 CountHandoffTransformKeys(const ULevelSequence* Sequence)
	{
		const UMovieScene* MovieScene = Sequence->GetMovieScene();
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			for (const UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (Track->IsA<UMovieScene3DTransformTrack>() && !Track->GetAllSections().IsEmpty())
				{
					return Track->GetAllSections()[0]->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>()[0]->GetNumKeys();
				}
			}
		}
		return 0;
	}
}

/**
 * Bake for handoff: a crane shot with an operator at the head and focus following a subject is
 * copied, and the copy shows the same pictures from a plain camera, with nothing of Rosc in it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscHandoffBakeTest, "Rosc.Editor.Handoff.Bake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscHandoffBakeTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::ShotEditing;
	using namespace Rosc::Handoff;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	TArray<AActor*> SpawnedActors;
	TArray<ULevelSequence*> CreatedSequences;
	ON_SCOPE_EXIT
	{
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		GEditor->SelectNone(false, true, false);
		for (AActor* Actor : SpawnedActors)
		{
			if (ARoscRig* Rig = Rosc::GripEditing::FindRig(Cast<ACineCameraActor>(Actor)))
			{
				World->EditorDestroyActor(Rig, false);
			}
			World->EditorDestroyActor(Actor, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc handoff test")));
		for (ULevelSequence* Sequence : CreatedSequences)
		{
			TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
		}
	};

	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	FText Error;
	TestNull(TEXT("With no sequence open there is nothing to hand off"), BakeForHandoff(Error));
	TestFalse(TEXT("The refusal says why"), Error.IsEmpty());

	// A camera on a crane, with an operator at the remote head, keeping its focus on the hero.
	ACineCameraActor* Camera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FRotator(-5.0, 30.0, 0.0), FVector(200.0, -300.0, 380.0)));
	AStaticMeshActor* Hero = World->SpawnActor<AStaticMeshActor>(FVector(900.0, 200.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("A camera"), Camera) || !TestNotNull(TEXT("A hero"), Hero))
	{
		return false;
	}
	SpawnedActors = { Camera, Hero };
	Camera->SetActorLabel(TEXT("A Cam"));
	Hero->SetActorLabel(TEXT("Hero"));
	Rosc::FocusEditing::FollowActor(Camera, Hero);

	ARoscRig* Crane = Rosc::GripEditing::PutCameraOnRig(Camera, *Rosc::Rig::FindPreset(TEXT("crane-30ft")));
	if (!TestNotNull(TEXT("The camera is put on a crane"), Crane))
	{
		return false;
	}
	TestFalse(TEXT("The crane's operator is not locked off"), Crane->Operator.IsStill());
	Rosc::GripEditing::AddMark(Crane);
	Rosc::GripEditing::EditRig(Crane, FText::FromString(TEXT("Move Rig")), [](ARoscRig& Rig)
	{
		FRoscRigPose Pose = Rig.GetPose();
		Pose.Swing = 35.0;
		Pose.Boom = 25.0;
		Pose.Extension += 250.0;
		Pose.Pan = -40.0;
		Pose.Tilt = -20.0;
		Rig.SetPose(Pose);
	});
	Rosc::GripEditing::AddMark(Crane);
	Rosc::GripEditing::EditRig(Crane, FText::FromString(TEXT("Time Move")), [](ARoscRig& Rig) { Rig.Marks[1].Arrival.DurationSeconds = 3.0; });
	Rosc::GripEditing::GoToMark(Crane, 0);

	// Two shots; the crane move is baked into the first, and the first has shot list data.
	if (!TestTrue(TEXT("Two shots are added"), AddShot(Camera, Error) && AddShot(Camera, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	ULevelSequence* Cut = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	const TArray<FShot> Shots = GetShots();
	CreatedSequences.Add(Cut);
	for (const FShot& Shot : Shots)
	{
		CreatedSequences.AddUnique(Shot.Sequence);
	}
	if (!TestEqual(TEXT("The sequence has two shots"), Shots.Num(), 2))
	{
		return false;
	}
	SetScene(Shots[0].Sequence, TEXT("7"));
	GoToShot(Shots[0]);
	TestTrue(TEXT("The crane move is baked into the first shot"), Rosc::GripEditing::BakeMoveToSequencer(Crane, Error) == Shots[0].Sequence);
	TestTrue(TEXT("The shot being worked on needs Rosc: the check below can fail"), FindRoscClasses(Shots[0].Sequence).Num() >= 2);

	// What the camera shows at moments through the cut: at rest, moving, at the last frame of a shot, and in the second shot.
	const TArray<int32> Frames = { 0, 12, 36, 71, Shots[0].DurationFrames - 1, Shots[1].StartFrame.Value, Shots[1].StartFrame.Value + 40 };
	TArray<FHandoffTestView> Expected;
	for (const int32 Frame : Frames)
	{
		ScrubToHandoffFrame(Frame);
		Expected.Add(ReadHandoffView(Camera));
	}
	TestTrue(TEXT("The crane carries the camera a long way during the first shot"), FVector::Dist(Expected[0].Location, Expected[3].Location) > 200.0);
	TestTrue(TEXT("The focus follows the hero as the camera moves"), !FMath::IsNearlyEqual(Expected[0].FocusDistance, Expected[3].FocusDistance, 10.0f));

	const auto CheckCopy = [this, &Frames, &Expected, Camera](ULevelSequence* CutCopy, const TCHAR* When)
	{
		if (!TestTrue(*FString::Printf(TEXT("%s: the copy is opened"), When), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(CutCopy)))
		{
			return;
		}
		const TArray<FShot> CopiedShots = GetShots();
		if (!TestEqual(*FString::Printf(TEXT("%s: the copy has the two shots"), When), CopiedShots.Num(), 2))
		{
			return;
		}
		TestTrue(*FString::Printf(TEXT("%s: the copy of the cut has nothing of Rosc in it"), When), FindRoscClasses(CutCopy).IsEmpty());
		for (const FShot& Shot : CopiedShots)
		{
			TestTrue(*FString::Printf(TEXT("%s: the copy plays the copy of %s"), When, *Shot.Name), IsHandoffCopy(Shot.Sequence));
			const TArray<FString> RoscClasses = FindRoscClasses(Shot.Sequence);
			TestTrue(*FString::Printf(TEXT("%s: the copy of %s has nothing of Rosc in it (found: %s)"), When, *Shot.Name, *FString::Join(RoscClasses, TEXT(", "))), RoscClasses.IsEmpty());
			TestEqual(*FString::Printf(TEXT("%s: the camera of %s has a key on every frame and one at the shot's last moment"), When, *Shot.Name),
				CountHandoffTransformKeys(Shot.Sequence), Shot.DurationFrames + 1);
		}

		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			ScrubToHandoffFrame(Frames[Index]);
			const int32 ShotIndex = FindShotAtPlayhead(CopiedShots);
			const ACineCameraActor* Plain = CopiedShots.IsValidIndex(ShotIndex) ? FindShotCamera(CopiedShots[ShotIndex]) : nullptr;
			const FString At = FString::Printf(TEXT("%s, frame %d"), When, Frames[Index]);
			if (!TestNotNull(*FString::Printf(TEXT("%s: the copy has a camera"), *At), Plain))
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s: it is a camera of the copy's own"), *At), Plain != Camera);
			TestNull(*FString::Printf(TEXT("%s: it is on no rig"), *At), Plain->GetAttachParentActor());
			TestNull(*FString::Printf(TEXT("%s: it has no camera package"), *At), URoscCameraPackageComponent::Find(Plain));
			TestTrue(*FString::Printf(TEXT("%s: it is focused by hand"), *At), Plain->GetCineCameraComponent()->FocusSettings.FocusMethod == ECameraFocusMethod::Manual);

			const FHandoffTestView View = ReadHandoffView(Plain);
			TestTrue(*FString::Printf(TEXT("%s: the lens is in the same place (%s, was %s)"), *At, *View.Location.ToString(), *Expected[Index].Location.ToString()),
				View.Location.Equals(Expected[Index].Location, 0.01));
			TestTrue(*FString::Printf(TEXT("%s: the lens looks the same way"), *At), FMath::RadiansToDegrees(View.Rotation.AngularDistance(Expected[Index].Rotation)) < 0.01);
			TestEqual(*FString::Printf(TEXT("%s: focal length"), *At), View.FocalLength, Expected[Index].FocalLength, 0.001f);
			TestEqual(*FString::Printf(TEXT("%s: aperture"), *At), View.Aperture, Expected[Index].Aperture, 0.001f);
			TestEqual(*FString::Printf(TEXT("%s: focus distance"), *At), View.FocusDistance, Expected[Index].FocusDistance, 0.01f);
		}
	};

	ULevelSequence* CutCopy = BakeForHandoff(Error);
	if (!TestNotNull(TEXT("The cut is baked for handoff"), CutCopy))
	{
		AddError(Error.ToString());
		return false;
	}
	CreatedSequences.Add(CutCopy);
	TestEqual(TEXT("The copy is kept beside the cut"), CutCopy->GetOutermost()->GetName(), Cut->GetOutermost()->GetName() + TEXT("_Handoff"));
	TestTrue(TEXT("The cut being worked on is still open"), ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence() == Cut);
	TestTrue(TEXT("The cut being worked on still plays its own shots"), GetShots().Num() == 2 && GetShots()[0].Sequence == Shots[0].Sequence);
	TestTrue(TEXT("The camera being worked on is still on its crane"), Rosc::GripEditing::FindRig(Camera) == Crane);
	TestEqual(TEXT("The shot being worked on keeps its shot list data"), GetScene(Shots[0].Sequence), FString(TEXT("7")));

	CheckCopy(CutCopy, TEXT("First bake"));
	for (const FShot& Shot : GetShots())
	{
		CreatedSequences.AddUnique(Shot.Sequence);
	}
	TestNull(TEXT("A handoff copy is not handed off again"), BakeForHandoff(Error));

	// Baking again replaces the copies; none is left over under another name.
	CreatedSequences.RemoveAll([](const ULevelSequence* Sequence) { return IsHandoffCopy(Sequence); });
	TestTrue(TEXT("The cut is opened again"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Cut));
	ULevelSequence* SecondCopy = BakeForHandoff(Error);
	if (TestNotNull(TEXT("The cut is baked for handoff a second time"), SecondCopy))
	{
		CreatedSequences.Add(SecondCopy);
		TestEqual(TEXT("The second copy takes the place of the first"), SecondCopy->GetOutermost()->GetName(), Cut->GetOutermost()->GetName() + TEXT("_Handoff"));
		CheckCopy(SecondCopy, TEXT("Second bake"));
		for (const FShot& Shot : GetShots())
		{
			CreatedSequences.AddUnique(Shot.Sequence);
		}
	}

	// A sequence that is not cut into shots is handed off as one shot.
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	Rosc::GripEditing::GoToMark(Crane, 0);
	ULevelSequence* Uncut = Rosc::GripEditing::BakeMoveToSequencer(Crane, Error);
	if (TestNotNull(TEXT("With no sequence open the move is baked into a new one"), Uncut))
	{
		CreatedSequences.Add(Uncut);
		ScrubToHandoffFrame(30);
		const FHandoffTestView UncutView = ReadHandoffView(Camera);

		ULevelSequence* UncutCopy = BakeForHandoff(Error);
		if (TestNotNull(TEXT("The uncut sequence is baked for handoff"), UncutCopy))
		{
			CreatedSequences.Add(UncutCopy);
			TestTrue(TEXT("Its copy has nothing of Rosc in it"), FindRoscClasses(UncutCopy).IsEmpty());
			TestTrue(TEXT("Its copy is opened"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(UncutCopy));
			ScrubToHandoffFrame(30);
			const TArray<FShot> UncutShots = GetShots();
			const ACineCameraActor* Plain = UncutShots.Num() == 1 ? FindShotCamera(UncutShots[0]) : nullptr;
			if (TestNotNull(TEXT("Its copy has a camera"), Plain))
			{
				TestTrue(TEXT("The camera is the copy's own"), Plain != Camera);
				TestTrue(TEXT("The lens is in the same place"), ReadHandoffView(Plain).Location.Equals(UncutView.Location, 0.01));
				TestTrue(TEXT("The lens looks the same way"), FMath::RadiansToDegrees(ReadHandoffView(Plain).Rotation.AngularDistance(UncutView.Rotation)) < 0.01);
			}
		}
	}
	return true;
}

#endif
