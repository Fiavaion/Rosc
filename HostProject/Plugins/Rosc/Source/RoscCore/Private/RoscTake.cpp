// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscTake.h"

#define LOCTEXT_NAMESPACE "RoscTake"

namespace
{
	/** Below this many frames the smoothing changes nothing worth the work. */
	constexpr double MinimumSigmaSamples = 0.3;

	double GetSmoothingBaseSeconds(ERoscTakeSmoothing Smoothing)
	{
		switch (Smoothing)
		{
		case ERoscTakeSmoothing::Light: return 0.02;
		case ERoscTakeSmoothing::Medium: return 0.04;
		case ERoscTakeSmoothing::Heavy: return 0.08;
		default: return 0.0;
		}
	}

	bool IsLocked(const FRoscTakeProcessing& Processing, ERoscRigAxis Axis)
	{
		switch (Axis)
		{
		case ERoscRigAxis::Pan: return Processing.bLockPan;
		case ERoscRigAxis::Tilt: return Processing.bLockTilt;
		case ERoscRigAxis::Roll: return Processing.bLockRoll;
		case ERoscRigAxis::Travel:
		case ERoscRigAxis::Traverse:
		case ERoscRigAxis::Lift: return Processing.bLockPosition;
		default: return false;
		}
	}

	/** Shifts each value by whole turns so it lies within half a turn of the one before. */
	void Unwrap(TArray<double>& Values)
	{
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			Values[Index] -= 360.0 * FMath::RoundToDouble((Values[Index] - Values[Index - 1]) / 360.0);
		}
	}

	/** Gaussian smoothing with point-reflected ends, so the ends hold and a straight ramp is unchanged. */
	void Smooth(TArray<double>& Values, double SigmaSamples)
	{
		const int32 Count = Values.Num();
		const int32 Radius = FMath::CeilToInt32(3.0 * SigmaSamples);

		TArray<double> Weights;
		Weights.SetNum(Radius + 1);
		double Sum = 0.0;
		for (int32 Offset = 0; Offset <= Radius; ++Offset)
		{
			Weights[Offset] = FMath::Exp(-(double(Offset) * Offset) / (2.0 * SigmaSamples * SigmaSamples));
			Sum += Offset == 0 ? Weights[Offset] : 2.0 * Weights[Offset];
		}
		for (double& Weight : Weights)
		{
			Weight /= Sum;
		}

		const TArray<double> Source = Values;
		auto Sample = [&Source, Count](int32 Index)
		{
			if (Index < 0)
			{
				return 2.0 * Source[0] - Source[FMath::Min(-Index, Count - 1)];
			}
			if (Index >= Count)
			{
				return 2.0 * Source[Count - 1] - Source[FMath::Max(2 * (Count - 1) - Index, 0)];
			}
			return Source[Index];
		};

		for (int32 Index = 0; Index < Count; ++Index)
		{
			double Smoothed = Weights[0] * Source[Index];
			for (int32 Offset = 1; Offset <= Radius; ++Offset)
			{
				Smoothed += Weights[Offset] * (Sample(Index - Offset) + Sample(Index + Offset));
			}
			Values[Index] = Smoothed;
		}
	}
}

double Rosc::Take::GetSmoothingSeconds(const FRoscTakeProcessing& Processing)
{
	const double Base = GetSmoothingBaseSeconds(Processing.Smoothing);
	const double Extra = 0.25 * FMath::Clamp(Processing.Stabilise, 0.0, 1.0);
	return FMath::Sqrt(Base * Base + Extra * Extra);
}

TArray<FRoscRigPose> Rosc::Take::Process(const FRoscTake& Take, const FRoscTakeProcessing& Processing)
{
	if (Take.Poses.IsEmpty() || Take.FramesPerSecond <= 0.0)
	{
		return Take.Poses;
	}

	const double SigmaSamples = GetSmoothingSeconds(Processing) * Take.FramesPerSecond;
	TArray<FRoscRigPose> Result = Take.Poses;
	TArray<double> Values;
	Values.SetNum(Result.Num());

	for (int32 AxisIndex = 0; AxisIndex < RoscRigAxisCount; ++AxisIndex)
	{
		const ERoscRigAxis Axis = static_cast<ERoscRigAxis>(AxisIndex);
		for (int32 Frame = 0; Frame < Result.Num(); ++Frame)
		{
			Values[Frame] = Take.Poses[Frame].Get(Axis);
		}

		if (!Rosc::Rig::IsDistanceAxis(Axis))
		{
			Unwrap(Values);
		}

		if (IsLocked(Processing, Axis))
		{
			const double Held = Values[0];
			Values.Init(Held, Values.Num());
		}
		else if (SigmaSamples >= MinimumSigmaSamples)
		{
			Smooth(Values, SigmaSamples);
		}

		for (int32 Frame = 0; Frame < Result.Num(); ++Frame)
		{
			Result[Frame].Set(Axis, Values[Frame]);
		}
	}
	return Result;
}

