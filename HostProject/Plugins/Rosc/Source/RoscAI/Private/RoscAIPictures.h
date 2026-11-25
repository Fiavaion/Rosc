// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ImageCore.h"

struct FRoscAIModelInput;

/** Pictures on their way into and out of a model. Safe to call from any thread. */
namespace Rosc::AIPictures
{
	/**
	 * Reads a still or a frame of a plate (PNG, JPEG, EXR, TIFF and the other formats the engine
	 * reads) as an 8-bit picture for display.
	 */
	bool Load(const FString& File, FImage& OutPicture, FText& OutError);

	/** The picture as a model wants it: resized, each colour brought to the model's range, colour planes one after another. */
	TArray<float> MakeTensor(const FImage& Picture, const FRoscAIModelInput& Input, FIntPoint& OutSize);

	/** A grid of numbers at another size. */
	TArray<float> Resize(TConstArrayView<float> Values, FIntPoint Size, FIntPoint NewSize);

	FImage MakeGrey16(TConstArrayView<uint16> Values, FIntPoint Size);
	FImage MakeGrey8(TConstArrayView<uint8> Values, FIntPoint Size);
}
