// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/**
 * Lens and sensor maths in film units: millimetres for sensor sizes, focal lengths and
 * distances. Pure functions with no engine state, so they can be tested in isolation.
 */
namespace Rosc::Optics
{
	/** Width of a full-frame (35 mm stills, "FF") sensor, the usual reference for equivalent focal lengths. */
	inline constexpr double FullFrameSensorWidthMm = 36.0;

	/**
	 * Horizontal angle of view in degrees.
	 * @param Squeeze Anamorphic squeeze factor of the lens; 1 for spherical lenses.
	 */
	ROSCCORE_API double HorizontalFieldOfViewDeg(double SensorWidthMm, double FocalLengthMm, double Squeeze = 1.0);

	/** The focal length that gives a horizontal angle of view: HorizontalFieldOfViewDeg the other way round. */
	ROSCCORE_API double FocalLengthForFieldOfViewMm(double SensorWidthMm, double HorizontalFieldOfViewDeg, double Squeeze = 1.0);

	/**
	 * Dolly zoom: the focal length that keeps a subject the same size in the frame once the
	 * camera has gone from StartDistance to Distance away from it. The background then grows
	 * or shrinks behind a subject that does not.
	 */
	ROSCCORE_API double DollyZoomFocalLengthMm(double StartFocalLengthMm, double StartDistance, double Distance);

	/** Vertical angle of view in degrees. Anamorphic squeeze does not affect the vertical axis. */
	ROSCCORE_API double VerticalFieldOfViewDeg(double SensorHeightMm, double FocalLengthMm);

	ROSCCORE_API double SensorDiagonalMm(double SensorWidthMm, double SensorHeightMm);

	/** Conventional circle of confusion for a format: sensor diagonal / 1500. */
	ROSCCORE_API double DefaultCircleOfConfusionMm(double SensorWidthMm, double SensorHeightMm);

	/** Focus distance beyond which everything to infinity is acceptably sharp. */
	ROSCCORE_API double HyperfocalDistanceMm(double FocalLengthMm, double FNumber, double CircleOfConfusionMm);

	struct FDepthOfField
	{
		double NearMm = 0.0;
		/** Meaningless when bFarIsInfinite is true. */
		double FarMm = 0.0;
		bool bFarIsInfinite = false;
	};

	/** Near and far limits of acceptable sharpness for a lens focused at FocusDistanceMm. */
	ROSCCORE_API FDepthOfField DepthOfField(double FocalLengthMm, double FNumber, double CircleOfConfusionMm, double FocusDistanceMm);

	/**
	 * Focal length that gives the same horizontal angle of view on a reference sensor.
	 * With the default reference this is the "full-frame equivalent" focal length.
	 */
	ROSCCORE_API double EquivalentFocalLengthMm(double FocalLengthMm, double SensorWidthMm, double Squeeze = 1.0, double ReferenceSensorWidthMm = FullFrameSensorWidthMm);

	/** Width (X) and height (Y) of the area the camera sees at a distance, in millimetres. */
	ROSCCORE_API FVector2d FieldSizeAtDistanceMm(double SensorWidthMm, double SensorHeightMm, double FocalLengthMm, double Squeeze, double DistanceMm);
}
