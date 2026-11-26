// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscRigTypes.h"
#include "RoscOperator.generated.h"

/** Whose hands the camera is in. Each leaves its own small, human trace on the frame. */
UENUM(BlueprintType)
enum class ERoscOperatorStyle : uint8
{
	/** No movement at all: a locked-off frame or a pure CG camera. */
	LockedOff,
	/** An operator on a fluid head: barely perceptible breathing. */
	FluidHead,
	/** A remote head at the end of an arm: steady, with a trace of arm sway. */
	RemoteHead,
	/** A Steadicam: a slow float, horizon drifting slightly. */
	Steadicam,
	/** On the shoulder: livelier, with footfall and breathing. */
	Handheld,
	/** A drone gimbal: steady framing, position wandering in the wind. */
	Drone
};

/** The operator imperfection added on top of a rig's designed move. */
USTRUCT(BlueprintType)
struct ROSCCORE_API FRoscOperator
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	ERoscOperatorStyle Style = ERoscOperatorStyle::LockedOff;

	/** 1 is a competent operator; 0 removes the imperfection; 2 is a rough day. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc", meta = (ClampMin = "0", ClampMax = "2"))
	float Intensity = 1.0f;

	/** The take. The same seed always gives the same motion; a new seed gives a new take. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	int32 Seed = 1;

	bool IsStill() const { return Style == ERoscOperatorStyle::LockedOff || Intensity <= 0.0f; }
};

namespace Rosc::Operator
{
	/**
	 * The camera's offset from the rig's mount at a time: a small translation in centimetres and
	 * rotation in degrees. A pure function of the operator settings and the time, so a take
	 * repeats exactly.
	 */
	ROSCCORE_API FTransform Offset(const FRoscOperator& Operator, double TimeSeconds);

	/** The furthest the offset can ever move the camera: location in centimetres, rotation in degrees (pan, tilt, roll). */
	ROSCCORE_API void GetBounds(const FRoscOperator& Operator, FVector& OutLocationCm, FVector& OutRotationDeg);

	/** The operator a rig type normally has. */
	ROSCCORE_API ERoscOperatorStyle GetDefaultStyle(ERoscRigType RigType);

	ROSCCORE_API FText GetStyleName(ERoscOperatorStyle Style);
}
