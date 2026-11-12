// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "UEcineRigTypes.h"
#include "UEcineWidgets.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ACineCameraActor;
class AUEcineRig;
class FUEcineCameraTarget;
class SVerticalBox;

/**
 * The Grip department: put the camera on a support, set the support's axes, mark positions,
 * time the moves between them, see where a move asks more than the equipment can do, choose
 * the operator, and bake the move to Sequencer. Everything shown is read from the rig each
 * frame, so the tab stays correct through undo, redo and Sequencer playback.
 */
class SUEcineGripTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcineGripTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** What the layout depends on; when it changes the content is rebuilt. */
	struct FLayoutKey
	{
		TWeakObjectPtr<ACineCameraActor> Camera;
		TWeakObjectPtr<const AUEcineRig> Rig;
		FString PresetId;
		EUEcineDistanceUnits Units = EUEcineDistanceUnits::Metric;

		bool operator==(const FLayoutKey& Other) const
		{
			return Camera == Other.Camera && Rig == Other.Rig && PresetId == Other.PresetId && Units == Other.Units;
		}
	};

	FLayoutKey MakeLayoutKey() const;
	void RebuildLayout();
	/** Refills the list of marks. Kept apart from the rest so marking a position does not rebuild the whole tab. */
	void RebuildMarks();

	TSharedRef<SWidget> BuildSupportRow();
	TSharedRef<SWidget> BuildTrackRow();
	TSharedRef<SWidget> BuildAxisRow(EUEcineRigType Type, EUEcineRigAxis Axis);
	TSharedRef<SWidget> BuildMarkRow(int32 MarkIndex);
	TSharedRef<SWidget> BuildMoveTimeRow(int32 MarkIndex);
	TSharedRef<SWidget> BuildEaseRow(int32 MarkIndex, bool bEaseIn);
	TSharedRef<SWidget> BuildOperatorRows();
	TSharedRef<SWidget> BuildBakeRows();

	TSharedRef<SWidget> MakeSupportMenu();
	TSharedRef<SWidget> MakeOperatorMenu();

	/** A spin box showing one number of the rig; drags and typed values are undoable. */
	TSharedRef<SWidget> MakeRigSpinBox(UEcine::Widgets::FSpinBoxArgs Args, TFunction<float(const AUEcineRig&)> GetValue, TFunction<void(AUEcineRig&, float)> SetValue);

	/** The rig the targeted camera is on, or null. */
	AUEcineRig* GetRig() const;

	FText GetSupportName() const;
	/** What the rig cannot do about the move that arrives at a mark, one warning per line. Empty when the move is fine. */
	FText GetMoveWarnings(int32 MarkIndex) const;
	FText GetBakeStatus() const;
	FReply OnBakeClicked();

	TSharedPtr<FUEcineCameraTarget> Target;
	FLayoutKey CurrentLayoutKey;

	TSharedPtr<SVerticalBox> MarksBox;
	int32 ShownMarkCount = 0;
	/** Where the keyboard focus goes when a mark is removed. */
	UEcine::Widgets::FRowFocus MarkRowFocus;
	TSharedPtr<SWidget> MarkPositionButton;
};
