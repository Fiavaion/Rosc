// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscMonitorSubsystem.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "SceneViewExtension.h"
#include "RoscCameraPackageComponent.h"
#include "RoscEditorSettings.h"
#include "RoscFocusEditing.h"
#include "RoscMonitorViewExtension.h"

namespace
{
	/** The frame a camera delivers and, when it is wanted, the line of data the monitor shows for it. */
	FRoscMonitorCameraOverlay MakeCameraOverlay(const ACineCameraActor& Camera, bool bDataReadout)
	{
		FRoscMonitorCameraOverlay Overlay;
		const URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(&Camera);
		if (Component != nullptr)
		{
			Overlay.DeliveryAspect = Component->Package.DeliveryAspect;
		}

		if (bDataReadout)
		{
			const UCineCameraComponent* Lens = Camera.GetCineCameraComponent();
			FRoscCameraData Data;
			Data.CameraName = Camera.GetActorLabel();
			Data.FocalLengthMm = Lens->CurrentFocalLength;
			Data.TStop = Lens->CurrentAperture;
			Data.FocusDistanceMm = Rosc::FocusEditing::GetLensDistance(&Camera) * 10.0;
			if (Component != nullptr)
			{
				const FRoscCameraPackage& Package = Component->Package;
				Data.bHasExposure = true;
				Data.Iso = Package.Iso;
				Data.ShutterAngleDeg = Package.ShutterAngleDeg;
				Data.FramesPerSecond = Package.FramesPerSecond;
				Data.NdDensity = Package.NdDensity;
			}
			Overlay.DataReadout = Rosc::Monitor::FormatDataReadout(Data, URoscEditorSettings::GetDistanceUnits()).ToString();
		}
		return Overlay;
	}
}

URoscMonitorSubsystem* URoscMonitorSubsystem::Get()
{
	return GEditor != nullptr ? GEditor->GetEditorSubsystem<URoscMonitorSubsystem>() : nullptr;
}

void URoscMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PictureExtension = FSceneViewExtensions::NewExtension<FRoscMonitorViewExtension>();

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

void URoscMonitorSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	PictureExtension.Reset();
	Super::Deinitialize();
}

const FRoscMonitorSettings& URoscMonitorSubsystem::GetSettings() const
{
	return GetDefault<URoscEditorSettings>()->MonitorSettings;
}

void URoscMonitorSubsystem::SetSettings(const FRoscMonitorSettings& NewSettings)
{
	URoscEditorSettings* EditorSettings = GetMutableDefault<URoscEditorSettings>();
	EditorSettings->MonitorSettings = NewSettings;
	EditorSettings->SaveConfig();

	PictureExtension->SetSettings(NewSettings);
	UpdateCameraOverlays();
	GEditor->RedrawLevelEditingViewports(false);
}

void URoscMonitorSubsystem::UpdateCameraOverlays()
{
	TMap<const FRenderTarget*, FRoscMonitorCameraOverlay> Overlays;
	const FRoscMonitorSettings& Settings = GetSettings();
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
