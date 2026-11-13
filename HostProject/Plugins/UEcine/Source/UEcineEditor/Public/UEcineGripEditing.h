// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameNumber.h"
#include "UEcineRigTypes.h"

class ACineCameraActor;
class AUEcineRig;
class ULevelSequence;

/**
 * The grip department's editing operations. Each one is a single undoable transaction, so the
 * panel, scripts and tests all work rigs the same way.
 */
namespace UEcine::GripEditing
{
	/** The rig a camera is mounted on, or null when it is on nothing. */
	UECINEEDITOR_API AUEcineRig* FindRig(const ACineCameraActor* Camera);

	/** How a support would be set to hold the camera where it is: the pose PutCameraOnRig stands it up in. */
	UECINEEDITOR_API FUEcineRigPose GetPoseOnRig(const ACineCameraActor* Camera, const FUEcineRigPreset& Preset);

	/**
	 * Puts a camera on a support without moving the camera: the rig is stood up underneath it,
	 * on the floor when there is one within reach. A camera already on a rig keeps that rig
	 * actor, which becomes the new kind of support; its marks are kept when the type is the same.
	 */
	UECINEEDITOR_API AUEcineRig* PutCameraOnRig(ACineCameraActor* Camera, const FUEcineRigPreset& Preset);

	/** Takes the camera off its rig, leaving it where it is, and removes the rig. */
	UECINEEDITOR_API void TakeCameraOffRig(ACineCameraActor* Camera);

	/** Changes a rig as one undoable step and redraws the viewports. */
	UECINEEDITOR_API void EditRig(AUEcineRig* Rig, const FText& Description, TFunctionRef<void(AUEcineRig&)> Edit);

	/** Stores where the rig is now as a new mark after the existing ones. Returns the mark's index. */
	UECINEEDITOR_API int32 AddMark(AUEcineRig* Rig);

	/** Moves an existing mark to where the rig is now. */
	UECINEEDITOR_API void UpdateMark(AUEcineRig* Rig, int32 MarkIndex);

	UECINEEDITOR_API void RemoveMark(AUEcineRig* Rig, int32 MarkIndex);

	/** Puts the rig on a mark. */
	UECINEEDITOR_API void GoToMark(AUEcineRig* Rig, int32 MarkIndex);

	/** What the lens does through a dolly zoom, and whether the lens on the camera can do it. */
	struct FDollyZoom
	{
		/** The focal length the move starts on and the one it asks for at its end, before the lens's limits. */
		double StartFocalLengthMm = 0.0;
		double EndFocalLengthMm = 0.0;
		/** The widest and the longest the move asks for on the way. */
		double WidestMm = 0.0;
		double LongestMm = 0.0;
		/** What the lens on the camera can zoom between. Equal for a prime. */
		double LensWidestMm = 0.0;
		double LensLongestMm = 0.0;
		/** True when the focus is held on a subject or a point, so the subject stays sharp as the camera travels. */
		bool bFocusHeld = false;

		bool IsZoomLens() const { return LensLongestMm > LensWidestMm + 0.01; }
		bool IsWithinLens() const { return WidestMm >= LensWidestMm - 0.01 && LongestMm <= LensLongestMm + 0.01; }
	};

	/** True for the supports a dolly zoom is made on: a dolly and a slider. */
	UECINEEDITOR_API bool CanDollyZoom(const AUEcineRig* Rig);

	/**
	 * Works out the dolly zoom of the move into a mark: the lens zooms as the camera travels so
	 * that what the camera is focused on stays the same size in the frame.
	 * @return false when the move into the mark is not a dolly zoom, or the rig has no camera.
	 */
	UECINEEDITOR_API bool PlanDollyZoom(const AUEcineRig* Rig, int32 MarkIndex, FDollyZoom& OutPlan);

	/**
	 * Writes the rig's move through its marks into a Level Sequence as keys on the rig's axes,
	 * the operator imperfection as keys on the mounted camera's transform, and the zoom of any
	 * dolly zoom as keys on the camera's focal length. Keys already in the move's time range
	 * are replaced.
	 * @param StartFrame The frame, at the sequence's display rate, where the rig leaves its first mark.
	 * @return false with the reason in OutError when there is nothing to bake.
	 */
	UECINEEDITOR_API bool BakeMove(AUEcineRig* Rig, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError);

	/**
	 * Bakes the rig's move into the sequence open in Sequencer, starting at the playhead. With no
	 * sequence open it makes one at the camera's frame rate, opens it and bakes from its first frame.
	 * @return the sequence baked into, or null with the reason in OutError.
	 */
	UECINEEDITOR_API ULevelSequence* BakeMoveToSequencer(AUEcineRig* Rig, FText& OutError);
}
