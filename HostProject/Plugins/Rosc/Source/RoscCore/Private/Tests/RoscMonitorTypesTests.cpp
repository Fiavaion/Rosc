// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "RoscCameraPackage.h"
#include "RoscMonitorTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscMonitorTypesTest, "Rosc.Core.Monitor.Picture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscMonitorTypesTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Monitor;

	// Signal level of a displayed pixel.
	TestEqual(TEXT("Black is 0 %"), GetSignalPercent(FColor(0, 0, 0)), 0.0f, 0.01f);
	TestEqual(TEXT("White is 100 %"), GetSignalPercent(FColor(255, 255, 255)), 100.0f, 0.01f);
	TestEqual(TEXT("Pure green carries 71.5 % of the signal"), GetSignalPercent(FColor(0, 255, 0)), 71.52f, 0.01f);
	TestEqual(TEXT("Pure blue carries 7.2 % of the signal"), GetSignalPercent(FColor(0, 0, 255)), 7.22f, 0.01f);

	// False colour bands: in order, not overlapping, each with a name and its own colour.
	const TConstArrayView<FRoscFalseColourBand> Bands = GetFalseColourBands();
	TestEqual(TEXT("Six bands"), Bands.Num(), 6);
	for (int32 Index = 0; Index < Bands.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Band %d has a range"), Index), Bands[Index].ToPercent > Bands[Index].FromPercent);
		TestFalse(*FString::Printf(TEXT("Band %d has a name"), Index), Bands[Index].Name.IsEmpty());
		TestTrue(*FString::Printf(TEXT("Band %d is not grey, so it stands out from the levels between bands"), Index),
			Bands[Index].Colour.R != Bands[Index].Colour.G || Bands[Index].Colour.G != Bands[Index].Colour.B);
		if (Index > 0)
		{
			TestTrue(*FString::Printf(TEXT("Band %d starts at or after the end of the band below"), Index), Bands[Index].FromPercent >= Bands[Index - 1].ToPercent);
			TestTrue(*FString::Printf(TEXT("Band %d differs in colour from the band below"), Index), Bands[Index].Colour != Bands[Index - 1].Colour);
		}
	}

	// Levels measured through the engine's default tone curve (see GetFalseColourBands).
	TestTrue(TEXT("18 % grey, 46 % signal, is in the middle grey band"), FindFalseColourBand(46.3f) == &Bands[2]);
	TestTrue(TEXT("A quarter of a stop under grey is still middle grey"), FindFalseColourBand(41.2f) == &Bands[2]);
	TestTrue(TEXT("A quarter of a stop over grey is still middle grey"), FindFalseColourBand(51.4f) == &Bands[2]);
	TestTrue(TEXT("One stop over grey, 67 % signal, is in its band"), FindFalseColourBand(66.7f) == &Bands[3]);
	TestNull(TEXT("One stop under grey, 28 % signal, is between bands"), FindFalseColourBand(28.2f));
	TestNull(TEXT("Two stops over grey, 82 % signal, is between bands"), FindFalseColourBand(81.6f));
	TestTrue(TEXT("0 % is crushed"), FindFalseColourBand(0.0f) == &Bands[0]);
	TestTrue(TEXT("100 % is clipped"), FindFalseColourBand(100.0f) == &Bands[5]);
	TestTrue(TEXT("99 % is clipped: the range starts where it says"), FindFalseColourBand(99.0f) == &Bands[5]);
	TestTrue(TEXT("Just under 99 % is near clipping"), FindFalseColourBand(98.9f) == &Bands[4]);
	TestNull(TEXT("30 % is between bands and shown in grey"), FindFalseColourBand(30.0f));
	TestNull(TEXT("80 % is between bands and shown in grey"), FindFalseColourBand(80.0f));

	// Scope layout.
	{
		const FRoscScopeLayout Off = GetScopeLayout(FIntPoint(1920, 1080), false, false);
		TestTrue(TEXT("No scopes: both rectangles are empty"), Off.Waveform.Area() == 0 && Off.Histogram.Area() == 0);

		const FRoscScopeLayout Both = GetScopeLayout(FIntPoint(1920, 1080), true, true);
		TestEqual(TEXT("1080p: scopes are a quarter of the picture height"), Both.Waveform.Height(), 270);
		TestEqual(TEXT("1080p: scopes are three to two"), Both.Waveform.Width(), 405);
		TestEqual(TEXT("1080p: waveform starts a margin in from the left"), Both.Waveform.Min.X, 16);
		TestEqual(TEXT("1080p: waveform ends a margin up from the bottom"), Both.Waveform.Max.Y, 1080 - 16);
		TestEqual(TEXT("1080p: histogram ends a margin in from the right"), Both.Histogram.Max.X, 1920 - 16);
		TestEqual(TEXT("1080p: both scopes are the same size"), Both.Histogram.Size(), Both.Waveform.Size());

		const FRoscScopeLayout OnlyHistogram = GetScopeLayout(FIntPoint(1920, 1080), false, true);
		TestTrue(TEXT("Histogram alone: no waveform"), OnlyHistogram.Waveform.Area() == 0 && OnlyHistogram.Histogram.Area() > 0);

		// A narrow picture: the scopes shrink so they do not overlap.
		const FRoscScopeLayout Narrow = GetScopeLayout(FIntPoint(400, 800), true, true);
		TestTrue(TEXT("Narrow picture: the scopes do not overlap"), Narrow.Waveform.Max.X < Narrow.Histogram.Min.X);
		TestTrue(TEXT("Narrow picture: the scopes stay inside the picture"), Narrow.Waveform.Min.X >= 0 && Narrow.Histogram.Max.X <= 400);

		// Too small for a scope to be readable: none is drawn.
		const FRoscScopeLayout Tiny = GetScopeLayout(FIntPoint(120, 60), true, true);
		TestTrue(TEXT("Tiny picture: no scopes"), Tiny.Waveform.Area() == 0 && Tiny.Histogram.Area() == 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscMonitorOverlayTypesTest, "Rosc.Core.Monitor.Overlay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscMonitorOverlayTypesTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Monitor;

	// The delivery frame inside the picture.
	TestTrue(TEXT("No delivery aspect: the frame is the whole picture"), GetDeliveryFrame(FIntPoint(1920, 1080), 0.0f) == FIntRect(0, 0, 1920, 1080));
	TestTrue(TEXT("2.39:1 in a 16:9 picture: full width, bars above and below"), GetDeliveryFrame(FIntPoint(1920, 1080), 2.39f) == FIntRect(0, 138, 1920, 941));
	TestTrue(TEXT("4:3 in a 16:9 picture: full height, bars at the sides"), GetDeliveryFrame(FIntPoint(1920, 1080), 4.0f / 3.0f) == FIntRect(240, 0, 1680, 1080));
	TestTrue(TEXT("9:16 in a 16:9 picture: a tall frame in the middle"), GetDeliveryFrame(FIntPoint(1920, 1080), 9.0f / 16.0f) == FIntRect(656, 0, 1264, 1080));
	TestTrue(TEXT("16:9 in a 16:9 picture: the whole picture"), GetDeliveryFrame(FIntPoint(1920, 1080), 16.0f / 9.0f) == FIntRect(0, 0, 1920, 1080));
	const FIntRect OddFrame = GetDeliveryFrame(FIntPoint(1015, 551), 2.39f);
	TestTrue(TEXT("An odd picture size keeps the frame inside it"), OddFrame.Min.X >= 0 && OddFrame.Min.Y >= 0 && OddFrame.Max.X <= 1015 && OddFrame.Max.Y <= 551);
	TestTrue(TEXT("A picture with no size has an empty frame"), GetDeliveryFrame(FIntPoint(0, 0), 2.39f).Area() == 0);

	// Safe areas sit in the middle of the frame.
	const FIntRect Frame(0, 138, 1920, 941);
	const FIntRect ActionSafe = GetSafeArea(Frame, ActionSafeFraction);
	const FIntRect TitleSafe = GetSafeArea(Frame, TitleSafeFraction);
	TestEqual(TEXT("Action safe is 93 % of the frame's width"), ActionSafe.Width(), 1786);
	TestEqual(TEXT("Action safe is 93 % of the frame's height"), ActionSafe.Height(), 747);
	TestEqual(TEXT("Action safe is centred across"), ActionSafe.Min.X, 67);
	TestEqual(TEXT("Action safe is centred up and down"), ActionSafe.Min.Y - Frame.Min.Y, 28);
	TestEqual(TEXT("Title safe is 90 % of the frame's width"), TitleSafe.Width(), 1728);
	TestTrue(TEXT("Title safe is inside action safe"), TitleSafe.Min.X > ActionSafe.Min.X && TitleSafe.Min.Y > ActionSafe.Min.Y && TitleSafe.Max.X < ActionSafe.Max.X && TitleSafe.Max.Y < ActionSafe.Max.Y);

	// The data readout.
	FRoscCameraData Data;
	Data.CameraName = TEXT("A Cam");
	Data.FocalLengthMm = 35.0;
	Data.TStop = 2.8;
	Data.FocusDistanceMm = 3050.0;
	TestEqual(TEXT("A camera with no package shows its lens and focus"), FormatDataReadout(Data, ERoscDistanceUnits::Metric).ToString(), FString(TEXT("A Cam   35 mm   T2.8   Focus 3.05 m")));
	Data.bHasExposure = true;
	Data.Iso = 800;
	Data.ShutterAngleDeg = 180.0;
	Data.FramesPerSecond = 23.976;
	TestEqual(TEXT("A camera package adds ISO, shutter angle and frame rate"), FormatDataReadout(Data, ERoscDistanceUnits::Metric).ToString(),
		FString(TEXT("A Cam   35 mm   T2.8   ISO 800   180°   23.976 fps   Focus 3.05 m")));
	Data.NdDensity = 0.6;
	Data.ShutterAngleDeg = 172.8;
	TestEqual(TEXT("An ND filter is shown when one is on, and distances follow the user's units"), FormatDataReadout(Data, ERoscDistanceUnits::Imperial).ToString(),
		FString(TEXT("A Cam   35 mm   T2.8   ISO 800   172.8°   23.976 fps   ND 0.6   Focus 10' 0\"")));

	// Every line colour has a name, so the choice is not made by colour alone.
	const UEnum* Colours = StaticEnum<ERoscMonitorColour>();
	TSet<FString> ColourNames;
	for (int32 Index = 0; Index < Colours->NumEnums() - 1; ++Index)
	{
		ColourNames.Add(GetColourName(static_cast<ERoscMonitorColour>(Colours->GetValueByIndex(Index))).ToString());
	}
	TestEqual(TEXT("Each colour has its own name"), ColourNames.Num(), Colours->NumEnums() - 1);

	// Delivery aspects.
	const TConstArrayView<FRoscDeliveryAspect> Aspects = Rosc::GetDeliveryAspects();
	TestEqual(TEXT("The first delivery frame offered is the whole recording format"), Aspects[0].Aspect, 0.0f);
	TestEqual(TEXT("An offered aspect is called by its name"), Rosc::GetDeliveryAspectName(2.39f).ToString(), FString(TEXT("2.39:1 Scope")));
	TestEqual(TEXT("16:9 is found from its ratio"), Rosc::GetDeliveryAspectName(1.7778f).ToString(), FString(TEXT("16:9")));
	TestEqual(TEXT("Any other aspect is written as a ratio"), Rosc::GetDeliveryAspectName(2.2f).ToString(), FString(TEXT("2.2:1")));

	// Something to draw only when a tool is on.
	FRoscMonitorSettings Settings;
	TestEqual(TEXT("A legend gives the top false colour band as ending at 100 %"), GetFalseColourRange(GetFalseColourBands().Last()).ToString(), FString(TEXT("99-100 %")));
	TestEqual(TEXT("A legend gives the bottom false colour band from 0 %"), GetFalseColourRange(GetFalseColourBands()[0]).ToString(), FString(TEXT("0-2 %")));
	TestFalse(TEXT("With every tool off there is nothing to draw"), Settings.IsAnyToolOn());
	Settings.bCentreMark = true;
	TestTrue(TEXT("The centre mark alone is something to draw"), Settings.IsAnyToolOn());
	return true;
}

#endif
