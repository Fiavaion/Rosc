#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Docking/TabManager.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Tests/AutomationCommon.h"
#include "TextureResource.h"
#include "UEcineEditorModule.h"
#include "UEcineStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineEditorPanelRegisteredTest, "UEcine.Editor.PanelRegistered", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineEditorPanelRegisteredTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("UEcineCore module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("UEcineCore")));
	TestTrue(TEXT("UEcineEditor module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("UEcineEditor")));
	TestTrue(TEXT("The UEcine panel has a registered tab spawner"), FGlobalTabmanager::Get()->HasTabSpawner(FUEcineEditorModule::PanelTabId));
	return true;
}

/**
 * The plugin's icon, shown in the Plugins window and on Fab, and the panel's icon, shown on its
 * tab. The panel's icon is drawn large and saved as a picture so that its shape can be looked at.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineEditorIconsTest, "UEcine.Editor.Icons", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineEditorIconsTest::RunTest(const FString& Parameters)
{
	const FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("UEcine"))->GetBaseDir();
	FImage PluginIcon;
	if (TestTrue(TEXT("The plugin has an icon"), FImageUtils::LoadImage(*(PluginDir / TEXT("Resources/Icon128.png")), PluginIcon)))
	{
		TestTrue(TEXT("The plugin's icon is 128 pixels square"), PluginIcon.SizeX == 128 && PluginIcon.SizeY == 128);
	}

	const FSlateBrush* PanelIcon = FUEcineStyle::Get().GetOptionalBrush("UEcine.Panel", nullptr, nullptr);
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
			TestTrue(TEXT("The icon's picture was saved"), FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("UEcine/Screenshots/panel-icon.png"))));
		}
	}, 0.25f));
	return true;
}

#endif
