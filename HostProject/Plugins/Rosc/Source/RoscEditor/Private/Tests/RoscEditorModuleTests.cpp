// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Docking/TabManager.h"
#include "ImageUtils.h"
#include "ISettingsCategory.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Tests/AutomationCommon.h"
#include "TextureResource.h"
#include "RoscAISettings.h"
#include "RoscEditorModule.h"
#include "RoscEditorSettings.h"
#include "RoscStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscEditorPanelRegisteredTest, "Rosc.Editor.PanelRegistered", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscEditorPanelRegisteredTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("RoscCore module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("RoscCore")));
	TestTrue(TEXT("RoscEditor module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("RoscEditor")));
	TestTrue(TEXT("The Rosc panel has a registered tab spawner"), FGlobalTabmanager::Get()->HasTabSpawner(FRoscEditorModule::PanelTabId));
	TestTrue(TEXT("The user guide the panel opens is in the plugin"), FPaths::FileExists(FRoscEditorModule::GetUserGuideFile()));

	TestEqual(TEXT("The Plugins window calls it Rosc"), IPluginManager::Get().FindPlugin(TEXT("Rosc"))->GetDescriptor().FriendlyName, FString(TEXT("Rosc")));

	// Window > Cinematics > Rosc: the level editor's Window menu lists each group's tabs by name.
	const TSharedRef<FWorkspaceItem> Cinematics = WorkspaceMenu::GetMenuStructure().GetLevelEditorCinematicsCategory();
	const TSharedRef<FWorkspaceItem>* PanelEntry = Cinematics->GetChildItems().FindByPredicate([](const TSharedRef<FWorkspaceItem>& Item)
	{
		return Item->GetDisplayName().ToString() == TEXT("Rosc");
	});
	TestNotNull(TEXT("The Window menu's Cinematics group has the Rosc panel"), PanelEntry);
	TestEqual(TEXT("The group is called Cinematics"), Cinematics->GetDisplayName().ToString(), FString(TEXT("Cinematics")));

	// The two pages under Edit > Editor Preferences (or Project Settings), by the names they show.
	ISettingsModule& Settings = FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings"));
	const auto FindPage = [&Settings](const UDeveloperSettings* Page) -> TSharedPtr<ISettingsSection>
	{
		const TSharedPtr<ISettingsContainer> Container = Settings.GetContainer(Page->GetContainerName());
		const TSharedPtr<ISettingsCategory> Category = Container.IsValid() ? Container->GetCategory(Page->GetCategoryName()) : nullptr;
		return Category.IsValid() ? Category->GetSection(Page->GetSectionName()) : nullptr;
	};
	const TSharedPtr<ISettingsSection> RoscPage = FindPage(GetDefault<URoscEditorSettings>());
	const TSharedPtr<ISettingsSection> AIPage = FindPage(GetDefault<URoscAISettings>());
	if (TestTrue(TEXT("The Rosc settings page is registered"), RoscPage.IsValid()))
	{
		TestEqual(TEXT("It is called Rosc"), RoscPage->GetDisplayName().ToString(), FString(TEXT("Rosc")));
	}
	if (TestTrue(TEXT("The Rosc AI settings page is registered"), AIPage.IsValid()))
	{
		TestEqual(TEXT("It is called Rosc AI"), AIPage->GetDisplayName().ToString(), FString(TEXT("Rosc AI")));
	}
	return true;
}

/**
 * The plugin's icon, shown in the Plugins window and on Fab, and the panel's icon, shown on its
 * tab. The panel's icon is drawn large and saved as a picture so that its shape can be looked at.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscEditorIconsTest, "Rosc.Editor.Icons", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscEditorIconsTest::RunTest(const FString& Parameters)
{
	const FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("Rosc"))->GetBaseDir();
	FImage PluginIcon;
	if (TestTrue(TEXT("The plugin has an icon"), FImageUtils::LoadImage(*(PluginDir / TEXT("Resources/Icon128.png")), PluginIcon)))
	{
		TestTrue(TEXT("The plugin's icon is 128 pixels square"), PluginIcon.SizeX == 128 && PluginIcon.SizeY == 128);
	}

	const FSlateBrush* PanelIcon = FRoscStyle::Get().GetOptionalBrush("Rosc.Panel", nullptr, nullptr);
	if (!TestNotNull(TEXT("The panel's icon is in the style set"), PanelIcon))
	{
		return false;
	}
	TestTrue(TEXT("The panel's icon file is in the plugin"), FPaths::FileExists(PanelIcon->GetResourceName().ToString()));

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("The picture of the panel's icon is skipped: drawing it needs a renderer (tools/test.ps1 -Rendering)."));
		return true;
	}

	struct FIconPicture
	{
		~FIconPicture()
		{
			BeginCleanup(Renderer);
		}

		FWidgetRenderer* Renderer = new FWidgetRenderer(true);
		TSharedPtr<SWidget> Widget;
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
	};
	// Eight times its size on a tab, so that its edges can be judged.
	const float Scale = 8.0f;
	const FVector2D DrawSize(24.0 * Scale, 24.0 * Scale);
	const TSharedRef<FIconPicture> Picture = MakeShared<FIconPicture>();
	Picture->Widget = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(SImage).Image(PanelIcon) ];
	Picture->Target.Reset(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, false));
	const auto Draw = [Picture, DrawSize, Scale]
	{
		Picture->Renderer->DrawWidget(Picture->Target.Get(), Picture->Widget.ToSharedRef(), Scale, DrawSize, 0.1f);
	};
	Draw();
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Picture, Draw]
	{
		Draw();
		FlushRenderingCommands();

		const UTextureRenderTarget2D* Target = Picture->Target.Get();
		TArray<FColor> Pixels;
		if (TestTrue(TEXT("The icon's picture is read back"), Picture->Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)))
		{
			int32 Lit = 0;
			for (FColor& Pixel : Pixels)
			{
				Lit += Pixel.R > 128 ? 1 : 0;
				Pixel.A = 255;
			}
			// The camera's shape covers about a third of the icon's square, which is 4/9 of the picture.
			const double LitShare = static_cast<double>(Lit) / Pixels.Num();
			AddInfo(FString::Printf(TEXT("The panel's icon lights %.3f of its picture"), LitShare));
			TestTrue(TEXT("The panel's icon draws a shape, neither empty nor a full square"), LitShare > 0.08 && LitShare < 0.35);

			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			TestTrue(TEXT("The icon's picture was saved"), FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("Rosc/Screenshots/panel-icon.png"))));
		}
	}, 0.25f));
	return true;
}

#endif
