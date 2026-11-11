#pragma once

#include "UEcineUnits.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class ACineCameraActor;
class FUEcineCameraTarget;
class SVerticalBox;

/**
 * The Focus department: set the focus distance, click a subject to focus on it, keep the focus
 * on a subject as it moves, read the depth of field, mark focus distances and bake racks between
 * them to Sequencer. Everything shown is read from the camera each frame, so the tab stays
 * correct through undo, redo and Sequencer playback.
 */
class SUEcineFocusTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcineFocusTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** What the layout depends on; when it changes the content is rebuilt. */
	struct FLayoutKey
	{
		TWeakObjectPtr<ACineCameraActor> Camera;
		bool bHasPackage = false;
		EUEcineDistanceUnits Units = EUEcineDistanceUnits::Metric;

		bool operator==(const FLayoutKey& Other) const
		{
			return Camera == Other.Camera && bHasPackage == Other.bHasPackage && Units == Other.Units;
		}
	};

	/** The limits of acceptable sharpness for the lens as it is set now, in millimetres. */
	struct FDepth
	{
		double NearMm = 0.0;
		double FarMm = 0.0;
		bool bFarIsInfinite = false;
		double HyperfocalMm = 0.0;
	};

	FLayoutKey MakeLayoutKey() const;
	void RebuildLayout();
	/** Refills the list of focus marks. Kept apart from the rest so marking focus does not rebuild the whole tab. */
	void RebuildMarks();

	TSharedRef<SWidget> BuildDistanceRow();
	TSharedRef<SWidget> BuildFollowRows();
	TSharedRef<SWidget> BuildDepthRows();
	TSharedRef<SWidget> BuildRackTimeRow();
	TSharedRef<SWidget> BuildMarkRow(int32 MarkIndex);

	/** Where the lens itself is focused, in centimetres: where the focus is set, held at the lens's close focus. */
	float GetLensDistance() const;
	/** Unset when there is no camera. */
	TOptional<FDepth> GetDepth() const;
	bool IsFollowing() const;
	/** The actor selected in the level that the focus could follow, or null. */
	AActor* GetSelectedSubject() const;
	int32 GetMarkCount() const;

	FText GetFollowedName() const;
	/** Says so when the focus is set nearer than the lens can go. Empty when it is not. */
	FText GetFocusWarning() const;
	FText GetRackStatus() const;
	FReply OnRackClicked(int32 MarkIndex);

	TSharedPtr<FUEcineCameraTarget> Target;
	FLayoutKey CurrentLayoutKey;

	TSharedPtr<SVerticalBox> MarksBox;
	int32 ShownMarkCount = 0;
};
