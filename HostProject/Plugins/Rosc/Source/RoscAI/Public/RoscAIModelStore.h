// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoscAIModels.h"

/**
 * The folder of model files on this machine. Models are not part of Rosc: each is downloaded
 * when the user asks for it, having seen its licence, and checked against the list before use.
 */
namespace Rosc::AIModelStore
{
	/** The models folder: the one in the user's settings, or Rosc/Models in their local application data. */
	ROSCAI_API FString GetFolder();

	/** Where a model's file is, or would be. */
	ROSCAI_API FString GetFile(const FRoscAIModel& Model);

	/** True when the model's file is in the folder at the size the list gives. Quick; see Verify. */
	ROSCAI_API bool IsInstalled(const FRoscAIModel& Model);

	/** The SHA-256 of a file in lower-case hexadecimal, or empty when it cannot be read. */
	ROSCAI_API FString HashFile(const FString& File);

	/**
	 * Checks that the model's file is the file the list means, by its SHA-256. This reads all of
	 * the file, so call it off the game thread, and when a model is brought into memory, not for
	 * every job. Safe to call from any thread.
	 * @return false with the reason in OutError.
	 */
	ROSCAI_API bool Verify(const FRoscAIModel& Model, FText& OutError);
}

/** Downloads one model's file into the models folder, checks it, and only then gives it its name. */
class FRoscAIModelDownload : public TSharedFromThis<FRoscAIModelDownload>
{
public:
	DECLARE_DELEGATE_TwoParams(FOnDone, bool /*bSuccess*/, const FText& /*Error*/);

	/** Starts at once. OnDone is called on the game thread, also when the download is cancelled. */
	ROSCAI_API static TSharedRef<FRoscAIModelDownload> Start(const FRoscAIModel& Model, FOnDone OnDone);

	ROSCAI_API ~FRoscAIModelDownload();

	const FRoscAIModel& GetModel() const { return Model; }
	/** From 0 to 1. */
	float GetFraction() const { return Fraction; }
	ROSCAI_API void Cancel();

private:
	explicit FRoscAIModelDownload(const FRoscAIModel& InModel) : Model(InModel) {}
	void Finish(bool bSuccess, const FText& Error);

	FRoscAIModel Model;
	FOnDone OnDone;
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request;
	TSharedPtr<FArchive> PartFile;
	float Fraction = 0.0f;
	bool bCancelled = false;
};
