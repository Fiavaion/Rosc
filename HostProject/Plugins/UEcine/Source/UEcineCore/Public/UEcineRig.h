#pragma once

#include "CoreMinimal.h"
#include "UEcineUnits.h"
#include "UEcineRig.generated.h"

/** The kinds of camera support the Grip department offers. */
UENUM(BlueprintType)
enum class EUEcineRigType : uint8
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
enum class EUEcineRigAxis : uint8
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

inline constexpr int32 UEcineRigAxisCount = 9;

/** Where every axis of a rig is at one moment. Unused axes stay at zero. */
USTRUCT(BlueprintType)
struct UECINECORE_API FUEcineRigPose
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Travel = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Traverse = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Lift = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Swing = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Boom = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Extension = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Pan = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Tilt = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Roll = 0.0;

	double Get(EUEcineRigAxis Axis) const;
	void Set(EUEcineRigAxis Axis, double Value);
};

/** What one axis of a particular rig can do. */
USTRUCT(BlueprintType)
struct UECINECORE_API FUEcineAxisLimits
{
	GENERATED_BODY()

	/** Whether this rig has the axis at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	bool bUsed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Min = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double Max = 0.0;

	/** Top speed per second. Zero means the axis is set when the rig is built and cannot move during a shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double MaxSpeed = 0.0;

	/** Greatest acceleration per second squared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	double MaxAcceleration = 0.0;

	bool IsFixedDuringShot() const { return MaxSpeed <= 0.0; }
};

/** A rig's type and the limits of each of its axes. */
USTRUCT(BlueprintType)
struct UECINECORE_API FUEcineRigSpec
{
	GENERATED_BODY()

	/** The preset these limits came from; kept so the panel can show which one is chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	FString PresetId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	EUEcineRigType Type = EUEcineRigType::Tripod;

	/** Indexed by EUEcineRigAxis. */
	UPROPERTY(EditAnywhere, Category = "UEcine")
	FUEcineAxisLimits Axes[UEcineRigAxisCount];

	const FUEcineAxisLimits& GetAxis(EUEcineRigAxis Axis) const { return Axes[static_cast<int32>(Axis)]; }
	FUEcineAxisLimits& GetAxis(EUEcineRigAxis Axis) { return Axes[static_cast<int32>(Axis)]; }

	/** The pose with every used axis brought inside its range. */
	FUEcineRigPose ClampPose(const FUEcineRigPose& Pose) const;
};

/** A ready-made rig the user can choose by name. */
struct FUEcineRigPreset
{
	FString Id;
	FText Name;
	FUEcineRigSpec Spec;
	/** A sensible way to stand the rig up before the user adjusts it. */
	FUEcineRigPose DefaultPose;
};

namespace UEcine::Rig
{
	/** Every preset, grouped by rig type in the order of EUEcineRigType. */
	UECINECORE_API TConstArrayView<FUEcineRigPreset> GetPresets();

	UECINECORE_API const FUEcineRigPreset* FindPreset(const FString& Id);

	/** The first preset of a type; every type has at least one. */
	UECINECORE_API const FUEcineRigPreset& GetDefaultPreset(EUEcineRigType Type);

	UECINECORE_API FText GetTypeName(EUEcineRigType Type);

	/** What the grip calls this axis on this kind of rig: "Telescope" on a crane, "Arm length" on a jib. */
	UECINECORE_API FText GetAxisName(EUEcineRigType Type, EUEcineRigAxis Axis);

	/** True for axes measured in centimetres, false for axes measured in degrees. */
	UECINECORE_API bool IsDistanceAxis(EUEcineRigAxis Axis);

	/** True for rigs whose Travel axis runs along a track or rail. */
	UECINECORE_API bool UsesTrack(EUEcineRigType Type);

	/** A point on a straight track that runs along the rig's forward axis. */
	UECINECORE_API FTransform StraightTrackPoint(double TravelCm);

	/**
	 * Where the camera mount is, relative to the rig's base.
	 * @param TrackPoint For slider and dolly: the track's position and direction at the pose's
	 *                   Travel distance, relative to the base. Ignored by the other rig types.
	 */
	UECINECORE_API FTransform SolveMount(EUEcineRigType Type, const FUEcineRigPose& Pose, const FTransform& TrackPoint = FTransform::Identity);

	/** An axis value as the crew would say it: "3.2 m" or 10' 6" for distances, "45°" for angles. */
	UECINECORE_API FText FormatAxisValue(EUEcineRigAxis Axis, double Value, EUEcineDistanceUnits Units);
}
