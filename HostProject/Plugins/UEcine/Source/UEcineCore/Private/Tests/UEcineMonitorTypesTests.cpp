#include "Misc/AutomationTest.h"
#include "UEcineMonitorTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMonitorTypesTest, "UEcine.Core.Monitor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineMonitorTypesTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Monitor;

	// Signal level of a displayed pixel.
	TestEqual(TEXT("Black is 0 %"), GetSignalPercent(FColor(0, 0, 0)), 0.0f, 0.01f);
	TestEqual(TEXT("White is 100 %"), GetSignalPercent(FColor(255, 255, 255)), 100.0f, 0.01f);
	TestEqual(TEXT("Pure green carries 71.5 % of the signal"), GetSignalPercent(FColor(0, 255, 0)), 71.52f, 0.01f);
	TestEqual(TEXT("Pure blue carries 7.2 % of the signal"), GetSignalPercent(FColor(0, 0, 255)), 7.22f, 0.01f);

	// False colour bands: in order, not overlapping, each with a name and its own colour.
	const TConstArrayView<FUEcineFalseColourBand> Bands = GetFalseColourBands();
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
		const FUEcineScopeLayout Off = GetScopeLayout(FIntPoint(1920, 1080), false, false);
		TestTrue(TEXT("No scopes: both rectangles are empty"), Off.Waveform.Area() == 0 && Off.Histogram.Area() == 0);

		const FUEcineScopeLayout Both = GetScopeLayout(FIntPoint(1920, 1080), true, true);
		TestEqual(TEXT("1080p: scopes are a quarter of the picture height"), Both.Waveform.Height(), 270);
		TestEqual(TEXT("1080p: scopes are three to two"), Both.Waveform.Width(), 405);
		TestEqual(TEXT("1080p: waveform starts a margin in from the left"), Both.Waveform.Min.X, 16);
		TestEqual(TEXT("1080p: waveform ends a margin up from the bottom"), Both.Waveform.Max.Y, 1080 - 16);
		TestEqual(TEXT("1080p: histogram ends a margin in from the right"), Both.Histogram.Max.X, 1920 - 16);
		TestEqual(TEXT("1080p: both scopes are the same size"), Both.Histogram.Size(), Both.Waveform.Size());

		const FUEcineScopeLayout OnlyHistogram = GetScopeLayout(FIntPoint(1920, 1080), false, true);
		TestTrue(TEXT("Histogram alone: no waveform"), OnlyHistogram.Waveform.Area() == 0 && OnlyHistogram.Histogram.Area() > 0);

		// A narrow picture: the scopes shrink so they do not overlap.
		const FUEcineScopeLayout Narrow = GetScopeLayout(FIntPoint(400, 800), true, true);
		TestTrue(TEXT("Narrow picture: the scopes do not overlap"), Narrow.Waveform.Max.X < Narrow.Histogram.Min.X);
		TestTrue(TEXT("Narrow picture: the scopes stay inside the picture"), Narrow.Waveform.Min.X >= 0 && Narrow.Histogram.Max.X <= 400);

		// Too small for a scope to be readable: none is drawn.
		const FUEcineScopeLayout Tiny = GetScopeLayout(FIntPoint(120, 60), true, true);
		TestTrue(TEXT("Tiny picture: no scopes"), Tiny.Waveform.Area() == 0 && Tiny.Histogram.Area() == 0);
	}

	return true;
}

#endif
