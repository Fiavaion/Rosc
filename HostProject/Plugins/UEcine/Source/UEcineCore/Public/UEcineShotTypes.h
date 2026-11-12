// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UEcineShotTypes.generated.h"

/** How much of the subject a shot holds, in the words of a shot list. */
UENUM(BlueprintType)
enum class EUEcineShotSize : uint8
{
	/** Not chosen yet. */
	Unset,
	ExtremeWide,
	Wide,
	Full,
	MediumWide,
	Medium,
	MediumCloseUp,
	CloseUp,
	ExtremeCloseUp,
	Insert,
	OverTheShoulder,
	PointOfView,
	TwoShot
};

/** One storyboard picture of a shot: what its camera saw at one frame. */
USTRUCT()
struct FUEcineStoryboardFrame
{
	GENERATED_BODY()

	/** The frame the picture was taken at, counted from the first frame of the shot. */
	UPROPERTY()
	int32 ShotFrame = 0;

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	/** The picture as a JPEG file. */
	UPROPERTY()
	TArray<uint8> Jpeg;
};

namespace UEcine::Shots
{
	/** Every shot size in the order a shot list offers them, "not set" first. */
	UECINECORE_API TConstArrayView<EUEcineShotSize> GetShotSizes();

	/** "Close-up", "Over the shoulder"; "Not set" for a size not chosen yet. */
	UECINECORE_API FText GetShotSizeName(EUEcineShotSize Size);

	/** The short form written in a shot list: "CU", "OTS". Empty for a size not chosen yet. */
	UECINECORE_API FText GetShotSizeAbbreviation(EUEcineShotSize Size);
}
