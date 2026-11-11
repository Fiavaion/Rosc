#include "UEcineCameraPackageComponent.h"

#include "CineCameraComponent.h"
#include "GameFramework/Actor.h"
#include "UEcineCameraDatabase.h"
#include "UEcineLog.h"

#define LOCTEXT_NAMESPACE "UEcineRuntime"

bool UUEcineCameraPackageComponent::ApplyToCamera(FText& OutError, TArray<FText>* OutWarnings)
{
	UCineCameraComponent* Camera = GetCineCamera();
	if (Camera == nullptr)
	{
		OutError = LOCTEXT("NoCineCamera", "The camera package is not on a Cine Camera Actor.");
		return false;
	}

	FUEcineResolvedCamera Resolved;
	if (!UEcine::ResolveCameraPackage(FUEcineCameraDatabase::Get(), Package, Resolved, OutError))
	{
		return false;
	}

	Camera->Modify();

	FCameraFilmbackSettings Filmback = Camera->Filmback;
	Filmback.SensorWidth = static_cast<float>(Resolved.SensorWidthMm);
	Filmback.SensorHeight = static_cast<float>(Resolved.SensorHeightMm);
	Camera->SetFilmback(Filmback);

	FCameraLensSettings Lens = Camera->LensSettings;
	Lens.MinFocalLength = static_cast<float>(Resolved.MinFocalLengthMm);
	Lens.MaxFocalLength = static_cast<float>(Resolved.MaxFocalLengthMm);
	Lens.MinFStop = static_cast<float>(Resolved.WidestTStop);
	Lens.MaxFStop = static_cast<float>(Resolved.MinApertureTStop);
	Lens.MinimumFocusDistance = static_cast<float>(Resolved.CloseFocusMm);
	Lens.SqueezeFactor = static_cast<float>(Resolved.Squeeze);
	if (Resolved.IrisBlades > 0)
	{
		Lens.DiaphragmBladeCount = Resolved.IrisBlades;
	}
	Camera->SetLensSettings(Lens);

	Camera->SetCurrentFocalLength(static_cast<float>(Resolved.FocalLengthMm));
	Camera->SetCurrentAperture(static_cast<float>(Resolved.TStop));

	FPostProcessSettings& PostProcess = Camera->PostProcessSettings;
	PostProcess.bOverride_MotionBlurAmount = true;
	PostProcess.MotionBlurAmount = static_cast<float>(Resolved.MotionBlurAmount);

	PostProcess.bOverride_AutoExposureMethod = Resolved.bPhysicalExposure;
	PostProcess.bOverride_AutoExposureApplyPhysicalCameraExposure = Resolved.bPhysicalExposure;
	PostProcess.bOverride_CameraISO = Resolved.bPhysicalExposure;
	PostProcess.bOverride_CameraShutterSpeed = Resolved.bPhysicalExposure;
	PostProcess.bOverride_AutoExposureBias = Resolved.bPhysicalExposure;
	if (Resolved.bPhysicalExposure)
	{
		PostProcess.AutoExposureMethod = AEM_Manual;
		PostProcess.AutoExposureApplyPhysicalCameraExposure = true;
		PostProcess.CameraISO = static_cast<float>(Resolved.Iso);
		PostProcess.CameraShutterSpeed = static_cast<float>(Resolved.ShutterSpeedReciprocal);
		PostProcess.AutoExposureBias = static_cast<float>(Resolved.ExposureBiasStops);
	}

	Camera->MarkRenderStateDirty();

	if (OutWarnings != nullptr)
	{
		*OutWarnings = MoveTemp(Resolved.Warnings);
	}
	return true;
}

UCineCameraComponent* UUEcineCameraPackageComponent::GetCineCamera() const
{
	const AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->FindComponentByClass<UCineCameraComponent>() : nullptr;
}

UUEcineCameraPackageComponent* UUEcineCameraPackageComponent::Find(const AActor* Actor)
{
	return Actor != nullptr ? Actor->FindComponentByClass<UUEcineCameraPackageComponent>() : nullptr;
}

#if WITH_EDITOR
void UUEcineCameraPackageComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FText Error;
	if (!ApplyToCamera(Error))
	{
		UE_LOG(LogUEcine, Warning, TEXT("%s: %s"), *GetPathName(), *Error.ToString());
	}
}
#endif

#undef LOCTEXT_NAMESPACE
