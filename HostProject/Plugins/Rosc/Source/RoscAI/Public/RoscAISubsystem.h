// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "RoscAIModels.h"
#include "RoscAISettings.h"
#include "RoscShotPlan.h"
#include "RoscAISubsystem.generated.h"

class FRoscAIHelper;
class FRoscAIModelDownload;
class FRoscAIServerBackend;
class IRoscAIBackend;
class IRoscAssistantModel;

/** A picture job: one task on one still or frame of a plate. */
struct FRoscAIRequest
{
	ERoscAITask Task = ERoscAITask::Depth;
	/** The picture's file. */
	FString PictureFile;
};

/** How a picture job ended. */
struct FRoscAIResult
{
	FRoscAIRequest Request;
	bool bSuccess = false;
	/** Why it did not succeed. Empty for a job the user gave up. */
	FText Error;
	/** The depth map or matte that was written. */
	FString File;
	/** The horizontal field of view the picture was taken with, for a focal length job. */
	double FieldOfViewDeg = 0.0;
};

/** How a request to the shot assistant ended. */
struct FRoscShotPlanResult
{
	bool bSuccess = false;
	/** Why there is no plan. Empty for a request the user gave up. */
	FText Error;
	FRoscShotPlan Plan;
	/** The plan as the model wrote it. */
	FString PlanJson;
	/** What the request and the plan came to, as the model's keeper counts and bills them. */
	int32 InputTokens = 0;
	int32 OutputTokens = 0;
};

/**
 * Runs Rosc's picture jobs (depth, matte, focal length) one at a time, in the order asked,
 * on the backend chosen in the user's settings: in the editor, in the helper on this machine,
 * or on a server elsewhere; and asks the shot assistant's language model for shot plans.
 * Nothing is sent to another machine until the user has agreed to that host
 * (Rosc::AIConsent); the check is made here, for every job and every request.
 */
UCLASS()
class ROSCAI_API URoscAISubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Whether the chosen backend can run a task now; when not, why, in words. */
	bool CanRun(ERoscAITask Task, FText& OutWhyNot) const;

	/**
	 * The host the chosen backend would send pictures to and the user has not agreed to yet.
	 * Empty when nothing stands in the way.
	 */
	FString GetHostNeedingConsent() const;

	/**
	 * Asks for a job. It runs after the jobs asked for before it; see IsWorking and OnJobFinished.
	 * @return false with the reason in OutError when the job cannot be taken.
	 */
	bool Submit(const FRoscAIRequest& Request, FText& OutError);

	bool IsWorking() const { return bRunning; }
	/** How many jobs are running or waiting. */
	int32 GetJobsLeft() const { return Waiting.Num() + (bRunning ? 1 : 0); }
	/** What the running job is doing, in words, and how far along it is from 0 to 1. */
	void GetProgress(FText& OutStage, float& OutFraction) const;

	/** Gives up the running job and those waiting. Each still ends through OnJobFinished. */
	void CancelAll();

	/** How the last job ended. */
	const FRoscAIResult& GetLastResult() const { return LastResult; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnJobFinished, const FRoscAIResult&);
	FOnJobFinished& OnJobFinished() { return JobFinished; }

	/** Where a job's result is written: a folder named after the picture under the project's Saved folder. */
	static FString GetResultFolder(const FString& PictureFile);

	/**
	 * Downloads a model's file into the models folder; one download at a time. The user asks
	 * for this, having seen the model's licence.
	 * @return false when another download is running.
	 */
	bool DownloadModel(const FRoscAIModel& Model);
	/** The model being downloaded, or null. */
	const FRoscAIModel* GetDownloadingModel() const;
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

	/** Whether the shot assistant's model can be asked; when not, why, in words. */
	bool CanPlanShot(FText& OutWhyNot) const;

	/**
	 * The host the shot assistant would send the user's words and the set's description to and
	 * the user has not agreed to yet. Empty when nothing stands in the way.
	 */
	FString GetAssistantHostNeedingConsent() const;

	/**
	 * Asks the shot assistant's model to turn a shot described in words into a plan; one request
	 * at a time. A plan changes nothing: applying it is the caller's business, and the user's.
	 * @param SetDescription The camera, its support, what is in the level and the open sequence, as JSON.
	 * @param OnDone Called on the game thread when the request ends, also when it is given up.
	 * @return false with the reason in OutError when the request cannot be made.
	 */
	bool PlanShot(const FString& Words, const FString& SetDescription, FText& OutError, TFunction<void(const FRoscShotPlanResult&)> OnDone);

	bool IsPlanningShot() const;

	/** Gives up the request being made. */
	void CancelShotPlan();

private:
	struct FWaitingJob
	{
		FRoscAIRequest Request;
		ERoscAIBackend Backend = ERoscAIBackend::Editor;
	};

	IRoscAIBackend& GetBackend(ERoscAIBackend Backend) const;
	void StartNext();
	void Finish(FRoscAIResult&& Result);

	TSharedPtr<IRoscAIBackend> EditorBackend;
	TSharedPtr<FRoscAIHelper> Helper;
	TSharedPtr<FRoscAIServerBackend> HelperBackend;
	TSharedPtr<FRoscAIServerBackend> ServerBackend;

	TArray<FWaitingJob> Waiting;
	FWaitingJob Running;
	bool bRunning = false;
	/** Counts jobs, so that an answer that comes after its job was dropped is not taken for the next one's. */
	int32 RunNumber = 0;

	FRoscAIResult LastResult;
	FOnJobFinished JobFinished;

	TSharedPtr<FRoscAIModelDownload> Download;
	FText DownloadError;

	TSharedPtr<IRoscAssistantModel> AssistantModel;
};
