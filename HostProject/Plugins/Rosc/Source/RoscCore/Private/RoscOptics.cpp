// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscOptics.h"

namespace Rosc::Optics
{
	double HorizontalFieldOfViewDeg(double SensorWidthMm, double FocalLengthMm, double Squeeze)
	{
		check(FocalLengthMm > 0.0);
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(SensorWidthMm * Squeeze / (2.0 * FocalLengthMm)));
	}

	double FocalLengthForFieldOfViewMm(double SensorWidthMm, double HorizontalFieldOfViewDeg, double Squeeze)
	{
		check(HorizontalFieldOfViewDeg > 0.0 && HorizontalFieldOfViewDeg < 180.0);
		return SensorWidthMm * Squeeze / (2.0 * FMath::Tan(FMath::DegreesToRadians(HorizontalFieldOfViewDeg) / 2.0));
	}

	double DollyZoomFocalLengthMm(double StartFocalLengthMm, double StartDistance, double Distance)
	{
		check(StartDistance > 0.0);
		// The subject's size in the frame goes with focal length over distance; keep that ratio.
		return StartFocalLengthMm * FMath::Max(Distance, 0.0) / StartDistance;
	}

	double VerticalFieldOfViewDeg(double SensorHeightMm, double FocalLengthMm)
	{
		check(FocalLengthMm > 0.0);
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(SensorHeightMm / (2.0 * FocalLengthMm)));
	}

	double SensorDiagonalMm(double SensorWidthMm, double SensorHeightMm)
	{
		return FMath::Sqrt(SensorWidthMm * SensorWidthMm + SensorHeightMm * SensorHeightMm);
	}

	double DefaultCircleOfConfusionMm(double SensorWidthMm, double SensorHeightMm)
	{
		return SensorDiagonalMm(SensorWidthMm, SensorHeightMm) / 1500.0;
	}

	double HyperfocalDistanceMm(double FocalLengthMm, double FNumber, double CircleOfConfusionMm)
	{
		check(FNumber > 0.0 && CircleOfConfusionMm > 0.0);
		return FocalLengthMm * FocalLengthMm / (FNumber * CircleOfConfusionMm) + FocalLengthMm;
	}

	FDepthOfField DepthOfField(double FocalLengthMm, double FNumber, double CircleOfConfusionMm, double FocusDistanceMm)
	{
		const double Hyperfocal = HyperfocalDistanceMm(FocalLengthMm, FNumber, CircleOfConfusionMm);
		const double Numerator = FocusDistanceMm * (Hyperfocal - FocalLengthMm);

		FDepthOfField Result;
		Result.NearMm = Numerator / (Hyperfocal + FocusDistanceMm - 2.0 * FocalLengthMm);
		Result.bFarIsInfinite = FocusDistanceMm >= Hyperfocal;
		if (!Result.bFarIsInfinite)
		{
			Result.FarMm = Numerator / (Hyperfocal - FocusDistanceMm);
		}
		return Result;
	}

	double EquivalentFocalLengthMm(double FocalLengthMm, double SensorWidthMm, double Squeeze, double ReferenceSensorWidthMm)
	{
		check(SensorWidthMm > 0.0 && Squeeze > 0.0);
		return FocalLengthMm * ReferenceSensorWidthMm / (SensorWidthMm * Squeeze);
	}

	FVector2d FieldSizeAtDistanceMm(double SensorWidthMm, double SensorHeightMm, double FocalLengthMm, double Squeeze, double DistanceMm)
	{
		check(FocalLengthMm > 0.0);
		return FVector2d(DistanceMm * SensorWidthMm * Squeeze / FocalLengthMm, DistanceMm * SensorHeightMm / FocalLengthMm);
	}
}
