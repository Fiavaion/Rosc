// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "Input/Reply.h"
#include "UEcineDeliverEditing.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ULevelSequence;
struct FUEcineRenderResult;

/**
 * The Deliver department: render the sequence open in Sequencer for review, for visual effects
 * or as a master. Choose what the delivery is for, where it goes and how its files are named,
 * read what preflight found and put it right, render, and bake for handoff.
 *
 * Preflight plays to the start of each shot, so it runs when something it depends on changes
 * and when asked, not every frame.
 */
class SUEcineDeliverTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcineDeliverTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** What the layout depends on; when it changes the content is rebuilt. */
	struct FLayoutKey
	{
		TWeakObjectPtr<ULevelSequence> OpenSequence;
		TArray<TWeakObjectPtr<ULevelSequence>> ShotSequences;
		TArray<FString> ShotNames;
		EUEcineDeliverIntent Intent = EUEcineDeliverIntent::Review;
		bool bRendering = false;
		/** Counts what else the layout shows: each preflight check, and each start and end of a render. */
		int32 Revision = 0;

		/** True when preflight would be told the same of the sequence. */
		bool HasSameDelivery(const FLayoutKey& Other) const
		{
			return OpenSequence == Other.OpenSequence && ShotSequences == Other.ShotSequences && ShotNames == Other.ShotNames && Intent == Other.Intent;
		}

		bool operator==(const FLayoutKey& Other) const
		{
			return HasSameDelivery(Other) && bRendering == Other.bRendering && Revision == Other.Revision;
		}
	};

	/** What preflight found of one kind that reads the same for several shots: shown once, with every shot it is about. */
	using FFindingGroup = TArray<FUEcinePreflightFinding>;

	FLayoutKey ReadLayoutKey() const;
	void RebuildLayout();

	TSharedRef<SWidget> BuildIntentChoice();
	TSharedRef<SWidget> BuildReviewOptions();
	TSharedRef<SWidget> BuildFolderRow();
	TSharedRef<SWidget> BuildNameRow();
	TSharedRef<SWidget> BuildNameButtons();
	TSharedRef<SWidget> BuildFilePreview();
	TSharedRef<SWidget> BuildPreflightStatus(TConstArrayView<FFindingGroup> Groups);
	TSharedRef<SWidget> BuildFinding(const FFindingGroup& Group);
	TSharedRef<SWidget> BuildRender();
	TSharedRef<SWidget> BuildHandoff();

	TSharedRef<SWidget> MakeTokenMenu();

	/** True when preflight has found something that stops the render. */
	bool IsRenderStopped() const;

	/** Changes the user's choices for deliveries and has preflight look again. */
	void EditChoices(TFunctionRef<void(UUEcineEditorSettings&)> Edit);
	void ChooseNamePattern(const FString& Pattern);

	FReply OnFixClicked(FFindingGroup Group);
	FReply OnRenderClicked();
	FReply OnStopClicked();
	FReply OnBakeForHandoffClicked();
	void HandleRenderFinished(const FUEcineRenderResult& Result);

	FText GetRenderProgressText() const;

	FLayoutKey CurrentLayoutKey;
	int32 Revision = 0;

	/** What preflight last found, and the files the delivery would write. */
	UEcine::DeliverEditing::FCheck Check;
	bool bChecked = false;
	/** Preflight is to look again as soon as nothing is playing or rendering. */
	bool bCheckWanted = true;
	/** The open sequence has a delivery folder on disk to open. */
	bool bFolderExists = false;

	/** The delivery this tab started, while it renders. */
	UEcine::DeliverEditing::FDelivery Delivery;
	FText DeliveryName;
	bool bDeliveryRunning = false;
	bool bStopAsked = false;

	/** How the last delivery started here turned out, and the icon that goes with it. Empty before the first. */
	FText ResultText;
	FName ResultIcon;
};
