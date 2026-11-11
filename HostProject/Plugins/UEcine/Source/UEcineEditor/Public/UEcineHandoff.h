#pragma once

#include "CoreMinimal.h"

class ULevelSequence;

/**
 * Bake for handoff: copies of a sequence and its shots that play the same pictures with no
 * UEcine rig, no camera package and no plugin installed, to give to editorial and VFX.
 */
namespace UEcine::Handoff
{
	/**
	 * Where the handoff copy of a sequence is kept: beside the sequence, with "_Handoff" after
	 * its name.
	 */
	UECINEEDITOR_API FString GetHandoffPackageName(const ULevelSequence* Sequence);

	/** True for a sequence that is itself a handoff copy. */
	UECINEEDITOR_API bool IsHandoffCopy(const ULevelSequence* Sequence);

	/**
	 * Makes handoff copies of the sequence open in Sequencer and of each of its shots. The
	 * sequences being worked on are left as they are.
	 *
	 * In a copy, every Cine Camera of a shot is replaced by a plain camera that the sequence
	 * spawns for itself, with a key on every frame for where it is in the level, its focal
	 * length, its aperture and its focus distance: what the rig, the operator, a followed
	 * subject and the camera's own keys made it do. The rigs and the shot list data are left
	 * out. Everything else in the shots is copied as it is. Copies from an earlier bake are
	 * replaced.
	 *
	 * @return the copy of the open sequence, or null with the reason in OutError.
	 */
	UECINEEDITOR_API ULevelSequence* BakeForHandoff(FText& OutError);
}
