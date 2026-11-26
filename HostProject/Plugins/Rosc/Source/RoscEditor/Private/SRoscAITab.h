// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Input/Reply.h"
#include "RoscAISubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ACineCameraActor;
class FRoscCameraTarget;
class SBox;
class SVerticalBox;
struct FSlateDynamicImageBrush;

/**
 * The AI department, all of it optional: the shot assistant, which turns a shot described in
 * words into a plan the user reads and applies; picture jobs (depth map, matte, lens estimate)
 * for stills and plates Unreal did not render; and the models those jobs need, which the user
 * downloads.
 *
 * Nothing leaves the machine from here until the user has agreed to the host it goes to, having
 * read what is sent. The agreement is asked for here and checked again where the data is sent.
 */
class SRoscAITab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoscAITab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FRoscCameraTarget> InTarget);
	virtual ~SRoscAITab() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Takes what the assistant answered: a plan for the user to read, or why there is none. */
	void ShowPlan(const FRoscShotPlanResult& Result);

	/** Takes how a picture job ended. */
	void ShowJobResult(const FRoscAIResult& Result);

private:
	/** What each part of the tab shows; a part is rebuilt when its key changes. */
	struct FAssistantKey
	{
		TWeakObjectPtr<ACineCameraActor> Camera;
		/** Why the model cannot be asked. Empty when it can. */
		FString WhyNot;
		FString Host;
		bool bConsentNeeded = false;
		bool bConsentGiven = false;
		bool bPlanning = false;
		/** The plan being read as it would go now: its review follows the camera as other tabs change it. */
		FString PlanReview;
		int32 Revision = 0;

		bool operator==(const FAssistantKey& Other) const = default;
	};

	struct FPicturesKey
	{
		ERoscAIBackend Backend = ERoscAIBackend::Editor;
		/** Why each task cannot be run, in the order of ERoscAITask. Empty when it can. */
		TArray<FString> WhyNot;
		FString Host;
		bool bConsentNeeded = false;
		bool bConsentGiven = false;
		bool bWorking = false;
		int32 Revision = 0;

		bool operator==(const FPicturesKey& Other) const = default;
	};

	struct FModelsKey
	{
		TArray<bool> Installed;
		FString DownloadingId;
		FString DownloadError;

		bool operator==(const FModelsKey& Other) const = default;
	};

	FAssistantKey ReadAssistantKey() const;
	FPicturesKey ReadPicturesKey() const;
	FModelsKey ReadModelsKey() const;

	void RebuildAssistant();
	void RebuildPictures();
	void RebuildModels();
	void AddPlanReview(SVerticalBox& Content, ACineCameraActor* Camera);
	void AddJobResult(SVerticalBox& Content);

	/** Has the tab look again at the next frame, after something the user did. */
	void RefreshSoon() { LastRefreshTime = 0.0; }

	bool CanAskForPlan() const;
	FReply OnPlanClicked();
	FReply OnApplyPlanClicked();
	FReply OnRunClicked(ERoscAITask Task);
	FReply OnPutOnLensClicked();

	/** The lens the last lens estimate comes to on the panel's camera, in a sentence. */
	FText GetLensMatchText() const;
	FText GetPutOnLensLabel() const;

	TSharedPtr<FRoscCameraTarget> Target;
	TSharedPtr<SBox> AssistantBox;
	TSharedPtr<SBox> PicturesBox;
	TSharedPtr<SBox> ModelsBox;

	FAssistantKey AssistantKey;
	FPicturesKey PicturesKey;
	FModelsKey ModelsKey;
	double LastRefreshTime = 0.0;

	/** The shot as the user describes it. */
	FText Words;
	/** The plan being read. */
	TOptional<FRoscShotPlanResult> Plan;
	/** Why the last request gave no plan, or the last plan could not be applied. */
	FText PlanError;
	/** How applying the last plan went, when it went well. */
	FText PlanOutcome;
	int32 AssistantRevision = 0;

	FString PictureFile;
	/** How the last picture job asked for here ended, or why it was not taken. */
	TOptional<FRoscAIResult> JobResult;
	TSharedPtr<FSlateDynamicImageBrush> ResultBrush;
	FText ServerCheckText;
	bool bServerReachable = false;
	int32 PicturesRevision = 0;

	FDelegateHandle JobFinishedHandle;
};
