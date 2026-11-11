#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "UEcineMonitorTypes.h"

struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

/**
 * Draws the monitor tools that work on the picture itself (false colour, zebras, peaking, waveform and
 * histogram) in one pass after tone mapping, so they measure the picture as it is displayed.
 *
 * On its own it is active nowhere. Whoever creates it adds a function to IsActiveThisFrameFunctions naming
 * the views it may draw in, so renders and scene captures never carry the tools unless someone asks.
 */
class UECINEMONITOR_API FUEcineMonitorViewExtension : public FSceneViewExtensionBase
{
public:
	/** The waveform has this many columns across the picture and both scopes this many signal levels. */
	static constexpr int32 ScopeColumns = 256;
	static constexpr int32 ScopeLevels = 256;

	FUEcineMonitorViewExtension(const FAutoRegister& AutoRegister);

	/** Game thread. Takes effect on the next frame drawn. */
	void SetSettings(const FUEcineMonitorSettings& InSettings);
	const FUEcineMonitorSettings& GetSettings() const { return Settings; }

	// The tools measure the finished picture, so they run after every other extension's work on it.
	virtual int32 GetPriority() const override { return MIN_int32; }
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override { return false; }

private:
	FScreenPassTexture AddPicturePass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	FUEcineMonitorSettings Settings;
	FUEcineMonitorSettings Settings_RenderThread;
};
