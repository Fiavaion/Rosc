// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Misc/AutomationTest.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscFocusEditing.h"
#include "RoscOptics.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCameraEditingTest, "Rosc.Editor.CameraEditing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscCameraEditingTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!TestNotNull(TEXT("The editor has a world"), World))
	{
		return false;
	}

	// New camera: a standard Cine Camera Actor carrying a package that is already applied.
	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(World, FTransform(FVector(100.0, 0.0, 150.0)));
	if (!TestNotNull(TEXT("SpawnCamera creates a camera"), CameraActor))
	{
		return false;
	}
	TestTrue(TEXT("The camera is labelled like a film camera"), CameraActor->GetActorLabel().EndsWith(TEXT(" Cam")));
	TestTrue(TEXT("The new camera is selected"), CameraActor->IsSelected());

	URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(CameraActor);
	if (!TestNotNull(TEXT("The camera has a package component"), Component))
	{
		return false;
	}
	TestTrue(TEXT("Adding a package twice returns the existing one"), Rosc::CameraEditing::AddCameraPackage(CameraActor) == Component);

	const FRoscCameraDatabase& Database = FRoscCameraDatabase::Get();
	const FRoscCameraBody* Body = Database.FindBody(Component->Package.BodyId);
	const FRoscLensKit* Kit = Database.FindLensKit(Component->Package.LensKitId);
	if (!TestNotNull(TEXT("The default body is in the database"), Body) || !TestNotNull(TEXT("The default lens kit is in the database"), Kit))
	{
		return false;
	}
	const FRoscRecordingFormat* Format = Body->FindFormat(Component->Package.FormatId);
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
	const bool bEdited = Rosc::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Change Lens")),
		[Kit, NewLensIndex](FRoscCameraPackage& Package) { Package.SelectLens(*Kit, NewLensIndex); }, Error);
	TestTrue(TEXT("Changing the lens succeeds"), bEdited);
	TestEqual(TEXT("The camera has the new focal length"), Camera->CurrentFocalLength, NewFocal, 0.001f);

	TestTrue(TEXT("The lens change can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores the camera's focal length"), Camera->CurrentFocalLength, FocalBefore, 0.001f);
	TestEqual(TEXT("Undo restores the package"), Component->Package.FocalLengthMm, FocalBefore, 0.001f);

	TestTrue(TEXT("The lens change can be redone"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo reapplies the focal length"), Camera->CurrentFocalLength, NewFocal, 0.001f);

	// An edit that cannot be applied leaves the package and camera as they were.
	const FString BodyIdBefore = Component->Package.BodyId;
	const bool bBadEdit = Rosc::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Bad Edit")),
		[](FRoscCameraPackage& Package) { Package.BodyId = TEXT("no-such-body"); }, Error);
	TestFalse(TEXT("An unknown body is rejected"), bBadEdit);
	TestFalse(TEXT("The rejection says why"), Error.IsEmpty());
	TestEqual(TEXT("The package is unchanged after a rejected edit"), *Component->Package.BodyId, *BodyIdBefore);
	TestEqual(TEXT("The camera is unchanged after a rejected edit"), Camera->CurrentFocalLength, NewFocal, 0.001f);

	// A field of view, such as the one a reference picture was taken with, matched to the kit:
	// the angle a 60 mm lens sees on this format, which the Signature Primes come nearest with the 58.
	const double FieldOfViewDeg = Rosc::Optics::HorizontalFieldOfViewDeg(Format->SensorWidthMm, 60.0);
	Rosc::CameraEditing::FLensMatch Match;
	if (TestTrue(TEXT("A field of view is matched to a lens"), Rosc::CameraEditing::MatchLensToFieldOfView(CameraActor, FieldOfViewDeg, Match)))
	{
		TestEqual(TEXT("The focal length that sees it on this format"), Match.FocalLengthMm, 60.0, 0.01);
		TestEqual(TEXT("The kit's nearest lens"), *Match.LensName, TEXT("58mm T1.8"));
		TestEqual(TEXT("And its focal length"), Match.LensFocalLengthMm, 58.0, 0.01);
	}
	TestEqual(TEXT("Finding the lens does not put it on"), Camera->CurrentFocalLength, NewFocal, 0.001f);
	TestFalse(TEXT("An angle no lens has is not matched"), Rosc::CameraEditing::MatchLensToFieldOfView(CameraActor, 180.0, Match));
	TestTrue(TEXT("The matched lens is put on"), Rosc::CameraEditing::PutOnLensForFieldOfView(CameraActor, FieldOfViewDeg, Error));
	TestEqual(TEXT("The camera has it"), Camera->CurrentFocalLength, 58.0f, 0.001f);
	TestTrue(TEXT("Putting it on can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo puts the lens before back"), Camera->CurrentFocalLength, NewFocal, 0.001f);

	// A longer lens that cannot focus as near as the focus was set: the lens goes to its close
	// focus and the user is told where the focus was.
	const int32 LongLensIndex = Kit->Lenses.Num() - 1;
	const float ShortCloseFocusCm = static_cast<float>(Kit->Lenses[0].CloseFocusM * 100.0);
	const float LongCloseFocusCm = static_cast<float>(Kit->Lenses[LongLensIndex].CloseFocusM * 100.0);
	const float SetFocusCm = (ShortCloseFocusCm + LongCloseFocusCm) * 0.5f;
	TestTrue(TEXT("The kit's longest lens focuses less near than its shortest"), LongCloseFocusCm > ShortCloseFocusCm);
	FText Notice;
	TestTrue(TEXT("The shortest lens goes on"), Rosc::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Change Lens")),
		[Kit](FRoscCameraPackage& Package) { Package.SelectLens(*Kit, 0); }, Error, &Notice));
	TestTrue(TEXT("A lens change that leaves the focus where it was says nothing"), Notice.IsEmpty());
	Rosc::FocusEditing::SetFocusDistance(CameraActor, SetFocusCm);
	TestTrue(TEXT("The longest lens goes on"), Rosc::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Change Lens")),
		[Kit, LongLensIndex](FRoscCameraPackage& Package) { Package.SelectLens(*Kit, LongLensIndex); }, Error, &Notice));
	TestEqual(TEXT("The lens is at its close focus"), Rosc::FocusEditing::GetFocusDistance(CameraActor), LongCloseFocusCm, 0.01f);
	AddInfo(FString::Printf(TEXT("Notice: %s"), *Notice.ToString()));
	TestFalse(TEXT("The user is told the focus moved"), Notice.IsEmpty());
	TestTrue(TEXT("Undo puts the shorter lens back"), GEditor->UndoTransaction());
	TestEqual(TEXT("And the focus where it was set"), Rosc::FocusEditing::GetFocusDistance(CameraActor), SetFocusCm, 0.01f);

	GEditor->SelectNone(false, true, false);
	World->EditorDestroyActor(CameraActor, false);
	GEditor->ResetTransaction(FText::FromString(TEXT("Rosc camera editing test")));
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

		ACineCameraActor* Added = Rosc::CameraEditing::SpawnCameraAtViewport();
		const URoscCameraPackageComponent* AddedPackage = Added != nullptr ? URoscCameraPackageComponent::Find(Added) : nullptr;
		if (TestNotNull(TEXT("Add Camera Here makes a camera with a package"), AddedPackage))
		{
			TestTrue(TEXT("The camera stands where the view was"), Added->GetActorLocation().Equals(FVector(-420.0, 130.0, 175.0), 0.01));
			TestTrue(TEXT("The camera looks where the view looked"), Added->GetActorRotation().Equals(FRotator(-8.0, 25.0, 0.0), 0.01));

			// The nearest lens of the kit to a 40 degree view is the one no other lens of the kit beats.
			const UCineCameraComponent* AddedLens = Added->GetCineCameraComponent();
			const double Wanted = Rosc::Optics::FocalLengthForFieldOfViewMm(AddedLens->Filmback.SensorWidth, 40.0, AddedLens->LensSettings.SqueezeFactor);
			const FRoscLensKit* AddedKit = Database.FindLensKit(AddedPackage->Package.LensKitId);
			double Nearest = TNumericLimits<double>::Max();
			for (const FRoscLens& Lens : AddedKit->Lenses)
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
