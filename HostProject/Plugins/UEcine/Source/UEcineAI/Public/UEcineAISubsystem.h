#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEcineAIModels.h"
#include "UEcineAISettings.h"
#include "UEcineAISubsystem.generated.h"

class FUEcineAIHelper;
class FUEcineAIModelDownload;
class FUEcineAIServerBackend;
class IUEcineAIBackend;

/** A picture job: one task on one still or frame of a plate. */
struct FUEcineAIRequest
{
	EUEcineAITask Task = EUEcineAITask::Depth;
	/** The picture's file. */
	FString PictureFile;
};

/** How a picture job ended. */
struct FUEcineAIResult
{
	FUEcineAIRequest Request;
	bool bSuccess = false;
	/** Why it did not succeed. Empty for a job the user gave up. */
	FText Error;
	/** The depth map or matte that was written. */
	FString File;
	/** The horizontal field of view the picture was taken with, for a focal length job. */
	double FieldOfViewDeg = 0.0;
};

/**
 * Runs UEcine's picture jobs (depth, matte, focal length) one at a time, in the order asked,
 * on the backend chosen in the user's settings: in the editor, in the helper on this machine,
 * or on a server elsewhere. Nothing is sent to a server elsewhere until the user has agreed to
 * that host (UEcine::AIConsent); the check is made here, for every job.
 */
UCLASS()
class UECINEAI_API UUEcineAISubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Whether the chosen backend can run a task now; when not, why, in words. */
	bool CanRun(EUEcineAITask Task, FText& OutWhyNot) const;

	/**
	 * The host the chosen backend would send pictures to and the user has not agreed to yet.
	 * Empty when nothing stands in the way.
	 */
	FString GetHostNeedingConsent() const;

	/**
	 * Asks for a job. It runs after the jobs asked for before it; see IsWorking and OnJobFinished.
	 * @return false with the reason in OutError when the job cannot be taken.
	 */
	bool Submit(const FUEcineAIRequest& Request, FText& OutError);

	bool IsWorking() const { return bRunning; }
	/** How many jobs are running or waiting. */
	int32 GetJobsLeft() const { return Waiting.Num() + (bRunning ? 1 : 0); }
	/** What the running job is doing, in words, and how far along it is from 0 to 1. */
	void GetProgress(FText& OutStage, float& OutFraction) const;

	/** Gives up the running job and those waiting. Each still ends through OnJobFinished. */
	void CancelAll();

	/** How the last job ended. */
	const FUEcineAIResult& GetLastResult() const { return LastResult; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnJobFinished, const FUEcineAIResult&);
	FOnJobFinished& OnJobFinished() { return JobFinished; }

	/** Where a job's result is written: a folder named after the picture under the project's Saved folder. */
	static FString GetResultFolder(const FString& PictureFile);

	/**
	 * Downloads a model's file into the models folder; one download at a time. The user asks
	 * for this, having seen the model's licence.
	 * @return false when another download is running.
	 */
	bool DownloadModel(const FUEcineAIModel& Model);
	/** The model being downloaded, or null. */
	const FUEcineAIModel* GetDownloadingModel() const;
	float GetDownloadFraction() const;
	void CancelDownload();
	/** Why the last download failed. Empty when it did not. */
	const FText& GetDownloadError() const { return DownloadError; }

	/** How the helper on this machine stands, in words: not started, starting, ready, or what is wrong. */
	FText GetHelperStatus() const;

	/**
	 * Asks the server elsewhere what it can do, to try the address and key in the settings.
	 * Sends the key and nothing else, so it does not wait for the user's agreement.
	 */
	void CheckServer(TFunction<void(bool bReachable, const FText& Message)> OnChecked);

private:
	struct FWaitingJob
	{
		FUEcineAIRequest Request;
		EUEcineAIBackend Backend = EUEcineAIBackend::Editor;
	};

	IUEcineAIBackend& GetBackend(EUEcineAIBackend Backend) const;
	void StartNext();
	void Finish(FUEcineAIResult&& Result);

	TSharedPtr<IUEcineAIBackend> EditorBackend;
	TSharedPtr<FUEcineAIHelper> Helper;
	TSharedPtr<FUEcineAIServerBackend> HelperBackend;
	TSharedPtr<FUEcineAIServerBackend> ServerBackend;

	TArray<FWaitingJob> Waiting;
	FWaitingJob Running;
	bool bRunning = false;
	/** Counts jobs, so that an answer that comes after its job was dropped is not taken for the next one's. */
	int32 RunNumber = 0;

	FUEcineAIResult LastResult;
	FOnJobFinished JobFinished;

	TSharedPtr<FUEcineAIModelDownload> Download;
	FText DownloadError;
};
