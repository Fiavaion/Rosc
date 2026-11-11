#include "Components/SceneCaptureComponent2D.h"
#include "Editor.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TextureResource.h"
#include "UEcineMonitorSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** A picture read back from a viewport or a capture. */
	struct FMonitorPicture
	{
		FIntPoint Size = FIntPoint::ZeroValue;
		TArray<FColor> Pixels;

		const FColor& At(int32 X, int32 Y) const
		{
			return Pixels[Y * Size.X + X];
		}

		/**
		 * How much of the upper part of the picture is a false colour band or a pure grey. With false colour on
		 * that is all of it; a plain picture of a blue sky has almost none. The upper part is used because the
		 * scopes sit at the bottom.
		 */
		float GetFalseColourShare() const
		{
			const TConstArrayView<FUEcineFalseColourBand> Bands = UEcine::Monitor::GetFalseColourBands();
			int32 Matching = 0;
			int32 Total = 0;
			for (int32 Y = Size.Y / 20; Y < Size.Y * 35 / 100; Y += 4)
			{
				for (int32 X = Size.X / 10; X < Size.X * 9 / 10; X += 4)
				{
					const FColor& Pixel = At(X, Y);
					const bool bGrey = FMath::Abs(Pixel.R - Pixel.G) <= 6 && FMath::Abs(Pixel.G - Pixel.B) <= 6;
					const bool bBand = Bands.ContainsByPredicate([&Pixel](const FUEcineFalseColourBand& Band)
					{
						return FMath::Abs(Pixel.R - Band.Colour.R) <= 6 && FMath::Abs(Pixel.G - Band.Colour.G) <= 6 && FMath::Abs(Pixel.B - Band.Colour.B) <= 6;
					});
					Matching += (bGrey || bBand) ? 1 : 0;
					++Total;
				}
			}
			return Total > 0 ? static_cast<float>(Matching) / Total : 0.0f;
		}

		/** Mean signal, in percent, of the middle of where the histogram is drawn: dark when the scope's panel is there. */
		float GetHistogramAreaSignal() const
		{
			const FIntRect Area = UEcine::Monitor::GetScopeLayout(Size, true, true).Histogram;
			const FIntRect Middle(Area.Min + Area.Size() / 4, Area.Max - Area.Size() / 4);
			double Sum = 0.0;
			int32 Total = 0;
			for (int32 Y = Middle.Min.Y; Y < Middle.Max.Y; ++Y)
			{
				for (int32 X = Middle.Min.X; X < Middle.Max.X; ++X)
				{
					Sum += UEcine::Monitor::GetSignalPercent(At(X, Y));
					++Total;
				}
			}
			return Total > 0 ? static_cast<float>(Sum / Total) : 0.0f;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMonitorViewportTest, "UEcine.Editor.Monitor.Viewport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineMonitorViewportTest::RunTest(const FString& Parameters)
{
	UUEcineMonitorSubsystem* Monitor = UUEcineMonitorSubsystem::Get();
	if (!TestNotNull(TEXT("The monitor exists in the editor"), Monitor))
	{
		return false;
	}

	// Whatever happens below, leave the user's monitor as it was found.
	const FUEcineMonitorSettings SettingsBefore = Monitor->GetSettings();
	ON_SCOPE_EXIT
	{
		Monitor->SetSettings(SettingsBefore);
	};

	FUEcineMonitorSettings AllTools;
	AllTools.bFalseColour = true;
	AllTools.bWaveform = true;
	AllTools.bHistogram = true;

	// The choice of tools is kept.
	Monitor->SetSettings(AllTools);
	TestTrue(TEXT("The monitor remembers that false colour is on"), Monitor->GetSettings().bFalseColour);
	TestTrue(TEXT("The monitor remembers that the waveform is on"), Monitor->GetSettings().bWaveform);
	Monitor->SetSettings(FUEcineMonitorSettings());
	TestFalse(TEXT("The monitor remembers that false colour is off again"), Monitor->GetSettings().bFalseColour);

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Picture checks skipped: they need a renderer (run tools/test.ps1 with -Rendering)."));
		return true;
	}

	FLevelEditorViewportClient* ViewportClient = nullptr;
	for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
	{
		if (Candidate != nullptr && Candidate->Viewport != nullptr && Candidate->IsPerspective() && Candidate->Viewport->GetSizeXY().GetMin() >= 200)
		{
			ViewportClient = Candidate;
			break;
		}
	}
	if (!TestNotNull(TEXT("There is a perspective level viewport to draw in"), ViewportClient))
	{
		return false;
	}

	// Game view hides the grid and gizmos, which the editor draws over the picture after the monitor tools.
	const bool bWasGameView = ViewportClient->IsInGameView();
	ViewportClient->SetGameView(true);
	ON_SCOPE_EXIT
	{
		ViewportClient->SetGameView(bWasGameView);
	};

	const auto ShootViewport = [Monitor, ViewportClient](const FUEcineMonitorSettings& Settings)
	{
		Monitor->SetSettings(Settings);
		FMonitorPicture Picture;
		ViewportClient->Viewport->Draw(false);
		Picture.Size = ViewportClient->Viewport->GetSizeXY();
		ViewportClient->Viewport->ReadPixels(Picture.Pixels);
		return Picture;
	};

	// The level viewport: plain, then with the tools on.
	const FMonitorPicture PlainViewport = ShootViewport(FUEcineMonitorSettings());
	const FMonitorPicture ToolsViewport = ShootViewport(AllTools);
	if (!TestEqual(TEXT("The viewport picture was read back whole"), ToolsViewport.Pixels.Num(), ToolsViewport.Size.X * ToolsViewport.Size.Y)
		|| !TestEqual(TEXT("The plain viewport picture was read back whole"), PlainViewport.Pixels.Num(), PlainViewport.Size.X * PlainViewport.Size.Y))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Level viewport %d x %d: false colour share %.2f plain, %.2f with tools; histogram area signal %.1f %% plain, %.1f %% with tools"),
		ToolsViewport.Size.X, ToolsViewport.Size.Y, PlainViewport.GetFalseColourShare(), ToolsViewport.GetFalseColourShare(), PlainViewport.GetHistogramAreaSignal(), ToolsViewport.GetHistogramAreaSignal()));
	TestTrue(TEXT("Plain viewport: the sky is in colour"), PlainViewport.GetFalseColourShare() < 0.5f);
	TestTrue(TEXT("Viewport with false colour: the picture is all bands and greys"), ToolsViewport.GetFalseColourShare() > 0.95f);
	TestTrue(TEXT("Viewport with scopes: the histogram's dark panel covers the picture"), ToolsViewport.GetHistogramAreaSignal() < 0.5f * PlainViewport.GetHistogramAreaSignal());

	// A scene capture from the same place while the tools are on: the same picture as with the tools off.
	ASceneCapture2D* CaptureActor = ViewportClient->GetWorld()->SpawnActor<ASceneCapture2D>(ViewportClient->GetViewLocation(), ViewportClient->GetViewRotation());
	ON_SCOPE_EXIT
	{
		CaptureActor->Destroy();
	};
	USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->TargetGamma = 2.2f;
	Target->InitAutoFormat(960, 540);
	Target->UpdateResourceImmediate(true);
	Capture->TextureTarget = Target;
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	// A fixed exposure, so that two captures of the same scene are the same picture.
	Capture->PostProcessBlendWeight = 1.0f;
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = AEM_Manual;

	const auto ShootCapture = [Monitor, Capture, Target](const FUEcineMonitorSettings& Settings)
	{
		Monitor->SetSettings(Settings);
		FMonitorPicture Picture;
		Capture->CaptureScene();
		Picture.Size = FIntPoint(Target->SizeX, Target->SizeY);
		Target->GameThread_GetRenderTargetResource()->ReadPixels(Picture.Pixels);
		return Picture;
	};
	const FMonitorPicture PlainCapture = ShootCapture(FUEcineMonitorSettings());
	const FMonitorPicture ToolsCapture = ShootCapture(AllTools);
	AddInfo(FString::Printf(TEXT("Scene capture: false colour share %.2f plain, %.2f with tools on; histogram area signal %.1f %% plain, %.1f %% with tools on"),
		PlainCapture.GetFalseColourShare(), ToolsCapture.GetFalseColourShare(), PlainCapture.GetHistogramAreaSignal(), ToolsCapture.GetHistogramAreaSignal()));
	TestTrue(TEXT("Scene capture: the sky is in colour"), PlainCapture.GetFalseColourShare() < 0.5f);
	TestEqual(TEXT("Scene capture with the tools on: no false colour"), ToolsCapture.GetFalseColourShare(), PlainCapture.GetFalseColourShare(), 0.05f);
	TestEqual(TEXT("Scene capture with the tools on: no histogram panel"), ToolsCapture.GetHistogramAreaSignal(), PlainCapture.GetHistogramAreaSignal(), 0.1f * PlainCapture.GetHistogramAreaSignal());

	return true;
}

#endif
