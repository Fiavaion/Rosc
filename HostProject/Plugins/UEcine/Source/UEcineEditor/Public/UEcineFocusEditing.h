#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameNumber.h"

class AActor;
class ACineCameraActor;
class ULevelSequence;

/**
 * The focus puller's editing operations. Each one is a single undoable transaction, so the
 * panel, scripts and tests all pull focus the same way. Distances are in centimetres from the
 * camera, measured along the lens axis, as the Cine Camera measures them.
 */
namespace UEcine::FocusEditing
{
	/**
	 * Where the camera's focus is set: the distance set by hand, or the distance to the subject
	 * it is following. This can be nearer than the lens can focus (see GetCloseFocus); the lens
	 * itself then stays at its close focus.
	 */
	UECINEEDITOR_API float GetFocusDistance(const ACineCameraActor* Camera);

	/** The nearest the camera's lens can focus. */
	UECINEEDITOR_API float GetCloseFocus(const ACineCameraActor* Camera);

	/** Where the lens itself is focused: where the focus is set, but never nearer than the lens can go. */
	UECINEEDITOR_API float GetLensDistance(const ACineCameraActor* Camera);

	/**
	 * Sets the focus by hand. A camera that was following a subject stops following it. A
	 * distance nearer than the lens can focus is set to its close focus.
	 */
	UECINEEDITOR_API void SetFocusDistance(ACineCameraActor* Camera, float DistanceCm);

	/**
	 * Focuses on a point in the level.
	 * @return false, with the reason in OutError and the focus unchanged, when the point is behind the camera.
	 */
	UECINEEDITOR_API bool FocusOnPoint(ACineCameraActor* Camera, const FVector& WorldPoint, FText& OutError);

	/**
	 * Lets the user click a subject in the level viewport to focus the camera on it. Picking
	 * ends with the click, with Escape, or when the viewport loses focus.
	 */
	UECINEEDITOR_API void BeginPickFocus(ACineCameraActor* Camera);

	UECINEEDITOR_API void EndPickFocus();

	/** True while the given camera is waiting for a click to focus on. */
	UECINEEDITOR_API bool IsPickingFocus(const ACineCameraActor* Camera);

	/** Keeps the focus on an actor as it and the camera move. */
	UECINEEDITOR_API void FollowActor(ACineCameraActor* Camera, AActor* Subject);

	/** Stops following, leaving the focus where it is. */
	UECINEEDITOR_API void StopFollowing(ACineCameraActor* Camera);

	/** The actor the camera's focus is following, or null. */
	UECINEEDITOR_API AActor* GetFollowedActor(const ACineCameraActor* Camera);

	/**
	 * Stores where the focus is now as a new focus mark after the existing ones. Marks are kept
	 * with the camera package. Returns the mark's index, or INDEX_NONE when the camera has no package.
	 */
	UECINEEDITOR_API int32 AddFocusMark(ACineCameraActor* Camera);

	/** Moves an existing mark to where the focus is now. */
	UECINEEDITOR_API void UpdateFocusMark(ACineCameraActor* Camera, int32 MarkIndex);

	UECINEEDITOR_API void RemoveFocusMark(ACineCameraActor* Camera, int32 MarkIndex);

	/** Puts the focus on a mark at once. */
	UECINEEDITOR_API void GoToFocusMark(ACineCameraActor* Camera, int32 MarkIndex);

	/** Sets how long the camera's racks take. */
	UECINEEDITOR_API void SetRackTime(ACineCameraActor* Camera, float Seconds);

	/**
	 * Writes a rack into a Level Sequence as keys on the camera's focus distance, one on every
	 * frame: from where the focus is at the start frame to the given distance, over the given
	 * time. Keys already in the rack's time range are replaced. A camera that was following a
	 * subject stops following it, because a keyed focus distance is set by hand.
	 * @param StartFrame The frame, at the sequence's display rate, where the focus leaves its old distance.
	 * @return false with the reason in OutError when the rack cannot be written.
	 */
	UECINEEDITOR_API bool BakeRack(ACineCameraActor* Camera, float ToDistanceCm, double RackSeconds, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError);

	/**
	 * Bakes a rack to one of the camera's focus marks, over the camera's rack time, into the
	 * sequence open in Sequencer, starting at the playhead. With no sequence open it makes one at
	 * the camera's frame rate, opens it and bakes from its first frame.
	 * @return the sequence baked into, or null with the reason in OutError.
	 */
	UECINEEDITOR_API ULevelSequence* RackToFocusMark(ACineCameraActor* Camera, int32 MarkIndex, FText& OutError);
}
