// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscCameraEditing.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraPackageComponent.h"
#include "RoscEditorSettings.h"
#include "RoscFocusEditing.h"
#include "RoscLog.h"
#include "RoscOptics.h"
#include "RoscUnits.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

namespace
{
	const TCHAR* DefaultBodyId = TEXT("arri-alexa-35");
	const TCHAR* DefaultLensKitId = TEXT("arri-signature-prime");

	/** "A Cam", "B Cam" … the first letter no camera in the world is using yet. */
	FString NextCameraLabel(const UWorld* World)
	{
		TSet<FString> UsedLabels;
		for (TActorIterator<ACineCameraActor> It(World); It; ++It)
		{
			UsedLabels.Add(It->GetActorLabel());
		}
		for (TCHAR Letter = TEXT('A'); Letter <= TEXT('Z'); ++Letter)
		{
			const FString Label = FString::Printf(TEXT("%c Cam"), Letter);
			if (!UsedLabels.Contains(Label))
			{
				return Label;
			}
		}
		return TEXT("Camera");
	}
}

FRoscCameraPackage Rosc::CameraEditing::MakeDefaultPackage(float FocalLengthMm, float TStop)
{
	const FRoscCameraDatabase& Database = FRoscCameraDatabase::Get();

	FRoscCameraPackage Package;
	Package.FocalLengthMm = FocalLengthMm;
	Package.TStop = TStop;

	const FRoscCameraBody* Body = Database.FindBody(DefaultBodyId);
	if (Body == nullptr && !Database.GetBodies().IsEmpty())
	{
		Body = &Database.GetBodies()[0];
	}
	if (Body != nullptr)
	{
		Package.SelectBody(*Body);
	}

	const FRoscLensKit* Kit = Database.FindLensKit(DefaultLensKitId);
	if (Kit == nullptr && !Database.GetLensKits().IsEmpty())
	{
		Kit = &Database.GetLensKits()[0];
	}
	if (Kit != nullptr)
	{
		Package.SelectLensKit(*Kit);
	}
	return Package;
}

URoscCameraPackageComponent* Rosc::CameraEditing::AddCameraPackage(ACineCameraActor* CameraActor)
{
	if (CameraActor == nullptr)
	{
		return nullptr;
	}
	if (URoscCameraPackageComponent* Existing = URoscCameraPackageComponent::Find(CameraActor))
	{
		return Existing;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddCameraPackage", "Build Camera Package"));
	CameraActor->Modify();

	URoscCameraPackageComponent* Component = NewObject<URoscCameraPackageComponent>(CameraActor, TEXT("RoscCameraPackage"), RF_Transactional);
	CameraActor->AddInstanceComponent(Component);
	Component->RegisterComponent();

	const UCineCameraComponent* Camera = CameraActor->GetCineCameraComponent();
	Component->Package = MakeDefaultPackage(Camera->CurrentFocalLength, Camera->CurrentAperture);

	FText Error;
	if (!Component->ApplyToCamera(Error))
	{
		UE_LOG(LogRosc, Warning, TEXT("%s: %s"), *CameraActor->GetActorLabel(), *Error.ToString());
	}
	GEditor->RedrawLevelEditingViewports();
	return Component;
}

ACineCameraActor* Rosc::CameraEditing::SpawnCamera(UWorld* World, const FTransform& Transform)
{
	if (World == nullptr)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("SpawnCamera", "New Camera"));
	const FString Label = NextCameraLabel(World);

	ACineCameraActor* CameraActor = Cast<ACineCameraActor>(GEditor->AddActor(World->GetCurrentLevel(), ACineCameraActor::StaticClass(), Transform));
	if (CameraActor == nullptr)
	{
		return nullptr;
	}
	CameraActor->SetActorLabel(Label);
	AddCameraPackage(CameraActor);
	return CameraActor;
}

