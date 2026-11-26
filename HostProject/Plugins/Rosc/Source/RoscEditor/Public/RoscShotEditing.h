// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameNumber.h"
#include "RoscPaperwork.h"
#include "RoscPlan.h"
#include "RoscShotTypes.h"

class ACineCameraActor;
class ULevelSequence;
class UMovieSceneCinematicShotSection;

/**
 * The Shots department's operations on the sequence open in Sequencer. The shot list is read from
 * Sequencer, not kept beside it (ADR-007): each section of the sequence's Shot track is a shot, and
 * a sequence with no Shot track is one shot by itself. Every change is a single undoable step.
 */
namespace Rosc::ShotEditing
{
	/** One shot of the sequence open in Sequencer. Good until that sequence is next changed. */
	struct FShot
	{
		/** The sequence that holds the shot's cameras, keys, notes and storyboard. */
		ULevelSequence* Sequence = nullptr;
		/** The shot's section in the open sequence's Shot track. Null when the open sequence is itself the shot. */
		UMovieSceneCinematicShotSection* Section = nullptr;
		FString Name;
		/** Where the shot starts and how long it runs, in frames of the open sequence. */
		FFrameNumber StartFrame = 0;
		int32 DurationFrames = 0;
	};

	/** The shots of the sequence open in Sequencer, in the order they play. Empty when none is open. */
	ROSCEDITOR_API TArray<FShot> GetShots();

	ROSCEDITOR_API FString GetScene(const ULevelSequence* ShotSequence);
	ROSCEDITOR_API ERoscShotSize GetSize(const ULevelSequence* ShotSequence);
	ROSCEDITOR_API FString GetMove(const ULevelSequence* ShotSequence);
	/** The shot's notes. These are the sequence's own notes, as Sequencer shows them. */
	ROSCEDITOR_API FString GetNotes(const ULevelSequence* ShotSequence);

	ROSCEDITOR_API void SetScene(ULevelSequence* ShotSequence, const FString& Scene);
	ROSCEDITOR_API void SetSize(ULevelSequence* ShotSequence, ERoscShotSize Size);
	ROSCEDITOR_API void SetMove(ULevelSequence* ShotSequence, const FString& Move);
	ROSCEDITOR_API void SetNotes(ULevelSequence* ShotSequence, const FString& Notes);

	/**
	 * Adds a shot after the last one, named, filed and timed by the project's Sequencer settings.
	 * With no sequence open, a new one is made first, at the camera's frame rate (24 with no
	 * camera), and opened.
	 * @param Camera The camera the shot is seen through, which goes into the shot's sequence with a
	 *               camera cut over the whole shot. Null makes an empty shot.
	 * @return false, with the reason in OutError, when the sequence or the shot could not be made.
	 */
	ROSCEDITOR_API bool AddShot(ACineCameraActor* Camera, FText& OutError);

	/**
	 * Moves Sequencer's playhead to the first frame of a shot and opens the shot there, so that its
	 * own tracks show and moves and racks are baked into it.
	 */
	ROSCEDITOR_API void GoToShot(const FShot& Shot);

	/** The shot under Sequencer's playhead, as an index into GetShots, or INDEX_NONE. */
	ROSCEDITOR_API int32 FindShotAtPlayhead(TConstArrayView<FShot> Shots);

	/**
	 * The camera the shot is seen through: the camera of its first camera cut, or failing that the
	 * first Cine Camera in its sequence. A camera that Sequencer spawns is found only while the
	 * playhead is in the shot. Null when the shot has no camera.
	 */
	ROSCEDITOR_API ACineCameraActor* FindShotCamera(const FShot& Shot);

	/** What a binding of the shot's sequence stands for in the level right now. */
	ROSCEDITOR_API TArray<UObject*> ResolveBinding(const FShot& Shot, const FGuid& Binding);

	/** The pictures taken of a shot, in the order of their frames. */
	ROSCEDITOR_API TConstArrayView<FRoscStoryboardFrame> GetStoryboardFrames(const ULevelSequence* ShotSequence);

	/**
	 * Takes a picture through the shot's camera as the level stands now, and keeps it with the shot
	 * against the frame the playhead is on.
	 * @return false, with the reason in OutError, when the shot has no camera or the picture could not be taken.
	 */
	ROSCEDITOR_API bool GrabStoryboardFrame(const FShot& Shot, FText& OutError);

	/** Takes a storyboard picture through a camera as the level stands. False when there is no renderer to take it with. */
	ROSCEDITOR_API bool TakeStoryboardPicture(const ACineCameraActor& Camera, FRoscStoryboardFrame& OutFrame);

	ROSCEDITOR_API void RemoveStoryboardFrame(ULevelSequence* ShotSequence, int32 FrameIndex);

	/**
	 * What the paperwork says about a shot as the level stands now: the typed fields, its timing,
	 * and its camera, lens, exposure and support. Storyboard and plan are left empty.
	 */
	ROSCEDITOR_API FRoscShotReport MakeShotReport(const FShot& Shot);

	/**
	 * The overhead plan of a shot: where its camera goes, the track it runs on and the actors in the
	 * shot. Plays through the shot in Sequencer to find out, and puts the playhead back.
	 */
	ROSCEDITOR_API FRoscPlan MakePlan(const FShot& Shot);

	/** Everything the paperwork says about the open sequence. Plays through every shot, and puts the playhead back. */
	ROSCEDITOR_API FRoscPaperwork BuildPaperwork();

	/**
	 * Writes the shot list and the camera report as CSV files and the printable page as an HTML file,
	 * named after the open sequence, into a folder.
	 * @param OutFiles The files written, in that order.
	 * @return false, with the reason in OutError, when no sequence is open or a file could not be written.
	 */
	ROSCEDITOR_API bool ExportPaperwork(const FString& Directory, TArray<FString>& OutFiles, FText& OutError);

	/** The folder paperwork is written under: the one the user chose, or Rosc/Paperwork in the project's Saved folder. */
	ROSCEDITOR_API FString GetPaperworkFolder();

	/** Chooses the folder paperwork is written under, for this user and project. Empty goes back to the project's own. */
	ROSCEDITOR_API void SetPaperworkFolder(const FString& Folder);

	/** Where the panel writes a sequence's paperwork: a folder named after it in the paperwork folder. */
	ROSCEDITOR_API FString GetPaperworkDirectory(const ULevelSequence* Sequence);

	/**
	 * A value that changes with every edit of a shot's sequence: a move baked into it, a key
	 * moved, a camera changed. What was worked out from the shot is as old as the value it
	 * was worked out at.
	 */
	ROSCEDITOR_API FGuid GetShotSignature(const ULevelSequence* Sequence);
}
