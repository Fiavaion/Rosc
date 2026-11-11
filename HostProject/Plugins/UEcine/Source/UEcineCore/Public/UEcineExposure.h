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

	/** Full-stop iris ladder from T1 to T22, the marks engraved on a cine lens. */
	UECINECORE_API TConstArrayView<double> FullTStops();

	/** Common ISO ratings in third-stop steps from 100 to 12800. */
	UECINECORE_API TConstArrayView<int32> StandardIsos();

	/** Common ND optical densities in one-stop steps: 0 (clear), 0.3, 0.6 … 2.4. */
	UECINECORE_API TConstArrayView<double> StandardNdDensities();

	/** Common shutter angles in degrees. */
	UECINECORE_API TConstArrayView<double> StandardShutterAngles();
}
