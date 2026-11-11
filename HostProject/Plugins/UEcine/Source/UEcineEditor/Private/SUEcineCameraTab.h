#pragma once

#include "UEcineCameraPackage.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ACineCameraActor;
class FUEcineCameraTarget;
class SVerticalBox;
struct FUEcineRecordingFormat;
class UUEcineCameraPackageComponent;

/**
 * The Camera department: choose the camera body, recording format and lens, and set exposure
 * in film terms. Everything shown is read from the targeted camera's package each frame, so the
 * tab stays correct through undo, redo and edits made elsewhere in the editor.
 */
class SUEcineCameraTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcineCameraTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget);
	/** Closes a drag's undo step if the tab goes away mid-drag. */
	virtual ~SUEcineCameraTab() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** Describes one exposure control whose values sit on a ladder of standard settings. */
	struct FLadderControl
	{
		FText Label;
		FText LabelToolTip;
		FText StepDownToolTip;
		FText StepUpToolTip;
		FText TransactionName;
		TFunction<TArray<double>()> GetLadder;
		TFunction<double(const FUEcineCameraPackage&)> GetValue;
		TFunction<void(FUEcineCameraPackage&, double)> SetValue;
		TFunction<FText(double)> FormatValue;
	};

	/** What the layout depends on; when it changes the content is rebuilt. */
	struct FLayoutKey
	{
		TWeakObjectPtr<ACineCameraActor> Camera;
		bool bHasPackage = false;
		FString LensKitId;

		bool operator==(const FLayoutKey& Other) const
		{
			return Camera == Other.Camera && bHasPackage == Other.bHasPackage && LensKitId == Other.LensKitId;
		}
	};

	FLayoutKey MakeLayoutKey() const;
	void RebuildLayout();
	void RefreshResolvedCamera();

	TSharedRef<SWidget> BuildNoCameraState();
	TSharedRef<SWidget> BuildNoPackageState();
	TSharedRef<SWidget> BuildPackageEditor();
	TSharedRef<SWidget> BuildLensButtons();
	TSharedRef<SWidget> BuildLadderRow(const TSharedRef<FLadderControl>& Control);

	TSharedRef<SWidget> MakeBodyMenu();
	TSharedRef<SWidget> MakeFormatMenu();
	TSharedRef<SWidget> MakeLensKitMenu();
	TSharedRef<SWidget> MakeLadderMenu(TSharedRef<FLadderControl> Control);

	UUEcineCameraPackageComponent* GetComponent() const;
	/** The package of the targeted camera, or a default-constructed one when there is none. */
	const FUEcineCameraPackage& GetPackage() const;

	/** Changes the package as one undoable step; shows a notification when it cannot be applied. */
	void Edit(const FText& Description, TFunctionRef<void(FUEcineCameraPackage&)> EditFunction);

	// Continuous controls (zoom, exposure compensation) change the package many times during
	// one drag; these keep the whole drag in a single undo step.
	void BeginDrag(const FText& Description);
	void DragEdit(TFunctionRef<void(FUEcineCameraPackage&)> EditFunction);
	void EndDrag();

	FReply OnNewCameraClicked();
	FReply OnBuildPackageClicked();

	TSharedPtr<FUEcineCameraTarget> Target;
	FLayoutKey CurrentLayoutKey;

	/** The package the readouts were last computed from, and whether that copy is current. */
	FUEcineCameraPackage ResolvedFromPackage;
	bool bResolveCacheValid = false;
	FUEcineResolvedCamera Resolved;
	bool bResolvedValid = false;
	/** The package's recording format in the database, or null when its ids match nothing. */
	const FUEcineRecordingFormat* ResolvedFormat = nullptr;

	TSharedPtr<SVerticalBox> WarningsBox;
	bool bDragTransactionOpen = false;
};
