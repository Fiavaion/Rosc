// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoscShotPlan.h"

class ACineCameraActor;

/**
 * The editor's side of the shot assistant: what the assistant is told about the set, and what
 * is done with the plan it gives back. A plan is read by the user before anything changes, and
 * is then carried out through the same operations the tabs use, as one undoable step.
 */
namespace Rosc::ShotAssistant
{
	/**
	 * The set as the assistant is told of it, as JSON: the camera's package, exposure and focus,
	 * the support it is on, the things in the level near it by name with where each is as seen
	 * from the camera, and the shots of the open sequence. Lengths are in metres. No pictures.
	 */
	ROSCEDITOR_API FString DescribeSet(const ACineCameraActor* Camera);

	/** One step of a plan as the user reads it. */
	struct FPlanLine
	{
		/** What the step does, in film words. */
		FText Text;
		/** Why the step cannot be carried out. Empty when it can. */
		FText Problem;
		/** What the gear cannot do about the step, or does differently. These do not stop it. */
		TArray<FText> Warnings;
	};

	/** The plan as it would go for this camera as things stand: one line for each step, in order. */
	ROSCEDITOR_API TArray<FPlanLine> Review(const FRoscShotPlan& Plan, const ACineCameraActor* Camera);

	/** True when the plan has steps and none of them has a problem. */
	ROSCEDITOR_API bool CanApply(TConstArrayView<FPlanLine> Lines);

	/**
	 * Carries the plan out for the camera. Everything it changes is one step in the undo history.
	 * A plan with a move leaves the rig on the move's first mark.
	 * @return false with the reason in OutError when the plan cannot be carried out; a plan with
	 *         a problem in its review changes nothing.
	 */
	ROSCEDITOR_API bool Apply(const FRoscShotPlan& Plan, ACineCameraActor* Camera, FText& OutError);
}
