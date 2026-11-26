// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscUnits.generated.h"

/** How distances are shown to the user. Camera crews work in either, by country and habit. */
UENUM()
enum class ERoscDistanceUnits : uint8
{
	/** Metres and centimetres. */
	Metric,
	/** Feet and inches. */
	Imperial
};

namespace Rosc::Units
{
	/**
	 * A distance as a focus puller would say it: "45 cm", "3.05 m", "12.5 m" in metric,
	 * or 9", 4' 6", 120' in imperial.
	 */
	ROSCCORE_API FText FormatDistance(double Millimetres, ERoscDistanceUnits Units);

	/**
	 * Reads a distance the way it is written: "3.2 m", "45 cm", 10' 6", 9", "10 ft 6 in". A bare
	 * number is metres in metric and feet in imperial. Returns millimetres, or nothing when the
	 * text is not a distance.
	 */
	ROSCCORE_API TOptional<double> ParseDistance(const FString& Text, ERoscDistanceUnits Units);

	/** A number with at most the given count of decimals and no digit grouping: 2.8, 180, 23.976. */
	ROSCCORE_API FText FormatNumber(double Value, int32 MaxDecimals);
}
