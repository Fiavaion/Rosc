// Copyright 2026 Fiavaion. All rights reserved.

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
	 * Keeps the focus on a point in the level as the camera moves. With a subject, the point is
	 * kept as a place on it (an eye, not the whole figure) and moves with it.
	 * @return false, with the reason in OutError and the focus unchanged, when the point is behind the camera.
	 */
	UECINEEDITOR_API bool HoldPoint(ACineCameraActor* Camera, AActor* Subject, const FVector& WorldPoint, FText& OutError);

	/**
	 * Lets the user click a subject in the level viewport to focus the camera on it. Picking
	 * ends with the click, with Escape, or when the viewport loses focus.
	 * @param bHold Keep the focus on the point clicked as it and the camera move, instead of setting a distance.
	 */
	UECINEEDITOR_API void BeginPickFocus(ACineCameraActor* Camera, bool bHold = false);

	UECINEEDITOR_API void EndPickFocus();

	/** True while the given camera is waiting for a click to focus on, in the given way. */
	UECINEEDITOR_API bool IsPickingFocus(const ACineCameraActor* Camera, bool bHold = false);

	/** Keeps the focus on an actor as it and the camera move. */
	UECINEEDITOR_API void FollowActor(ACineCameraActor* Camera, AActor* Subject);

	/** True when the focus is kept on something, a subject or a point, as the camera moves. */
	UECINEEDITOR_API bool IsFollowing(const ACineCameraActor* Camera);

	/** True when what the focus is kept on is a picked point, not a subject as a whole. */
	UECINEEDITOR_API bool IsHoldingPoint(const ACineCameraActor* Camera);

	/** Stops following, leaving the focus where it is. */
	UECINEEDITOR_API void StopFollowing(ACineCameraActor* Camera);

	/** The actor the camera's focus is following, or null. */
	UECINEEDITOR_API AActor* GetFollowedActor(const ACineCameraActor* Camera);

	/**
	 * Stores where the focus is now as a new focus mark after the existing ones: the point or
	 * subject the focus is kept on, or else its distance. Marks are kept with the camera
	 * package. Returns the mark's index, or INDEX_NONE when the camera has no package.
	 */
	UECINEEDITOR_API int32 AddFocusMark(ACineCameraActor* Camera);

	/** Moves an existing mark to where the focus is now. */
	UECINEEDITOR_API void UpdateFocusMark(ACineCameraActor* Camera, int32 MarkIndex);

	UECINEEDITOR_API void RemoveFocusMark(ACineCameraActor* Camera, int32 MarkIndex);

	/** Puts the focus on a mark at once. A mark on a point keeps the focus on that point from then on. */
	UECINEEDITOR_API void GoToFocusMark(ACineCameraActor* Camera, int32 MarkIndex);

	/** How far a mark is from the camera now: a mark on a point is measured to where the point is. */
	UECINEEDITOR_API float GetFocusMarkDistance(const ACineCameraActor* Camera, int32 MarkIndex);

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
	 * Writes a rack to a point in the level, and then keeps the focus on the point: a key on
	 * every frame from the start frame to the end of the sequence's playback range, each measured
	 * from where the camera and the subject are on that frame. The frames are read by moving
	 * Sequencer's playhead, so the sequence has to be the one open in it; otherwise every frame
	 * is measured from where the camera is now.
	 */
	UECINEEDITOR_API bool BakeRackToPoint(ACineCameraActor* Camera, AActor* Subject, const FVector& Offset, double RackSeconds, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError);

	/**
	 * Bakes a rack to one of the camera's focus marks, over the camera's rack time, into the
	 * sequence open in Sequencer, starting at the playhead. With no sequence open it makes one at
	 * the camera's frame rate, opens it and bakes from its first frame.
	 * @return the sequence baked into, or null with the reason in OutError.
	 */
	UECINEEDITOR_API ULevelSequence* RackToFocusMark(ACineCameraActor* Camera, int32 MarkIndex, FText& OutError);
}