TArray<FRoscMoveFinding> Rosc::Take::Check(const FRoscRigSpec& Spec, TConstArrayView<FRoscRigPose> Poses, double FramesPerSecond)
{
	TArray<FRoscMoveFinding> Findings;
	const int32 Count = Poses.Num();
	if (Count < 2 || FramesPerSecond <= 0.0)
	{
		return Findings;
	}

	const double Step = 1.0 / FramesPerSecond;
	auto Add = [&Findings](ERoscRigAxis Axis, ERoscMoveProblem Problem, double Value, double Limit)
	{
		FRoscMoveFinding Finding;
		Finding.Axis = Axis;
		Finding.Problem = Problem;
		Finding.Value = Value;
		Finding.Limit = Limit;
		Findings.Add(Finding);
	};

	for (int32 AxisIndex = 0; AxisIndex < RoscRigAxisCount; ++AxisIndex)
	{
		const ERoscRigAxis Axis = static_cast<ERoscRigAxis>(AxisIndex);
		const FRoscAxisLimits& Limits = Spec.GetAxis(Axis);
		if (!Limits.bUsed)
		{
			continue;
		}

		const double First = Poses[0].Get(Axis);
		double Lowest = First;
		double Highest = First;
		double Furthest = First;
		for (const FRoscRigPose& Pose : Poses)
		{
			const double Value = Pose.Get(Axis);
			Lowest = FMath::Min(Lowest, Value);
			Highest = FMath::Max(Highest, Value);
			if (FMath::Abs(Value - First) > FMath::Abs(Furthest - First))
			{
				Furthest = Value;
			}
		}

		if (Lowest < Limits.Min)
		{
			Add(Axis, ERoscMoveProblem::BelowRange, Lowest, Limits.Min);
		}
		if (Highest > Limits.Max)
		{
			Add(Axis, ERoscMoveProblem::AboveRange, Highest, Limits.Max);
		}

		if (Limits.IsFixedDuringShot())
		{
			if (FMath::Abs(Furthest - First) > 1e-6)
			{
				Add(Axis, ERoscMoveProblem::FixedAxisMoved, Furthest, First);
			}
			continue;
		}

		double PeakSpeed = 0.0;
		for (int32 Frame = 0; Frame < Count; ++Frame)
		{
			const int32 Before = FMath::Max(Frame - 1, 0);
			const int32 After = FMath::Min(Frame + 1, Count - 1);
			const double Speed = (Poses[After].Get(Axis) - Poses[Before].Get(Axis)) / ((After - Before) * Step);
			PeakSpeed = FMath::Max(PeakSpeed, FMath::Abs(Speed));
		}
		if (PeakSpeed > Limits.MaxSpeed)
		{
			Add(Axis, ERoscMoveProblem::TooFast, PeakSpeed, Limits.MaxSpeed);
		}

		double PeakAcceleration = 0.0;
		for (int32 Frame = 1; Frame + 1 < Count; ++Frame)
		{
			const double Acceleration = (Poses[Frame + 1].Get(Axis) - 2.0 * Poses[Frame].Get(Axis) + Poses[Frame - 1].Get(Axis)) / (Step * Step);
			PeakAcceleration = FMath::Max(PeakAcceleration, FMath::Abs(Acceleration));
		}
		if (Limits.MaxAcceleration > 0.0 && PeakAcceleration > Limits.MaxAcceleration)
		{
			Add(Axis, ERoscMoveProblem::TooAbrupt, PeakAcceleration, Limits.MaxAcceleration);
		}
	}
	return Findings;
}

