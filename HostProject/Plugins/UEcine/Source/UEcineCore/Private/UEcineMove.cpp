#include "UEcineMove.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

namespace
{
	/** An ease shorter than this share of the move counts as no ease at all. */
	constexpr double MinEaseFraction = 1.e-6;

	/**
	 * Steady speed of the move as a multiple of its average speed. Easing spends time below the
	 * steady speed, so the steady part has to run faster to arrive on time.
	 */
	double SteadySpeedFactor(const FUEcineMoveTiming& Normalized)
	{
		return 1.0 / (1.0 - 0.5 * (Normalized.EaseInFraction + Normalized.EaseOutFraction));
	}

	/** "1.8 m/s" or "40°/s"; with bSquared, "/s²". */
	FText FormatRate(EUEcineRigAxis Axis, double Value, EUEcineDistanceUnits Units, bool bSquared)
	{
		return FText::Format(
			bSquared ? LOCTEXT("PerSecondSquared", "{0}/s²") : LOCTEXT("PerSecond", "{0}/s"),
			UEcine::Rig::FormatAxisValue(Axis, Value, Units));
	}

	FText FormatSeconds(double Seconds)
	{
		// Round up so the suggested duration is always enough.
		return FText::Format(LOCTEXT("Seconds", "{0} s"), UEcine::Units::FormatNumber(FMath::CeilToDouble(Seconds * 10.0) / 10.0, 1));
	}
}

FUEcineMoveTiming FUEcineMoveTiming::Normalized() const
{
	FUEcineMoveTiming Result;
	Result.DurationSeconds = FMath::Max(DurationSeconds, 0.001);
	Result.EaseInFraction = FMath::Clamp(EaseInFraction, 0.0, 1.0);
	Result.EaseOutFraction = FMath::Clamp(EaseOutFraction, 0.0, 1.0);

	const double EaseTotal = Result.EaseInFraction + Result.EaseOutFraction;
	if (EaseTotal > 1.0)
	{
		Result.EaseInFraction /= EaseTotal;
		Result.EaseOutFraction /= EaseTotal;
	}
	return Result;
}

double UEcine::Move::Progress(const FUEcineMoveTiming& Timing, double TimeSeconds)
{
	const FUEcineMoveTiming T = Timing.Normalized();
	const double U = FMath::Clamp(TimeSeconds / T.DurationSeconds, 0.0, 1.0);
	const double Speed = SteadySpeedFactor(T);

	if (U < T.EaseInFraction)
	{
		return Speed * U * U / (2.0 * T.EaseInFraction);
	}
	if (U <= 1.0 - T.EaseOutFraction)
	{
		return Speed * (U - 0.5 * T.EaseInFraction);
	}
	const double Remaining = 1.0 - U;
	return 1.0 - Speed * Remaining * Remaining / (2.0 * T.EaseOutFraction);
}

FUEcineRigPose UEcine::Move::Evaluate(const FUEcineRigPose& Start, const FUEcineRigPose& End, const FUEcineMoveTiming& Timing, double TimeSeconds)
{
	const double Alpha = Progress(Timing, TimeSeconds);

	FUEcineRigPose Pose;
	for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
	{
		const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
		Pose.Set(Axis, FMath::Lerp(Start.Get(Axis), End.Get(Axis), Alpha));
	}
	return Pose;
}

double UEcine::Move::GetTotalDuration(TConstArrayView<FUEcineRigMark> Marks)
{
	double Total = 0.0;
	for (int32 Index = 1; Index < Marks.Num(); ++Index)
	{
		Total += Marks[Index].Arrival.Normalized().DurationSeconds;
	}
	return Total;
}

FUEcineRigPose UEcine::Move::EvaluateMarks(TConstArrayView<FUEcineRigMark> Marks, double TimeSeconds)
{
	check(!Marks.IsEmpty());

	double MoveStart = 0.0;
	for (int32 Index = 1; Index < Marks.Num(); ++Index)
	{
		const double Duration = Marks[Index].Arrival.Normalized().DurationSeconds;
		if (TimeSeconds < MoveStart + Duration)
		{
			return Evaluate(Marks[Index - 1].Pose, Marks[Index].Pose, Marks[Index].Arrival, TimeSeconds - MoveStart);
		}
		MoveStart += Duration;
	}
	return Marks.Last().Pose;
}

double UEcine::Move::PeakSpeed(const FUEcineMoveTiming& Timing, double Distance)
{
	const FUEcineMoveTiming T = Timing.Normalized();
	return FMath::Abs(Distance) * SteadySpeedFactor(T) / T.DurationSeconds;
}

double UEcine::Move::PeakAcceleration(const FUEcineMoveTiming& Timing, double Distance)
{
	if (FMath::IsNearlyZero(Distance))
	{
		return 0.0;
	}
	const FUEcineMoveTiming T = Timing.Normalized();
	const double ShortestEase = FMath::Min(T.EaseInFraction, T.EaseOutFraction);
	if (ShortestEase < MinEaseFraction)
	{
		return TNumericLimits<double>::Max();
	}
	return PeakSpeed(T, Distance) / (ShortestEase * T.DurationSeconds);
}

