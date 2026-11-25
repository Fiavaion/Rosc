// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "RoscPlan.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

/**
 * Draws the overhead plan of a shot: where the camera starts and ends, the path between, what it
 * takes in, the track it runs on and the subjects, to scale with a scale bar. A picture only: the
 * tab that shows it says the same in words beside it.
 */
class SRoscPlanView : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRoscPlanView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetPlan(const FRoscPlan& InPlan);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	FRoscPlan Plan;
};
