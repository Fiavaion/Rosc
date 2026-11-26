// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscUnits.h"
#include "RoscRigTypes.generated.h"

/** The kinds of camera support the Grip department offers. */
UENUM(BlueprintType)
enum class ERoscRigType : uint8
{
	/** Sticks and a fluid head: the camera pans and tilts in place. */
	Tripod,
	/** A short rail the camera slides along. */
	Slider,
	/** A dolly on straight or curved track, with a rising column. */
	Dolly,
	/** A fixed-length arm that swings and booms. */
	Jib,
	/** A telescoping crane with a remote head. */
	Crane,
	/** A body-worn stabiliser: moves freely at walking pace, floats slightly. */
	Steadicam,
	/** On the operator's shoulder. */
	Handheld,
	/** A camera drone. */
	Drone
};

/**
 * The ways a rig can move. Each rig type uses some of them. Distances are centimetres and
 * angles are degrees, as everywhere in the engine.
 */
UENUM(BlueprintType)
enum class ERoscRigAxis : uint8
{
	/** Along the slider rail or dolly track; forward and back for free-moving rigs. */
	Travel,
	/** Sideways, for free-moving rigs. */
	Traverse,
	/** Tripod height, dolly column, crane pivot height, lens height or altitude. */
	Lift,
	/** The arm swinging left and right about its pivot. */
	Swing,
	/** The arm rising and falling about its pivot. */
	Boom,
	/** Arm length from pivot to head: fixed on a jib, telescoping on a crane. */
	Extension,
	Pan,
	Tilt,
	Roll
};

inline constexpr int32 RoscRigAxisCount = 9;

/** Where every axis of a rig is at one moment. Unused axes stay at zero. */
USTRUCT(BlueprintType)
struct ROSCCORE_API FRoscRigPose
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Travel = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Traverse = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Lift = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Swing = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Boom = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Extension = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Pan = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Tilt = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Roll = 0.0;

	double Get(ERoscRigAxis Axis) const;
	void Set(ERoscRigAxis Axis, double Value);
};

/** What one axis of a particular rig can do. */
USTRUCT(BlueprintType)
struct ROSCCORE_API FRoscAxisLimits
{
	GENERATED_BODY()

	/** Whether this rig has the axis at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	bool bUsed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Min = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double Max = 0.0;

	/** Top speed per second. Zero means the axis is set when the rig is built and cannot move during a shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double MaxSpeed = 0.0;

	/** Greatest acceleration per second squared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	double MaxAcceleration = 0.0;

	bool IsFixedDuringShot() const { return MaxSpeed <= 0.0; }
};

/** A rig's type and the limits of each of its axes. */
USTRUCT(BlueprintType)
struct ROSCCORE_API FRoscRigSpec
{
	GENERATED_BODY()

	/** The preset these limits came from; kept so the panel can show which one is chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	FString PresetId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	ERoscRigType Type = ERoscRigType::Tripod;

	/** Indexed by ERoscRigAxis. */
	UPROPERTY(EditAnywhere, Category = "Rosc")
	FRoscAxisLimits Axes[RoscRigAxisCount];

	const FRoscAxisLimits& GetAxis(ERoscRigAxis Axis) const { return Axes[static_cast<int32>(Axis)]; }
	FRoscAxisLimits& GetAxis(ERoscRigAxis Axis) { return Axes[static_cast<int32>(Axis)]; }

	/** The pose with every used axis brought inside its range. */
	FRoscRigPose ClampPose(const FRoscRigPose& Pose) const;
};

/** A ready-made rig the user can choose by name. */
struct FRoscRigPreset
{
	FString Id;
	FText Name;
	FRoscRigSpec Spec;
	/** A sensible way to stand the rig up before the user adjusts it. */
	FRoscRigPose DefaultPose;
};

namespace Rosc::Rig
{
	/** Every preset, grouped by rig type in the order of ERoscRigType. */
	ROSCCORE_API TConstArrayView<FRoscRigPreset> GetPresets();

	ROSCCORE_API const FRoscRigPreset* FindPreset(const FString& Id);

	/** The first preset of a type; every type has at least one. */
	ROSCCORE_API const FRoscRigPreset& GetDefaultPreset(ERoscRigType Type);

	ROSCCORE_API FText GetTypeName(ERoscRigType Type);

	/** What the grip calls this axis on this kind of rig: "Telescope" on a crane, "Arm length" on a jib. */
	ROSCCORE_API FText GetAxisName(ERoscRigType Type, ERoscRigAxis Axis);

	/** True for axes measured in centimetres, false for axes measured in degrees. */
	ROSCCORE_API bool IsDistanceAxis(ERoscRigAxis Axis);

	/** A point on a straight track that runs along the rig's forward axis. */
	ROSCCORE_API FTransform StraightTrackPoint(double TravelCm);

	/**
	 * Where the camera mount is, relative to the rig's base.
	 * @param TrackPoint For slider and dolly: the track's position and direction at the pose's
	 *                   Travel distance, relative to the base. Ignored by the other rig types.
	 */
	ROSCCORE_API FTransform SolveMount(ERoscRigType Type, const FRoscRigPose& Pose, const FTransform& TrackPoint = FTransform::Identity);

	/** An axis value as the crew would say it: "3.2 m" or 10' 6" for distances, "45°" for angles. */
	ROSCCORE_API FText FormatAxisValue(ERoscRigAxis Axis, double Value, ERoscDistanceUnits Units);

	/** Reads an axis value written the way FormatAxisValue shows it. Centimetres or degrees; nothing when the text is not a value. */
	ROSCCORE_API TOptional<double> ParseAxisValue(ERoscRigAxis Axis, const FString& Text, ERoscDistanceUnits Units);
}
