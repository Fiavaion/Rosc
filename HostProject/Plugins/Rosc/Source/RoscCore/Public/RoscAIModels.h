// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/** What an AI job makes of a picture. */
enum class ERoscAITask : uint8
{
	/** A grey picture of how near things are: near is bright. Relative, not a distance. */
	Depth,
	/** A grey picture of the subject: white where the subject is. */
	Matte,
	/** The field of view the picture was taken with. */
	FocalLength
};

namespace Rosc::AI
{
	/** The task's name in data files and in the helper's protocol: depth, matte, focal. */
	ROSCCORE_API const TCHAR* GetTaskId(ERoscAITask Task);
}

/** How a model wants its picture. */
struct FRoscAIModelInput
{
	/** The name of the model's picture input. */
	FString Name;
	/** A fixed size, when the model takes only one. Zero otherwise. */
	FIntPoint FixedSize = FIntPoint::ZeroValue;
	/** Otherwise the picture keeps its shape, with its short side about this long and both sides multiples of a number. */
	int32 ShortSide = 0;
	int32 MultipleOf = 1;
	/** Each colour is brought to (value - mean) / deviation, from values of 0 to 1. */
	FVector3f Mean = FVector3f::ZeroVector;
	FVector3f Deviation = FVector3f::OneVector;

	/** The size a picture is given to the model at. */
	ROSCCORE_API FIntPoint GetSizeFor(int32 PictureWidth, int32 PictureHeight) const;
};

/** One model Rosc offers. The file itself is not part of Rosc: the user downloads it. */
struct FRoscAIModel
{
	FString Id;
	ERoscAITask Task = ERoscAITask::Depth;
	FString Name;
	/** The licence of the model's weights, and the page that states it. */
	FString Licence;
	FString LicencePage;
	/** Where the file is downloaded from, what it is called in the models folder, and how it is checked. */
	FString Url;
	FString FileName;
	int64 SizeBytes = 0;
	/** Lower-case hexadecimal. */
	FString Sha256;
	FRoscAIModelInput Input;
	/** Whole numbers the model takes besides the picture, by input name. */
	TMap<FString, int64> IntegerInputs;
	/** The names of the model's result, and of the result that says which pixels to trust when it has one. */
	FString Output;
	FString MaskOutput;
};

/** The models Rosc offers, read from Resources/AI/models.json. */
class FRoscAIModelList
{
public:
	/** The list that ships with the plugin. Faults in the file are logged once. */
	ROSCCORE_API static const FRoscAIModelList& Get();

	/** The file Get reads, which the helper reads too. */
	ROSCCORE_API static FString GetListFile();

	/**
	 * Adds the models of a JSON text. A model with a fault is left out, and the fault is
	 * reported with the source name and the field.
	 * @return true when every model was added.
	 */
	ROSCCORE_API bool AddFromJson(const FString& JsonText, const FString& SourceName, TArray<FString>& OutErrors);

	TConstArrayView<FRoscAIModel> GetModels() const { return Models; }

	/** The model offered for a task, or null. */
	ROSCCORE_API const FRoscAIModel* FindForTask(ERoscAITask Task) const;

private:
	TArray<FRoscAIModel> Models;
};
