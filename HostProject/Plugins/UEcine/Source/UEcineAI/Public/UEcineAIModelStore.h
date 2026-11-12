#pragma once

#include "CoreMinimal.h"
#include "UEcineAIModels.h"

/**
 * The folder of model files on this machine. Models are not part of UEcine: each is downloaded
 * when the user asks for it, having seen its licence, and checked against the list before use.
 */
namespace UEcine::AIModelStore
{
	/** The models folder: the one in the user's settings, or UEcine/Models in their local application data. */
	UECINEAI_API FString GetFolder();

	/** Where a model's file is, or would be. */
	UECINEAI_API FString GetFile(const FUEcineAIModel& Model);

	/** True when the model's file is in the folder at the size the list gives. Quick; see Verify. */
	UECINEAI_API bool IsInstalled(const FUEcineAIModel& Model);

	/** The SHA-256 of a file in lower-case hexadecimal, or empty when it cannot be read. */
	UECINEAI_API FString HashFile(const FString& File);

	/**
	 * Checks that the model's file is the file the list means, by its SHA-256. This reads all of
	 * the file, so call it off the game thread, and when a model is brought into memory, not for
	 * every job. Safe to call from any thread.
	 * @return false with the reason in OutError.
	 */
	UECINEAI_API bool Verify(const FUEcineAIModel& Model, FText& OutError);
}

/** Downloads one model's file into the models folder, checks it, and only then gives it its name. */
class FUEcineAIModelDownload : public TSharedFromThis<FUEcineAIModelDownload>
{
public:
	DECLARE_DELEGATE_TwoParams(FOnDone, bool /*bSuccess*/, const FText& /*Error*/);

	/** Starts at once. OnDone is called on the game thread, also when the download is cancelled. */
	UECINEAI_API static TSharedRef<FUEcineAIModelDownload> Start(const FUEcineAIModel& Model, FOnDone OnDone);

	UECINEAI_API ~FUEcineAIModelDownload();

	const FUEcineAIModel& GetModel() const { return Model; }
	/** From 0 to 1. */
	float GetFraction() const { return Fraction; }
	UECINEAI_API void Cancel();

private:
	explicit FUEcineAIModelDownload(const FUEcineAIModel& InModel) : Model(InModel) {}
	void Finish(bool bSuccess, const FText& Error);

	FUEcineAIModel Model;
	FOnDone OnDone;
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request;
	TSharedPtr<FArchive> PartFile;
	float Fraction = 0.0f;
	bool bCancelled = false;
};
