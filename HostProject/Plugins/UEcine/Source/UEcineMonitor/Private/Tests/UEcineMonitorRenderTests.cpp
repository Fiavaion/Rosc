#include "AssetCompilingManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"
#include "TextureResource.h"
#include "UEcineMonitorViewExtension.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const FIntPoint PictureSize(1920, 1080);

	// Where things appear in the picture. The camera sees 300 cm either side of centre at the cards, so 3.2 pixels per cm.
	const FIntPoint BlackCardCentre(320, 540);
	const FIntPoint GreyCardCentre(960, 540);
	const FIntPoint BrightCardCentre(1600, 540);
	const FIntPoint BackdropSample(960, 300);
	const int32 GreyCardLeftEdge = 800;

	// A row of small cards along the top of the picture, each this many stops brighter than 18 % grey.
	const float RampStops[] = { -0.25f, 0.25f, 0.75f, 1.0f, 1.25f, 2.0f, 4.0f };
	FIntPoint GetRampCardCentre(int32 Index)
	{
		return FIntPoint(96 + 288 * Index, 156);
	}

	/**
	 * A private scene shot at 1920 x 1080: three glowing cards (black, 18 % grey, far over white) and a row of
	 * small ones in front of a backdrop, with nothing else that could change the picture (no lights, bloom,
	 * vignette or auto exposure). The monitor extension made here is active for this scene only.
	 */
	struct FMonitorTestStage
	{
		FPreviewScene Scene;
		USceneCaptureComponent2D* Capture = nullptr;
		UTextureRenderTarget2D* Target = nullptr;
		TSharedPtr<FUEcineMonitorViewExtension, ESPMode::ThreadSafe> Extension;

		explicit FMonitorTestStage(bool bAntiAliasing)
			: Scene(FPreviewScene::ConstructionValues().SetCreateDefaultLighting(false).SetCreatePhysicsScene(false))
		{
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			const auto AddCard = [this, Cube](const FVector& Location, const FVector& Scale, float Brightness)
			{
				// A surface that glows at exactly this brightness whatever the lighting.
				UMaterial* Material = NewObject<UMaterial>(GetTransientPackage());
				// Its own identity, or the engine may hand every card the shaders and constants of the first one compiled.
				Material->StateId = FGuid::NewGuid();
				Material->SetShadingModel(MSM_Unlit);
				UMaterialExpressionConstant3Vector* Glow = NewObject<UMaterialExpressionConstant3Vector>(Material);
				Glow->Constant = FLinearColor(Brightness, Brightness, Brightness);
				Material->GetExpressionCollection().AddExpression(Glow);
				Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Glow);
				Material->PostEditChange();

				UStaticMeshComponent* Card = NewObject<UStaticMeshComponent>(GetTransientPackage());
				Card->SetStaticMesh(Cube);
				Card->SetMaterial(0, Material);
				Scene.AddComponent(Card, FTransform(FRotator::ZeroRotator, Location, Scale));
			};
			// The cubes are 100 cm; every card's front face is 300 cm from the camera.
			AddCard(FVector(50.0, -200.0, 0.0), FVector::OneVector, 0.0f);
			AddCard(FVector(50.0, 0.0, 0.0), FVector::OneVector, 0.18f);
			AddCard(FVector(50.0, 200.0, 0.0), FVector::OneVector, 8.0f);
			AddCard(FVector(600.0, 0.0, 0.0), FVector(1.0, 40.0, 40.0), 0.08f);
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(RampStops); ++Index)
			{
				AddCard(FVector(15.0, -270.0 + 90.0 * Index, 120.0), FVector(0.3, 0.3, 0.3), 0.18f * FMath::Pow(2.0f, RampStops[Index]));
			}

			Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
			// Eight bits that hold the picture as displayed, like a viewport: the tone mapper encodes it and nothing re-encodes it.
			Target->RenderTargetFormat = RTF_RGBA8;
			Target->TargetGamma = 2.2f;
			Target->InitAutoFormat(PictureSize.X, PictureSize.Y);
			Target->UpdateResourceImmediate(true);

			Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
			Capture->TextureTarget = Target;
			Capture->CaptureSource = SCS_FinalColorLDR;
			Capture->FOVAngle = 90.0f;
			Capture->bCaptureEveryFrame = false;
			Capture->bCaptureOnMovement = false;
			// A capture smooths edges with FXAA unless told otherwise.
			Capture->SetShowFlagSettings({ FEngineShowFlagsSetting{ TEXT("AntiAliasing"), bAntiAliasing } });
			Capture->PostProcessBlendWeight = 1.0f;
			FPostProcessSettings& PostProcess = Capture->PostProcessSettings;
			PostProcess.bOverride_AutoExposureMethod = true;
			PostProcess.AutoExposureMethod = AEM_Manual;
			PostProcess.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			PostProcess.AutoExposureApplyPhysicalCameraExposure = false;
			PostProcess.bOverride_AutoExposureBias = true;
			PostProcess.AutoExposureBias = 0.0f;
			PostProcess.bOverride_BloomIntensity = true;
			PostProcess.BloomIntensity = 0.0f;
			PostProcess.bOverride_VignetteIntensity = true;
			PostProcess.VignetteIntensity = 0.0f;
			Scene.AddComponent(Capture, FTransform(FVector(-300.0, 0.0, 0.0)));

			Extension = FSceneViewExtensions::NewExtension<FUEcineMonitorViewExtension>();
			FSceneViewExtensionIsActiveFunctor OnlyThisScene;
			OnlyThisScene.IsActiveFunction = [StageScene = Scene.GetScene()](const ISceneViewExtension*, const FSceneViewExtensionContext& Context)
			{
				return TOptional<bool>(Context.Scene == StageScene);
			};
			Extension->IsActiveThisFrameFunctions.Add(OnlyThisScene);

			// Meshes are built in the background and shaders compiled when first needed. Wait for both,
			// or the first pictures would show no cards, or cards in the default material.
			FAssetCompilingManager::Get().FinishAllCompilation();
			UMaterialInterface::SubmitRemainingJobsForWorld(Scene.GetWorld());
			FAssetCompilingManager::Get().FinishAllCompilation();
		}

		TArray<FColor> Shoot(const FUEcineMonitorSettings& Settings) const
		{
			Extension->SetSettings(Settings);
			Capture->CaptureScene();
			TArray<FColor> Pixels;
			Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
			return Pixels;
		}

		/** Draws one frame and waits for the graphics card to finish it. Returns the time taken in milliseconds. */
		double TimeFrame(const FUEcineMonitorSettings& Settings) const
		{
			Extension->SetSettings(Settings);
			const double Start = FPlatformTime::Seconds();
			Capture->CaptureScene();
			ENQUEUE_RENDER_COMMAND(UEcineMonitorTestWaitForGpu)([](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			});
			FlushRenderingCommands();
			return (FPlatformTime::Seconds() - Start) * 1000.0;
		}
	};

	const FColor& PixelAt(const TArray<FColor>& Pixels, FIntPoint Position)
	{
		return Pixels[Position.Y * PictureSize.X + Position.X];
	}

	// The tone mapper dithers the last bit, so a flat surface reads a level either side of its own.
	bool IsNear(const FColor& A, const FColor& B, int32 Tolerance = 2)
	{
		return FMath::Abs(A.R - B.R) <= Tolerance && FMath::Abs(A.G - B.G) <= Tolerance && FMath::Abs(A.B - B.B) <= Tolerance;
	}

	FString Describe(const FColor& Colour)
	{
		return FString::Printf(TEXT("(%d, %d, %d)"), Colour.R, Colour.G, Colour.B);
	}

	void SavePicture(const TArray<FColor>& Pixels, const TCHAR* Name)
	{
		TArray<FColor> Opaque = Pixels;
		for (FColor& Pixel : Opaque)
		{
			Pixel.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(PictureSize.X, PictureSize.Y, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
		FFileHelper::SaveArrayToFile(Png, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UEcine"), TEXT("Screenshots"), Name));
	}

	double Median(TArray<double> Values)
	{
		Values.Sort();
		return Values[Values.Num() / 2];
	}

	/** The first pixel, counted from the low end of a scope this many pixels long, that shows the given signal level. */
	int32 GetScopePixelForLevel(int32 Level, int32 ScopeLength)
	{
		return FMath::CeilToInt32((Level - 0.001f) * ScopeLength / FUEcineMonitorViewExtension::ScopeLevels);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMonitorPictureTest, "UEcine.Monitor.Picture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineMonitorPictureTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: needs a renderer (run tools/test.ps1 with -Rendering)."));
		return true;
	}

	using namespace UEcine::Monitor;
	const FMonitorTestStage Stage(false);
	const TConstArrayView<FUEcineFalseColourBand> Bands = GetFalseColourBands();
	const FUEcineFalseColourBand* Crushed = &Bands[0];
	const FUEcineFalseColourBand* MiddleGrey = &Bands[2];
	const FUEcineFalseColourBand* OneStopOver = &Bands[3];
	const FUEcineFalseColourBand* NearClipping = &Bands[4];
	const FUEcineFalseColourBand* Clipped = &Bands[5];

	// The plain picture, and where the engine's tone curve puts each card on the signal scale.
	const TArray<FColor> Plain = Stage.Shoot(FUEcineMonitorSettings());
	TestEqual(TEXT("The picture is 1920 x 1080"), Plain.Num(), PictureSize.X * PictureSize.Y);
	const FColor PlainBlack = PixelAt(Plain, BlackCardCentre);
	const FColor PlainGrey = PixelAt(Plain, GreyCardCentre);
	const FColor PlainBright = PixelAt(Plain, BrightCardCentre);
	const FColor PlainBackdrop = PixelAt(Plain, BackdropSample);
	FString Ramp;
	const FUEcineFalseColourBand* ExpectedRampBands[] = { MiddleGrey, MiddleGrey, OneStopOver, OneStopOver, OneStopOver, nullptr, NearClipping };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RampStops); ++Index)
	{
		const float Signal = GetSignalPercent(PixelAt(Plain, GetRampCardCentre(Index)));
		Ramp += FString::Printf(TEXT(" %+.2f stops = %.1f %%;"), RampStops[Index], Signal);
		TestTrue(*FString::Printf(TEXT("A card %+.2f stops from grey, at %.1f %% signal, sits in the band meant for it"), RampStops[Index], Signal),
			FindFalseColourBand(Signal) == ExpectedRampBands[Index]);
	}
	AddInfo(FString::Printf(TEXT("Signal through the engine's tone curve: black card %.1f %%; 18 %% grey card %.1f %%; card 5.5 stops over %.1f %%; backdrop %.1f %%;%s"),
		GetSignalPercent(PlainBlack), GetSignalPercent(PlainGrey), GetSignalPercent(PlainBright), GetSignalPercent(PlainBackdrop), *Ramp));
	TestTrue(TEXT("The 18 % grey card sits in the middle grey band"), FindFalseColourBand(GetSignalPercent(PlainGrey)) == MiddleGrey);
	TestTrue(TEXT("The black card sits in the crushed band"), FindFalseColourBand(GetSignalPercent(PlainBlack)) == Crushed);
	TestTrue(TEXT("The over-bright card sits in the clipped band"), FindFalseColourBand(GetSignalPercent(PlainBright)) == Clipped);
	TestNull(TEXT("The backdrop sits between bands"), FindFalseColourBand(GetSignalPercent(PlainBackdrop)));

	// False colour: each card takes the colour of its band, and what is between bands turns grey.
	{
		FUEcineMonitorSettings Settings;
		Settings.bFalseColour = true;
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		SavePicture(Picture, TEXT("monitor-false-colour.png"));
		TestTrue(TEXT("False colour: the grey card is the middle grey colour"), IsNear(PixelAt(Picture, GreyCardCentre), MiddleGrey->Colour));
		TestTrue(TEXT("False colour: the black card is the crushed colour"), IsNear(PixelAt(Picture, BlackCardCentre), Crushed->Colour));
		TestTrue(TEXT("False colour: the bright card is the clipped colour"), IsNear(PixelAt(Picture, BrightCardCentre), Clipped->Colour));
		TestTrue(TEXT("False colour: the card one stop over grey is the one-stop-over colour"), IsNear(PixelAt(Picture, GetRampCardCentre(3)), OneStopOver->Colour));
		const uint8 BackdropGrey = static_cast<uint8>(FMath::RoundToInt32(GetSignalPercent(PlainBackdrop) * 2.55f));
		TestTrue(TEXT("False colour: the backdrop is grey at its own signal level"), IsNear(PixelAt(Picture, BackdropSample), FColor(BackdropGrey, BackdropGrey, BackdropGrey)));
	}

	// Zebras: stripes on the bright card only. Stripes are four pixels wide on the diagonal.
	{
		FUEcineMonitorSettings Settings;
		Settings.bZebras = true;
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		TestTrue(TEXT("Zebras: between stripes the bright card is unchanged"), IsNear(PixelAt(Picture, BrightCardCentre), PlainBright));
		TestTrue(TEXT("Zebras: a stripe on the bright card is black"), IsNear(PixelAt(Picture, BrightCardCentre + FIntPoint(4, 0)), FColor::Black));
		TestTrue(TEXT("Zebras: the grey card has none"), IsNear(PixelAt(Picture, GreyCardCentre), PlainGrey) && IsNear(PixelAt(Picture, GreyCardCentre + FIntPoint(4, 0)), PlainGrey));
	}

	// Peaking: the sharp edge of the grey card is outlined, its flat face is not.
	{
		FUEcineMonitorSettings Settings;
		Settings.bPeaking = true;
		Settings.PeakingColour = EUEcineMonitorColour::Blue;
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		const FColor Outline = GetColour(EUEcineMonitorColour::Blue);
		TestTrue(TEXT("Peaking: the edge of the grey card is outlined in the chosen colour"),
			IsNear(PixelAt(Picture, FIntPoint(GreyCardLeftEdge, 540)), Outline, 0) || IsNear(PixelAt(Picture, FIntPoint(GreyCardLeftEdge - 1, 540)), Outline, 0));
		TestTrue(TEXT("Peaking: the face of the grey card is unchanged"), IsNear(PixelAt(Picture, GreyCardCentre), PlainGrey));
	}

	// Scopes. The trace and the bars are checked where the grey card must put them.
	{
		FUEcineMonitorSettings Settings;
		Settings.bWaveform = true;
		Settings.bHistogram = true;
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		const FUEcineScopeLayout Layout = GetScopeLayout(PictureSize, true, true);
		const int32 GreyLevel = FMath::FloorToInt32(GetSignalPercent(PlainGrey) / 100.0f * FUEcineMonitorViewExtension::ScopeLevels);

		// Waveform: the middle column of the picture holds the grey card, so the trace is lit at the card's level.
		const int32 WaveformX = Layout.Waveform.Min.X + Layout.Waveform.Width() / 2;
		const FColor Trace = PixelAt(Picture, FIntPoint(WaveformX, Layout.Waveform.Max.Y - 1 - GetScopePixelForLevel(GreyLevel, Layout.Waveform.Height())));
		TestTrue(*FString::Printf(TEXT("Waveform: the trace is lit at the grey card's level (found %s)"), *Describe(Trace)), IsNear(Trace, FColor(204, 255, 204)));
		const FColor NoTrace = PixelAt(Picture, FIntPoint(WaveformX, Layout.Waveform.Max.Y - 1 - GetScopePixelForLevel(230, Layout.Waveform.Height())));
		TestTrue(*FString::Printf(TEXT("Waveform: nothing is drawn at a level the column does not hold (found %s)"), *Describe(NoTrace)), NoTrace.G < 64);
		TestTrue(TEXT("Waveform: the picture outside the scope is unchanged"), IsNear(PixelAt(Picture, GreyCardCentre), PlainGrey));

		// Histogram: the grey card fills about 5 % of the picture, which draws a tall bar at its level.
		const int32 HistogramRow = Layout.Histogram.Max.Y - 1 - FMath::FloorToInt32(0.4f * Layout.Histogram.Height());
		const FColor Bar = PixelAt(Picture, FIntPoint(Layout.Histogram.Min.X + GetScopePixelForLevel(GreyLevel, Layout.Histogram.Width()), HistogramRow));
		TestTrue(*FString::Printf(TEXT("Histogram: there is a bar at the grey card's level (found %s)"), *Describe(Bar)), IsNear(Bar, FColor(217, 217, 217)));
		const FColor NoBar = PixelAt(Picture, FIntPoint(Layout.Histogram.Min.X + GetScopePixelForLevel(100, Layout.Histogram.Width()), HistogramRow));
		TestTrue(*FString::Printf(TEXT("Histogram: there is no bar at a level the picture does not hold (found %s)"), *Describe(NoBar)), NoBar.G < 64);
	}

	// Everything at once, kept as pictures to look at.
	{
		FUEcineMonitorSettings Settings;
		Settings.bFalseColour = true;
		Settings.bPeaking = true;
		Settings.bWaveform = true;
		Settings.bHistogram = true;
		SavePicture(Stage.Shoot(Settings), TEXT("monitor-all-tools.png"));
		Settings.bFalseColour = false;
		Settings.bZebras = true;
		SavePicture(Stage.Shoot(Settings), TEXT("monitor-zebras-peaking-scopes.png"));
	}

	// An extension nobody has activated draws nowhere, whatever its tools are set to.
	{
		const TSharedRef<FUEcineMonitorViewExtension, ESPMode::ThreadSafe> Unused = FSceneViewExtensions::NewExtension<FUEcineMonitorViewExtension>();
		FUEcineMonitorSettings Settings;
		Settings.bFalseColour = true;
		Settings.bWaveform = true;
		Unused->SetSettings(Settings);
		const TArray<FColor> Picture = Stage.Shoot(FUEcineMonitorSettings());
		TestTrue(TEXT("A monitor that has not been told where to draw leaves the grey card alone"), IsNear(PixelAt(Picture, GreyCardCentre), PlainGrey));
		TestTrue(TEXT("A monitor that has not been told where to draw leaves the bright card alone"), IsNear(PixelAt(Picture, BrightCardCentre), PlainBright));
		const FUEcineScopeLayout Layout = GetScopeLayout(PictureSize, true, true);
		TestTrue(TEXT("A monitor that has not been told where to draw draws no waveform"), IsNear(PixelAt(Picture, Layout.Waveform.Min + FIntPoint(40, 40)), PlainBackdrop, 4));
	}

	// A view that smooths edges after tone mapping: the tools are drawn after the smoothing, so they stay sharp.
	// Every pixel across the edge of the grey card is then exactly a band colour or a grey, never a blend of the two.
	{
		const FMonitorTestStage SmoothedStage(true);
		FUEcineMonitorSettings Settings;
		Settings.bFalseColour = true;
		const TArray<FColor> Picture = SmoothedStage.Shoot(Settings);
		for (int32 X = GreyCardLeftEdge - 4; X < GreyCardLeftEdge + 4; ++X)
		{
			const FColor Pixel = PixelAt(Picture, FIntPoint(X, 540));
			const bool bIsGrey = FMath::Abs(Pixel.R - Pixel.G) <= 1 && FMath::Abs(Pixel.G - Pixel.B) <= 1;
			const bool bIsBandColour = Bands.ContainsByPredicate([&Pixel](const FUEcineFalseColourBand& Band) { return IsNear(Pixel, Band.Colour, 1); });
			TestTrue(*FString::Printf(TEXT("With edge smoothing, the pixel at x = %d is a band colour or a grey (found %s)"), X, *Describe(Pixel)), bIsGrey || bIsBandColour);
		}
	}

	return true;
}

