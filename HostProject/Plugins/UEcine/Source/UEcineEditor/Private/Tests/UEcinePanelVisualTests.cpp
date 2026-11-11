#include "CineCameraActor.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SUEcinePanel.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** One picture of the panel: its window size in pixels and the application scale it is drawn at. */
	struct FPanelCapture
	{
		const TCHAR* Name;
		FVector2D WindowSize;
		float ApplicationScale;
	};

	struct FPanelCaptureState
	{
		TWeakObjectPtr<ACineCameraActor> Camera;
		TSharedPtr<SWindow> Window;
		TSharedPtr<SUEcinePanel> Panel;
	};
}

/**
 * Draws the panel in a window and saves pictures of it to Saved/UEcine/Screenshots, at a
 * comfortable width, at the narrowest supported width, and at 150% application scale.
 * Needs a renderer: run with tools/test.ps1 -Rendering. Without one the test reports itself
 * skipped.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcinePanelVisualTest, "UEcine.Editor.Visual.CameraTab", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcinePanelVisualTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: drawing the panel needs a renderer (tools/test.ps1 -Rendering)."));
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(World, FTransform::Identity);
	UUEcineCameraPackageComponent* Component = UUEcineCameraPackageComponent::Find(CameraActor);
	if (!TestNotNull(TEXT("A camera with a package to show"), Component))
	{
		return false;
	}

	// Show the busiest state: a zoom (extra row), physical exposure on, and a lens kit that
	// does not cover the recording format (warning row).
	const FUEcineLensKit* Zoom = FUEcineCameraDatabase::Get().FindLensKit(TEXT("angenieux-optimo-24-290"));
	if (!TestNotNull(TEXT("The zoom kit used for the picture is in the database"), Zoom))
	{
		return false;
	}
	FText Error;
	UEcine::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Visual test setup")), [Zoom](FUEcineCameraPackage& Package)
	{
		Package.SelectLensKit(*Zoom);
		Package.bPhysicalExposure = true;
		Package.NdDensity = 0.6f;
	}, Error);

	const TSharedRef<FPanelCaptureState> State = MakeShared<FPanelCaptureState>();
	State->Camera = CameraActor;

	static const FPanelCapture Captures[] =
	{
		{ TEXT("camera-tab-420"), FVector2D(420.0, 860.0), 1.0f },
		{ TEXT("camera-tab-320"), FVector2D(320.0, 860.0), 1.0f },
		{ TEXT("camera-tab-320-at-150pc"), FVector2D(480.0, 1290.0), 1.5f },
	};
	const float ScaleBefore = FSlateApplication::Get().GetApplicationScale();

	for (const FPanelCapture& Capture : Captures)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State, Capture]
		{
			FSlateApplication::Get().SetApplicationScale(Capture.ApplicationScale);
			State->Window = SNew(SWindow)
				.ClientSize(Capture.WindowSize)
				.AutoCenter(EAutoCenter::None)
				.ScreenPosition(FVector2D(40.0, 40.0))
				.SizingRule(ESizingRule::FixedSize)
				.CreateTitleBar(false)
				.FocusWhenFirstShown(false)
				.ActivationPolicy(EWindowActivationPolicy::Never)
				[
					SAssignNew(State->Panel, SUEcinePanel)
				];
			FSlateApplication::Get().AddWindow(State->Window.ToSharedRef());
			return true;
		}));

		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State, Capture]
		{
			TArray<FColor> Pixels;
			FIntVector Size;
			const bool bCaptured = FSlateApplication::Get().TakeScreenshot(State->Panel.ToSharedRef(), Pixels, Size);
			TestTrue(FString::Printf(TEXT("%s: the panel was drawn"), Capture.Name), bCaptured && Size.X > 0 && Size.Y > 0);

			if (bCaptured && Pixels.Num() == Size.X * Size.Y && Pixels.Num() > 0)
			{
				TSet<uint32> Colours;
				for (FColor& Pixel : Pixels)
				{
					Pixel.A = 255;
					Colours.Add(Pixel.ToPackedARGB());
				}
				TestTrue(FString::Printf(TEXT("%s: the picture is not blank"), Capture.Name), Colours.Num() > 8);

				TArray64<uint8> Png;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
				const FString FilePath = FPaths::ProjectSavedDir() / TEXT("UEcine/Screenshots") / FString(Capture.Name) + TEXT(".png");
				TestTrue(FString::Printf(TEXT("%s: the picture was saved"), Capture.Name), FFileHelper::SaveArrayToFile(Png, *FilePath));
			}

			State->Panel.Reset();
			State->Window->RequestDestroyWindow();
			State->Window.Reset();
		}, 0.5f));
	}

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State, ScaleBefore]
	{
		FSlateApplication::Get().SetApplicationScale(ScaleBefore);
		if (ACineCameraActor* Camera = State->Camera.Get())
		{
			GEditor->SelectNone(false, true, false);
			Camera->GetWorld()->EditorDestroyActor(Camera, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine panel visual test")));
		return true;
	}));

	return true;
}

#endif
