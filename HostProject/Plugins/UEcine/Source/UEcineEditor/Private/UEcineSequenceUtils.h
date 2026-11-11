#pragma once

#include "CoreMinimal.h"
#include "Math/Range.h"
#include "Misc/FrameNumber.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"

class AActor;
class ULevelSequence;

/** Small helpers shared by everything in UEcine that writes keys into a Level Sequence. */
namespace UEcine::SequenceUtils
{
	/** The sequence's binding for an actor in the level, or an invalid guid when the actor is not in the sequence. */
	FGuid FindBinding(ULevelSequence* Sequence, AActor* Actor);

	/** The sequence's binding for an actor, made if the actor is not in the sequence yet. */
	FGuid FindOrAddBinding(ULevelSequence* Sequence, AActor* Actor);

	/** The track's one section, covering all time, made if the track is empty. */
	template <typename SectionType>
	SectionType* FindOrAddSection(UMovieSceneTrack* Track)
	{
		if (!Track->GetAllSections().IsEmpty())
		{
			return CastChecked<SectionType>(Track->GetAllSections()[0]);
		}
		SectionType* Section = CastChecked<SectionType>(Track->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>::All());
		Track->AddSection(*Section);
		return Section;
	}

	template <typename ChannelType>
	void DeleteKeysInRange(ChannelType& Channel, const TRange<FFrameNumber>& Range)
	{
		TArray<FKeyHandle> Handles;
		Channel.GetKeys(Range, nullptr, &Handles);
		Channel.DeleteKeys(Handles);
	}
}
