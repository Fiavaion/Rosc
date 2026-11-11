#pragma once

#include "Input/Reply.h"
#include "UEcinePaperwork.h"
#include "UEcinePlan.h"
#include "UEcineUnits.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FUEcineCameraTarget;
class SUEcinePlanView;
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
class SUEcineShotsTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcineShotsTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget);

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
		EUEcineDistanceUnits Units = EUEcineDistanceUnits::Metric;

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

	FReply OnNewShotClicked();
	FReply OnGrabFrameClicked();
	FReply OnExportClicked();
	void GoToShotAt(int32 ShotIndex);

	TSharedPtr<FUEcineCameraTarget> Target;
	FLayoutKey CurrentLayoutKey;

	/** What each row of the list says, read each frame. */
	TArray<FText> ShotLabels;
	/** The current shot's timing, camera, lens and support, read each frame. */
	FUEcineShotReport CurrentReport;
	/** The sequence of the shot the fields, storyboard and plan are of. */
	TWeakObjectPtr<ULevelSequence> CurrentShotSequence;

	TSharedPtr<SUEcinePlanView> PlanView;
	/** The plan as last drawn, and the shot it is of, so that it is drawn once per shot and not on every rebuild. */
	FUEcinePlan CurrentPlan;
	TWeakObjectPtr<ULevelSequence> PlanSequence;
	/** What the plan shows, in words. */
	FText PlanCaption;

	TArray<TSharedPtr<FSlateDynamicImageBrush>> StoryboardBrushes;
	/** Where the paperwork was last written, empty until it has been. */
	FString LastExportDirectory;
};
