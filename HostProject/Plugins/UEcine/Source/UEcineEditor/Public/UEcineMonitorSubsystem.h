#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEcineMonitorTypes.h"
#include "UEcineMonitorSubsystem.generated.h"

class FUEcineMonitorViewExtension;

/**
 * The on-set monitor: holds which monitor tools are on and draws them in the level viewports.
 * The tools are a way of looking at the picture, so they are a per-user preference, are not part of
 * undo history, and never appear in renders or scene captures.
 */
UCLASS()
class UECINEEDITOR_API UUEcineMonitorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	static UUEcineMonitorSubsystem* Get();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	const FUEcineMonitorSettings& GetSettings() const;

	/** Changes which tools are on. Level viewports follow at once, and the choice is remembered between sessions. */
	void SetSettings(const FUEcineMonitorSettings& NewSettings);

private:
	TSharedPtr<FUEcineMonitorViewExtension, ESPMode::ThreadSafe> PictureExtension;
};
