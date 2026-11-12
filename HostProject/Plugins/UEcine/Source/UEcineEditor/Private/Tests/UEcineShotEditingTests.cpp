#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "MovieSceneMetaData.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneToolHelpers.h"
#include "MovieSceneToolsProjectSettings.h"
#include "ObjectTools.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineEditorSettings.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineRig.h"
#include "UEcineShotEditing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void ScrubToShotListFrame(int32 Frame)
	{
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
	}

	/** The width and height of the picture in a JPEG file, or zero when the bytes are not a JPEG file. */
	FIntPoint ReadJpegSize(TConstArrayView<uint8> Jpeg)
	{
		FImage Picture;
		const bool bJpeg = Jpeg.Num() > 3 && Jpeg[0] == 0xFF && Jpeg[1] == 0xD8 && Jpeg[2] == 0xFF;
		return bJpeg && FImageUtils::DecompressImage(Jpeg.GetData(), Jpeg.Num(), Picture) ? FIntPoint(Picture.SizeX, Picture.SizeY) : FIntPoint::ZeroValue;
	}
}

/**
 * The Phase 4 acceptance run: build a three-shot sequence from the panel's operations, fill in the
 * shot list, block and bake a dolly move inside a shot, grab storyboard frames, export the
 * paperwork and read the files back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineShotsThreeShotsTest, "UEcine.Editor.Shots.ThreeShots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineShotsThreeShotsTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::ShotEditing;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	// The paperwork is left in the Saved folder to look at, like the pictures the other tests save.
	const FString ExportDirectory = FPaths::ProjectSavedDir() / TEXT("UEcine/Paperwork/ShotsTest");
	TArray<AActor*> SpawnedActors;
	TArray<ULevelSequence*> CreatedSequences;
	ON_SCOPE_EXIT
	{
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		GEditor->SelectNone(false, true, false);
		for (AActor* Actor : SpawnedActors)
		{
			if (AUEcineRig* Rig = UEcine::GripEditing::FindRig(Cast<ACineCameraActor>(Actor)))
			{
				World->EditorDestroyActor(Rig, false);
			}
			World->EditorDestroyActor(Actor, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine shots test")));
		for (ULevelSequence* Sequence : CreatedSequences)
		{
			TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
		}
	};

	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	TestTrue(TEXT("With no sequence open there are no shots"), GetShots().IsEmpty());
	TestEqual(TEXT("With no sequence open no shot is under the playhead"), FindShotAtPlayhead(GetShots()), static_cast<int32>(INDEX_NONE));
	FText Error;
	TArray<FString> Files;
	TestFalse(TEXT("With no sequence open there is nothing to export"), ExportPaperwork(ExportDirectory, Files, Error));
	TestFalse(TEXT("The refusal says why"), Error.IsEmpty());

	// Two cameras and something to look at. The A camera is on a dolly and keeps its focus on the hero.
	ACineCameraActor* CameraA = UEcine::CameraEditing::SpawnCamera(World, FTransform(FRotator::ZeroRotator, FVector(0.0, -200.0, 150.0)));
	ACineCameraActor* CameraB = UEcine::CameraEditing::SpawnCamera(World, FTransform(FRotator(0.0, 90.0, 0.0), FVector(400.0, -600.0, 160.0)));
	AStaticMeshActor* Hero = World->SpawnActor<AStaticMeshActor>(FVector(600.0, -200.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("The A camera"), CameraA) || !TestNotNull(TEXT("The B camera"), CameraB) || !TestNotNull(TEXT("The hero"), Hero))
	{
		return false;
	}
	SpawnedActors = { CameraA, CameraB, Hero };
	CameraA->SetActorLabel(TEXT("A Cam"));
	CameraB->SetActorLabel(TEXT("B Cam"));
	Hero->SetActorLabel(TEXT("Hero"));
	UEcine::FocusEditing::FollowActor(CameraA, Hero);
	TestTrue(TEXT("The B camera gets a filter and a faster stock"), UEcine::CameraEditing::EditPackage(UUEcineCameraPackageComponent::Find(CameraB), FText::FromString(TEXT("Change Exposure")), [](FUEcineCameraPackage& Package)
	{
		Package.Iso = 1600;
		Package.NdDensity = 0.6f;
	}, Error));

	AUEcineRig* Dolly = UEcine::GripEditing::PutCameraOnRig(CameraA, *UEcine::Rig::FindPreset(TEXT("dolly")));
	if (!TestNotNull(TEXT("The A camera is put on a dolly"), Dolly))
	{
		return false;
	}
	constexpr double DollyTravelCm = 300.0;
	UEcine::GripEditing::EditRig(Dolly, FText::FromString(TEXT("Lock Off")), [](AUEcineRig& Rig) { Rig.Operator.Style = EUEcineOperatorStyle::LockedOff; });
	UEcine::GripEditing::AddMark(Dolly);
	UEcine::GripEditing::EditRig(Dolly, FText::FromString(TEXT("Move Rig")), [DollyTravelCm](AUEcineRig& Rig)
	{
		FUEcineRigPose Pose = Rig.GetPose();
		Pose.Travel += DollyTravelCm;
		Rig.SetPose(Pose);
	});
	UEcine::GripEditing::AddMark(Dolly);
	UEcine::GripEditing::EditRig(Dolly, FText::FromString(TEXT("Time Move")), [](AUEcineRig& Rig) { Rig.Marks[1].Arrival.DurationSeconds = 4.0; });
	UEcine::GripEditing::GoToMark(Dolly, 0);

	// Three shots. The first makes the sequence they are cut into.
	if (!TestTrue(TEXT("The first shot is added"), AddShot(CameraA, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (!TestNotNull(TEXT("Adding a shot with nothing open makes a sequence and opens it"), OpenSequence))
	{
		return false;
	}
	CreatedSequences.Add(OpenSequence);
	const auto RememberShotSequences = [&CreatedSequences]()
	{
		for (const FShot& Shot : GetShots())
		{
			CreatedSequences.AddUnique(Shot.Sequence);
		}
	};
	RememberShotSequences();
	TestTrue(TEXT("The new sequence is an asset under the UEcine folder"), OpenSequence->GetPathName().StartsWith(TEXT("/Game/UEcine/Sequences/")));

	// Paperwork goes under the project's own folder until the user chooses another.
	{
		const FString FolderBefore = GetDefault<UUEcineEditorSettings>()->PaperworkFolder.Path;
		SetPaperworkFolder(FString());
		TestTrue(TEXT("With no folder chosen, paperwork goes under the project's Saved folder"), GetPaperworkFolder().EndsWith(TEXT("Saved/UEcine/Paperwork")));
		const FString Elsewhere = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEcine/PaperworkElsewhere"));
		SetPaperworkFolder(Elsewhere);
		TestEqual(TEXT("A sequence's paperwork goes in a folder of its own under the chosen one"), GetPaperworkDirectory(OpenSequence), Elsewhere / OpenSequence->GetName());
		SetPaperworkFolder(FolderBefore);
	}
	TestTrue(TEXT("The new sequence runs at the camera's frame rate"), OpenSequence->GetMovieScene()->GetDisplayRate() == FFrameRate(24, 1));
	TestTrue(TEXT("The second shot is added"), AddShot(CameraB, Error));
	RememberShotSequences();
	TestTrue(TEXT("The third shot is added"), AddShot(CameraA, Error));
	RememberShotSequences();

	TArray<FShot> Shots = GetShots();
	if (!TestEqual(TEXT("The sequence has three shots"), Shots.Num(), 3))
	{
		return false;
	}

	// Named and timed the way Sequencer's project settings say.
	const UMovieSceneToolsProjectSettings* SequencerSettings = GetDefault<UMovieSceneToolsProjectSettings>();
	const int32 ShotFrames = FMath::RoundToInt32(SequencerSettings->DefaultDuration * 24.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		// The take number is left out of the check: it goes up when an earlier run in this editor session used the name.
		FString Prefix;
		uint32 ShotNumber = 0;
		uint32 TakeNumber = 0;
		uint32 ShotNumberDigits = 0;
		uint32 TakeNumberDigits = 0;
		const bool bNamedAsShot = MovieSceneToolHelpers::ParseShotName(Shots[Index].Name, Prefix, ShotNumber, TakeNumber, ShotNumberDigits, TakeNumberDigits);
		TestTrue(*FString::Printf(TEXT("Shot %d is named and numbered by Sequencer's settings"), Index + 1), bNamedAsShot && Prefix == SequencerSettings->ShotPrefix
			&& ShotNumber == SequencerSettings->FirstShotNumber + Index * SequencerSettings->ShotIncrement && ShotNumberDigits == SequencerSettings->ShotNumDigits);
		TestEqual(*FString::Printf(TEXT("Shot %d follows the one before"), Index + 1), Shots[Index].StartFrame.Value, Index * ShotFrames);
		TestEqual(*FString::Printf(TEXT("Shot %d runs for Sequencer's default duration"), Index + 1), Shots[Index].DurationFrames, ShotFrames);
		TestNotNull(*FString::Printf(TEXT("Shot %d is a section of the Shot track"), Index + 1), Shots[Index].Section);
		TestTrue(*FString::Printf(TEXT("Shot %d is filed with the sequence's shots"), Index + 1), Shots[Index].Sequence->GetPathName().StartsWith(TEXT("/Game/UEcine/Sequences/") + SequencerSettings->ShotDirectory + TEXT("/")));
		TestNotNull(*FString::Printf(TEXT("Shot %d has a camera cut"), Index + 1), Shots[Index].Sequence->GetMovieScene()->GetCameraCutTrack());
	}
	const TRange<FFrameNumber> PlaybackRange = OpenSequence->GetMovieScene()->GetPlaybackRange();
	const FFrameNumber EndTicks = ConvertFrameTime(FFrameTime(3 * ShotFrames), FFrameRate(24, 1), OpenSequence->GetMovieScene()->GetTickResolution()).FloorToFrame();
	TestTrue(TEXT("The sequence plays all three shots"), PlaybackRange.Contains(FFrameNumber(0)) && PlaybackRange.Contains(EndTicks - 1));

	// Each shot is seen through the camera it was added with, wherever the playhead is.
	TestTrue(TEXT("Shot 1 is on the A camera"), FindShotCamera(Shots[0]) == CameraA);
	TestTrue(TEXT("Shot 2 is on the B camera"), FindShotCamera(Shots[1]) == CameraB);
	TestTrue(TEXT("Shot 3 is on the A camera"), FindShotCamera(Shots[2]) == CameraA);

	// Adding a shot is one undoable step.
	TestTrue(TEXT("Adding a shot can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("After undo there are two shots"), GetShots().Num(), 2);
	TestTrue(TEXT("It can be redone"), GEditor->RedoTransaction());
	Shots = GetShots();
	if (!TestEqual(TEXT("After redo there are three shots again"), Shots.Num(), 3))
	{
		return false;
	}

	// The shot list.
	const FString NotesWithComma = TEXT("Dawn light, haze");
	const FString NotesWithQuotes = TEXT("Eyes sharp, \"soft\" background");
	SetScene(Shots[0].Sequence, TEXT("12"));
	SetSize(Shots[0].Sequence, EUEcineShotSize::Wide);
	SetMove(Shots[0].Sequence, TEXT("Track right with Hero"));
	SetNotes(Shots[0].Sequence, NotesWithComma);
	SetScene(Shots[1].Sequence, TEXT("12"));
	SetSize(Shots[1].Sequence, EUEcineShotSize::CloseUp);
	SetMove(Shots[1].Sequence, TEXT("Static"));
	SetNotes(Shots[1].Sequence, NotesWithQuotes);
	SetScene(Shots[2].Sequence, TEXT("12A"));
	SetSize(Shots[2].Sequence, EUEcineShotSize::MediumCloseUp);
	SetMove(Shots[2].Sequence, TEXT("Push in"));
	TestEqual(TEXT("The scene is kept with the shot"), GetScene(Shots[2].Sequence), FString(TEXT("12A")));
	TestTrue(TEXT("The size is kept with the shot"), GetSize(Shots[1].Sequence) == EUEcineShotSize::CloseUp);
	TestEqual(TEXT("The move is kept with the shot"), GetMove(Shots[0].Sequence), FString(TEXT("Track right with Hero")));
	TestEqual(TEXT("The notes are kept with the shot"), GetNotes(Shots[0].Sequence), NotesWithComma);
	const UMovieSceneMetaData* SequencerNotes = Shots[0].Sequence->FindMetaData<UMovieSceneMetaData>();
	TestTrue(TEXT("The notes are the ones Sequencer shows for the shot's sequence"), SequencerNotes != nullptr && SequencerNotes->GetNotes() == NotesWithComma);
	TestTrue(TEXT("A shot list entry can be undone"), GEditor->UndoTransaction());
	TestTrue(TEXT("After undo the move is not set"), GetMove(Shots[2].Sequence).IsEmpty());
	TestTrue(TEXT("It can be redone"), GEditor->RedoTransaction());
	TestEqual(TEXT("After redo the move is back"), GetMove(Shots[2].Sequence), FString(TEXT("Push in")));
	TestTrue(TEXT("A shot with nothing written has no notes"), GetNotes(Shots[2].Sequence).IsEmpty());

	// Going to a shot opens it, and what is blocked there is baked into it.
	GoToShot(Shots[0]);
	TestEqual(TEXT("The playhead is in shot 1"), FindShotAtPlayhead(Shots), 0);
	TestTrue(TEXT("Sequencer is inside shot 1"), ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() == Shots[0].Sequence);
	const FGuid ShotBeforeBake = GetShotSignature(Shots[0].Sequence);
	TestTrue(TEXT("A move blocked in a shot is baked into that shot"), UEcine::GripEditing::BakeMoveToSequencer(Dolly, Error) == Shots[0].Sequence);
	TestTrue(TEXT("A bake into a shot is a change a plan drawn before it can be told of"), GetShotSignature(Shots[0].Sequence) != ShotBeforeBake);
	GoToShot(Shots[1]);
	TestEqual(TEXT("The playhead is in shot 2"), FindShotAtPlayhead(Shots), 1);
	TestTrue(TEXT("Sequencer is inside shot 2"), ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() == Shots[1].Sequence);
	TestTrue(TEXT("From inside shot 2, shot 1 is still on the A camera"), FindShotCamera(Shots[0]) == CameraA);

	// The overhead plan of the dolly shot, found by playing through it.
	const FGuid ShotBeforePlan = GetShotSignature(Shots[0].Sequence);
	const FUEcinePlan Plan = MakePlan(Shots[0]);
	TestTrue(TEXT("Drawing the plan is not itself a change to the shot"), GetShotSignature(Shots[0].Sequence) == ShotBeforePlan);
	if (TestTrue(TEXT("The plan follows the camera through the shot"), Plan.CameraPath.Num() > 2))
	{
		TestEqual(TEXT("The camera travels the length of the dolly move"), FVector2D::Distance(Plan.CameraPath[0].Position, Plan.CameraPath.Last().Position), DollyTravelCm, 1.0);
		TestTrue(TEXT("The plan starts where the camera starts"), Plan.CameraPath[0].Position.Equals(FVector2D(0.0, -200.0), 1.0));
	}
	TestEqual(TEXT("The plan shows the dolly's track"), Plan.Tracks.Num(), 1);
	TestTrue(TEXT("The plan shows what the camera keeps in focus, and only that"), Plan.Subjects.Num() == 1 && Plan.Subjects[0].Name == TEXT("Hero") && Plan.Subjects[0].Position.Equals(FVector2D(600.0, -200.0), 0.1));
	TestEqual(TEXT("Drawing the plan puts the playhead back"), FindShotAtPlayhead(Shots), 1);
	TestTrue(TEXT("A static shot's plan has a camera that does not travel"), [&Shots]()
	{
		const FUEcinePlan StaticPlan = MakePlan(Shots[1]);
		return StaticPlan.CameraPath.Num() > 0 && StaticPlan.Tracks.IsEmpty() && StaticPlan.CameraPath[0].Position.Equals(StaticPlan.CameraPath.Last().Position, 0.01);
	}());

	// Storyboard frames. The checks that need no picture run everywhere.
	TestFalse(TEXT("A frame is not grabbed for a shot the playhead is not in"), GrabStoryboardFrame(Shots[0], Error));
	TestFalse(TEXT("The refusal says why"), Error.IsEmpty());
	const bool bPictures = FApp::CanEverRender();
	if (bPictures)
	{
		ScrubToShotListFrame(ShotFrames + 7);
		TestTrue(TEXT("A storyboard frame is grabbed in shot 2"), GrabStoryboardFrame(Shots[1], Error));
		TConstArrayView<FUEcineStoryboardFrame> Frames = GetStoryboardFrames(Shots[1].Sequence);
		if (TestEqual(TEXT("Shot 2 has one storyboard frame"), Frames.Num(), 1))
		{
			TestEqual(TEXT("The frame is counted from the start of its shot"), Frames[0].ShotFrame, 7);
		}

		GoToShot(Shots[0]);
		for (const int32 Frame : { 40, 12, 12, 90 })
		{
			ScrubToShotListFrame(Frame);
			TestTrue(TEXT("A storyboard frame is grabbed in shot 1"), GrabStoryboardFrame(Shots[0], Error));
		}
		Frames = GetStoryboardFrames(Shots[0].Sequence);
		if (TestEqual(TEXT("Grabbing a frame twice keeps one picture of it"), Frames.Num(), 3))
		{
			TestTrue(TEXT("The frames are kept in the order they play"), Frames[0].ShotFrame == 12 && Frames[1].ShotFrame == 40 && Frames[2].ShotFrame == 90);
			TestEqual(TEXT("A storyboard frame is 640 wide"), Frames[0].Width, 640);
			TestTrue(TEXT("The picture is a JPEG file of that size"), ReadJpegSize(Frames[0].Jpeg) == FIntPoint(Frames[0].Width, Frames[0].Height) && Frames[0].Height > 0);
			TestTrue(TEXT("A storyboard frame is small enough to keep with the shot"), Frames[0].Jpeg.Num() < 150 * 1024);
			TestTrue(TEXT("The dolly has moved between the first and last frames"), Frames[0].Jpeg != Frames[2].Jpeg);
			TestTrue(TEXT("The first storyboard frame is saved to look at"), FFileHelper::SaveArrayToFile(Frames[0].Jpeg, *(FPaths::ProjectSavedDir() / TEXT("UEcine/Screenshots/storyboard-frame.jpg"))));
		}
		RemoveStoryboardFrame(Shots[0].Sequence, 1);
		Frames = GetStoryboardFrames(Shots[0].Sequence);
		TestTrue(TEXT("A storyboard frame is removed"), Frames.Num() == 2 && Frames[1].ShotFrame == 90);
		TestTrue(TEXT("Removing a frame can be undone"), GEditor->UndoTransaction());
		TestEqual(TEXT("After undo the frame is back"), GetStoryboardFrames(Shots[0].Sequence).Num(), 3);
	}
	else
	{
		AddInfo(TEXT("Storyboard pictures skipped: they need a renderer (run tools/test.ps1 with -Rendering)."));
	}

	// The paperwork, read back from the files.
	GoToShot(Shots[1]);
	if (!TestTrue(TEXT("The paperwork is exported"), ExportPaperwork(ExportDirectory, Files, Error)) || !TestEqual(TEXT("Three files are written"), Files.Num(), 3))
	{
		AddError(Error.ToString());
		return false;
	}
	TestEqual(TEXT("Exporting puts the playhead back"), FindShotAtPlayhead(Shots), 1);

	const FString FocalLength = FString::Printf(TEXT("%d mm"), FMath::RoundToInt32(CameraA->GetCineCameraComponent()->CurrentFocalLength));
	const FString DollyName = UEcine::Rig::GetTypeName(EUEcineRigType::Dolly).ToString();
	const FString Duration = FString::Printf(TEXT("5 s (%d frames)"), ShotFrames);
	const auto GetSizeName = [](EUEcineShotSize Size) { return UEcine::Shots::GetShotSizeName(Size).ToString(); };

	TArray<uint8> ShotListBytes;
	FString ShotList;
	TestTrue(TEXT("The shot list file is there"), FFileHelper::LoadFileToArray(ShotListBytes, *Files[0]) && FFileHelper::LoadFileToString(ShotList, *Files[0]));
	TestTrue(TEXT("The shot list says it is UTF-8, for Excel"), ShotListBytes.Num() > 3 && ShotListBytes[0] == 0xEF && ShotListBytes[1] == 0xBB && ShotListBytes[2] == 0xBF);
	TArray<FString> Lines;
	ShotList.ParseIntoArray(Lines, TEXT("\r\n"));
	if (TestEqual(TEXT("The shot list has a heading and a line for each shot"), Lines.Num(), 4))
	{
		TestEqual(TEXT("Shot list heading"), Lines[0], FString(TEXT("Scene,Shot,Size,Move,Lens,Support,Start,Duration,Notes")));
		TestEqual(TEXT("Shot list, shot 1"), Lines[1], FString::Printf(TEXT("12,%s,%s,Track right with Hero,%s,%s,00:00:00:00,%s,\"Dawn light, haze\""),
			*Shots[0].Name, *GetSizeName(EUEcineShotSize::Wide), *FocalLength, *DollyName, *Duration));
		TestEqual(TEXT("Shot list, shot 2"), Lines[2], FString::Printf(TEXT("12,%s,%s,Static,%s,,00:00:05:00,%s,\"Eyes sharp, \"\"soft\"\" background\""),
			*Shots[1].Name, *GetSizeName(EUEcineShotSize::CloseUp), *FocalLength, *Duration));
		TestEqual(TEXT("Shot list, shot 3"), Lines[3], FString::Printf(TEXT("12A,%s,%s,Push in,%s,%s,00:00:10:00,%s,"),
			*Shots[2].Name, *GetSizeName(EUEcineShotSize::MediumCloseUp), *FocalLength, *DollyName, *Duration));
	}

	FString CameraReport;
	TestTrue(TEXT("The camera report file is there"), FFileHelper::LoadFileToString(CameraReport, *Files[1]));
	Lines.Reset();
	CameraReport.ParseIntoArray(Lines, TEXT("\r\n"));
	if (TestEqual(TEXT("The camera report has a heading and a line for each shot"), Lines.Num(), 4))
	{
		TestTrue(TEXT("Camera report, shot 1 is on the A camera"), Lines[1].Contains(TEXT(",A Cam,")) && !Lines[1].Contains(TEXT("ND ")));
		TestTrue(TEXT("Camera report, shot 1 is focused on the hero"), Lines[1].Contains(TEXT(",6 m,")));
		TestTrue(TEXT("Camera report, shot 2 is on the B camera with its filter and stock"), Lines[2].Contains(TEXT(",B Cam,")) && Lines[2].Contains(TEXT(",1600,")) && Lines[2].Contains(TEXT(",ND 0.6,")));
		TestTrue(TEXT("Camera report, every shot has its lens and stop"), Lines[1].Contains(FocalLength) && Lines[2].Contains(TEXT(",T")) && Lines[3].Contains(TEXT(",24 fps,")));
	}

	FString Page;
	TestTrue(TEXT("The printable page is there"), FFileHelper::LoadFileToString(Page, *Files[2]));
	TestTrue(TEXT("The page is a web page in the editor's language"), Page.StartsWith(TEXT("<!DOCTYPE html>")) && Page.Contains(TEXT("<html lang=\"")));
	for (const FShot& Shot : Shots)
	{
		TestTrue(TEXT("The page has every shot"), Page.Contains(Shot.Name));
		TestTrue(TEXT("The page has every shot's overhead plan"), Page.Contains(TEXT("Overhead plan of ") + Shot.Name));
	}
	TestTrue(TEXT("The page draws the plans"), Page.Contains(TEXT("<svg")));
	TestTrue(TEXT("The page has the notes"), Page.Contains(TEXT("Dawn light, haze")) && Page.Contains(TEXT("&quot;soft&quot;")));
	if (bPictures)
	{
		TestTrue(TEXT("The page has the storyboard frames inside it"), Page.Contains(TEXT("data:image/jpeg;base64,")) && Page.Contains(TEXT("Frame 13")) && Page.Contains(TEXT("Frame 8")));
	}

	// A sequence that is not cut into shots is one shot.
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	ULevelSequence* Uncut = NewObject<ULevelSequence>(GetTransientPackage(), TEXT("UEcineUncutSequence"), RF_Transactional);
	Uncut->Initialize();
	Uncut->GetMovieScene()->SetPlaybackRange(0, Uncut->GetMovieScene()->GetTickResolution().AsFrameNumber(2.0).Value);
	if (TestTrue(TEXT("A sequence with no shots opens"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Uncut)))
	{
		const TArray<FShot> UncutShots = GetShots();
		if (TestEqual(TEXT("It is one shot"), UncutShots.Num(), 1))
		{
			TestTrue(TEXT("The shot is the sequence itself"), UncutShots[0].Sequence == Uncut && UncutShots[0].Section == nullptr && UncutShots[0].Name == Uncut->GetName());
			TestEqual(TEXT("The playhead is in it"), FindShotAtPlayhead(UncutShots), 0);
			TestNull(TEXT("It has no camera"), FindShotCamera(UncutShots[0]));
			TestFalse(TEXT("A shot with no camera has no picture to grab"), GrabStoryboardFrame(UncutShots[0], Error));
			SetScene(Uncut, TEXT("3"));
			TestEqual(TEXT("It takes shot list entries"), MakeShotReport(UncutShots[0]).Scene, FString(TEXT("3")));
			TestTrue(TEXT("With no camera its report has no lens"), MakeShotReport(UncutShots[0]).FocalLength.IsEmpty());
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