/**
 * The lines and words drawn over the picture: frame lines, safe areas, centre mark, data readout, false
 * colour legend and scope titles, read back from real pictures.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMonitorOverlayTest, "UEcine.Monitor.Overlay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineMonitorOverlayTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: reading pictures back needs a renderer (run tools/test.ps1 with -Rendering)."));
		return true;
	}
	using namespace UEcine::Monitor;

	const FMonitorTestStage Stage(false);
	const TArray<FColor> Plain = Stage.Shoot(FUEcineMonitorSettings());

	// The view is looking through a camera that delivers 2.39:1.
	const FRenderTarget* StageTarget = Stage.Target->GameThread_GetRenderTargetResource();
	FUEcineMonitorCameraOverlay Camera;
	Camera.DeliveryAspect = 2.39f;
	Camera.DataReadout = TEXT("A Cam   35 mm   T2.8   ISO 800   180\u00B0   24 fps   Focus 3.05 m");
	const auto LookThroughCamera = [&Stage, StageTarget, &Camera](bool bLookThrough)
	{
		TMap<const FRenderTarget*, FUEcineMonitorCameraOverlay> Overlays;
		if (bLookThrough)
		{
			Overlays.Add(StageTarget, Camera);
		}
		Stage.Extension->SetCameraOverlays(Overlays);
	};
	const FIntRect Frame = GetDeliveryFrame(PictureSize, Camera.DeliveryAspect);
	const FColor Yellow = GetColour(EUEcineMonitorColour::Yellow);
	const FColor Green = GetColour(EUEcineMonitorColour::Green);

	// How much of an area is white text, and how bright the area is.
	const auto CountPixels = [](const TArray<FColor>& Pixels, const FIntRect& Area, TFunctionRef<bool(const FColor&)> Matches)
	{
		int32 Count = 0;
		for (int32 Y = Area.Min.Y; Y < Area.Max.Y; ++Y)
		{
			for (int32 X = Area.Min.X; X < Area.Max.X; ++X)
			{
				Count += Matches(PixelAt(Pixels, FIntPoint(X, Y))) ? 1 : 0;
			}
		}
		return Count;
	};
	const auto IsWhite = [](const FColor& Pixel) { return Pixel.R >= 235 && Pixel.G >= 235 && Pixel.B >= 235; };
	const FIntRect WordsArea(16, 16, 700, 240);

	// Frame lines: two pixels of the chosen colour on the edge of the delivery frame, black either side.
	{
		FUEcineMonitorSettings Settings;
		Settings.bFrameLines = true;
		Settings.LineColour = EUEcineMonitorColour::Yellow;
		LookThroughCamera(true);
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		AddInfo(FString::Printf(TEXT("Frame line at the top of the 2.39:1 frame, rows %d to %d: %s, %s, %s, %s"), Frame.Min.Y - 1, Frame.Min.Y + 2,
			*Describe(PixelAt(Picture, FIntPoint(500, Frame.Min.Y - 1))), *Describe(PixelAt(Picture, FIntPoint(500, Frame.Min.Y))),
			*Describe(PixelAt(Picture, FIntPoint(500, Frame.Min.Y + 1))), *Describe(PixelAt(Picture, FIntPoint(500, Frame.Min.Y + 2)))));
		TestTrue(TEXT("Frame lines: the top line is on the first row of the frame"), IsNear(PixelAt(Picture, FIntPoint(500, Frame.Min.Y)), Yellow));
		TestTrue(TEXT("Frame lines: the top line is two pixels thick"), IsNear(PixelAt(Picture, FIntPoint(500, Frame.Min.Y + 1)), Yellow));
		TestTrue(TEXT("Frame lines: black outside the top line"), IsNear(PixelAt(Picture, FIntPoint(500, Frame.Min.Y - 1)), FColor::Black));
		TestTrue(TEXT("Frame lines: black inside the top line"), IsNear(PixelAt(Picture, FIntPoint(500, Frame.Min.Y + 2)), FColor::Black));
		TestTrue(TEXT("Frame lines: the bottom line is on the last row of the frame"), IsNear(PixelAt(Picture, FIntPoint(500, Frame.Max.Y - 1)), Yellow));
		TestTrue(TEXT("Frame lines: the left line is on the first column of the frame"), IsNear(PixelAt(Picture, FIntPoint(Frame.Min.X, 540)), Yellow));
		TestTrue(TEXT("Frame lines: the right line is on the last column of the frame"), IsNear(PixelAt(Picture, FIntPoint(Frame.Max.X - 1, 540)), Yellow));
		TestTrue(TEXT("Frame lines: the picture inside the frame is untouched"), IsNear(PixelAt(Picture, BackdropSample), PixelAt(Plain, BackdropSample)));
		TestTrue(TEXT("Frame lines: the picture outside the frame is untouched"), IsNear(PixelAt(Picture, FIntPoint(500, 60)), PixelAt(Plain, FIntPoint(500, 60))));

		// The user chooses the line colour.
		Settings.LineColour = EUEcineMonitorColour::Green;
		TestTrue(TEXT("Frame lines: drawn in the colour chosen"), IsNear(PixelAt(Stage.Shoot(Settings), FIntPoint(500, Frame.Min.Y)), Green));

		// A view that is not looking through a camera has no delivery frame to outline.
		LookThroughCamera(false);
		const TArray<FColor> FreeView = Stage.Shoot(Settings);
		TestTrue(TEXT("Frame lines: none in a view that is not looking through a camera"), IsNear(PixelAt(FreeView, FIntPoint(500, Frame.Min.Y)), PixelAt(Plain, FIntPoint(500, Frame.Min.Y))));

		// A camera that delivers its whole recording format has no lines either: they would sit on the picture's edge.
		TMap<const FRenderTarget*, FUEcineMonitorCameraOverlay> WholeFormat;
		WholeFormat.Add(StageTarget, FUEcineMonitorCameraOverlay());
		Stage.Extension->SetCameraOverlays(WholeFormat);
		const TArray<FColor> WholePicture = Stage.Shoot(Settings);
		TestTrue(TEXT("Frame lines: none when the delivery frame is the whole picture"), IsNear(PixelAt(WholePicture, FIntPoint(500, 0)), PixelAt(Plain, FIntPoint(500, 0))));
	}

	// Safe areas: action safe and title safe inside the delivery frame, one pixel each.
	{
		FUEcineMonitorSettings Settings;
		Settings.bSafeAreas = true;
		Settings.LineColour = EUEcineMonitorColour::Yellow;
		LookThroughCamera(true);
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		const FIntRect ActionSafe = GetSafeArea(Frame, ActionSafeFraction);
		const FIntRect TitleSafe = GetSafeArea(Frame, TitleSafeFraction);
		TestTrue(TEXT("Safe areas: the action safe line is where 93 % of the frame ends"), IsNear(PixelAt(Picture, FIntPoint(500, ActionSafe.Min.Y)), Yellow));
		TestTrue(TEXT("Safe areas: black outside the action safe line"), IsNear(PixelAt(Picture, FIntPoint(500, ActionSafe.Min.Y - 1)), FColor::Black));
		TestTrue(TEXT("Safe areas: black inside the action safe line"), IsNear(PixelAt(Picture, FIntPoint(500, ActionSafe.Min.Y + 1)), FColor::Black));
		TestTrue(TEXT("Safe areas: the action safe line runs down the side too"), IsNear(PixelAt(Picture, FIntPoint(ActionSafe.Max.X - 1, 400)), Yellow));
		TestTrue(TEXT("Safe areas: the title safe line is where 90 % of the frame ends"), IsNear(PixelAt(Picture, FIntPoint(500, TitleSafe.Min.Y)), Yellow));
		TestTrue(TEXT("Safe areas: the title safe line runs along the bottom too"), IsNear(PixelAt(Picture, FIntPoint(500, TitleSafe.Max.Y - 1)), Yellow));
		TestTrue(TEXT("Safe areas: no frame line is drawn with them"), IsNear(PixelAt(Picture, FIntPoint(500, Frame.Min.Y)), PixelAt(Plain, FIntPoint(500, Frame.Min.Y))));
	}

	// Centre mark: a cross in the middle of the delivery frame.
	{
		FUEcineMonitorSettings Settings;
		Settings.bCentreMark = true;
		Settings.LineColour = EUEcineMonitorColour::Yellow;
		LookThroughCamera(true);
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		const FIntPoint Centre = (Frame.Min + Frame.Max) / 2;
		TestTrue(TEXT("Centre mark: the cross has an arm to the left"), IsNear(PixelAt(Picture, Centre + FIntPoint(-12, 0)), Yellow));
		TestTrue(TEXT("Centre mark: the cross has an arm to the right"), IsNear(PixelAt(Picture, Centre + FIntPoint(12, -1)), Yellow));
		TestTrue(TEXT("Centre mark: the cross has an arm upwards"), IsNear(PixelAt(Picture, Centre + FIntPoint(0, -12)), Yellow));
		TestTrue(TEXT("Centre mark: the cross has an arm downwards"), IsNear(PixelAt(Picture, Centre + FIntPoint(-1, 12)), Yellow));
		TestTrue(TEXT("Centre mark: black round the cross"), IsNear(PixelAt(Picture, Centre + FIntPoint(12, 1)), FColor::Black));
		TestTrue(TEXT("Centre mark: the picture beside it is untouched"), IsNear(PixelAt(Picture, Centre + FIntPoint(40, 40)), PixelAt(Plain, Centre + FIntPoint(40, 40))));
	}

	// Data readout: white words on a dark backing in the top left corner, for a view looking through a camera.
	{
		FUEcineMonitorSettings Settings;
		Settings.bDataReadout = true;
		LookThroughCamera(true);
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		const int32 WhitePixels = CountPixels(Picture, WordsArea, IsWhite);
		const float BackingSignal = GetSignalPercent(PixelAt(Picture, FIntPoint(17, 17)));
		const float PlainSignal = GetSignalPercent(PixelAt(Plain, FIntPoint(17, 17)));
		AddInfo(FString::Printf(TEXT("Data readout: %d white pixels of text; the backing shows %.1f %% signal where the plain picture shows %.1f %%"), WhitePixels, BackingSignal, PlainSignal));
		TestTrue(TEXT("Data readout: the words are drawn in white"), WhitePixels > 150);
		TestTrue(TEXT("Data readout: the backing darkens the picture behind the words"), BackingSignal < 0.5f * PlainSignal);
		TestTrue(TEXT("Data readout: the picture still shows through the backing"), BackingSignal > 0.0f);
		TestEqual(TEXT("Data readout: the plain picture has no white there"), CountPixels(Plain, WordsArea, IsWhite), 0);

		LookThroughCamera(false);
		TestEqual(TEXT("Data readout: nothing to say in a view that is not looking through a camera"), CountPixels(Stage.Shoot(Settings), WordsArea, IsWhite), 0);
	}

	// False colour comes with its legend, and each scope with its name.
	{
		FUEcineMonitorSettings Settings;
		Settings.bFalseColour = true;
		Settings.bWaveform = true;
		Settings.bHistogram = true;
		LookThroughCamera(false);
		const TArray<FColor> Picture = Stage.Shoot(Settings);
		for (const FUEcineFalseColourBand& Band : GetFalseColourBands())
		{
			const int32 SwatchPixels = CountPixels(Picture, WordsArea, [&Band](const FColor& Pixel) { return IsNear(Pixel, Band.Colour); });
			TestTrue(*FString::Printf(TEXT("Legend: there is a swatch for %s (%d pixels)"), *Band.Name.ToString(), SwatchPixels), SwatchPixels > 30);
		}
		TestTrue(TEXT("Legend: each band is named in words"), CountPixels(Picture, WordsArea, IsWhite) > 600);

		const FUEcineScopeLayout Layout = GetScopeLayout(PictureSize, true, true);
		TestTrue(TEXT("The waveform is named"), CountPixels(Picture, FIntRect(Layout.Waveform.Min, Layout.Waveform.Min + FIntPoint(120, 24)), IsWhite) > 40);
		TestTrue(TEXT("The histogram is named"), CountPixels(Picture, FIntRect(Layout.Histogram.Min, Layout.Histogram.Min + FIntPoint(120, 24)), IsWhite) > 40);
		SavePicture(Picture, TEXT("monitor-false-colour-legend.png"));
	}

	// Everything a camera operator frames with, for a person to look at.
	{
		FUEcineMonitorSettings Settings;
		Settings.bFrameLines = true;
		Settings.bSafeAreas = true;
		Settings.bCentreMark = true;
		Settings.bDataReadout = true;
		LookThroughCamera(true);
		SavePicture(Stage.Shoot(Settings), TEXT("monitor-framing.png"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMonitorCostTest, "UEcine.Monitor.Cost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineMonitorCostTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: needs a renderer (run tools/test.ps1 with -Rendering)."));
		return true;
	}

	const FMonitorTestStage Stage(false);
	const FUEcineMonitorSettings Off;
	FUEcineMonitorSettings FalseColourAndWaveform;
	FalseColourAndWaveform.bFalseColour = true;
	FalseColourAndWaveform.bWaveform = true;
	FUEcineMonitorSettings Everything = FalseColourAndWaveform;
	Everything.bZebras = true;
	Everything.bPeaking = true;
	Everything.bHistogram = true;
	Everything.bFrameLines = true;
	Everything.bSafeAreas = true;
	Everything.bCentreMark = true;
	Everything.bDataReadout = true;
	TMap<const FRenderTarget*, FUEcineMonitorCameraOverlay> Overlays;
	Overlays.Add(Stage.Target->GameThread_GetRenderTargetResource(), FUEcineMonitorCameraOverlay{ 2.39f, TEXT("A Cam   35 mm   T2.8   ISO 800   24 fps   Focus 3.05 m") });
	Stage.Extension->SetCameraOverlays(Overlays);

	// Warm up, then time the three set-ups turn and turn about so that drift affects them alike.
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Stage.TimeFrame(Off);
		Stage.TimeFrame(FalseColourAndWaveform);
		Stage.TimeFrame(Everything);
	}
	TArray<double> OffTimes;
	TArray<double> TwoToolTimes;
	TArray<double> EverythingTimes;
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		OffTimes.Add(Stage.TimeFrame(Off));
		TwoToolTimes.Add(Stage.TimeFrame(FalseColourAndWaveform));
		EverythingTimes.Add(Stage.TimeFrame(Everything));
	}

	const double OffTime = Median(OffTimes);
	const double TwoToolCost = Median(TwoToolTimes) - OffTime;
	const double EverythingCost = Median(EverythingTimes) - OffTime;
	AddInfo(FString::Printf(TEXT("1920 x 1080 frame, median of 100: tools off %.2f ms; false colour and waveform add %.2f ms; every tool adds %.2f ms"),
		OffTime, TwoToolCost, EverythingCost));

	// The budget from ADR-005.
	TestTrue(*FString::Printf(TEXT("False colour and waveform together add no more than 1.5 ms to a frame (added %.2f ms)"), TwoToolCost), TwoToolCost <= 1.5);
	TestTrue(*FString::Printf(TEXT("Every tool together adds no more than 1.5 ms to a frame (added %.2f ms)"), EverythingCost), EverythingCost <= 1.5);
	return true;
}

#endif
