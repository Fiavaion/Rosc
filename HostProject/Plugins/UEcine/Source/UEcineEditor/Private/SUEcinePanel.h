#pragma once

#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FUEcineCameraTarget;

/**
 * Root widget of the dockable UEcine panel. The header says which camera the panel is working
 * on and lets the user look through it; the departments sit below.
 */
class SUEcinePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcinePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> MakeCameraMenu();
	FText GetCameraName() const;

	bool HasCamera() const;
	ECheckBoxState IsLookingThrough() const;
	void OnLookThroughChanged(ECheckBoxState NewState);
	FReply OnNewCameraClicked();

	TSharedPtr<FUEcineCameraTarget> Target;
};
