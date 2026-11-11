#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameNumber.h"
#include "UEcinePaperwork.h"
#include "UEcinePlan.h"
#include "UEcineShotTypes.h"

class ACineCameraActor;
class ULevelSequence;
class UMovieSceneCinematicShotSection;

/**
 * The Shots department's operations on the sequence open in Sequencer. The shot list is read from
 * Sequencer, not kept beside it (ADR-007): each section of the sequence's Shot track is a shot, and
 * a sequence with no Shot track is one shot by itself. Every change is a single undoable step.
 */
namespace UEcine::ShotEditing
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
	UECINEEDITOR_API TArray<FShot> GetShots();

	UECINEEDITOR_API FString GetScene(const ULevelSequence* ShotSequence);
	UECINEEDITOR_API EUEcineShotSize GetSize(const ULevelSequence* ShotSequence);
	UECINEEDITOR_API FString GetMove(const ULevelSequence* ShotSequence);
	/** The shot's notes. These are the sequence's own notes, as Sequencer shows them. */
	UECINEEDITOR_API FString GetNotes(const ULevelSequence* ShotSequence);

	UECINEEDITOR_API void SetScene(ULevelSequence* ShotSequence, const FString& Scene);
	UECINEEDITOR_API void SetSize(ULevelSequence* ShotSequence, EUEcineShotSize Size);
	UECINEEDITOR_API void SetMove(ULevelSequence* ShotSequence, const FString& Move);
	UECINEEDITOR_API void SetNotes(ULevelSequence* ShotSequence, const FString& Notes);

	/**
	 * Adds a shot after the last one, named, filed and timed by the project's Sequencer settings.
	 * With no sequence open, a new one is made first, at the camera's frame rate (24 with no
	 * camera), and opened.
	 * @param Camera The camera the shot is seen through, which goes into the shot's sequence with a
	 *               camera cut over the whole shot. Null makes an empty shot.
	 * @return false, with the reason in OutError, when the sequence or the shot could not be made.
	 */
	UECINEEDITOR_API bool AddShot(ACineCameraActor* Camera, FText& OutError);

	/**
	 * Moves Sequencer's playhead to the first frame of a shot and opens the shot there, so that its
	 * own tracks show and moves and racks are baked into it.
	 */
	UECINEEDITOR_API void GoToShot(const FShot& Shot);

	/** The shot under Sequencer's playhead, as an index into GetShots, or INDEX_NONE. */
	UECINEEDITOR_API int32 FindShotAtPlayhead(TConstArrayView<FShot> Shots);

	/**
	 * The camera the shot is seen through: the camera of its first camera cut, or failing that the
	 * first Cine Camera in its sequence. A camera that Sequencer spawns is found only while the
	 * playhead is in the shot. Null when the shot has no camera.
	 */
	UECINEEDITOR_API ACineCameraActor* FindShotCamera(const FShot& Shot);

	/** The pictures taken of a shot, in the order of their frames. */
	UECINEEDITOR_API TConstArrayView<FUEcineStoryboardFrame> GetStoryboardFrames(const ULevelSequence* ShotSequence);

	/**
	 * Takes a picture through the shot's camera as the level stands now, and keeps it with the shot
	 * against the frame the playhead is on.
	 * @return false, with the reason in OutError, when the shot has no camera or the picture could not be taken.
	 */
	UECINEEDITOR_API bool GrabStoryboardFrame(const FShot& Shot, FText& OutError);

	UECINEEDITOR_API void RemoveStoryboardFrame(ULevelSequence* ShotSequence, int32 FrameIndex);

	/**
	 * What the paperwork says about a shot as the level stands now: the typed fields, its timing,
	 * and its camera, lens, exposure and support. Storyboard and plan are left empty.
	 */
	UECINEEDITOR_API FUEcineShotReport MakeShotReport(const FShot& Shot);

	/**
	 * The overhead plan of a shot: where its camera goes, the track it runs on and the actors in the
	 * shot. Plays through the shot in Sequencer to find out, and puts the playhead back.
	 */
	UECINEEDITOR_API FUEcinePlan MakePlan(const FShot& Shot);

	/** Everything the paperwork says about the open sequence. Plays through every shot, and puts the playhead back. */
	UECINEEDITOR_API FUEcinePaperwork BuildPaperwork();

	/**
	 * Writes the shot list and the camera report as CSV files and the printable page as an HTML file,
	 * named after the open sequence, into a folder.
	 * @param OutFiles The files written, in that order.
	 * @return false, with the reason in OutError, when no sequence is open or a file could not be written.
	 */
	UECINEEDITOR_API bool ExportPaperwork(const FString& Directory, TArray<FString>& OutFiles, FText& OutError);

	/** Where the panel writes a sequence's paperwork: a folder named after it under the project's Saved folder. */
	UECINEEDITOR_API FString GetDefaultPaperworkDirectory(const ULevelSequence* Sequence);
}
