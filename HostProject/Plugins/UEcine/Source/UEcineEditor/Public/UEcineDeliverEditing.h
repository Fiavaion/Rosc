#pragma once

#include "CoreMinimal.h"
#include "UEcineDeliver.h"

class UUEcineEditorSettings;

/**
 * The Deliver department's operations on the sequence open in Sequencer: what a delivery would
 * be made of, what is wrong with it, putting that right, and rendering it. The panel, scripts
 * and tests all deliver the same way.
 */
namespace UEcine::DeliverEditing
{
	/**
	 * Remembers the colour space the editor started in, which is the one its pictures are in
	 * until it is restarted, whatever the project is set to since. Called once as the editor
	 * module starts.
	 */
	UECINEEDITOR_API void NoteColourSpaceAtStartup();

	/**
	 * Everything a delivery is told beside its intent: the user's choices, the sequence open in
	 * Sequencer, today's date and the project's colour space. The output folder is the
	 * sequence's own folder under the delivery folder.
	 */
	UECINEEDITOR_API FUEcineDeliverSettings GetSettings();

	/** Changes the user's choices for deliveries and keeps them for the next session. */
	UECINEEDITOR_API void EditChoices(TFunctionRef<void(UUEcineEditorSettings&)> Edit);

	/** What a look through the open sequence found for a delivery. */
	struct FCheck
	{
		/** What preflight found, the findings that stop the render first. */
		TArray<FUEcinePreflightFinding> Findings;
		/** The first file each shot would be written to, from the sequence's delivery folder on. Empty while the name pattern cannot name files. */
		TArray<FString> Files;
	};

	/**
	 * Looks through the open sequence for a delivery. Plays to the start of each shot to find its
	 * camera, and puts the playhead back.
	 */
	UECINEEDITOR_API FCheck CheckDelivery(EUEcineDeliverIntent Intent);

	/**
	 * Puts right what a finding with a fix found, as one undoable step. The project's working
	 * colour space is a setting of the project, written to its settings file, and is not undone
	 * with the editor's undo.
	 * @return false with the reason in OutError when it could not be put right.
	 */
	UECINEEDITOR_API bool ApplyFix(const FUEcinePreflightFinding& Finding, FText& OutError);

	/** A delivery that has been started. */
	struct FDelivery
	{
		/** The folder the files are written to. */
		FString Directory;
		/** The version every shot of the delivery is named with. */
		int32 Version = 0;
		/** The files written beside the pictures before the render began: each shot's sidecar and camera, for a VFX handoff. */
		TArray<FString> Files;
	};

	/**
	 * Renders every shot of the open sequence for an intent. Preflight runs first, and a finding
	 * that stops the render stops it here. A VFX handoff is baked for handoff first, and each
	 * shot's sidecar file and FBX camera are written beside its frames. The render then runs over
	 * the following editor frames: see UUEcineRenderSubsystem.
	 * @return false with the reason in OutError when the delivery cannot start.
	 */
	UECINEEDITOR_API bool StartDelivery(EUEcineDeliverIntent Intent, FDelivery& OutDelivery, FText& OutError);
}
