// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UEcineUnits.generated.h"

/** How distances are shown to the user. Camera crews work in either, by country and habit. */
UENUM()
enum class EUEcineDistanceUnits : uint8
{
	/** Metres and centimetres. */
	Metric,
	/** Feet and inches. */
	Imperial
};

namespace UEcine::Units
{
	/**
	 * A distance as a focus puller would say it: "45 cm", "3.05 m", "12.5 m" in metric,
	 * or 9", 4' 6", 120' in imperial.
	 */
	UECINECORE_API FText FormatDistance(double Millimetres, EUEcineDistanceUnits Units);

	/**
	 * Reads a distance the way it is written: "3.2 m", "45 cm", 10' 6", 9", "10 ft 6 in". A bare
	 * number is metres in metric and feet in imperial. Returns millimetres, or nothing when the
	 * text is not a distance.
	 */
	UECINECORE_API TOptional<double> ParseDistance(const FString& Text, EUEcineDistanceUnits Units);

	/** A number with at most the given count of decimals and no digit grouping: 2.8, 180, 23.976. */
	UECINECORE_API FText FormatNumber(double Value, int32 MaxDecimals);
}
