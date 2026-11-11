#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * The one style set for UEcine's UI: text styles, spacing and colours. Colours come from the
 * editor theme so the panel keeps its contrast in any theme the user picks.
 */
class FUEcineStyle
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

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