TArray<FUEcineMoveFinding> UEcine::Move::Check(const FUEcineRigSpec& Spec, const FUEcineRigPose& Start, const FUEcineRigPose& End, const FUEcineMoveTiming& Timing)
{
	const FUEcineMoveTiming T = Timing.Normalized();
	const double ShortestEase = FMath::Min(T.EaseInFraction, T.EaseOutFraction);

	TArray<FUEcineMoveFinding> Findings;
	const auto AddFinding = [&Findings](EUEcineRigAxis Axis, EUEcineMoveProblem Problem, double Value, double Limit, double MinimumDurationSeconds = 0.0)
	{
		FUEcineMoveFinding& Finding = Findings.AddDefaulted_GetRef();
		Finding.Axis = Axis;
		Finding.Problem = Problem;
		Finding.Value = Value;
		Finding.Limit = Limit;
		Finding.MinimumDurationSeconds = MinimumDurationSeconds;
	};

	for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
	{
		const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
		const FUEcineAxisLimits& Limits = Spec.Axes[AxisIndex];
		if (!Limits.bUsed)
		{
			continue;
		}

		const double From = Start.Get(Axis);
		const double To = End.Get(Axis);
		const double Lowest = FMath::Min(From, To);
		const double Highest = FMath::Max(From, To);
		const double Tolerance = 1.e-4;

		if (Lowest < Limits.Min - Tolerance)
		{
			AddFinding(Axis, EUEcineMoveProblem::BelowRange, Lowest, Limits.Min);
		}
		if (Highest > Limits.Max + Tolerance)
		{
			AddFinding(Axis, EUEcineMoveProblem::AboveRange, Highest, Limits.Max);
		}

		const double Distance = FMath::Abs(To - From);
		if (Distance <= Tolerance)
		{
			continue;
		}
		if (Limits.IsFixedDuringShot())
		{
			AddFinding(Axis, EUEcineMoveProblem::FixedAxisMoved, To, From);
			continue;
		}

		const double SpeedPerUnitTime = Distance * SteadySpeedFactor(T);
		const double Speed = SpeedPerUnitTime / T.DurationSeconds;
		if (Speed > Limits.MaxSpeed * (1.0 + Tolerance))
		{
			AddFinding(Axis, EUEcineMoveProblem::TooFast, Speed, Limits.MaxSpeed, SpeedPerUnitTime / Limits.MaxSpeed);
		}

		if (Limits.MaxAcceleration > 0.0)
		{
			if (ShortestEase < MinEaseFraction)
			{
				AddFinding(Axis, EUEcineMoveProblem::TooAbrupt, TNumericLimits<double>::Max(), Limits.MaxAcceleration);
			}
			else
			{
				const double Acceleration = Speed / (ShortestEase * T.DurationSeconds);
				if (Acceleration > Limits.MaxAcceleration * (1.0 + Tolerance))
				{
					AddFinding(Axis, EUEcineMoveProblem::TooAbrupt, Acceleration, Limits.MaxAcceleration,
						FMath::Sqrt(SpeedPerUnitTime / (ShortestEase * Limits.MaxAcceleration)));
				}
			}
		}
	}
	return Findings;
}

