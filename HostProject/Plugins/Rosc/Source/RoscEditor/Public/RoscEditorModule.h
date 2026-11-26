// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modules/ModuleInterface.h"
#include "UObject/NameTypes.h"

class FSpawnTabArgs;
class SDockTab;

class FRoscEditorModule : public IModuleInterface
{
public:
	/** Identifier of the dockable Rosc panel, for opening it from code or tests. */
	ROSCEDITOR_API static const FName PanelTabId;

	/** The user guide that ships with the plugin: a page a web browser opens. */
	ROSCEDITOR_API static FString GetUserGuideFile();

	/** The page beside the user guide that lists the data each AI model learnt from, and its owners' terms. */
	ROSCEDITOR_API static FString GetTrainingDataFile();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<SDockTab> SpawnPanelTab(const FSpawnTabArgs& Args);
};
