// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "UObject/NameTypes.h"

class FSpawnTabArgs;
class SDockTab;

class FUEcineEditorModule : public IModuleInterface
{
public:
	/** Identifier of the dockable UEcine panel, for opening it from code or tests. */
	UECINEEDITOR_API static const FName PanelTabId;

	/** The user guide that ships with the plugin: a page a web browser opens. */
	UECINEEDITOR_API static FString GetUserGuideFile();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<SDockTab> SpawnPanelTab(const FSpawnTabArgs& Args);
};