FText Rosc::Take::Describe(const FRoscMoveFinding& Finding, ERoscRigType RigType, ERoscDistanceUnits Units)
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
			? LOCTEXT("TakeBelowRange", "{Axis} goes to {Value}; this {Rig} stops at {Limit}. Zero the device nearer the middle of the move and record again, or choose a larger {Rig}.")
			: LOCTEXT("TakeAboveRange", "{Axis} goes to {Value}; this {Rig} reaches {Limit}. Zero the device nearer the middle of the move and record again, or choose a larger {Rig}."), Args);

	case ERoscMoveProblem::FixedAxisMoved:
		Args.Add(TEXT("From"), Rosc::Rig::FormatAxisValue(Finding.Axis, Finding.Limit, Units));
		Args.Add(TEXT("To"), Rosc::Rig::FormatAxisValue(Finding.Axis, Finding.Value, Units));
		return FText::Format(LOCTEXT("TakeFixedAxisMoved", "{Axis} changes from {From} to {To}, but on a {Rig} it is set before the shot and cannot move during it. Lock it, or choose a support that can make this move."), Args);

	case ERoscMoveProblem::TooFast:
		Args.Add(TEXT("Value"), Rosc::Move::FormatRate(Finding.Axis, Finding.Value, Units, false));
		Args.Add(TEXT("Limit"), Rosc::Move::FormatRate(Finding.Axis, Finding.Limit, Units, false));
		return FText::Format(LOCTEXT("TakeTooFast", "{Axis} peaks at {Value}; this {Rig} manages {Limit}. Operate more slowly and record again, or stabilise more."), Args);

	case ERoscMoveProblem::TooAbrupt:
		Args.Add(TEXT("Value"), Rosc::Move::FormatRate(Finding.Axis, Finding.Value, Units, true));
		Args.Add(TEXT("Limit"), Rosc::Move::FormatRate(Finding.Axis, Finding.Limit, Units, true));
		return FText::Format(LOCTEXT("TakeTooAbrupt", "{Axis} changes speed by {Value}; this {Rig} manages {Limit}. Start and stop the move more gently and record again, or stabilise more."), Args);
	}
	checkNoEntry();
	return FText::GetEmpty();
}

FText Rosc::Take::GetSmoothingName(ERoscTakeSmoothing Smoothing)
{
	switch (Smoothing)
	{
	case ERoscTakeSmoothing::Off: return LOCTEXT("SmoothingOff", "Off");
	case ERoscTakeSmoothing::Light: return LOCTEXT("SmoothingLight", "Light");
	case ERoscTakeSmoothing::Medium: return LOCTEXT("SmoothingMedium", "Medium");
	case ERoscTakeSmoothing::Heavy: return LOCTEXT("SmoothingHeavy", "Heavy");
	}
	return FText::GetEmpty();
}

bool Rosc::Take::IsFreeMoving(ERoscRigType Type)
{
	return Type == ERoscRigType::Steadicam || Type == ERoscRigType::Handheld || Type == ERoscRigType::Drone;
}

FRoscRigPose Rosc::Take::DrivePose(const FRoscRigSpec& Spec, const FRoscRigPose& PoseAtZero, const FTransform& DeviceAtZero, const FTransform& DeviceNow, const FRoscRigPose& PreviousPose)
{
	FRoscRigPose Pose = PoseAtZero;
	const FRotator Zero = DeviceAtZero.Rotator();
	const FRotator Now = DeviceNow.Rotator();
	const auto Turn = [&](ERoscRigAxis Axis, double DeviceTurn)
	{
		if (Spec.GetAxis(Axis).bUsed)
		{
			const double Wanted = PoseAtZero.Get(Axis) + DeviceTurn;
			const double Previous = PreviousPose.Get(Axis);
			Pose.Set(Axis, Previous + FRotator::NormalizeAxis(Wanted - Previous));
		}
	};
	Turn(ERoscRigAxis::Pan, Now.Yaw - Zero.Yaw);
	Turn(ERoscRigAxis::Tilt, Now.Pitch - Zero.Pitch);
	Turn(ERoscRigAxis::Roll, Now.Roll - Zero.Roll);

	if (IsFreeMoving(Spec.Type))
	{
		// The device's forward when zeroed is the camera's forward then, which is the head's pan on the rig.
		const FVector Moved = FRotator(0.0, PoseAtZero.Pan - Zero.Yaw, 0.0).RotateVector(DeviceNow.GetLocation() - DeviceAtZero.GetLocation());
		const auto Move = [&](ERoscRigAxis Axis, double Distance)
		{
			if (Spec.GetAxis(Axis).bUsed)
			{
				Pose.Set(Axis, PoseAtZero.Get(Axis) + Distance);
			}
		};
		Move(ERoscRigAxis::Travel, Moved.X);
		Move(ERoscRigAxis::Traverse, Moved.Y);
		Move(ERoscRigAxis::Lift, Moved.Z);
	}
	return Pose;
}

FRoscRigPose Rosc::Take::TurnHead(const FRoscRigSpec& Spec, const FRoscRigPose& Pose, const FRotator& CameraRotation, const FRotator& ViewRotation)
{
	FRoscRigPose Turned = Pose;
	const auto Turn = [&](ERoscRigAxis Axis, double By)
	{
		const FRoscAxisLimits& Limits = Spec.GetAxis(Axis);
		if (Limits.bUsed)
		{
			Turned.Set(Axis, FMath::Clamp(Pose.Get(Axis) + FRotator::NormalizeAxis(By), Limits.Min, Limits.Max));
		}
	};
	Turn(ERoscRigAxis::Pan, ViewRotation.Yaw - CameraRotation.Yaw);
	Turn(ERoscRigAxis::Tilt, ViewRotation.Pitch - CameraRotation.Pitch);
	return Turned;
}

#undef LOCTEXT_NAMESPACE
