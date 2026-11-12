// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Turns what a picture model gives back into pictures and numbers a film crew can use. Pure
 * functions over plain arrays, row by row from the top left.
 */
namespace UEcine::Vision
{
	/**
	 * A model's relative depth as a 16-bit grey picture, stretched so the nearest thing is
	 * white and the farthest black. Takes values that grow as things come nearer.
	 */
	UECINECORE_API TArray<uint16> NearnessToGrey16(TConstArrayView<float> Nearness);

	/** A model's matte, given as logits, as an 8-bit grey picture: white where the subject is. */
	UECINECORE_API TArray<uint8> LogitsToGrey8(TConstArrayView<float> Logits);

	/**
	 * The horizontal field of view, in degrees, of the camera that saw a map of points.
	 *
	 * A point map gives every pixel a position in front of the camera (X right, Y down, Z along
	 * the lens axis), right up to a scale and a shift along the lens axis. For any shift the
	 * focal length that best puts the points back on their pixels is known at once, so the
	 * shift is searched for (after MoGe: Wang et al., 2024).
	 *
	 * @param PointsXyz Three numbers for every pixel.
	 * @param Mask One number for every pixel: above 0.5 where the point can be trusted.
	 * @return Unset when there is too little to judge from.
	 */
	UECINECORE_API TOptional<double> FieldOfViewFromPoints(TConstArrayView<float> PointsXyz, TConstArrayView<float> Mask, int32 Width, int32 Height);
}
