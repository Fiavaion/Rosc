// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FUEcineCameraTarget;

/**
 * The Monitor department: the tools of an on-set monitor, drawn over the picture in the level
 * viewport. Exposure (false colour, zebras, waveform, histogram), focus peaking, framing (frame
 * lines, safe areas, centre mark) and a line of camera data.
 *
 * The tools are a way of looking at the picture, so switching them is a per-user preference and
 * not an undo step. The delivery frame is part of the camera package, and choosing it is.
 */
class SUEcineMonitorTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcineMonitorTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget);

private:
	TSharedRef<SWidget> BuildDeliveryRow();

	bool HasPackage() const;
	FText GetDeliveryName() const;
	/** Says why frame lines that are switched on have nothing to draw. Empty when they have. */
	FText GetFramingWarning() const;

	TSharedPtr<FUEcineCameraTarget> Target;
};
