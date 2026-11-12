// Copyright 2026 Fiavaion. All rights reserved.

#include "Misc/AutomationTest.h"
#include "UEcineExposure.h"
#include "UEcineOptics.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineOpticsFieldOfViewTest, "UEcine.Core.Optics.FieldOfView", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineOpticsFieldOfViewTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Optics;

	// A 50 mm lens on full frame (36 x 24 mm): the textbook 39.6 x 27.0 degree angle of view.
	TestEqual(TEXT("50mm on full frame, horizontal"), HorizontalFieldOfViewDeg(36.0, 50.0), 39.598, 0.001);
	TestEqual(TEXT("50mm on full frame, vertical"), VerticalFieldOfViewDeg(24.0, 50.0), 26.991, 0.001);

	TestEqual(TEXT("The focal length for an angle of view is the lens that gives it"), FocalLengthForFieldOfViewMm(36.0, HorizontalFieldOfViewDeg(36.0, 50.0)), 50.0, 1e-9);
	TestEqual(TEXT("And through a 2x anamorphic lens"), FocalLengthForFieldOfViewMm(20.22, HorizontalFieldOfViewDeg(20.22, 50.0, 2.0), 2.0), 50.0, 1e-9);

	// A 2x anamorphic lens sees twice the sensor width horizontally and the same height.
	TestEqual(TEXT("50mm 2x anamorphic matches a spherical lens on a sensor twice as wide"),
		HorizontalFieldOfViewDeg(20.22, 50.0, 2.0), HorizontalFieldOfViewDeg(40.44, 50.0), 1e-9);

	// 35 mm on Super 35 (24.89 mm wide) frames like a 50.6 mm on full frame.
	TestEqual(TEXT("35mm on Super 35 as a full-frame equivalent"), EquivalentFocalLengthMm(35.0, 24.89), 50.623, 0.001);
	TestEqual(TEXT("The equivalent focal length gives the same angle of view on the reference sensor"),
		HorizontalFieldOfViewDeg(36.0, EquivalentFocalLengthMm(35.0, 24.89)), HorizontalFieldOfViewDeg(24.89, 35.0), 1e-9);

	// At 5 m a 50 mm on full frame sees 3.6 m x 2.4 m.
	const FVector2d Field = FieldSizeAtDistanceMm(36.0, 24.0, 50.0, 1.0, 5000.0);
	TestEqual(TEXT("Field width at 5 m"), Field.X, 3600.0, 1e-6);
	TestEqual(TEXT("Field height at 5 m"), Field.Y, 2400.0, 1e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineOpticsDepthOfFieldTest, "UEcine.Core.Optics.DepthOfField", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineOpticsDepthOfFieldTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Optics;

	TestEqual(TEXT("Full-frame circle of confusion is diagonal / 1500"), DefaultCircleOfConfusionMm(36.0, 24.0), 0.028844, 1e-6);

	// 50 mm at f/2.8 with a 0.03 mm circle of confusion: hyperfocal 29.81 m.
	const double Hyperfocal = HyperfocalDistanceMm(50.0, 2.8, 0.03);
	TestEqual(TEXT("Hyperfocal distance"), Hyperfocal, 29811.905, 0.001);

	// Focused at 3 m the sharp zone runs from 2.73 m to 3.33 m.
	const FDepthOfField AtThreeMetres = DepthOfField(50.0, 2.8, 0.03, 3000.0);
	TestEqual(TEXT("Near limit at 3 m"), AtThreeMetres.NearMm, 2729.456, 0.001);
	TestEqual(TEXT("Far limit at 3 m"), AtThreeMetres.FarMm, 3330.077, 0.001);
	TestFalse(TEXT("Far limit at 3 m is finite"), AtThreeMetres.bFarIsInfinite);

	// Focused at the hyperfocal distance, sharpness runs from half that distance to infinity.
	const FDepthOfField AtHyperfocal = DepthOfField(50.0, 2.8, 0.03, Hyperfocal);
	TestTrue(TEXT("Far limit at the hyperfocal distance is infinite"), AtHyperfocal.bFarIsInfinite);
	TestEqual(TEXT("Near limit at the hyperfocal distance is half of it"), AtHyperfocal.NearMm, Hyperfocal / 2.0, 0.001);

	// Stopping down deepens the sharp zone on both sides.
	const FDepthOfField StoppedDown = DepthOfField(50.0, 8.0, 0.03, 3000.0);
	TestTrue(TEXT("Stopping down brings the near limit closer"), StoppedDown.NearMm < AtThreeMetres.NearMm);
	TestTrue(TEXT("Stopping down pushes the far limit away"), StoppedDown.FarMm > AtThreeMetres.FarMm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineExposureTest, "UEcine.Core.Exposure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineExposureTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Exposure;

	TestEqual(TEXT("180 degrees at 24 fps is 1/48 s"), ShutterTimeSeconds(180.0, 24.0), 1.0 / 48.0, 1e-12);
	TestEqual(TEXT("172.8 degrees at 24 fps is 1/50 s"), ShutterTimeSeconds(172.8, 24.0), 1.0 / 50.0, 1e-12);
	TestEqual(TEXT("A 180 degree shutter is open for half the frame"), ShutterOpenFraction(180.0), 0.5, 1e-12);

	TestEqual(TEXT("ND 0.3 is one stop"), NdStops(0.3), 0.99658, 1e-5);
	TestEqual(TEXT("ND 0.9 is three stops"), NdStops(0.9), 2.98974, 1e-5);
	TestEqual(TEXT("No filter loses no light"), NdStops(0.0), 0.0, 1e-12);

	// The "sunny 16" rule: f/16 at 1/125 s and ISO 100 is EV 15.
	TestEqual(TEXT("Sunny 16"), EV100(16.0, 1.0 / 125.0, 100.0), 14.966, 0.001);
	// Doubling the ISO suits a scene one stop darker.
	TestEqual(TEXT("Doubling ISO lowers EV100 by one"), EV100(16.0, 1.0 / 125.0, 200.0), 13.966, 0.001);
	// Opening one stop (f/16 to f/11.3) also suits a scene one stop darker.
	TestEqual(TEXT("Opening the iris a stop lowers EV100 by one"), EV100(16.0 / FMath::Sqrt(2.0), 1.0 / 125.0, 100.0), 13.966, 0.001);

	// A T1.9 lens that closes to T22: the widest aperture leads, then third stops above it.
	const TArray<double> Ladder = TStopLadder(1.9, 22.0);
	TestEqual(TEXT("The ladder starts at the widest aperture"), Ladder[0], 1.9);
	TestEqual(TEXT("then the next third stop"), Ladder[1], 2.0);
	TestEqual(TEXT("and ends at the minimum aperture"), Ladder.Last(), 22.0);
	TestEqual(TEXT("A T2 lens does not list T2 twice"), TStopLadder(2.0, 16.0)[1], 2.2);

	TestEqual(TEXT("Stepping up from a ladder value"), StepOnLadder(Ladder, 2.8, 1).Get(0.0), 3.2);
	TestEqual(TEXT("Stepping down from a ladder value"), StepOnLadder(Ladder, 2.8, -1).Get(0.0), 2.5);
	TestEqual(TEXT("Stepping up from an off-ladder value goes to the next mark"), StepOnLadder(Ladder, 3.0, 1).Get(0.0), 3.2);
	TestEqual(TEXT("Stepping down from an off-ladder value goes to the previous mark"), StepOnLadder(Ladder, 3.0, -1).Get(0.0), 2.8);
	TestFalse(TEXT("There is nothing wider than the widest aperture"), StepOnLadder(Ladder, 1.9, -1).IsSet());
	TestFalse(TEXT("There is nothing smaller than the minimum aperture"), StepOnLadder(Ladder, 22.0, 1).IsSet());
	// A float round trip (2.8f) must still count as being on the 2.8 mark.
	TestEqual(TEXT("A value within rounding of a mark steps from that mark"), StepOnLadder(Ladder, 2.8f, 1).Get(0.0), 3.2);

	// Neighbouring frame rates differ by a tenth of a percent and must stay separate steps.
	TestEqual(TEXT("24 fps steps down to 23.976"), StepOnLadder(StandardFrameRates(), 24.0f, -1).Get(0.0), 23.976);
	TestEqual(TEXT("23.976 fps steps up to 24"), StepOnLadder(StandardFrameRates(), 23.976f, 1).Get(0.0), 24.0);
	TestFalse(TEXT("23.976 and 24 are different settings"), IsSameSetting(23.976, 24.0));

	return true;
}

#endif
