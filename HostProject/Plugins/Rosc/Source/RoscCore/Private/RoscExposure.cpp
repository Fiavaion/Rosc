// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscExposure.h"

namespace Rosc::Exposure
{
	double ShutterTimeSeconds(double ShutterAngleDeg, double FramesPerSecond)
	{
		check(FramesPerSecond > 0.0);
		return ShutterAngleDeg / (360.0 * FramesPerSecond);
	}

	double ShutterOpenFraction(double ShutterAngleDeg)
	{
		return FMath::Clamp(ShutterAngleDeg / 360.0, 0.0, 1.0);
	}

	double NdStops(double OpticalDensity)
	{
		// Transmission is 10^-density, and one stop halves the light.
		return OpticalDensity / FMath::LogX(10.0, 2.0);
	}

	double EV100(double TStop, double ShutterTimeSeconds, double Iso)
	{
		check(TStop > 0.0 && ShutterTimeSeconds > 0.0 && Iso > 0.0);
		return FMath::Log2(TStop * TStop / ShutterTimeSeconds) - FMath::Log2(Iso / 100.0);
	}

	bool IsSameSetting(double A, double B)
	{
		return FMath::Abs(A - B) <= 0.0002 * FMath::Max(FMath::Abs(A), FMath::Abs(B));
	}

	TArray<double> TStopLadder(double WidestTStop, double MinApertureTStop)
	{
		// The conventional third-stop markings.
		static const double ThirdStops[] = {
			1.0, 1.1, 1.3, 1.4, 1.6, 1.8, 2.0, 2.2, 2.5, 2.8, 3.2, 3.5, 4.0, 4.5, 5.0, 5.6,
			6.3, 7.1, 8.0, 9.0, 10.0, 11.0, 13.0, 14.0, 16.0, 18.0, 20.0, 22.0, 25.0, 29.0, 32.0 };

		TArray<double> Ladder;
		Ladder.Add(WidestTStop);
		for (const double Stop : ThirdStops)
		{
			if (Stop > WidestTStop && !IsSameSetting(Stop, WidestTStop) && (Stop < MinApertureTStop || IsSameSetting(Stop, MinApertureTStop)))
			{
				Ladder.Add(Stop);
			}
		}
		return Ladder;
	}

	TOptional<double> StepOnLadder(TConstArrayView<double> Ladder, double Current, int32 Direction)
	{
		if (Direction > 0)
		{
			for (const double Value : Ladder)
			{
				if (Value > Current && !IsSameSetting(Value, Current))
				{
					return Value;
				}
			}
		}
		else if (Direction < 0)
		{
			for (int32 Index = Ladder.Num() - 1; Index >= 0; --Index)
			{
				if (Ladder[Index] < Current && !IsSameSetting(Ladder[Index], Current))
				{
					return Ladder[Index];
				}
			}
		}
		return TOptional<double>();
	}

	TConstArrayView<double> StandardIsos()
	{
		static const double Isos[] = { 100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1280, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000, 10000, 12800 };
		return Isos;
	}

	TConstArrayView<double> StandardNdDensities()
	{
		static const double Densities[] = { 0.0, 0.3, 0.6, 0.9, 1.2, 1.5, 1.8, 2.1, 2.4 };
		return Densities;
	}

	TConstArrayView<double> StandardShutterAngles()
	{
		static const double Angles[] = { 11.25, 22.5, 45.0, 90.0, 144.0, 172.8, 180.0, 270.0, 360.0 };
		return Angles;
	}

	TConstArrayView<double> StandardFrameRates()
	{
		static const double Rates[] = { 23.976, 24.0, 25.0, 29.97, 30.0, 48.0, 50.0, 59.94, 60.0, 96.0, 100.0, 120.0 };
		return Rates;
	}
}
