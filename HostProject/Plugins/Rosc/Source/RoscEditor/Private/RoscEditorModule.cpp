// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SRoscPanel.h"
#include "RoscDeliverEditing.h"
#include "RoscStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

const FName FRoscEditorModule::PanelTabId(TEXT("RoscPanel"));

FString FRoscEditorModule::GetUserGuideFile()
{
	return FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(TEXT("Rosc"))->GetBaseDir() / TEXT("Docs/UserGuide.html"));
}

void FRoscEditorModule::StartupModule()
{
	FRoscStyle::Register();
	Rosc::DeliverEditing::NoteColourSpaceAtStartup();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PanelTabId, FOnSpawnTab::CreateRaw(this, &FRoscEditorModule::SpawnPanelTab))
		.SetDisplayName(LOCTEXT("PanelTabTitle", "Rosc"))
		.SetTooltipText(LOCTEXT("PanelTabTooltip", "Open the Rosc camera department: camera, grip, focus, monitor, shots, delivery and the optional AI tools."))
		.SetIcon(FSlateIcon(FRoscStyle::Get().GetStyleSetName(), "Rosc.Panel"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCinematicsCategory());
}

void FRoscEditorModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanelTabId);
	}
	FRoscStyle::Unregister();
}

TSharedRef<SDockTab> FRoscEditorModule::SpawnPanelTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SRoscPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRoscEditorModule, RoscEditor);
