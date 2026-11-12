#include "UEcineCameraEditing.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineLog.h"
#include "UEcineOptics.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

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

FUEcineCameraPackage UEcine::CameraEditing::MakeDefaultPackage(float FocalLengthMm, float TStop)
{
	const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();

	FUEcineCameraPackage Package;
	Package.FocalLengthMm = FocalLengthMm;
	Package.TStop = TStop;

	const FUEcineCameraBody* Body = Database.FindBody(DefaultBodyId);
	if (Body == nullptr && !Database.GetBodies().IsEmpty())
	{
		Body = &Database.GetBodies()[0];
	}
	if (Body != nullptr)
	{
		Package.SelectBody(*Body);
	}

	const FUEcineLensKit* Kit = Database.FindLensKit(DefaultLensKitId);
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

UUEcineCameraPackageComponent* UEcine::CameraEditing::AddCameraPackage(ACineCameraActor* CameraActor)
{
	if (CameraActor == nullptr)
	{
		return nullptr;
	}
	if (UUEcineCameraPackageComponent* Existing = UUEcineCameraPackageComponent::Find(CameraActor))
	{
		return Existing;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddCameraPackage", "Build Camera Package"));
	CameraActor->Modify();

	UUEcineCameraPackageComponent* Component = NewObject<UUEcineCameraPackageComponent>(CameraActor, TEXT("UEcineCameraPackage"), RF_Transactional);
	CameraActor->AddInstanceComponent(Component);
	Component->RegisterComponent();

	const UCineCameraComponent* Camera = CameraActor->GetCineCameraComponent();
	Component->Package = MakeDefaultPackage(Camera->CurrentFocalLength, Camera->CurrentAperture);

	FText Error;
	if (!Component->ApplyToCamera(Error))
	{
		UE_LOG(LogUEcine, Warning, TEXT("%s: %s"), *CameraActor->GetActorLabel(), *Error.ToString());
	}
	GEditor->RedrawLevelEditingViewports();
	return Component;
}

ACineCameraActor* UEcine::CameraEditing::SpawnCamera(UWorld* World, const FTransform& Transform)
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

ACineCameraActor* UEcine::CameraEditing::SpawnCameraAtViewport()
{
	FTransform Transform = FTransform::Identity;
	if (GCurrentLevelEditingViewportClient != nullptr)
	{
		Transform = FTransform(GCurrentLevelEditingViewportClient->GetViewRotation(), GCurrentLevelEditingViewportClient->GetViewLocation());
	}
	return SpawnCamera(GEditor->GetEditorWorldContext().World(), Transform);
}

bool UEcine::CameraEditing::EditPackage(UUEcineCameraPackageComponent* Component, const FText& Description, TFunctionRef<void(FUEcineCameraPackage&)> Edit, FText& OutError, TArray<FText>* OutWarnings)
{
	if (Component == nullptr)
	{
		OutError = LOCTEXT("NoPackageComponent", "There is no camera package to change.");
		return false;
	}

	FScopedTransaction Transaction(Description);
	Component->Modify();

	const FUEcineCameraPackage PackageBefore = Component->Package;
	Edit(Component->Package);

	if (!Component->ApplyToCamera(OutError, OutWarnings))
	{
		Component->Package = PackageBefore;
		Transaction.Cancel();
		return false;
	}

	GEditor->RedrawLevelEditingViewports();
	return true;
}

bool UEcine::CameraEditing::MatchLensToFieldOfView(const ACineCameraActor* Camera, double HorizontalFieldOfViewDeg, FLensMatch& OutMatch)
{
	const UUEcineCameraPackageComponent* Component = UUEcineCameraPackageComponent::Find(Camera);
	if (Component == nullptr || HorizontalFieldOfViewDeg <= 0.0 || HorizontalFieldOfViewDeg >= 180.0)
	{
		return false;
	}
	const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();
	FUEcineCameraPackage Package = Component->Package;
	FUEcineResolvedCamera Resolved;
	FText Error;
	const FUEcineLensKit* Kit = Database.FindLensKit(Package.LensKitId);
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

bool UEcine::CameraEditing::PutOnLensForFieldOfView(ACineCameraActor* Camera, double HorizontalFieldOfViewDeg, FText& OutError)
{
	FLensMatch Match;
	if (!MatchLensToFieldOfView(Camera, HorizontalFieldOfViewDeg, Match))
	{
		OutError = LOCTEXT("NoLensMatch", "No lens of this camera's kit could be matched to the picture.");
		return false;
	}
	return EditPackage(UUEcineCameraPackageComponent::Find(Camera), LOCTEXT("MatchLens", "Match Lens to Picture"), [&Match](FUEcineCameraPackage& Package)
	{
		Package.FocalLengthMm = static_cast<float>(Match.FocalLengthMm);
		Package.SelectLensKit(*FUEcineCameraDatabase::Get().FindLensKit(Package.LensKitId));
	}, OutError);
}

#undef LOCTEXT_NAMESPACE
