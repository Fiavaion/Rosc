#include "UEcineMonitorSubsystem.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "SceneViewExtension.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineEditorSettings.h"
#include "UEcineFocusEditing.h"
#include "UEcineMonitorViewExtension.h"

namespace
{
	/** The frame a camera delivers and, when it is wanted, the line of data the monitor shows for it. */
	FUEcineMonitorCameraOverlay MakeCameraOverlay(const ACineCameraActor& Camera, bool bDataReadout)
	{
		FUEcineMonitorCameraOverlay Overlay;
		const UUEcineCameraPackageComponent* Component = UUEcineCameraPackageComponent::Find(&Camera);
		if (Component != nullptr)
		{
			Overlay.DeliveryAspect = Component->Package.DeliveryAspect;
		}

		if (bDataReadout)
		{
			const UCineCameraComponent* Lens = Camera.GetCineCameraComponent();
			FUEcineCameraData Data;
			Data.CameraName = Camera.GetActorLabel();
			Data.FocalLengthMm = Lens->CurrentFocalLength;
			Data.TStop = Lens->CurrentAperture;
			Data.FocusDistanceMm = UEcine::FocusEditing::GetLensDistance(&Camera) * 10.0;
			if (Component != nullptr)
			{
				const FUEcineCameraPackage& Package = Component->Package;
				Data.bHasExposure = true;
				Data.Iso = Package.Iso;
				Data.ShutterAngleDeg = Package.ShutterAngleDeg;
				Data.FramesPerSecond = Package.FramesPerSecond;
				Data.NdDensity = Package.NdDensity;
			}
			Overlay.DataReadout = UEcine::Monitor::FormatDataReadout(Data, UUEcineEditorSettings::GetDistanceUnits()).ToString();
		}
		return Overlay;
	}
}

UUEcineMonitorSubsystem* UUEcineMonitorSubsystem::Get()
{
	return GEditor != nullptr ? GEditor->GetEditorSubsystem<UUEcineMonitorSubsystem>() : nullptr;
}

void UUEcineMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PictureExtension = FSceneViewExtensions::NewExtension<FUEcineMonitorViewExtension>();

	// The tools are drawn in the level viewports' perspective views and nowhere else:
	// not in renders, scene captures, asset editors or thumbnails.
	FSceneViewExtensionIsActiveFunctor LevelViewportsOnly;
	LevelViewportsOnly.IsActiveFunction = [](const ISceneViewExtension*, const FSceneViewExtensionContext& Context)
	{
		if (Context.Viewport != nullptr && GEditor != nullptr)
		{
			for (const FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
			{
				if (ViewportClient != nullptr && ViewportClient->Viewport == Context.Viewport)
				{
					return TOptional<bool>(ViewportClient->IsPerspective());
				}
			}
		}
		return TOptional<bool>(false);
	};
	PictureExtension->IsActiveThisFrameFunctions.Add(LevelViewportsOnly);
	PictureExtension->SetSettings(GetSettings());

	// Cameras change under the monitor all the time: a lens swap, a focus pull, Sequencer cutting to another camera.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		UpdateCameraOverlays();
		return true;
	}));
}

void UUEcineMonitorSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	PictureExtension.Reset();
	Super::Deinitialize();
}

const FUEcineMonitorSettings& UUEcineMonitorSubsystem::GetSettings() const
{
	return GetDefault<UUEcineEditorSettings>()->MonitorSettings;
}

void UUEcineMonitorSubsystem::SetSettings(const FUEcineMonitorSettings& NewSettings)
{
	UUEcineEditorSettings* EditorSettings = GetMutableDefault<UUEcineEditorSettings>();
	EditorSettings->MonitorSettings = NewSettings;
	EditorSettings->SaveConfig();

	PictureExtension->SetSettings(NewSettings);
	UpdateCameraOverlays();
	GEditor->RedrawLevelEditingViewports(false);
}

void UUEcineMonitorSubsystem::UpdateCameraOverlays()
{
	TMap<const FRenderTarget*, FUEcineMonitorCameraOverlay> Overlays;
	const FUEcineMonitorSettings& Settings = GetSettings();
	if (Settings.IsAnyCameraToolOn())
	{
		for (const FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
		{
			if (ViewportClient == nullptr || ViewportClient->Viewport == nullptr || !ViewportClient->IsPerspective())
			{
				continue;
			}
			const UCameraComponent* ViewCamera = ViewportClient->GetCameraComponentForView();
			if (const ACineCameraActor* Camera = ViewCamera != nullptr ? Cast<ACineCameraActor>(ViewCamera->GetOwner()) : nullptr)
			{
				Overlays.Add(ViewportClient->Viewport, MakeCameraOverlay(*Camera, Settings.bDataReadout));
			}
		}
	}

	if (PictureExtension->SetCameraOverlays(Overlays))
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}
