// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "SRoscStepper.h"

#include "Styling/AppStyle.h"
#include "RoscStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SRoscStepper::Construct(const FArguments& InArgs)
{
	OnStep = InArgs._OnStep;
	CanStep = InArgs._CanStep;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeStepButton(-1, "Icons.ChevronLeft", InArgs._StepDownToolTip)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SBox)
			.MinDesiredHeight(FRoscStyle::MinTargetSize)
			[
				SNew(SComboButton)
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
				.HasDownArrow(false)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ToolTipText(InArgs._Label)
				.AccessibleText(InArgs._Label)
				.OnGetMenuContent(InArgs._OnGetMenuContent)
				.ButtonContent()
				[
					SNew(STextBlock)
					.TextStyle(&FRoscStyle::Get().GetWidgetStyle<FTextBlockStyle>("Rosc.Value"))
					.Text(InArgs._ValueText)
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeStepButton(1, "Icons.ChevronRight", InArgs._StepUpToolTip)
		]
	];
}

TSharedRef<SWidget> SRoscStepper::MakeStepButton(int32 Direction, const FName IconName, const FText& ToolTip)
{
	return SNew(SBox)
		.WidthOverride(FRoscStyle::MinTargetSize)
		.HeightOverride(FRoscStyle::MinTargetSize)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ContentPadding(0.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ToolTipText(ToolTip)
			.AccessibleText(ToolTip)
			.IsEnabled(this, &SRoscStepper::CanStepInDirection, Direction)
			.OnClicked_Lambda([this, Direction]
			{
				OnStep.ExecuteIfBound(Direction);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush(IconName))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
}

bool SRoscStepper::CanStepInDirection(int32 Direction) const
{
	return !CanStep.IsBound() || CanStep.Execute(Direction);
}

FReply SRoscStepper::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const int32 Direction = Key == EKeys::Up ? 1 : (Key == EKeys::Down ? -1 : 0);
	if (Direction != 0)
	{
		if (CanStepInDirection(Direction))
		{
			OnStep.ExecuteIfBound(Direction);
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}
