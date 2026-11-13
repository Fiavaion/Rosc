// Copyright 2026 Fiavaion. All rights reserved.

#include "CineCameraActor.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SUEcineAITab.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraTarget.h"
#include "UEcineGripEditing.h"
#include "UEcineRig.h"
#include "UEcineShotPlan.h"
#include "UEcineWidgets.h"
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
	FString ReadAITabText(SUEcineAITab& Tab, double Time)
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineAITabTest, "UEcine.Editor.AITab", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineAITabTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 160.0)));
	ON_SCOPE_EXIT
	{
		GEditor->SelectNone(false, true, false);
		if (AUEcineRig* Rig = UEcine::GripEditing::FindRig(Camera))
		{
			World->EditorDestroyActor(Rig, false);
		}
		World->EditorDestroyActor(Camera, false);
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine AI tab test")));
	};
	UEcine::GripEditing::PutCameraOnRig(Camera, *UEcine::Rig::FindPreset(TEXT("crane-30ft")));

	const TSharedRef<FUEcineCameraTarget> Target = MakeShared<FUEcineCameraTarget>();
	Target->SetCamera(Camera);
	const TSharedRef<SUEcineAITab> Tab = SNew(SUEcineAITab, Target);

	FUEcineShotPlanResult Plan;
	Plan.PlanJson = TEXT(R"({ "summary": "A slow push forward.", "steps": [
		{ "step": "move_to_mark", "travelM": 1, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 0, "extensionM": 0,
		  "panDeg": 0, "tiltDeg": 0, "rollDeg": 0, "durationSeconds": 8, "easeIn": 0.25, "easeOut": 0.25 } ] })");
	FString Fault;
	Plan.bSuccess = UEcine::ShotPlan::Parse(Plan.PlanJson, Plan.Plan, Fault);
	TestTrue(*FString::Printf(TEXT("The test's plan is read (%s)"), *Fault), Plan.bSuccess);
	Tab->ShowPlan(Plan);

	const TCHAR* LeftOut = TEXT("That part of the move is left out.");
	const FString OnCrane = ReadAITabText(*Tab, 1.0);
	TestTrue(TEXT("The plan is shown"), OnCrane.Contains(TEXT("A slow push forward.")));
	TestTrue(TEXT("On a crane with no track, the push forward is said to be left out"), OnCrane.Contains(LeftOut));

	UEcine::GripEditing::PutCameraOnRig(Camera, UEcine::Rig::GetDefaultPreset(EUEcineRigType::Dolly));
	const FString OnDolly = ReadAITabText(*Tab, 2.0);
	TestTrue(TEXT("The plan is still shown"), OnDolly.Contains(TEXT("A slow push forward.")));
	TestFalse(TEXT("On a dolly the push forward is part of the move, and the warning has gone"), OnDolly.Contains(LeftOut));
	return true;
}

/** What the file and folder boxes take when something is dragged onto them from Explorer. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDroppedPathTest, "UEcine.Editor.DroppedPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDroppedPathTest::RunTest(const FString& Parameters)
{
	using UEcine::Widgets::ResolveDroppedPath;

	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("UEcineDroppedPathTest"));
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
