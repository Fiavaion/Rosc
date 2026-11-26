// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscShotTypes.h"
#include "UObject/Object.h"
#include "RoscShotMetaData.generated.h"

/**
 * What the shot list says about one shot that Sequencer does not already know. It is kept inside
 * the shot's Level Sequence as one of its metadata objects, so it travels with the shot when the
 * shot is duplicated or moved (ADR-007). Notes are not here: they use the engine's own sequence
 * metadata.
 */
UCLASS()
class ROSCRUNTIME_API URoscShotMetaData : public UObject
{
	GENERATED_BODY()

public:
	/** The scene of the script the shot belongs to, as written there: "12", "12A". */
	UPROPERTY(EditAnywhere, Category = "Rosc")
	FString Scene;

	UPROPERTY(EditAnywhere, Category = "Rosc")
	ERoscShotSize Size = ERoscShotSize::Unset;

	/** What the camera does, in the crew's words: "Push in", "Crane up and hold". */
	UPROPERTY(EditAnywhere, Category = "Rosc")
	FString Move;

	/** Pictures of the shot through its camera, in the order of their frames. */
	UPROPERTY()
	TArray<FRoscStoryboardFrame> StoryboardFrames;
};
