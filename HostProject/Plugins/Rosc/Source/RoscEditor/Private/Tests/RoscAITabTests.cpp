// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "CineCameraActor.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SRoscAITab.h"
#include "RoscAIConsent.h"
#include "RoscAIModels.h"
#include "RoscAISettings.h"
#include "RoscAISubsystem.h"
#include "RoscCameraEditing.h"
#include "RoscCameraTarget.h"
#include "RoscEditorModule.h"
#include "RoscGripEditing.h"
#include "RoscRig.h"
#include "RoscShotPlan.h"
#include "RoscWidgets.h"
#include "Tests/RoscWidgetTestUtils.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Every line of text a widget shows, one after the other. */
	void CollectAITabText(const TSharedRef<SWidget>& Widget, FString& OutText)
	{
		if (Widget->GetType() == FName("STextBlock"))
		{
			OutText += StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() + LINE_TERMINATOR;
		}
		FChildren* Children = Widget->GetAllChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			CollectAITabText(Children->GetChildAt(Index), OutText);
		}
	}

	/** The tab's text at a moment later than its last look at the camera. */
	FString ReadAITabText(SRoscAITab& Tab, double Time)
	{
		Tab.Tick(FGeometry(), Time, 0.0f);
		FString Text;
		CollectAITabText(Tab.AsShared(), Text);
		return Text;
	}
}

