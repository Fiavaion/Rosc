// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "Input/Reply.h"
#include "RoscPaperwork.h"
#include "RoscPlan.h"
#include "RoscUnits.h"
#include "RoscWidgets.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FRoscCameraTarget;
class SRoscPlanView;
class ULevelSequence;
struct FSlateDynamicImageBrush;

/**
 * The Shots department: the shot list of the sequence open in Sequencer. Choose a shot to work
 * in it, write its scene, size, move and notes, grab storyboard frames through its camera, see
 * its overhead plan, and export the shot list and camera report.
 *
 * The list is read from Sequencer each frame, and the shot being worked on is the one under the
 * playhead, so the tab stays correct through undo, redo and whatever is done in Sequencer itself.
 */
class SRoscShotsTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoscShotsTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FRoscCameraTarget>& InTarget);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** What the layout depends on; when it changes the content is rebuilt. */
	struct FLayoutKey
	{
		TWeakObjectPtr<ULevelSequence> OpenSequence;
		TArray<TWeakObjectPtr<ULevelSequence>> ShotSequences;
		/** Index of the shot under the playhead, or INDEX_NONE. */
		int32 CurrentShot = INDEX_NONE;
		/** Changes when a storyboard frame of the current shot is grabbed, replaced or removed. */
		uint32 StoryboardSignature = 0;
		ERoscDistanceUnits Units = ERoscDistanceUnits::Metric;

		bool operator==(const FLayoutKey& Other) const
		{
			return OpenSequence == Other.OpenSequence && ShotSequences == Other.ShotSequences && CurrentShot == Other.CurrentShot
				&& StoryboardSignature == Other.StoryboardSignature && Units == Other.Units;
		}
	};

	/** Reads the shot list from Sequencer: the layout key, the rows' labels and the current shot's report. */
	FLayoutKey ReadShots();
	void RebuildLayout();

	TSharedRef<SWidget> BuildShotRow(int32 ShotIndex);
	TSharedRef<SWidget> BuildShotFields();
	TSharedRef<SWidget> BuildStoryboard();
	TSharedRef<SWidget> BuildPlan();
	TSharedRef<SWidget> BuildPaperwork();

	/** Plays through the current shot and draws where its camera goes. */
	void RedrawPlan();
	/** Says so when the shot has been changed since its plan was drawn. Empty when the plan is as the shot is. */
	FText GetPlanAgeText() const;

	FReply OnNewShotClicked();
	FReply OnGrabFrameClicked();
	FReply OnExportClicked();
	void GoToShotAt(int32 ShotIndex);

	TSharedPtr<FRoscCameraTarget> Target;
	FLayoutKey CurrentLayoutKey;

	/** What each row of the list says, read each frame. */
	TArray<FText> ShotLabels;
	/** The current shot's timing, camera, lens and support, read each frame. */
	FRoscShotReport CurrentReport;
	/** The sequence of the shot the fields, storyboard and plan are of. */
	TWeakObjectPtr<ULevelSequence> CurrentShotSequence;

	TSharedPtr<SRoscPlanView> PlanView;
	/** The plan as last drawn, and the shot it is of, so that it is drawn once per shot and not on every rebuild. */
	FRoscPlan CurrentPlan;
	TWeakObjectPtr<ULevelSequence> PlanSequence;
	/** The shot's sequence as it stood when the plan was drawn: it changes with every edit of the shot. */
	FGuid PlanSignature;
	/** Where the keyboard focus goes when a storyboard frame is removed. */
	Rosc::Widgets::FRowFocus StoryboardFocus;
	/** What the plan shows, in words. */
	FText PlanCaption;

	TArray<TSharedPtr<FSlateDynamicImageBrush>> StoryboardBrushes;
	/** Where the paperwork was last written, empty until it has been. */
	FString LastExportDirectory;
};
