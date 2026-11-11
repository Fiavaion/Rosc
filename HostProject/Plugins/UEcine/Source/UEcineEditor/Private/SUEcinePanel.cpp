#include "SUEcinePanel.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

void SUEcinePanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PanelHeading", "UEcine"))
		]
	];
}

#undef LOCTEXT_NAMESPACE