/**
 * The plan the AI tab shows is read against the camera as it stands: put the camera on another
 * support from the Grip tab while a plan is being read, and the plan's warnings follow.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAITabTest, "Rosc.Editor.AITab", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAITabTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 160.0)));
	ON_SCOPE_EXIT
	{
		GEditor->SelectNone(false, true, false);
		if (ARoscRig* Rig = Rosc::GripEditing::FindRig(Camera))
		{
			World->EditorDestroyActor(Rig, false);
		}
		World->EditorDestroyActor(Camera, false);
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc AI tab test")));
	};
	Rosc::GripEditing::PutCameraOnRig(Camera, *Rosc::Rig::FindPreset(TEXT("crane-30ft")));

	const TSharedRef<FRoscCameraTarget> Target = MakeShared<FRoscCameraTarget>();
	Target->SetCamera(Camera);
	const TSharedRef<SRoscAITab> Tab = SNew(SRoscAITab, Target);

	FRoscShotPlanResult Plan;
	Plan.PlanJson = TEXT(R"({ "summary": "A slow push forward.", "steps": [
		{ "step": "move_to_mark", "travelM": 1, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 0, "extensionM": 0,
		  "panDeg": 0, "tiltDeg": 0, "rollDeg": 0, "durationSeconds": 8, "easeIn": 0.25, "easeOut": 0.25 } ] })");
	FString Fault;
	Plan.bSuccess = Rosc::ShotPlan::Parse(Plan.PlanJson, Plan.Plan, Fault);
	TestTrue(*FString::Printf(TEXT("The test's plan is read (%s)"), *Fault), Plan.bSuccess);
	Tab->ShowPlan(Plan);

	const TCHAR* LeftOut = TEXT("That part of the move is left out.");
	const FString OnCrane = ReadAITabText(*Tab, 1.0);
	TestTrue(TEXT("The plan is shown"), OnCrane.Contains(TEXT("A slow push forward.")));
	TestTrue(TEXT("On a crane with no track, the push forward is said to be left out"), OnCrane.Contains(LeftOut));

	Rosc::GripEditing::PutCameraOnRig(Camera, Rosc::Rig::GetDefaultPreset(ERoscRigType::Dolly));
	const FString OnDolly = ReadAITabText(*Tab, 2.0);
	TestTrue(TEXT("The plan is still shown"), OnDolly.Contains(TEXT("A slow push forward.")));
	TestFalse(TEXT("On a dolly the push forward is part of the move, and the warning has gone"), OnDolly.Contains(LeftOut));
	return true;
}

/**
 * The notice before a model's first download (ADR-012): Download opens it in the model's row and
 * downloads nothing, Not Now closes it, and Agree and Download records the agreement and starts.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIModelNoticeTest, "Rosc.Editor.AIModelNotice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIModelNoticeTest::RunTest(const FString& Parameters)
{
	using Rosc::WidgetTests::FindButtonShowing;

	// An empty models folder of the test's own, so no model is downloaded yet, and no agreement.
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	const FString FolderBefore = Settings->ModelsFolder.Path;
	const TArray<FString> NoticesBefore = Settings->AcceptedModelNotices;
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Rosc") / TEXT("AITabTests") / TEXT("Models"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	Settings->ModelsFolder.Path = Folder;
	Settings->AcceptedModelNotices.Reset();
	URoscAISubsystem* AI = GEditor->GetEditorSubsystem<URoscAISubsystem>();
	ON_SCOPE_EXIT
	{
		AI->CancelDownload();
		Settings->ModelsFolder.Path = FolderBefore;
		Settings->AcceptedModelNotices = NoticesBefore;
		Settings->SaveConfig();
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};

	const FRoscAIModel* Depth = FRoscAIModelList::Get().FindForTask(ERoscAITask::Depth);
	if (!TestNotNull(TEXT("The shipped list has a depth model"), Depth))
	{
		return false;
	}
	const TSharedRef<FRoscCameraTarget> Target = MakeShared<FRoscCameraTarget>();
	const TSharedRef<SRoscAITab> Tab = SNew(SRoscAITab, Target);
	const TCHAR* Heading = TEXT("Before you download Depth Anything V2 Small");

	FString Text = ReadAITabText(*Tab, 1.0);
	TestFalse(TEXT("No notice is open to begin with"), Text.Contains(Heading));
	TestNotNull(TEXT("Each model has a Training Data button"), FindButtonShowing(Tab, TEXT("Training Data")).Get());
	TSharedPtr<SButton> Download = FindButtonShowing(Tab, TEXT("Download"));
	if (!TestNotNull(TEXT("The depth model has a Download button"), Download.Get()))
	{
		return false;
	}

	Download->SimulateClick();
	Text = ReadAITabText(*Tab, 2.0);
	TestTrue(TEXT("Download opens the notice in the model's row"), Text.Contains(Heading));
	TestTrue(TEXT("Which nothing is downloaded before"), AI->GetDownloadingModel() == nullptr);
	TestTrue(TEXT("It names who holds the licence"), Text.Contains(TEXT("held by The Depth Anything authors")));
	TestTrue(TEXT("It names the non-commercial data with its owner"), Text.Contains(TEXT("Virtual KITTI 2 from Naver Corporation")));
	TestTrue(TEXT("And counts the sets whose terms do not say"), Text.Contains(TEXT("For 3 sets the terms do not say")));
	TestTrue(TEXT("And gives the caution"), Text.Contains(TEXT("Whether those terms reach what you make with the model is not settled.")));
	TestFalse(TEXT("A set whose owner allows commercial use is not among them"), Text.Contains(TEXT("TartanAir from")));
	Download = FindButtonShowing(Tab, TEXT("Download"));
	TestTrue(TEXT("While the notice is open, Download waits for it"), Download.IsValid() && !Download->IsEnabled());

	const TSharedPtr<SButton> NotNow = FindButtonShowing(Tab, TEXT("Not Now"));
	if (!TestNotNull(TEXT("The notice has Not Now"), NotNow.Get()))
	{
		return false;
	}
	NotNow->SimulateClick();
	Text = ReadAITabText(*Tab, 3.0);
	TestFalse(TEXT("Not Now closes the notice"), Text.Contains(Heading));
	TestFalse(TEXT("And agrees to nothing"), Rosc::AIConsent::IsModelNoticeAgreed(*Depth));
	TestTrue(TEXT("And downloads nothing"), AI->GetDownloadingModel() == nullptr);

	FindButtonShowing(Tab, TEXT("Download"))->SimulateClick();
	ReadAITabText(*Tab, 4.0);
	const TSharedPtr<SButton> Agree = FindButtonShowing(Tab, TEXT("Agree and Download"));
	if (!TestNotNull(TEXT("The notice opens again, with Agree and Download"), Agree.Get()))
	{
		return false;
	}
	Agree->SimulateClick();
	TestTrue(TEXT("Agree and Download records the agreement"), Rosc::AIConsent::IsModelNoticeAgreed(*Depth));
	const FRoscAIModel* Downloading = AI->GetDownloadingModel();
	TestTrue(TEXT("And starts the download"), Downloading != nullptr && Downloading->Id == Depth->Id);
	// The test downloads nothing in earnest.
	AI->CancelDownload();
	Text = ReadAITabText(*Tab, 5.0);
	TestFalse(TEXT("And the notice has closed"), Text.Contains(Heading));

	// Training Data opens a page beside the user guide, which names every set of every model and its owner.
	FString Page;
	TestTrue(TEXT("The page Training Data opens is in the plugin"), FFileHelper::LoadFileToString(Page, *FRoscEditorModule::GetTrainingDataFile()));
	for (const FRoscAIModel& Model : FRoscAIModelList::Get().GetModels())
	{
		TestTrue(*FString::Printf(TEXT("The page says who holds the licence of %s"), *Model.Name), Page.Contains(Model.LicenceHolder));
		for (const FRoscAITrainingSet& Set : Model.TrainingData)
		{
			TestTrue(*FString::Printf(TEXT("The page names %s, which %s learnt from, and its owner"), *Set.Name, *Model.Name),
				Page.Contains(Set.Name) && Page.Contains(Set.Owner));
		}
	}
	FString Guide;
	FFileHelper::LoadFileToString(Guide, *FRoscEditorModule::GetUserGuideFile());
	TestTrue(TEXT("The user guide links to it"), Guide.Contains(TEXT("href=\"TrainingData.html\"")));
	return true;
}

/** What the file and folder boxes take when something is dragged onto them from Explorer. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscDroppedPathTest, "Rosc.Editor.DroppedPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscDroppedPathTest::RunTest(const FString& Parameters)
{
	using Rosc::Widgets::ResolveDroppedPath;

	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("RoscDroppedPathTest"));
	const FString Picture = Folder / TEXT("Plate.PNG");
	const FString Notes = Folder / TEXT("Notes.txt");
	IFileManager::Get().MakeDirectory(*Folder, true);
	FFileHelper::SaveStringToFile(TEXT("x"), *Picture);
	FFileHelper::SaveStringToFile(TEXT("x"), *Notes);
	const TArray<FString> Pictures = { TEXT("png"), TEXT("jpg") };

	FString Path;
	FText Error;
	TestTrue(TEXT("A picture dropped on the picture box is taken"), ResolveDroppedPath({ Picture, Notes }, false, Pictures, Path, Error) && Path == Picture);
	TestFalse(TEXT("A text file is not a picture"), ResolveDroppedPath({ Notes }, false, Pictures, Path, Error));
	TestTrue(TEXT("The refusal names the file and the kinds taken"), Error.ToString().Contains(TEXT("Notes.txt")) && Error.ToString().Contains(TEXT("png, jpg")));
	TestFalse(TEXT("A folder is not a file"), ResolveDroppedPath({ Folder }, false, Pictures, Path, Error));
	TestTrue(TEXT("A folder dropped on a folder box is taken"), ResolveDroppedPath({ Folder }, true, {}, Path, Error) && Path == Folder);
	TestTrue(TEXT("A file dropped on a folder box gives the folder it is in"), ResolveDroppedPath({ Picture }, true, {}, Path, Error) && Path == Folder);
	TestFalse(TEXT("Nothing dropped is refused"), ResolveDroppedPath({}, true, {}, Path, Error));

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif
