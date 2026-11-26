// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Math/Range.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameRate.h"
#include "Misc/FrameTime.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"

class AActor;
class ACineCameraActor;
class UActorComponent;
class ULevelSequence;
class UMovieScene;
class UMovieSceneFloatTrack;

/** Small helpers shared by everything in Rosc that writes keys into a Level Sequence. */
namespace Rosc::SequenceUtils
{
	/** The Sequencer frame rate for a camera frame rate: 23.976 becomes 24000/1001, and so on. */
	FFrameRate GetFrameRate(double FramesPerSecond);

	/** The frame rate a sequence made for a camera runs at: the camera package's, or 24 with no package. */
	FFrameRate GetCameraFrameRate(const ACineCameraActor* Camera);

	/** Where the sequences the panel makes by itself are kept. */
	inline const TCHAR* const SequenceFolder = TEXT("/Game/Rosc/Sequences");

	/**
	 * Creates a Level Sequence asset with the given display rate.
	 * @param PackageName Where to make it, such as "/Game/Rosc/Sequences/Crane". A number is added when the name is taken.
	 */
	ULevelSequence* CreateSequence(const FString& PackageName, FFrameRate DisplayRate);

	/** Puts Sequencer's playhead back where it was when a look through the open sequence is over. */
	struct FScopedPlayhead
	{
		FScopedPlayhead();
		~FScopedPlayhead();

		FScopedPlayhead(const FScopedPlayhead&) = delete;
		FScopedPlayhead& operator=(const FScopedPlayhead&) = delete;

	private:
		/** In ticks of the open sequence. */
		FFrameTime Ticks;
	};

	/** Moves Sequencer's playhead to a tick of the open sequence and plays the level to that moment. */
	void SetPlayheadTicks(FFrameTime Ticks);

	/** Where a bake from the panel is written. */
	struct FBakeTarget
	{
		/** Null when there was no sequence open and a new one could not be made. */
		ULevelSequence* Sequence = nullptr;
		/** At the sequence's display rate. */
		FFrameNumber StartFrame = 0;
	};

	/**
	 * The sequence being worked on in Sequencer, at its playhead: inside a shot, that is the shot's
	 * own sequence. With none open, a new sequence is made at the camera's frame rate, named after
	 * the actor with the suffix, and opened; the bake then starts at its first frame.
	 */
	FBakeTarget FindOrCreateBakeTarget(const AActor* NamedAfter, const TCHAR* NameSuffix, const ACineCameraActor* Camera);

	/** The sequence's binding for an actor or component in the level, or an invalid guid when it is not in the sequence. */
	FGuid FindBinding(ULevelSequence* Sequence, UObject* Object);

	/** The sequence's binding for an actor, made if the actor is not in the sequence yet. */
	FGuid FindOrAddBinding(ULevelSequence* Sequence, AActor* Actor);

	/** The sequence's binding for a component, under its actor's binding, made if it is not in the sequence yet. */
	FGuid FindOrAddComponentBinding(ULevelSequence* Sequence, UActorComponent* Component);

	/** The binding's track for a number property, or null when the property has no track. */
	UMovieSceneFloatTrack* FindFloatTrack(UMovieScene* MovieScene, const FGuid& Binding, FName PropertyName);

	/**
	 * The binding's track for a number property, made if the property has none yet.
	 * @param PropertyPath The property's path from the bound object, such as "FocusSettings.ManualFocusDistance".
	 */
	UMovieSceneFloatTrack* FindOrAddFloatTrack(UMovieScene* MovieScene, const FGuid& Binding, FName PropertyName, const FString& PropertyPath, const FText& DisplayName);

	/** Grows the sequence's playback range to take in a range of ticks, so what was baked there can be played. */
	void IncludeInPlaybackRange(UMovieScene* MovieScene, const TRange<FFrameNumber>& Range);

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
