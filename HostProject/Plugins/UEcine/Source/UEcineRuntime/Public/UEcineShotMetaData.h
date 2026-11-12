// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UEcineShotTypes.h"
#include "UObject/Object.h"
#include "UEcineShotMetaData.generated.h"

/**
 * What the shot list says about one shot that Sequencer does not already know. It is kept inside
 * the shot's Level Sequence as one of its metadata objects, so it travels with the shot when the
 * shot is duplicated or moved (ADR-007). Notes are not here: they use the engine's own sequence
 * metadata.
 */
UCLASS()
class UECINERUNTIME_API UUEcineShotMetaData : public UObject
{
	GENERATED_BODY()

public:
	/** The scene of the script the shot belongs to, as written there: "12", "12A". */
	UPROPERTY(EditAnywhere, Category = "UEcine")
	FString Scene;

	UPROPERTY(EditAnywhere, Category = "UEcine")
	EUEcineShotSize Size = EUEcineShotSize::Unset;

	/** What the camera does, in the crew's words: "Push in", "Crane up and hold". */
	UPROPERTY(EditAnywhere, Category = "UEcine")
	FString Move;

	/** Pictures of the shot through its camera, in the order of their frames. */
	UPROPERTY()
	TArray<FUEcineStoryboardFrame> StoryboardFrames;
};
