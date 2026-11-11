#include "UEcineMonitorSubsystem.h"

#include "Editor.h"
#include "LevelEditorViewport.h"
#include "SceneViewExtension.h"
#include "UEcineEditorSettings.h"
#include "UEcineMonitorViewExtension.h"

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
}

void UUEcineMonitorSubsystem::Deinitialize()
{
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
	GEditor->RedrawLevelEditingViewports(false);
}
