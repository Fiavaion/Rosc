// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "RoscMonitorTypes.h"
#include "RoscMonitorSubsystem.generated.h"

class FRoscMonitorViewExtension;

/**
 * The on-set monitor: holds which monitor tools are on and draws them in the level viewports.
 * The tools are a way of looking at the picture, so they are a per-user preference, are not part of
 * undo history, and never appear in renders or scene captures.
 *
 * Frame lines and the data readout belong to a camera, so a viewport shows them for the camera it
 * is looking through, whether the user is piloting it or Sequencer has cut to it.
 */
UCLASS()
class ROSCEDITOR_API URoscMonitorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	static URoscMonitorSubsystem* Get();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	const FRoscMonitorSettings& GetSettings() const;

	/** Changes which tools are on. Level viewports follow at once, and the choice is remembered between sessions. */
	void SetSettings(const FRoscMonitorSettings& NewSettings);

	/**
	 * Looks at which level viewports are looking through a camera, and what the frame lines and the
	 * data readout should show there. Done every frame; call it to have a change in the next picture drawn.
	 */
	void UpdateCameraOverlays();

private:
	TSharedPtr<FRoscMonitorViewExtension, ESPMode::ThreadSafe> PictureExtension;
	FTSTicker::FDelegateHandle TickerHandle;
};
