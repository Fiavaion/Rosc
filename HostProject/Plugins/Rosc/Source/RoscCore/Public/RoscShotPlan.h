// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoscMove.h"
#include "RoscRigTypes.h"
#include "RoscShotTypes.h"

class FJsonValue;
class FRoscCameraDatabase;

/** The kinds of step a shot plan is made of. Each is something a Rosc tab can already do. */
enum class ERoscShotStep : uint8
{
	/** Choose the camera body and its recording format. */
	ChooseCamera,
	/** Choose the lens kit and put on the lens nearest a focal length. */
	ChooseLens,
	/** Set T-stop, ISO, shutter angle and ND. */
	SetExposure,
	/** Put the camera on a support. */
	PutOnSupport,
	/** Move the rig from where it stands to a new mark, over a time. */
	MoveToMark,
	/** Focus on a subject, once or by following it. */
	FocusOn,
	/** Set the focus to a distance. */
	FocusAt,
	/** Add a shot to the sequence, seen through the camera. */
	AddShot,
	/** Write the move through the marks into the sequence. */
	BakeMove
};

/** One step of a shot plan. Only the fields of its kind are used. */
struct FRoscShotStep
{
	ERoscShotStep Kind = ERoscShotStep::ChooseCamera;

	// Choose camera.
	FString BodyId;
	FString FormatId;

	// Choose lens.
	FString LensKitId;
	double FocalLengthMm = 0.0;

	// Set exposure.
	double TStop = 0.0;
	int32 Iso = 0;
	double ShutterAngleDeg = 0.0;
	/** Stops of neutral density in front of the lens. */
	double NdStops = 0.0;

	// Put on support.
	FString SupportId;

	// Move to mark: how far each axis goes from where the rig stands, in centimetres and degrees.
	FRoscRigPose Change;
	FRoscMoveTiming Timing;

	// Focus on.
	FString Subject;
	bool bFollow = false;

	// Focus at.
	double FocusDistanceCm = 0.0;

	// Add shot.
	FString Scene;
	ERoscShotSize Size = ERoscShotSize::Unset;
	FString Move;
	FString Notes;
};

/**
 * A shot described in words, turned into steps. A plan changes nothing by itself: the user
 * reads it and applies it, and the steps then run through the same operations the tabs use.
 */
struct FRoscShotPlan
{
	/** What the plan does, in a sentence or two for the director. */
	FString Summary;
	TArray<FRoscShotStep> Steps;
};

namespace Rosc::ShotPlan
{
	/** The name a kind of step has in a written plan, such as "move_to_mark". */
	ROSCCORE_API const TCHAR* GetStepId(ERoscShotStep Kind);

	/** The name a shot size has in a written plan, such as "close_up". */
	ROSCCORE_API const TCHAR* GetShotSizeId(ERoscShotSize Size);

	/** The name a rig axis has in a written plan, with its unit: "travelM", "panDeg". */
	ROSCCORE_API const TCHAR* GetAxisId(ERoscRigAxis Axis);

	/** An axis value as a written plan gives it: metres for lengths, degrees for angles. */
	ROSCCORE_API double ToPlanUnits(ERoscRigAxis Axis, double Value);

	/**
	 * A number for JSON a language model reads, rounded as a crew would say it and written as
	 * "2.8", where the engine would write the nearest number a computer holds: 2.7999999999999998.
	 */
	ROSCCORE_API TSharedRef<FJsonValue> MakeNumber(double Value, int32 Decimals);

	/**
	 * The JSON Schema of a written plan, for a language model that is held to a schema. Every
	 * field is required and says what it means, so the schema is also the model's instructions
	 * for each step.
	 */
	ROSCCORE_API const FString& GetJsonSchema();

	/**
	 * Reads a written plan.
	 * @return false, with what is wrong and where in OutError, when the text is not a plan.
	 */
	ROSCCORE_API bool Parse(const FString& JsonText, FRoscShotPlan& OutPlan, FString& OutError);

	/**
	 * The equipment a plan can choose from, as JSON: camera bodies with their recording formats,
	 * lens kits with their lenses, and supports with what each axis can do. Lengths are in
	 * metres, to match a written plan.
	 */
	ROSCCORE_API FString DescribeEquipment(const FRoscCameraDatabase& Database);
}