ACineCameraActor* Rosc::CameraEditing::SpawnCameraAtViewport()
{
	const FLevelEditorViewportClient* View = GCurrentLevelEditingViewportClient;
	if (View == nullptr || !View->IsPerspective())
	{
		// A plan or elevation view is no place for a camera: take the level's perspective view.
		View = nullptr;
		for (const FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
		{
			if (Candidate != nullptr && Candidate->IsPerspective())
			{
				View = Candidate;
				break;
			}
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("AddCameraHere", "Add Camera Here"));
	ACineCameraActor* Camera = SpawnCamera(GEditor->GetEditorWorldContext().World(),
		View != nullptr ? FTransform(View->GetViewRotation(), View->GetViewLocation()) : FTransform::Identity);
	if (Camera != nullptr && View != nullptr)
	{
		// The lens of the kit that frames nearest to what the view showed. A view wider or
		// longer than any lens of the kit gets the kit's widest or longest.
		FText Error;
		PutOnLensForFieldOfView(Camera, View->ViewFOV, Error);
	}
	return Camera;
}

bool Rosc::CameraEditing::EditPackage(URoscCameraPackageComponent* Component, const FText& Description, TFunctionRef<void(FRoscCameraPackage&)> Edit, FText& OutError, FText* OutNotice)
{
	if (Component == nullptr)
	{
		OutError = LOCTEXT("NoPackageComponent", "There is no camera package to change.");
		return false;
	}

	FScopedTransaction Transaction(Description);
	Component->Modify();

	// A new lens moves a hand-set focus nearer than it can focus out to its close focus (the Cine
	// Camera does it), so where the focus was set is known only before the change.
	const ACineCameraActor* Camera = Cast<ACineCameraActor>(Component->GetOwner());
	const bool bHandSetFocus = Camera != nullptr && Camera->GetCineCameraComponent()->FocusSettings.FocusMethod == ECameraFocusMethod::Manual;
	const float FocusBeforeCm = Rosc::FocusEditing::GetFocusDistance(Camera);

	const FRoscCameraPackage PackageBefore = Component->Package;
	Edit(Component->Package);

	if (!Component->ApplyToCamera(OutError))
	{
		Component->Package = PackageBefore;
		Transaction.Cancel();
		return false;
	}

	const float CloseFocusCm = Rosc::FocusEditing::GetCloseFocus(Camera);
	if (OutNotice != nullptr && bHandSetFocus && FocusBeforeCm < CloseFocusCm - 0.01f)
	{
		const ERoscDistanceUnits Units = URoscEditorSettings::GetDistanceUnits();
		*OutNotice = FText::Format(LOCTEXT("FocusMovedToCloseFocus", "The focus was at {0}, nearer than this lens can focus. It is now at the lens's close focus, {1}. Set the focus again in the Focus tab or move the camera."),
			Rosc::Units::FormatDistance(FocusBeforeCm * 10.0, Units), Rosc::Units::FormatDistance(CloseFocusCm * 10.0, Units));
	}

	GEditor->RedrawLevelEditingViewports();
	return true;
}

bool Rosc::CameraEditing::MatchLensToFieldOfView(const ACineCameraActor* Camera, double HorizontalFieldOfViewDeg, FLensMatch& OutMatch)
{
	const URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(Camera);
	if (Component == nullptr || HorizontalFieldOfViewDeg <= 0.0 || HorizontalFieldOfViewDeg >= 180.0)
	{
		return false;
	}
	const FRoscCameraDatabase& Database = FRoscCameraDatabase::Get();
	FRoscCameraPackage Package = Component->Package;
	FRoscResolvedCamera Resolved;
	FText Error;
	const FRoscLensKit* Kit = Database.FindLensKit(Package.LensKitId);
	if (Kit == nullptr || !ResolveCameraPackage(Database, Package, Resolved, Error))
	{
		return false;
	}

	OutMatch.FocalLengthMm = Optics::FocalLengthForFieldOfViewMm(Resolved.SensorWidthMm, HorizontalFieldOfViewDeg, Resolved.Squeeze);
	Package.FocalLengthMm = static_cast<float>(OutMatch.FocalLengthMm);
	Package.SelectLensKit(*Kit);
	OutMatch.LensName = Kit->Lenses[Package.LensIndex].GetDisplayName();
	OutMatch.LensFocalLengthMm = Package.FocalLengthMm;
	return true;
}

bool Rosc::CameraEditing::PutOnLensForFieldOfView(ACineCameraActor* Camera, double HorizontalFieldOfViewDeg, FText& OutError, FText* OutNotice)
{
	FLensMatch Match;
	if (!MatchLensToFieldOfView(Camera, HorizontalFieldOfViewDeg, Match))
	{
		OutError = LOCTEXT("NoLensMatch", "No lens of this camera's kit could be matched to the picture.");
		return false;
	}
	return EditPackage(URoscCameraPackageComponent::Find(Camera), LOCTEXT("MatchLens", "Match Lens to Picture"), [&Match](FRoscCameraPackage& Package)
	{
		Package.FocalLengthMm = static_cast<float>(Match.FocalLengthMm);
		Package.SelectLensKit(*FRoscCameraDatabase::Get().FindLensKit(Package.LensKitId));
	}, OutError, OutNotice);
}

#undef LOCTEXT_NAMESPACE
