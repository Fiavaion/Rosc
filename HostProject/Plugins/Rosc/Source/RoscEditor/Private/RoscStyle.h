// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Styling/SlateStyle.h"

/**
 * The one style set for Rosc's UI: text styles, spacing and colours. Colours come from the
 * editor theme so the panel keeps its contrast in any theme the user picks.
 */
class FRoscStyle
{
public:
	static void Register();
	static void Unregister();
	static const ISlateStyle& Get();

	/** Height and width of the smallest interactive target. */
	static constexpr float MinTargetSize = 24.0f;
	/** Width of the label column in a label-and-control row. */
	static constexpr float LabelColumnWidth = 76.0f;
	/** Gap between rows inside a section. */
	static constexpr float RowSpacing = 4.0f;
	/** Gap between sections. */
	static constexpr float SectionSpacing = 14.0f;
	/** Padding between the panel edge and its content. */
	static constexpr float PanelPadding = 10.0f;

	/** The space above a row inside a section, above a row of buttons, and above a section. */
	static inline const FMargin RowPadding{ 0.0f, RowSpacing, 0.0f, 0.0f };
	static inline const FMargin ButtonRowPadding{ 0.0f, RowSpacing + 2.0f, 0.0f, 0.0f };
	static inline const FMargin SectionPadding{ 0.0f, SectionSpacing, 0.0f, 0.0f };

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
