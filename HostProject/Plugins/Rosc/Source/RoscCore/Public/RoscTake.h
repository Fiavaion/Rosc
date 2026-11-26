// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscMove.h"
#include "RoscRigTypes.h"
#include "RoscTake.generated.h"

/** How much of the operator's hand shake to take out of a take. */
UENUM(BlueprintType)
enum class ERoscTakeSmoothing : uint8
{
	Off,
	Light,
	Medium,
	Heavy
};

/** What to do to a recorded take before it is baked. */
USTRUCT(BlueprintType)
struct ROSCCORE_API FRoscTakeProcessing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	ERoscTakeSmoothing Smoothing = ERoscTakeSmoothing::Medium;

	/** Extra smoothing for a steadier shot, 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc", meta = (ClampMin = "0", ClampMax = "1"))
	double Stabilise = 0.25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	bool bLockPan = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	bool bLockTilt = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	bool bLockRoll = false;

	/** Holds Travel, Traverse and Lift. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	bool bLockPosition = false;
};

/** A recorded move of a rig: one pose for every frame. */
struct ROSCCORE_API FRoscTake
{
	double FramesPerSecond = 24.0;
	TArray<FRoscRigPose> Poses;
};

namespace Rosc::Take
{
	/** The width of the smoothing in seconds: the sigma of the Gaussian laid over each axis. */
	ROSCCORE_API double GetSmoothingSeconds(const FRoscTakeProcessing& Processing);

	/**
	 * The take's poses with every angle unwrapped, so a pan across 180 degrees does not jump, then
	 * smoothed and locked as asked. The first and last frames hold. The values stay unwrapped.
	 */
	ROSCCORE_API TArray<FRoscRigPose> Process(const FRoscTake& Take, const FRoscTakeProcessing& Processing);

	/** Everything in the poses that the rig cannot do, in axis order. Empty when the take is within the rig's limits. */
	ROSCCORE_API TArray<FRoscMoveFinding> Check(const FRoscRigSpec& Spec, TConstArrayView<FRoscRigPose> Poses, double FramesPerSecond);

	/**
	 * A finding about a take as a sentence that says what is wrong and what the operator can do:
	 * a take has no duration or eases to change, so the advice is about operating and processing.
	 */
	ROSCCORE_API FText Describe(const FRoscMoveFinding& Finding, ERoscRigType RigType, ERoscDistanceUnits Units);

	ROSCCORE_API FText GetSmoothingName(ERoscTakeSmoothing Smoothing);

	/** True for the supports the operator carries, whose position a device drives as well as their head. */
	ROSCCORE_API bool IsFreeMoving(ERoscRigType Type);

	/**
	 * Where a device drives a rig. The device's turn since it was zeroed is added to the head's pan,
	 * tilt and roll. On a free-moving support its movement is added to travel, traverse and lift,
	 * read in the direction the camera looked when the device was zeroed. Each angle is carried on
	 * from the previous pose, so a turn past half a circle does not jump back.
	 */
	ROSCCORE_API FRoscRigPose DrivePose(const FRoscRigSpec& Spec, const FRoscRigPose& PoseAtZero, const FTransform& DeviceAtZero, const FTransform& DeviceNow, const FRoscRigPose& PreviousPose);

	/** The pose whose head turns the camera from where it looks to where the view looks, within the head's pan and tilt limits. */
	ROSCCORE_API FRoscRigPose TurnHead(const FRoscRigSpec& Spec, const FRoscRigPose& Pose, const FRotator& CameraRotation, const FRotator& ViewRotation);
}
