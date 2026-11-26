// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "RoscFocus.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscFocusRackTest, "Rosc.Core.Focus.Rack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscFocusRackTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::Focus;

	const FRoscMoveTiming Timing = MakeRackTiming(2.0);
	TestEqual(TEXT("A rack takes the time it is given"), Timing.DurationSeconds, 2.0, 1.e-9);
	TestEqual(TEXT("A quarter of a rack eases off the first mark"), Timing.EaseInFraction, 0.25, 1.e-9);
	TestEqual(TEXT("A quarter of a rack settles on the second mark"), Timing.EaseOutFraction, 0.25, 1.e-9);

	// From 1 m to 4 m is from 1 dioptre to 0.25 dioptres.
	TestEqual(TEXT("Before the rack the lens is on the first mark"), RackDistance(100.0, 400.0, Timing, -1.0), 100.0);
	TestEqual(TEXT("At the start the lens is on the first mark"), RackDistance(100.0, 400.0, Timing, 0.0), 100.0);
	TestEqual(TEXT("At the end the lens is exactly on the second mark"), RackDistance(100.0, 400.0, Timing, 2.0), 400.0);
	TestEqual(TEXT("After the rack the lens stays on the second mark"), RackDistance(100.0, 400.0, Timing, 5.0), 400.0);

	// Half way round the ring is half way in dioptres, 0.625, which is 1.6 m and not the 2.5 m half way in distance.
	TestEqual(TEXT("Half way through, the lens is half way in dioptres"), RackDistance(100.0, 400.0, Timing, 1.0), 160.0, 1.e-6);
	// A quarter of the way in time the wheel has only just got up to speed: a sixth of the turn, 0.875 dioptres.
	TestEqual(TEXT("At the end of the ease in, a sixth of the turn is done"), RackDistance(100.0, 400.0, Timing, 0.5), 100.0 / 0.875, 1.e-6);

	// The same rack backwards passes the same distances in reverse.
	TestEqual(TEXT("Racking back, half way is the same distance"), RackDistance(400.0, 100.0, Timing, 1.0), 160.0, 1.e-6);
	TestEqual(TEXT("Racking back mirrors racking out"), RackDistance(400.0, 100.0, Timing, 1.5), RackDistance(100.0, 400.0, Timing, 0.5), 1.e-6);

	// The lens never passes through a distance outside the two marks, and never turns back.
	double Previous = 50.0;
	for (int32 Step = 0; Step <= 48; ++Step)
	{
		const double Distance = RackDistance(50.0, 100000.0, MakeRackTiming(2.0), Step / 24.0);
		TestTrue(*FString::Printf(TEXT("Frame %d of a rack to the far distance keeps moving away, within the marks"), Step), Distance >= Previous && Distance <= 100000.0);
		Previous = Distance;
	}

	// A rack between a mark and itself holds still.
	TestEqual(TEXT("A rack to the same distance holds still"), RackDistance(250.0, 250.0, Timing, 0.7), 250.0, 1.e-9);
	return true;
}

#endif
