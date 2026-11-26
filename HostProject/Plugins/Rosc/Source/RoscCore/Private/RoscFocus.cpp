// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscFocus.h"

FRoscMoveTiming Rosc::Focus::MakeRackTiming(double DurationSeconds)
{
	FRoscMoveTiming Timing;
	Timing.DurationSeconds = DurationSeconds;
	return Timing.Normalized();
}

double Rosc::Focus::RackDistance(double FromDistance, double ToDistance, const FRoscMoveTiming& Timing, double TimeSeconds)
{
	const double Progress = Rosc::Move::Progress(Timing, TimeSeconds);
	// Exact at both ends, whatever rounding the division does in between.
	if (Progress <= 0.0)
	{
		return FromDistance;
	}
	if (Progress >= 1.0)
	{
		return ToDistance;
	}
	return 1.0 / FMath::Lerp(1.0 / FromDistance, 1.0 / ToDistance, Progress);
}
