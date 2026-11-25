// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "RoscMonitorTypes.h"

class FCanvas;
class FRenderTarget;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

/** What the monitor draws for the camera a view is looking through, beyond the tools shared by every view. */
struct FRoscMonitorCameraOverlay
{
	/** Width over height of the frame the shot is delivered in; zero when that is the whole picture. */
	float DeliveryAspect = 0.0f;
	/** One line of camera data, drawn when the data readout is on. */
	FString DataReadout;

	bool operator==(const FRoscMonitorCameraOverlay& Other) const = default;
};

/**
 * Draws the monitor tools in one pass after tone mapping, so they measure the picture as it is displayed:
 * false colour, zebras, peaking, waveform and histogram on the picture itself, then frame lines, safe
 * areas, centre mark, data readout and labels over it.
 *
 * On its own it is active nowhere. Whoever creates it adds a function to IsActiveThisFrameFunctions naming
 * the views it may draw in, so renders and scene captures never carry the tools unless someone asks.
 */
class ROSCMONITOR_API FRoscMonitorViewExtension : public FSceneViewExtensionBase
{
public:
	/** The waveform has this many columns across the picture and both scopes this many signal levels. */
	static constexpr int32 ScopeColumns = 256;
	static constexpr int32 ScopeLevels = 256;

	FRoscMonitorViewExtension(const FAutoRegister& AutoRegister);

	/** Game thread. Takes effect on the next frame drawn. */
	void SetSettings(const FRoscMonitorSettings& InSettings);
	const FRoscMonitorSettings& GetSettings() const { return Settings; }

	/**
	 * Game thread. Says which views are looking through a camera, keyed by what the view renders to: a
	 * viewport, or a render target's resource. A view that is not named shows no frame lines and no data readout.
	 * @return true when this changes what is drawn.
	 */
	bool SetCameraOverlays(const TMap<const FRenderTarget*, FRoscMonitorCameraOverlay>& InOverlays);

	// The tools measure the finished picture, so they run after every other extension's work on it.
	virtual int32 GetPriority() const override { return MIN_int32; }
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override { return false; }

private:
	/** The words drawn over the picture that depend only on the settings, in the language of the editor. */
	struct FLabels
	{
		FString WaveformTitle;
		FString HistogramTitle;
		/** One entry per false colour band, darkest first. */
		TArray<FString> FalseColourLegend;
	};

	FScreenPassTexture AddPicturePass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);
	void DrawOverlay_RenderThread(FCanvas& Canvas, FIntPoint PictureSize, const FRoscScopeLayout& ScopeLayout, const FRoscMonitorCameraOverlay* Camera) const;

	FRoscMonitorSettings Settings;
	TMap<const FRenderTarget*, FRoscMonitorCameraOverlay> CameraOverlays;

	FRoscMonitorSettings Settings_RenderThread;
	FLabels Labels_RenderThread;
	TMap<const FRenderTarget*, FRoscMonitorCameraOverlay> CameraOverlays_RenderThread;
};
