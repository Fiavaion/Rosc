// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FRoscCameraTarget;
class SLevelViewport;
class SRoscAITab;

/** The departments of the panel, in tab order. */
enum class ERoscDepartment : uint8
{
	Camera,
	Grip,
	Focus,
	Monitor,
	Shots,
	Deliver,
	AI
};

/**
 * Root widget of the dockable Rosc panel. The header says which camera the panel is working
 * on and lets the user look through it; the departments sit below as tabs.
 */
class SRoscPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoscPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Brings a department's tab to the front. */
	void ShowDepartment(ERoscDepartment Department) { ActiveDepartment = Department; }

	/** The AI department's tab, which is handed what the assistant and the picture jobs give back. */
	TSharedRef<SRoscAITab> GetAITab() const { return AITab.ToSharedRef(); }

private:
	TSharedRef<SWidget> MakeDepartmentTab(ERoscDepartment Department, const FText& Label, const FText& ToolTip);

	TSharedRef<SWidget> MakeCameraMenu();
	FText GetCameraName() const;

	bool HasCamera() const;
	/** The slot of the switcher to show: the active department, or the invitation to make a camera when it needs one. */
	int32 GetShownSlot() const;
	bool IsLookingThrough() const;
	void OnLookThroughChanged(bool bLookThrough);
	FReply OnNewCameraClicked();
	FReply OnUserGuideClicked();

	TSharedPtr<FRoscCameraTarget> Target;
	TSharedPtr<SRoscAITab> AITab;
	ERoscDepartment ActiveDepartment = ERoscDepartment::Camera;

	/** The viewport Look Through took over and where its view was, put back when Look Through is turned off. */
	struct FViewBeforeLookThrough
	{
		TWeakPtr<SLevelViewport> Viewport;
		FVector Location;
		FRotator Rotation;
	};
	TOptional<FViewBeforeLookThrough> ViewBeforeLookThrough;
};
