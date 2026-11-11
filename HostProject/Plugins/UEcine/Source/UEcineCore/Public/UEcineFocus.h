#pragma once

#include "CoreMinimal.h"
#include "UEcineMove.h"

/** Focus pulling maths. Distances are in any one unit, the same for every argument and the result. */
namespace UEcine::Focus
{
	/**
	 * The timing of a rack that takes the given time: the focus puller eases the wheel off the
	 * first mark for a quarter of it and settles onto the second for the last quarter.
	 */
	UECINECORE_API FUEcineMoveTiming MakeRackTiming(double DurationSeconds);

	/**
	 * Where the lens is focused at a time into a rack from one distance to another. The scale
	 * on a focus ring is even in dioptres (one over the distance), not in distance: the marks
	 * crowd together towards infinity. A wheel turned at an even pace therefore covers the near
	 * distances slowly and the far ones quickly, and so does this.
	 * Both distances must be greater than zero.
	 */
	UECINECORE_API double RackDistance(double FromDistance, double ToDistance, const FUEcineMoveTiming& Timing, double TimeSeconds);
}
