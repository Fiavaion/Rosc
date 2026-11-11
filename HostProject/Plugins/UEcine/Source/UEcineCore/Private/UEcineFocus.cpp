#include "UEcineFocus.h"

FUEcineMoveTiming UEcine::Focus::MakeRackTiming(double DurationSeconds)
{
	FUEcineMoveTiming Timing;
	Timing.DurationSeconds = DurationSeconds;
	return Timing.Normalized();
}

double UEcine::Focus::RackDistance(double FromDistance, double ToDistance, const FUEcineMoveTiming& Timing, double TimeSeconds)
{
	const double Progress = UEcine::Move::Progress(Timing, TimeSeconds);
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
