// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * A value that moves along a fixed ladder, like the iris ring or ISO dial of a camera:
 * a step-down button, the current value (which opens the whole ladder as a menu), and a
 * step-up button. With keyboard focus inside it, Down and Up step the value.
 */
class SUEcineStepper : public SCompoundWidget
{
public:
	/** Direction is -1 to step to the previous ladder value, +1 to the next. */
	DECLARE_DELEGATE_OneParam(FOnStep, int32 /*Direction*/);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FCanStep, int32 /*Direction*/);

	SLATE_BEGIN_ARGS(SUEcineStepper) {}
		/** The current value as shown between the buttons. */
		SLATE_ATTRIBUTE(FText, ValueText)
		/** Names the control for tooltips and screen readers, for example "T-stop". */
		SLATE_ARGUMENT(FText, Label)
		/** What stepping down does, for example "Open the iris a third of a stop". */
		SLATE_ARGUMENT(FText, StepDownToolTip)
		/** What stepping up does. */
		SLATE_ARGUMENT(FText, StepUpToolTip)
		SLATE_EVENT(FOnStep, OnStep)
		SLATE_EVENT(FCanStep, CanStep)
		/** Builds the menu listing every ladder value. */
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TSharedRef<SWidget> MakeStepButton(int32 Direction, const FName IconName, const FText& ToolTip);
	bool CanStepInDirection(int32 Direction) const;

	FOnStep OnStep;
	FCanStep CanStep;
};
