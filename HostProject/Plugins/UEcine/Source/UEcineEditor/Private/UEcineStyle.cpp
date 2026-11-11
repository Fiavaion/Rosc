#include "UEcineStyle.h"

#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/StyleColors.h"

TSharedPtr<FSlateStyleSet> FUEcineStyle::StyleSet;

void FUEcineStyle::Register()
{
	if (StyleSet.IsValid())
	{
		return;
	}
	StyleSet = MakeShared<FSlateStyleSet>(TEXT("UEcineStyle"));

	const FTextBlockStyle& NormalText = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

	// Section headings: small, bold, shown in capitals by the widgets that use them.
	StyleSet->Set("UEcine.SectionHeader", FTextBlockStyle(NormalText)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 8))
		.SetColorAndOpacity(FStyleColors::Foreground));

	// Row labels on the left of a control.
	StyleSet->Set("UEcine.Label", FTextBlockStyle(NormalText)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		.SetColorAndOpacity(FStyleColors::Foreground));

	// The value a control is currently set to.
	StyleSet->Set("UEcine.Value", FTextBlockStyle(NormalText)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.SetColorAndOpacity(FStyleColors::White));

	// Derived numbers the user reads but does not edit; monospaced so digits do not jitter.
	StyleSet->Set("UEcine.Readout", FTextBlockStyle(NormalText)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 9))
		.SetColorAndOpacity(FStyleColors::Foreground));

	// Guidance text in empty states and warnings.
	StyleSet->Set("UEcine.Body", FTextBlockStyle(NormalText)
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		.SetColorAndOpacity(FStyleColors::Foreground));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FUEcineStyle::Unregister()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}

const ISlateStyle& FUEcineStyle::Get()
{
	check(StyleSet.IsValid());
	return *StyleSet;
}