FText UEcine::Move::Describe(const FUEcineMoveFinding& Finding, EUEcineRigType RigType, EUEcineDistanceUnits Units)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Axis"), UEcine::Rig::GetAxisName(RigType, Finding.Axis));
	Args.Add(TEXT("Rig"), UEcine::Rig::GetTypeName(RigType).ToLower());

	switch (Finding.Problem)
	{
	case EUEcineMoveProblem::BelowRange:
	case EUEcineMoveProblem::AboveRange:
		Args.Add(TEXT("Value"), UEcine::Rig::FormatAxisValue(Finding.Axis, Finding.Value, Units));
		Args.Add(TEXT("Limit"), UEcine::Rig::FormatAxisValue(Finding.Axis, Finding.Limit, Units));
		return FText::Format(Finding.Problem == EUEcineMoveProblem::BelowRange
			? LOCTEXT("FindingBelowRange", "{Axis} goes to {Value}; this {Rig} stops at {Limit}. Reposition the {Rig} or choose a larger one.")
			: LOCTEXT("FindingAboveRange", "{Axis} goes to {Value}; this {Rig} reaches {Limit}. Reposition the {Rig} or choose a larger one."), Args);

	case EUEcineMoveProblem::FixedAxisMoved:
		Args.Add(TEXT("From"), UEcine::Rig::FormatAxisValue(Finding.Axis, Finding.Limit, Units));
		Args.Add(TEXT("To"), UEcine::Rig::FormatAxisValue(Finding.Axis, Finding.Value, Units));
		return FText::Format(LOCTEXT("FindingFixedAxisMoved", "{Axis} changes from {From} to {To}, but on a {Rig} it is set before the shot and cannot move during it. Use the same value at both marks, or choose a support that can make this move."), Args);

	case EUEcineMoveProblem::TooFast:
		Args.Add(TEXT("Value"), FormatRate(Finding.Axis, Finding.Value, Units, false));
		Args.Add(TEXT("Limit"), FormatRate(Finding.Axis, Finding.Limit, Units, false));
		Args.Add(TEXT("Duration"), FormatSeconds(Finding.MinimumDurationSeconds));
		return FText::Format(LOCTEXT("FindingTooFast", "{Axis} peaks at {Value}; this {Rig} manages {Limit}. Give the move at least {Duration}."), Args);

	case EUEcineMoveProblem::TooAbrupt:
		Args.Add(TEXT("Limit"), FormatRate(Finding.Axis, Finding.Limit, Units, true));
		if (Finding.MinimumDurationSeconds <= 0.0)
		{
			return FText::Format(LOCTEXT("FindingInstant", "{Axis} starts or stops instantly; this {Rig} can change speed by {Limit} at most. Add an ease in and an ease out."), Args);
		}
		Args.Add(TEXT("Value"), FormatRate(Finding.Axis, Finding.Value, Units, true));
		Args.Add(TEXT("Duration"), FormatSeconds(Finding.MinimumDurationSeconds));
		return FText::Format(LOCTEXT("FindingTooAbrupt", "{Axis} changes speed by {Value}; this {Rig} manages {Limit}. Give the move at least {Duration}, or lengthen the ease in and ease out."), Args);
	}
	checkNoEntry();
	return FText::GetEmpty();
}

TArray<FUEcineMoveKey> UEcine::Move::MakeKeys(double StartValue, double EndValue, const FUEcineMoveTiming& Timing)
{
	const FUEcineMoveTiming T = Timing.Normalized();
	const double Distance = EndValue - StartValue;
	const double SteadySlope = Distance * SteadySpeedFactor(T) / T.DurationSeconds;

	const bool bHasEaseIn = T.EaseInFraction >= MinEaseFraction;
	const bool bHasEaseOut = T.EaseOutFraction >= MinEaseFraction;
	const double EaseInEnd = T.EaseInFraction;
	const double EaseOutStart = 1.0 - T.EaseOutFraction;

	TArray<FUEcineMoveKey> Keys;
	const auto AddKey = [&](double U, double Slope)
	{
		FUEcineMoveKey& Key = Keys.AddDefaulted_GetRef();
		Key.TimeSeconds = U * T.DurationSeconds;
		Key.Value = StartValue + Distance * Progress(T, Key.TimeSeconds);
		Key.ArriveSlopePerSecond = Slope;
		Key.LeaveSlopePerSecond = Slope;
	};

	AddKey(0.0, bHasEaseIn ? 0.0 : SteadySlope);
	if (bHasEaseIn && EaseInEnd < 1.0 - MinEaseFraction)
	{
		AddKey(EaseInEnd, SteadySlope);
	}
	// When the eases meet there is no steady part and one key serves as the end of both.
	if (bHasEaseOut && EaseOutStart > EaseInEnd + MinEaseFraction)
	{
		AddKey(EaseOutStart, SteadySlope);
	}
	AddKey(1.0, bHasEaseOut ? 0.0 : SteadySlope);
	return Keys;
}

TArray<FUEcineMoveKey> UEcine::Move::MakeKeysForMarks(TConstArrayView<FUEcineRigMark> Marks, EUEcineRigAxis Axis)
{
	check(!Marks.IsEmpty());

	TArray<FUEcineMoveKey> Keys;
	FUEcineMoveKey& First = Keys.AddDefaulted_GetRef();
	First.Value = Marks[0].Pose.Get(Axis);

	double MoveStart = 0.0;
	for (int32 Index = 1; Index < Marks.Num(); ++Index)
	{
		const TArray<FUEcineMoveKey> MoveKeys = MakeKeys(Marks[Index - 1].Pose.Get(Axis), Marks[Index].Pose.Get(Axis), Marks[Index].Arrival);

		// The move's first key lands on the key that ends the move before it: one key, leaving at this move's slope.
		Keys.Last().LeaveSlopePerSecond = MoveKeys[0].LeaveSlopePerSecond;
		for (int32 KeyIndex = 1; KeyIndex < MoveKeys.Num(); ++KeyIndex)
		{
			FUEcineMoveKey& Key = Keys.Add_GetRef(MoveKeys[KeyIndex]);
			Key.TimeSeconds += MoveStart;
		}
		MoveStart += Marks[Index].Arrival.Normalized().DurationSeconds;

		// At rest on the mark until the next move says otherwise.
		Keys.Last().LeaveSlopePerSecond = 0.0;
	}
	return Keys;
}

#undef LOCTEXT_NAMESPACE
