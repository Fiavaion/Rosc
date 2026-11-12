// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SUEcinePanel.h"
#include "UEcineDeliverEditing.h"
#include "UEcineStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

const FName FUEcineEditorModule::PanelTabId(TEXT("UEcinePanel"));

FString FUEcineEditorModule::GetUserGuideFile()
{
	return FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(TEXT("UEcine"))->GetBaseDir() / TEXT("Docs/UserGuide.html"));
}

void FUEcineEditorModule::StartupModule()
{
	FUEcineStyle::Register();
	UEcine::DeliverEditing::NoteColourSpaceAtStartup();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PanelTabId, FOnSpawnTab::CreateRaw(this, &FUEcineEditorModule::SpawnPanelTab))
		.SetDisplayName(LOCTEXT("PanelTabTitle", "UEcine"))
		.SetTooltipText(LOCTEXT("PanelTabTooltip", "Open the UEcine camera department: camera, grip, focus, monitor, shots, delivery and the optional AI tools."))
		.SetIcon(FSlateIcon(FUEcineStyle::Get().GetStyleSetName(), "UEcine.Panel"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCinematicsCategory());
}

void FUEcineEditorModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanelTabId);
	}
	FUEcineStyle::Unregister();
}

TSharedRef<SDockTab> FUEcineEditorModule::SpawnPanelTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SUEcinePanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEcineEditorModule, UEcineEditor);
