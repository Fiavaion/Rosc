#pragma once

#include "CoreMinimal.h"

/**
 * Exposure maths in film terms: ISO, shutter angle, T-stop and neutral density.
 * Pure functions with no engine state.
 */
namespace UEcine::Exposure
{
	/** Time the shutter is open per frame, in seconds. A 180 degree shutter at 24 fps is 1/48 s. */
	UECINECORE_API double ShutterTimeSeconds(double ShutterAngleDeg, double FramesPerSecond);

	/** Fraction of the frame interval the shutter is open (0 to 1); this is the engine's motion blur amount. */
	UECINECORE_API double ShutterOpenFraction(double ShutterAngleDeg);

	/** Light loss in stops for a neutral density filter of the given optical density. ND 0.3 is one stop. */
	UECINECORE_API double NdStops(double OpticalDensity);

	/**
	 * Exposure value at ISO 100 that the given settings expose correctly for.
	 * Higher values mean the settings suit a brighter scene. A sunny day is about 15.
	 */
	UECINECORE_API double EV100(double TStop, double ShutterTimeSeconds, double Iso);

	/**
	 * Iris positions in third-stop steps that a lens can be set to, from its widest aperture to
	 * its minimum aperture. The widest aperture is always the first entry, even when it is not a
	 * standard third-stop value (T1.9, T2.3).
	 */
	UECINECORE_API TArray<double> TStopLadder(double WidestTStop, double MinApertureTStop);

	/**
	 * Whether two values are the same camera setting once storage rounding is ignored (2.8 held
	 * as a float is still T2.8). Neighbouring settings stay distinct: 23.976 is not 24.
	 */
	UECINECORE_API bool IsSameSetting(double A, double B);

	/**
	 * The next value on an ascending ladder in the given direction. A value that is not on the
	 * ladder steps to its neighbour.
	 * @param Direction Negative to step to a smaller value, positive to a larger one.
	 * @return Unset when there is no value in that direction.
	 */
	UECINECORE_API TOptional<double> StepOnLadder(TConstArrayView<double> Ladder, double Current, int32 Direction);

	/** Common ISO ratings in third-stop steps from 100 to 12800. */
	UECINECORE_API TConstArrayView<double> StandardIsos();

	/** Common ND optical densities in one-stop steps: 0 (clear), 0.3, 0.6 … 2.4. */
	UECINECORE_API TConstArrayView<double> StandardNdDensities();

	/** Common shutter angles in degrees. */
	UECINECORE_API TConstArrayView<double> StandardShutterAngles();

	/** Common recording frame rates. */
	UECINECORE_API TConstArrayView<double> StandardFrameRates();
}
