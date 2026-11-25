// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "RoscCameraPackage.h"
#include "RoscWidgets.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ACineCameraActor;
class FRoscCameraTarget;
class SVerticalBox;
struct FRoscRecordingFormat;
class URoscCameraPackageComponent;

/**
 * The Camera department: choose the camera body, recording format and lens, and set exposure
 * in film terms. Everything shown is read from the targeted camera's package each frame, so the
 * tab stays correct through undo, redo and edits made elsewhere in the editor.
 */
class SRoscCameraTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoscCameraTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FRoscCameraTarget>& InTarget);

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
		TFunction<double(const FRoscCameraPackage&)> GetValue;
		TFunction<void(FRoscCameraPackage&, double)> SetValue;
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

	TSharedRef<SWidget> BuildNoPackageState();
	TSharedRef<SWidget> BuildPackageEditor();
	TSharedRef<SWidget> BuildLensButtons();
	TSharedRef<SWidget> BuildLadderRow(const TSharedRef<FLadderControl>& Control);

	TSharedRef<SWidget> MakeBodyMenu();
	TSharedRef<SWidget> MakeFormatMenu();
	TSharedRef<SWidget> MakeLensKitMenu();
	TSharedRef<SWidget> MakeLadderMenu(TSharedRef<FLadderControl> Control);

	URoscCameraPackageComponent* GetComponent() const;
	/** The package of the targeted camera, or a default-constructed one when there is none. */
	const FRoscCameraPackage& GetPackage() const;

	/** Changes the package as one undoable step; shows a notification when it cannot be applied. */
	void Edit(const FText& Description, TFunctionRef<void(FRoscCameraPackage&)> EditFunction);

	/** Changes the package during a slider drag, inside the drag's open undo step. */
	void DragEdit(TFunctionRef<void(FRoscCameraPackage&)> EditFunction);

	/** A spin box for one continuous number of the package; a drag is one undo step. */
	TSharedRef<SWidget> MakePackageSpinBox(Rosc::Widgets::FSpinBoxArgs Args, TFunction<float(const FRoscCameraPackage&)> GetValue, TFunction<void(FRoscCameraPackage&, float)> SetValue);

	FReply OnBuildPackageClicked();

	TSharedPtr<FRoscCameraTarget> Target;
	FLayoutKey CurrentLayoutKey;

	/** The package the readouts were last computed from, and whether that copy is current. */
	FRoscCameraPackage ResolvedFromPackage;
	bool bResolveCacheValid = false;
	FRoscResolvedCamera Resolved;
	bool bResolvedValid = false;
	/** The package's recording format in the database, or null when its ids match nothing. */
	const FRoscRecordingFormat* ResolvedFormat = nullptr;

	TSharedPtr<SVerticalBox> WarningsBox;
};
