// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "CineCameraActor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCameraLoadTest, "Rosc.Editor.CameraLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * A level saved before its camera's lens kit gained lenses: the camera named its lens by a place in
 * the kit that now holds another lens. Saved and loaded for real, the camera has to come back on
 * the lens it had.
 */
bool FRoscCameraLoadTest::RunTest(const FString& Parameters)
{
	const FString MapPath = TEXT("/Game/RoscTest/CameraLoad");
	const FRoscLensKit* Kit = FRoscCameraDatabase::Get().FindLensKit(TEXT("zeiss-supreme-prime"));
	if (!TestNotNull(TEXT("The Supreme Prime kit is in the database"), Kit))
	{
		return false;
	}
	const int32 Lens21 = Kit->Lenses.IndexOfByPredicate([](const FRoscLens& Lens) { return Lens.FocalLengthMm == 21.0; });
	if (!TestTrue(TEXT("The kit has lenses before its 21 mm, as it has had since 0.2.0"), Lens21 > 0))
	{
		return false;
	}

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	ACineCameraActor* Camera = World != nullptr ? Rosc::CameraEditing::SpawnCamera(World, FTransform::Identity) : nullptr;
	URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(Camera);
	if (!TestNotNull(TEXT("A camera with a package is made"), Component))
	{
		return false;
	}
	// The 21 mm was the first lens of the kit in 0.1.0.
	Component->Package.LensKitId = Kit->Id;
	Component->Package.LensIndex = 0;
	Component->Package.FocalLengthMm = 21.0f;
	if (!TestTrue(TEXT("The level is saved"), UEditorLoadingAndSavingUtils::SaveMap(World, MapPath)))
	{
		return false;
	}

	UEditorLoadingAndSavingUtils::NewBlankMap(false);
	UWorld* Loaded = UEditorLoadingAndSavingUtils::LoadMap(MapPath);
	const URoscCameraPackageComponent* LoadedComponent = nullptr;
	if (Loaded != nullptr)
	{
		for (TActorIterator<ACineCameraActor> It(Loaded); It && LoadedComponent == nullptr; ++It)
		{
			LoadedComponent = URoscCameraPackageComponent::Find(*It);
		}
	}
	if (TestNotNull(TEXT("The saved camera loads"), LoadedComponent))
	{
		TestEqual(TEXT("The loaded camera is on its 21 mm again"), LoadedComponent->Package.LensIndex, Lens21);
		TestEqual(TEXT("Its focal length is kept"), LoadedComponent->Package.FocalLengthMm, 21.0f);
	}

	// The tests after this one look through cameras in the lit level the editor starts with.
	UEditorLoadingAndSavingUtils::NewMapFromTemplate(TEXT("/Engine/Maps/Templates/Template_Default"), false);
	const FString File = FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
	TestTrue(TEXT("The test level is deleted"), IFileManager::Get().Delete(*File, false, true));
	IFileManager::Get().DeleteDirectory(*FPaths::GetPath(File));
	return true;
}

#endif
