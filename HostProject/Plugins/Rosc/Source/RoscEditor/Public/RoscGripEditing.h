// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameNumber.h"
#include "RoscRigTypes.h"

class ACineCameraActor;
class ARoscRig;
class ULevelSequence;

/**
 * The grip department's editing operations. Each one is a single undoable transaction, so the
 * panel, scripts and tests all work rigs the same way.
 */
namespace Rosc::GripEditing
{
	/** The rig a camera is mounted on, or null when it is on nothing. */
	ROSCEDITOR_API ARoscRig* FindRig(const ACineCameraActor* Camera);

	/** How a support would be set to hold the camera where it is: the pose PutCameraOnRig stands it up in. */
	ROSCEDITOR_API FRoscRigPose GetPoseOnRig(const ACineCameraActor* Camera, const FRoscRigPreset& Preset);

	/**
	 * Puts a camera on a support without moving the camera: the rig is stood up underneath it,
	 * on the floor when there is one within reach. A camera already on a rig keeps that rig
	 * actor, which becomes the new kind of support; its marks are kept when the type is the same.
	 */
	ROSCEDITOR_API ARoscRig* PutCameraOnRig(ACineCameraActor* Camera, const FRoscRigPreset& Preset);

	/** Takes the camera off its rig, leaving it where it is, and removes the rig. */
	ROSCEDITOR_API void TakeCameraOffRig(ACineCameraActor* Camera);

	/** Changes a rig as one undoable step and redraws the viewports. */
	ROSCEDITOR_API void EditRig(ARoscRig* Rig, const FText& Description, TFunctionRef<void(ARoscRig&)> Edit);

	/** Stores where the rig is now as a new mark after the existing ones. Returns the mark's index. */
	ROSCEDITOR_API int32 AddMark(ARoscRig* Rig);

	/** Moves an existing mark to where the rig is now. */
	ROSCEDITOR_API void UpdateMark(ARoscRig* Rig, int32 MarkIndex);

	ROSCEDITOR_API void RemoveMark(ARoscRig* Rig, int32 MarkIndex);

	/** Puts the rig on a mark. */
	ROSCEDITOR_API void GoToMark(ARoscRig* Rig, int32 MarkIndex);

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
	ROSCEDITOR_API bool CanDollyZoom(const ARoscRig* Rig);

	/**
	 * Works out the dolly zoom of the move into a mark: the lens zooms as the camera travels so
	 * that what the camera is focused on stays the same size in the frame.
	 * @return false when the move into the mark is not a dolly zoom, or the rig has no camera.
	 */
	ROSCEDITOR_API bool PlanDollyZoom(const ARoscRig* Rig, int32 MarkIndex, FDollyZoom& OutPlan);

	/**
	 * Writes the rig's move through its marks into a Level Sequence as keys on the rig's axes,
	 * the operator imperfection as keys on the mounted camera's transform, and the zoom of any
	 * dolly zoom as keys on the camera's focal length. Keys already in the move's time range
	 * are replaced.
	 * @param StartFrame The frame, at the sequence's display rate, where the rig leaves its first mark.
	 * @return false with the reason in OutError when there is nothing to bake.
	 */
	ROSCEDITOR_API bool BakeMove(ARoscRig* Rig, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError);

	/**
	 * Bakes the rig's move into the sequence open in Sequencer, starting at the playhead. With no
	 * sequence open it makes one at the camera's frame rate, opens it and bakes from its first frame.
	 * @return the sequence baked into, or null with the reason in OutError.
	 */
	ROSCEDITOR_API ULevelSequence* BakeMoveToSequencer(ARoscRig* Rig, FText& OutError);

	/**
	 * Writes a take, one pose for every frame of the sequence from the start frame, as a key on
	 * every frame of the axes a device drives: the head's, and on a free-moving support its
	 * position. Keys already in the take's frames are replaced, the camera's baked imperfection
	 * among them.
	 * @return false with the reason in OutError.
	 */
	ROSCEDITOR_API bool BakeTake(ARoscRig* Rig, ULevelSequence* Sequence, FFrameNumber StartFrame, TConstArrayView<FRoscRigPose> Poses, FText& OutError);
}
