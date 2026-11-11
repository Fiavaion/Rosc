#pragma once

#include "CoreMinimal.h"
#include "UEcineRig.h"
#include "UEcineOperator.generated.h"

/** Whose hands the camera is in. Each leaves its own small, human trace on the frame. */
UENUM(BlueprintType)
enum class EUEcineOperatorStyle : uint8
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
struct UECINECORE_API FUEcineOperator
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	EUEcineOperatorStyle Style = EUEcineOperatorStyle::LockedOff;

	/** 1 is a competent operator; 0 removes the imperfection; 2 is a rough day. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine", meta = (ClampMin = "0", ClampMax = "2"))
	float Intensity = 1.0f;

	/** The take. The same seed always gives the same motion; a new seed gives a new take. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	int32 Seed = 1;

	bool IsStill() const { return Style == EUEcineOperatorStyle::LockedOff || Intensity <= 0.0f; }
};

namespace UEcine::Operator
{
	/**
	 * The camera's offset from the rig's mount at a time: a small translation in centimetres and
	 * rotation in degrees. A pure function of the operator settings and the time, so a take
	 * repeats exactly.
	 */
	UECINECORE_API FTransform Offset(const FUEcineOperator& Operator, double TimeSeconds);

	/** The furthest the offset can ever move the camera: location in centimetres, rotation in degrees (pan, tilt, roll). */
	UECINECORE_API void GetBounds(const FUEcineOperator& Operator, FVector& OutLocationCm, FVector& OutRotationDeg);

	/** The operator a rig type normally has. */
	UECINECORE_API EUEcineOperatorStyle GetDefaultStyle(EUEcineRigType RigType);

	UECINECORE_API FText GetStyleName(EUEcineOperatorStyle Style);
}
