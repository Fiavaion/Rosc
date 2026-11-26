// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscMove.h"

#define LOCTEXT_NAMESPACE "RoscCore"

namespace
{
	/** An ease shorter than this share of the move counts as no ease at all. */
	constexpr double MinEaseFraction = 1.e-6;

	/**
	 * Steady speed of the move as a multiple of its average speed. Easing spends time below the
	 * steady speed, so the steady part has to run faster to arrive on time.
	 */
	double SteadySpeedFactor(const FRoscMoveTiming& Normalized)
	{
		return 1.0 / (1.0 - 0.5 * (Normalized.EaseInFraction + Normalized.EaseOutFraction));
	}

	FText FormatSeconds(double Seconds)
	{
		// Round up so the suggested duration is always enough.
		return FText::Format(LOCTEXT("Seconds", "{0} s"), Rosc::Units::FormatNumber(FMath::CeilToDouble(Seconds * 10.0) / 10.0, 1));
	}
}

FRoscMoveTiming FRoscMoveTiming::Normalized() const
{
	FRoscMoveTiming Result;
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

double Rosc::Move::Progress(const FRoscMoveTiming& Timing, double TimeSeconds)
{
	const FRoscMoveTiming T = Timing.Normalized();
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

FRoscRigPose Rosc::Move::Evaluate(const FRoscRigPose& Start, const FRoscRigPose& End, const FRoscMoveTiming& Timing, double TimeSeconds)
{
	const double Alpha = Progress(Timing, TimeSeconds);

	FRoscRigPose Pose;
	for (int32 AxisIndex = 0; AxisIndex < RoscRigAxisCount; ++AxisIndex)
	{
		const ERoscRigAxis Axis = static_cast<ERoscRigAxis>(AxisIndex);
		Pose.Set(Axis, FMath::Lerp(Start.Get(Axis), End.Get(Axis), Alpha));
	}
	return Pose;
}

double Rosc::Move::GetTotalDuration(TConstArrayView<FRoscRigMark> Marks)
{
	double Total = 0.0;
	for (int32 Index = 1; Index < Marks.Num(); ++Index)
	{
		Total += Marks[Index].Arrival.Normalized().DurationSeconds;
	}
	return Total;
}

FRoscRigPose Rosc::Move::EvaluateMarks(TConstArrayView<FRoscRigMark> Marks, double TimeSeconds)
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

double Rosc::Move::PeakSpeed(const FRoscMoveTiming& Timing, double Distance)
{
	const FRoscMoveTiming T = Timing.Normalized();
	return FMath::Abs(Distance) * SteadySpeedFactor(T) / T.DurationSeconds;
}

double Rosc::Move::PeakAcceleration(const FRoscMoveTiming& Timing, double Distance)
{
	if (FMath::IsNearlyZero(Distance))
	{
		return 0.0;
	}
	const FRoscMoveTiming T = Timing.Normalized();
	const double ShortestEase = FMath::Min(T.EaseInFraction, T.EaseOutFraction);
	if (ShortestEase < MinEaseFraction)
	{
		return TNumericLimits<double>::Max();
	}
	return PeakSpeed(T, Distance) / (ShortestEase * T.DurationSeconds);
}

TArray<FRoscMoveFinding> Rosc::Move::Check(const FRoscRigSpec& Spec, const FRoscRigPose& Start, const FRoscRigPose& End, const FRoscMoveTiming& Timing)
{
	const FRoscMoveTiming T = Timing.Normalized();
	const double ShortestEase = FMath::Min(T.EaseInFraction, T.EaseOutFraction);

	TArray<FRoscMoveFinding> Findings;
	const auto AddFinding = [&Findings](ERoscRigAxis Axis, ERoscMoveProblem Problem, double Value, double Limit, double MinimumDurationSeconds = 0.0)
	{
		FRoscMoveFinding& Finding = Findings.AddDefaulted_GetRef();
		Finding.Axis = Axis;
		Finding.Problem = Problem;
		Finding.Value = Value;
		Finding.Limit = Limit;
		Finding.MinimumDurationSeconds = MinimumDurationSeconds;
	};

	for (int32 AxisIndex = 0; AxisIndex < RoscRigAxisCount; ++AxisIndex)
	{
		const ERoscRigAxis Axis = static_cast<ERoscRigAxis>(AxisIndex);
		const FRoscAxisLimits& Limits = Spec.Axes[AxisIndex];
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
			AddFinding(Axis, ERoscMoveProblem::BelowRange, Lowest, Limits.Min);
		}
		if (Highest > Limits.Max + Tolerance)
		{
			AddFinding(Axis, ERoscMoveProblem::AboveRange, Highest, Limits.Max);
		}

		const double Distance = FMath::Abs(To - From);
		if (Distance <= Tolerance)
		{
			continue;
		}
		if (Limits.IsFixedDuringShot())
		{
			AddFinding(Axis, ERoscMoveProblem::FixedAxisMoved, To, From);
			continue;
		}

		const double SpeedPerUnitTime = Distance * SteadySpeedFactor(T);
		const double Speed = SpeedPerUnitTime / T.DurationSeconds;
		if (Speed > Limits.MaxSpeed * (1.0 + Tolerance))
		{
			AddFinding(Axis, ERoscMoveProblem::TooFast, Speed, Limits.MaxSpeed, SpeedPerUnitTime / Limits.MaxSpeed);
		}

		if (Limits.MaxAcceleration > 0.0)
		{
			if (ShortestEase < MinEaseFraction)
			{
				AddFinding(Axis, ERoscMoveProblem::TooAbrupt, TNumericLimits<double>::Max(), Limits.MaxAcceleration);
			}
			else
			{
				const double Acceleration = Speed / (ShortestEase * T.DurationSeconds);
				if (Acceleration > Limits.MaxAcceleration * (1.0 + Tolerance))
				{
					AddFinding(Axis, ERoscMoveProblem::TooAbrupt, Acceleration, Limits.MaxAcceleration,
						FMath::Sqrt(SpeedPerUnitTime / (ShortestEase * Limits.MaxAcceleration)));
				}
			}
		}
	}
	return Findings;
}

TArray<FRoscMoveFinding> Rosc::Move::MostLimiting(TConstArrayView<FRoscMoveFinding> Findings)
{
	TArray<FRoscMoveFinding> Kept;
	const FRoscMoveFinding* NeedsMostTime = nullptr;
	for (const FRoscMoveFinding& Finding : Findings)
	{
		if (Finding.MinimumDurationSeconds <= 0.0)
		{
			Kept.Add(Finding);
		}
		else if (NeedsMostTime == nullptr || Finding.MinimumDurationSeconds > NeedsMostTime->MinimumDurationSeconds)
		{
			NeedsMostTime = &Finding;
		}
	}
	if (NeedsMostTime != nullptr)
	{
		Kept.Add(*NeedsMostTime);
	}
	return Kept;
}

FText Rosc::Move::FormatRate(ERoscRigAxis Axis, double Value, ERoscDistanceUnits Units, bool bSquared)
{
	return FText::Format(
		bSquared ? LOCTEXT("PerSecondSquared", "{0}/s²") : LOCTEXT("PerSecond", "{0}/s"),
		Rosc::Rig::FormatAxisValue(Axis, Value, Units));
}

FText Rosc::Move::Describe(const FRoscMoveFinding& Finding, ERoscRigType RigType, ERoscDistanceUnits Units)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Axis"), Rosc::Rig::GetAxisName(RigType, Finding.Axis));
	Args.Add(TEXT("Rig"), Rosc::Rig::GetTypeName(RigType).ToLower());

	switch (Finding.Problem)
	{
	case ERoscMoveProblem::BelowRange:
	case ERoscMoveProblem::AboveRange:
		Args.Add(TEXT("Value"), Rosc::Rig::FormatAxisValue(Finding.Axis, Finding.Value, Units));
		Args.Add(TEXT("Limit"), Rosc::Rig::FormatAxisValue(Finding.Axis, Finding.Limit, Units));
		return FText::Format(Finding.Problem == ERoscMoveProblem::BelowRange
			? LOCTEXT("FindingBelowRange", "{Axis} goes to {Value}; this {Rig} stops at {Limit}. Reposition the {Rig} or choose a larger one.")
			: LOCTEXT("FindingAboveRange", "{Axis} goes to {Value}; this {Rig} reaches {Limit}. Reposition the {Rig} or choose a larger one."), Args);

	case ERoscMoveProblem::FixedAxisMoved:
		Args.Add(TEXT("From"), Rosc::Rig::FormatAxisValue(Finding.Axis, Finding.Limit, Units));
		Args.Add(TEXT("To"), Rosc::Rig::FormatAxisValue(Finding.Axis, Finding.Value, Units));
		return FText::Format(LOCTEXT("FindingFixedAxisMoved", "{Axis} changes from {From} to {To}, but on a {Rig} it is set before the shot and cannot move during it. Use the same value at both marks, or choose a support that can make this move."), Args);

	case ERoscMoveProblem::TooFast:
		Args.Add(TEXT("Value"), FormatRate(Finding.Axis, Finding.Value, Units, false));
		Args.Add(TEXT("Limit"), FormatRate(Finding.Axis, Finding.Limit, Units, false));
		Args.Add(TEXT("Duration"), FormatSeconds(Finding.MinimumDurationSeconds));
		return FText::Format(LOCTEXT("FindingTooFast", "{Axis} peaks at {Value}; this {Rig} manages {Limit}. Give the move at least {Duration}."), Args);

	case ERoscMoveProblem::TooAbrupt:
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

TArray<FRoscMoveKey> Rosc::Move::MakeKeys(double StartValue, double EndValue, const FRoscMoveTiming& Timing)
{
	const FRoscMoveTiming T = Timing.Normalized();
	const double Distance = EndValue - StartValue;
	const double SteadySlope = Distance * SteadySpeedFactor(T) / T.DurationSeconds;

	const bool bHasEaseIn = T.EaseInFraction >= MinEaseFraction;
	const bool bHasEaseOut = T.EaseOutFraction >= MinEaseFraction;
	const double EaseInEnd = T.EaseInFraction;
	const double EaseOutStart = 1.0 - T.EaseOutFraction;

	TArray<FRoscMoveKey> Keys;
	const auto AddKey = [&](double U, double Slope)
	{
		FRoscMoveKey& Key = Keys.AddDefaulted_GetRef();
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

TArray<FRoscMoveKey> Rosc::Move::MakeKeysForMarks(TConstArrayView<FRoscRigMark> Marks, ERoscRigAxis Axis)
{
	check(!Marks.IsEmpty());

	TArray<FRoscMoveKey> Keys;
	FRoscMoveKey& First = Keys.AddDefaulted_GetRef();
	First.Value = Marks[0].Pose.Get(Axis);

	double MoveStart = 0.0;
	for (int32 Index = 1; Index < Marks.Num(); ++Index)
	{
		const TArray<FRoscMoveKey> MoveKeys = MakeKeys(Marks[Index - 1].Pose.Get(Axis), Marks[Index].Pose.Get(Axis), Marks[Index].Arrival);

		// The move's first key lands on the key that ends the move before it: one key, leaving at this move's slope.
		Keys.Last().LeaveSlopePerSecond = MoveKeys[0].LeaveSlopePerSecond;
		for (int32 KeyIndex = 1; KeyIndex < MoveKeys.Num(); ++KeyIndex)
		{
			FRoscMoveKey& Key = Keys.Add_GetRef(MoveKeys[KeyIndex]);
			Key.TimeSeconds += MoveStart;
		}
		MoveStart += Marks[Index].Arrival.Normalized().DurationSeconds;

		// At rest on the mark until the next move says otherwise.
		Keys.Last().LeaveSlopePerSecond = 0.0;
	}
	return Keys;
}

#undef LOCTEXT_NAMESPACE
