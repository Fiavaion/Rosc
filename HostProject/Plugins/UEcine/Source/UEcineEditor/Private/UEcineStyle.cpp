// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
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

	// A button that stays lit while its state is on. The editor's own toggle tells on from off by
	// its fill alone, blue against grey at 2.8:1; this one is also ringed in white while it is on,
	// so the state can be read without telling the colours apart.
	const float ToggleRadius = 4.0f;
	const float ToggleRing = 1.5f;
	StyleSet->Set("UEcine.Toggle", FCheckBoxStyle(FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("DetailsView.SectionButton"))
		.SetCheckedImage(FSlateRoundedBoxBrush(FStyleColors::Primary, ToggleRadius, FStyleColors::White, ToggleRing))
		.SetCheckedHoveredImage(FSlateRoundedBoxBrush(FStyleColors::PrimaryHover, ToggleRadius, FStyleColors::White, ToggleRing))
		.SetCheckedPressedImage(FSlateRoundedBoxBrush(FStyleColors::PrimaryHover, ToggleRadius, FStyleColors::White, ToggleRing)));

	// The panel's icon, on its tab and beside its name in the Window menu.
	StyleSet->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("UEcine"))->GetBaseDir() / TEXT("Resources"));
	StyleSet->Set("UEcine.Panel", new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("Icons/Panel"), TEXT(".svg")), FVector2f(16.0f, 16.0f)));

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
