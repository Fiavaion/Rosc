#pragma once

#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FUEcineCameraTarget;

/** The departments of the panel, in tab order. */
enum class EUEcineDepartment : uint8
{
	Camera,
	Grip,
	Focus,
	Monitor,
	Shots,
	Deliver
};

/**
 * Root widget of the dockable UEcine panel. The header says which camera the panel is working
 * on and lets the user look through it; the departments sit below as tabs.
 */
class SUEcinePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcinePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Brings a department's tab to the front. */
	void ShowDepartment(EUEcineDepartment Department) { ActiveDepartment = Department; }

private:
	TSharedRef<SWidget> MakeDepartmentTab(EUEcineDepartment Department, const FText& Label, const FText& ToolTip);

	TSharedRef<SWidget> MakeCameraMenu();
	FText GetCameraName() const;

	bool HasCamera() const;
	/** The slot of the switcher to show: the active department, or the invitation to make a camera when it needs one. */
	int32 GetShownSlot() const;
	bool IsLookingThrough() const;
	void OnLookThroughChanged(bool bLookThrough);
	FReply OnNewCameraClicked();

	TSharedPtr<FUEcineCameraTarget> Target;
	EUEcineDepartment ActiveDepartment = EUEcineDepartment::Camera;
};
