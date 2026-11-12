// Copyright 2026 Fiavaion. All rights reserved.

#include "CineCameraActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Editor.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "TextureResource.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineMonitorSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool IsNearMonitorColour(const FColor& Pixel, const FColor& Colour)
	{
		return FMath::Abs(Pixel.R - Colour.R) <= 8 && FMath::Abs(Pixel.G - Colour.G) <= 8 && FMath::Abs(Pixel.B - Colour.B) <= 8;
	}

	/** A picture read back from a viewport or a capture. */
	struct FMonitorPicture
	{
		FIntPoint Size = FIntPoint::ZeroValue;
		TArray<FColor> Pixels;

		const FColor& At(int32 X, int32 Y) const
		{
			return Pixels[Y * Size.X + X];
		}

		/** How many pixels of an area a test accepts. */
		template <typename PredicateType>
		int32 Count(const FIntRect& Area, PredicateType Predicate) const
		{
			int32 Matching = 0;
			for (int32 Y = Area.Min.Y; Y < Area.Max.Y; ++Y)
			{
				for (int32 X = Area.Min.X; X < Area.Max.X; ++X)
				{
					Matching += Predicate(At(X, Y)) ? 1 : 0;
				}
			}
			return Matching;
		}

		/** The runs of accepted pixels down one column, each given as its first and last row. */
		template <typename PredicateType>
		TArray<FIntPoint> FindRunsInColumn(int32 X, PredicateType Predicate) const
		{
			TArray<FIntPoint> Runs;
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				if (Predicate(At(X, Y)))
				{
					if (Runs.Num() > 0 && Runs.Last().Y == Y - 1)
					{
						Runs.Last().Y = Y;
					}
					else
					{
						Runs.Add(FIntPoint(Y, Y));
					}
				}
			}
			return Runs;
		}

		/** The share of the upper part of the picture that is clearly not the same as in another picture of the same view. */
		float GetDifferenceShare(const FMonitorPicture& Other) const
		{
			int32 Different = 0;
			int32 Total = 0;
			for (int32 Y = Size.Y / 20; Y < Size.Y * 35 / 100; Y += 2)
			{
				for (int32 X = Size.X / 10; X < Size.X * 9 / 10; X += 2)
				{
					const FColor& Mine = At(X, Y);
					const FColor& Theirs = Other.At(X, Y);
					Different += (FMath::Abs(Mine.R - Theirs.R) > 40 || FMath::Abs(Mine.G - Theirs.G) > 40 || FMath::Abs(Mine.B - Theirs.B) > 40) ? 1 : 0;
					++Total;
				}
			}
			return Total > 0 ? static_cast<float>(Different) / Total : 0.0f;
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

	// Zebras and peaking in the level viewport. A second plain picture shows how much two pictures of the same view differ anyway.
	const FMonitorPicture PlainAgain = ShootViewport(FUEcineMonitorSettings());
	const FIntRect WholePicture(FIntPoint::ZeroValue, PlainAgain.Size);

	FUEcineMonitorSettings Zebras;
	Zebras.bZebras = true;
	Zebras.ZebraLevelPercent = 5.0f;
	const FMonitorPicture ZebraViewport = ShootViewport(Zebras);

	FUEcineMonitorSettings Peaking;
	Peaking.bPeaking = true;
	Peaking.PeakingColour = EUEcineMonitorColour::Green;
	Peaking.PeakingThresholdPercent = 1.0f;
	const FColor PeakingColour = UEcine::Monitor::GetColour(Peaking.PeakingColour);
	const auto IsPeaking = [PeakingColour](const FColor& Pixel) { return IsNearMonitorColour(Pixel, PeakingColour); };
	const FMonitorPicture PeakingViewport = ShootViewport(Peaking);
	AddInfo(FString::Printf(TEXT("Level viewport: %.3f of the picture differs between two plain pictures, %.3f with zebras from 5 %%; %d peaking pixels plain, %d with peaking"),
		PlainViewport.GetDifferenceShare(PlainAgain), ZebraViewport.GetDifferenceShare(PlainAgain), PlainAgain.Count(WholePicture, IsPeaking), PeakingViewport.Count(WholePicture, IsPeaking)));
	TestTrue(TEXT("Two plain pictures of the viewport are the same picture"), PlainViewport.GetDifferenceShare(PlainAgain) < 0.02f);
	TestTrue(TEXT("Viewport with zebras: the stripes are on the picture"), ZebraViewport.GetDifferenceShare(PlainAgain) > 0.1f);
	TestTrue(TEXT("Viewport with peaking: detail is outlined in the colour chosen"), PeakingViewport.Count(WholePicture, IsPeaking) > PlainAgain.Count(WholePicture, IsPeaking) + 20);

	// Frame lines, safe areas, the centre mark and the data readout belong to the camera a viewport looks through.
	UWorld* World = ViewportClient->GetWorld();
	const FVector ViewLocation = ViewportClient->GetViewLocation();
	const FRotator ViewRotation = ViewportClient->GetViewRotation();
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform(ViewRotation, ViewLocation));
	UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(Camera);
	if (!TestNotNull(TEXT("A camera with a camera package to look through"), Package))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		ViewportClient->SetActorLock(nullptr);
		ViewportClient->UpdateViewForLockedActor();
		ViewportClient->SetViewLocation(ViewLocation);
		ViewportClient->SetViewRotation(ViewRotation);
		GEditor->SelectNone(false, true, false);
		World->EditorDestroyActor(Camera, false);
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine monitor viewport test")));
	};

	const auto SetDeliveryFrame = [Package](float Aspect)
	{
		FText Error;
		return UEcine::CameraEditing::EditPackage(Package, FText::FromString(TEXT("Test delivery frame")), [Aspect](FUEcineCameraPackage& Edited) { Edited.DeliveryAspect = Aspect; }, Error);
	};
	TestTrue(TEXT("The camera takes a 2.39:1 delivery frame"), SetDeliveryFrame(2.39f));

	FUEcineMonitorSettings Framing;
	Framing.bFrameLines = true;
	Framing.LineColour = EUEcineMonitorColour::Yellow;
	const FColor LineColour = UEcine::Monitor::GetColour(Framing.LineColour);
	const auto IsLine = [LineColour](const FColor& Pixel) { return IsNearMonitorColour(Pixel, LineColour); };

	TestEqual(TEXT("Free view: no frame lines, as the viewport looks through no camera"), ShootViewport(Framing).Count(WholePicture, IsLine), 0);

	// The editor does this on every tick: the viewport takes on the shape of the camera's recording format.
	ViewportClient->SetActorLock(Camera);
	ViewportClient->MoveCameraToLockedActor();
	ViewportClient->UpdateViewForLockedActor();
	const FMonitorPicture FramedViewport = ShootViewport(Framing);
	const int32 ProbeColumn = FramedViewport.Size.X * 5 / 8;
	const TArray<FIntPoint> FrameRuns = FramedViewport.FindRunsInColumn(ProbeColumn, IsLine);
	if (TestEqual(TEXT("Looking through the camera: one frame line above the frame and one below"), FrameRuns.Num(), 2))
	{
		const int32 TopRow = FrameRuns[0].X;
		const int32 BottomRow = FrameRuns[1].Y;
		int32 LeftColumn = INDEX_NONE;
		int32 RightColumn = INDEX_NONE;
		for (int32 X = 0; X < FramedViewport.Size.X; ++X)
		{
			if (IsLine(FramedViewport.At(X, TopRow)))
			{
				LeftColumn = LeftColumn == INDEX_NONE ? X : LeftColumn;
				RightColumn = X;
			}
		}
		const int32 FrameWidth = RightColumn - LeftColumn + 1;
		const int32 FrameHeight = BottomRow - TopRow + 1;
		AddInfo(FString::Printf(TEXT("Frame lines in the %d x %d viewport: columns %d to %d, rows %d to %d, a %d x %d frame"),
			FramedViewport.Size.X, FramedViewport.Size.Y, LeftColumn, RightColumn, TopRow, BottomRow, FrameWidth, FrameHeight));
		TestEqual(TEXT("Frame lines: the top line is unbroken from side to side"), FramedViewport.Count(FIntRect(0, TopRow, FramedViewport.Size.X, TopRow + 1), IsLine), FrameWidth);
		TestEqual(TEXT("Frame lines: each line is two pixels thick"), FrameRuns[0].Y - TopRow + 1, 2);
		TestEqual(TEXT("Frame lines: they enclose a 2.39:1 frame"), static_cast<float>(FrameWidth) / FrameHeight, 2.39f, 0.02f);
		TestTrue(TEXT("Frame lines: the frame sits in the middle of the picture from side to side"), FMath::Abs(LeftColumn + RightColumn - (FramedViewport.Size.X - 1)) <= 2);
		TestTrue(TEXT("Frame lines: the frame sits in the middle of the picture from top to bottom"), FMath::Abs(TopRow + BottomRow - (FramedViewport.Size.Y - 1)) <= 2);
		TestTrue(TEXT("Frame lines: a black edge sets the line off from the picture"), TopRow > 0 && UEcine::Monitor::GetSignalPercent(FramedViewport.At(ProbeColumn, TopRow - 1)) < 5.0f);
	}

	const FIntRect CentreArea(FramedViewport.Size / 2 - FIntPoint(3, 3), FramedViewport.Size / 2 + FIntPoint(4, 4));
	TestEqual(TEXT("Frame lines alone: nothing at the centre of the frame"), FramedViewport.Count(CentreArea, IsLine), 0);

	TestTrue(TEXT("The camera goes back to delivering the whole recording format"), SetDeliveryFrame(0.0f));
	TestEqual(TEXT("Whole recording format: there is no frame line to draw"), ShootViewport(Framing).Count(WholePicture, IsLine), 0);
	TestTrue(TEXT("The camera takes the 2.39:1 delivery frame again"), SetDeliveryFrame(2.39f));

	Framing.bSafeAreas = true;
	Framing.bCentreMark = true;
	Framing.bDataReadout = true;
	const FMonitorPicture MarkedViewport = ShootViewport(Framing);
	{
		// Kept for a look by eye: the level viewport as the user sees it through the camera.
		TArray<FColor> Opaque = MarkedViewport.Pixels;
		for (FColor& Pixel : Opaque)
		{
			Pixel.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(MarkedViewport.Size.X, MarkedViewport.Size.Y, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
		TestTrue(TEXT("The picture of the viewport was saved"), FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("UEcine/Screenshots/monitor-viewport-framing.png"))));
	}
	TestEqual(TEXT("Safe areas: action safe and title safe add two lines above and two below"), MarkedViewport.FindRunsInColumn(ProbeColumn, IsLine).Num(), 6);
	TestTrue(TEXT("Centre mark: a cross at the centre of the frame"), MarkedViewport.Count(CentreArea, IsLine) > 0);

	FUEcineMonitorSettings Readout;
	Readout.bDataReadout = true;
	const FIntRect ReadoutArea(0, 0, FramedViewport.Size.X * 2 / 3, FramedViewport.Size.Y / 5);
	const auto IsWhite = [](const FColor& Pixel) { return Pixel.R >= 250 && Pixel.G >= 250 && Pixel.B >= 250; };
	const int32 WhiteWithoutReadout = ShootViewport(FUEcineMonitorSettings()).Count(ReadoutArea, IsWhite);
	const int32 WhiteWithReadout = ShootViewport(Readout).Count(ReadoutArea, IsWhite);
	ViewportClient->SetActorLock(nullptr);
	ViewportClient->UpdateViewForLockedActor();
	const int32 FreeWhiteWithoutReadout = ShootViewport(FUEcineMonitorSettings()).Count(ReadoutArea, IsWhite);
	const int32 FreeWhiteWithReadout = ShootViewport(Readout).Count(ReadoutArea, IsWhite);
	AddInfo(FString::Printf(TEXT("Data readout: white pixels top left looking through the camera %d without and %d with the readout; in a free view %d and %d"),
		WhiteWithoutReadout, WhiteWithReadout, FreeWhiteWithoutReadout, FreeWhiteWithReadout));
	TestTrue(TEXT("Looking through the camera: the data readout is written over the picture"), WhiteWithReadout > WhiteWithoutReadout + 100);
	TestTrue(TEXT("Free view: no data readout"), FMath::Abs(FreeWhiteWithReadout - FreeWhiteWithoutReadout) <= 20);

	return true;
}

#endif
