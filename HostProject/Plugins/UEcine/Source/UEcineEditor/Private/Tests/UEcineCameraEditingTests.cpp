// Copyright 2026 Fiavaion. All rights reserved.

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Misc/AutomationTest.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineOptics.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineCameraEditingTest, "UEcine.Editor.CameraEditing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineCameraEditingTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!TestNotNull(TEXT("The editor has a world"), World))
	{
		return false;
	}

	// New camera: a standard Cine Camera Actor carrying a package that is already applied.
	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(World, FTransform(FVector(100.0, 0.0, 150.0)));
	if (!TestNotNull(TEXT("SpawnCamera creates a camera"), CameraActor))
	{
		return false;
	}
	TestTrue(TEXT("The camera is labelled like a film camera"), CameraActor->GetActorLabel().EndsWith(TEXT(" Cam")));
	TestTrue(TEXT("The new camera is selected"), CameraActor->IsSelected());

	UUEcineCameraPackageComponent* Component = UUEcineCameraPackageComponent::Find(CameraActor);
	if (!TestNotNull(TEXT("The camera has a package component"), Component))
	{
		return false;
	}
	TestTrue(TEXT("Adding a package twice returns the existing one"), UEcine::CameraEditing::AddCameraPackage(CameraActor) == Component);

	const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();
	const FUEcineCameraBody* Body = Database.FindBody(Component->Package.BodyId);
	const FUEcineLensKit* Kit = Database.FindLensKit(Component->Package.LensKitId);
	if (!TestNotNull(TEXT("The default body is in the database"), Body) || !TestNotNull(TEXT("The default lens kit is in the database"), Kit))
	{
		return false;
	}
	const FUEcineRecordingFormat* Format = Body->FindFormat(Component->Package.FormatId);
	if (!TestNotNull(TEXT("The default format belongs to the body"), Format))
	{
		return false;
	}

	const UCineCameraComponent* Camera = CameraActor->GetCineCameraComponent();
	TestEqual(TEXT("Filmback width is the recording format's"), Camera->Filmback.SensorWidth, static_cast<float>(Format->SensorWidthMm), 0.001f);
	TestEqual(TEXT("Filmback height is the recording format's"), Camera->Filmback.SensorHeight, static_cast<float>(Format->SensorHeightMm), 0.001f);

	// Change the lens: one undoable step that reaches the camera.
	const float FocalBefore = Camera->CurrentFocalLength;
	const int32 NewLensIndex = Component->Package.LensIndex == 0 ? Kit->Lenses.Num() - 1 : 0;
	const float NewFocal = static_cast<float>(Kit->Lenses[NewLensIndex].FocalLengthMm);
	TestNotEqual(TEXT("The test changes to a different focal length"), NewFocal, FocalBefore);

	FText Error;
	const bool bEdited = UEcine::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Change Lens")),
		[Kit, NewLensIndex](FUEcineCameraPackage& Package) { Package.SelectLens(*Kit, NewLensIndex); }, Error);
	TestTrue(TEXT("Changing the lens succeeds"), bEdited);
	TestEqual(TEXT("The camera has the new focal length"), Camera->CurrentFocalLength, NewFocal, 0.001f);

	TestTrue(TEXT("The lens change can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores the camera's focal length"), Camera->CurrentFocalLength, FocalBefore, 0.001f);
	TestEqual(TEXT("Undo restores the package"), Component->Package.FocalLengthMm, FocalBefore, 0.001f);

	TestTrue(TEXT("The lens change can be redone"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo reapplies the focal length"), Camera->CurrentFocalLength, NewFocal, 0.001f);

	// An edit that cannot be applied leaves the package and camera as they were.
	const FString BodyIdBefore = Component->Package.BodyId;
	const bool bBadEdit = UEcine::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Bad Edit")),
		[](FUEcineCameraPackage& Package) { Package.BodyId = TEXT("no-such-body"); }, Error);
	TestFalse(TEXT("An unknown body is rejected"), bBadEdit);
	TestFalse(TEXT("The rejection says why"), Error.IsEmpty());
	TestEqual(TEXT("The package is unchanged after a rejected edit"), *Component->Package.BodyId, *BodyIdBefore);
	TestEqual(TEXT("The camera is unchanged after a rejected edit"), Camera->CurrentFocalLength, NewFocal, 0.001f);

	// A field of view, such as the one a reference picture was taken with, matched to the kit:
	// the angle a 60 mm lens sees on this format, which the Signature Primes come nearest with the 58.
	const double FieldOfViewDeg = UEcine::Optics::HorizontalFieldOfViewDeg(Format->SensorWidthMm, 60.0);
	UEcine::CameraEditing::FLensMatch Match;
	if (TestTrue(TEXT("A field of view is matched to a lens"), UEcine::CameraEditing::MatchLensToFieldOfView(CameraActor, FieldOfViewDeg, Match)))
	{
		TestEqual(TEXT("The focal length that sees it on this format"), Match.FocalLengthMm, 60.0, 0.01);
		TestEqual(TEXT("The kit's nearest lens"), *Match.LensName, TEXT("58mm T1.8"));
		TestEqual(TEXT("And its focal length"), Match.LensFocalLengthMm, 58.0, 0.01);
	}
	TestEqual(TEXT("Finding the lens does not put it on"), Camera->CurrentFocalLength, NewFocal, 0.001f);
	TestFalse(TEXT("An angle no lens has is not matched"), UEcine::CameraEditing::MatchLensToFieldOfView(CameraActor, 180.0, Match));
	TestTrue(TEXT("The matched lens is put on"), UEcine::CameraEditing::PutOnLensForFieldOfView(CameraActor, FieldOfViewDeg, Error));
	TestEqual(TEXT("The camera has it"), Camera->CurrentFocalLength, 58.0f, 0.001f);
	TestTrue(TEXT("Putting it on can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo puts the lens before back"), Camera->CurrentFocalLength, NewFocal, 0.001f);

	GEditor->SelectNone(false, true, false);
	World->EditorDestroyActor(CameraActor, false);
	GEditor->ResetTransaction(FText::FromString(TEXT("UEcine camera editing test")));
	// Add Camera Here: the new camera stands where the view was, with the kit's lens nearest to what the view showed.
	FLevelEditorViewportClient* View = nullptr;
	for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
	{
		if (Candidate != nullptr && Candidate->IsPerspective())
		{
			View = Candidate;
			break;
		}
	}
	if (TestNotNull(TEXT("The level has a perspective view"), View))
	{
		const FVector ViewLocationBefore = View->GetViewLocation();
		const FRotator ViewRotationBefore = View->GetViewRotation();
		const float ViewFovBefore = View->ViewFOV;
		TGuardValue<FLevelEditorViewportClient*> CurrentView(GCurrentLevelEditingViewportClient, View);
		View->SetViewLocation(FVector(-420.0, 130.0, 175.0));
		View->SetViewRotation(FRotator(-8.0, 25.0, 0.0));
		View->ViewFOV = 40.0f;

		ACineCameraActor* Added = UEcine::CameraEditing::SpawnCameraAtViewport();
		const UUEcineCameraPackageComponent* AddedPackage = Added != nullptr ? UUEcineCameraPackageComponent::Find(Added) : nullptr;
		if (TestNotNull(TEXT("Add Camera Here makes a camera with a package"), AddedPackage))
		{
			TestTrue(TEXT("The camera stands where the view was"), Added->GetActorLocation().Equals(FVector(-420.0, 130.0, 175.0), 0.01));
			TestTrue(TEXT("The camera looks where the view looked"), Added->GetActorRotation().Equals(FRotator(-8.0, 25.0, 0.0), 0.01));

			// The nearest lens of the kit to a 40 degree view is the one no other lens of the kit beats.
			const UCineCameraComponent* AddedLens = Added->GetCineCameraComponent();
			const double Wanted = UEcine::Optics::FocalLengthForFieldOfViewMm(AddedLens->Filmback.SensorWidth, 40.0, AddedLens->LensSettings.SqueezeFactor);
			const FUEcineLensKit* AddedKit = Database.FindLensKit(AddedPackage->Package.LensKitId);
			double Nearest = TNumericLimits<double>::Max();
			for (const FUEcineLens& Lens : AddedKit->Lenses)
			{
				Nearest = FMath::Min(Nearest, FMath::Abs(Lens.FocalLengthMm - Wanted));
			}
			AddInfo(FString::Printf(TEXT("A 40 degree view asks for %.1f mm; the camera was given the %.0f mm"), Wanted, AddedLens->CurrentFocalLength));
			TestEqual(TEXT("The camera has the kit's lens nearest to the view"), FMath::Abs(AddedLens->CurrentFocalLength - Wanted), Nearest, 0.01);

			GEditor->UndoTransaction();
			TestFalse(TEXT("One undo takes the added camera away"), IsValid(Added) && Added->GetLevel() != nullptr && Added->GetLevel()->Actors.Contains(Added));
		}
		View->SetViewLocation(ViewLocationBefore);
		View->SetViewRotation(ViewRotationBefore);
		View->ViewFOV = ViewFovBefore;
	}

	return true;
}

#endif
