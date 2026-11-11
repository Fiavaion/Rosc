#include "UEcineExposure.h"

namespace UEcine::Exposure
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

	TConstArrayView<double> FullTStops()
	{
		static const double Stops[] = { 1.0, 1.4, 2.0, 2.8, 4.0, 5.6, 8.0, 11.0, 16.0, 22.0 };
		return Stops;
	}

	TConstArrayView<int32> StandardIsos()
	{
		static const int32 Isos[] = { 100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1280, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000, 10000, 12800 };
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
}
