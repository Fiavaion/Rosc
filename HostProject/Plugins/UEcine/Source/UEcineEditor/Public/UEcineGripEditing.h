#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameRate.h"
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

	/** The Sequencer frame rate for a camera frame rate: 23.976 becomes 24000/1001, and so on. */
	UECINEEDITOR_API FFrameRate GetFrameRate(double FramesPerSecond);

	/** Creates a Level Sequence asset under /Game/UEcine/Sequences with the given display rate. */
	UECINEEDITOR_API ULevelSequence* CreateSequence(const FString& BaseName, FFrameRate DisplayRate);

	/**
	 * Writes the rig's move through its marks into a Level Sequence as keys on the rig's axes,
	 * and the operator imperfection as keys on the mounted camera's transform. Keys already in
	 * the move's time range are replaced.
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
