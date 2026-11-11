#include "UEcineWidgets.h"

#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "UEcineStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The undo step of one drag. Closes itself if the control is destroyed mid-drag. */
	struct FDragTransaction
	{
		~FDragTransaction()
		{
			End();
		}

		void Begin(const FText& Description)
		{
			if (!bOpen && GEditor != nullptr)
			{
				GEditor->BeginTransaction(Description);
				bOpen = true;
			}
		}

		void End()
		{
			if (bOpen && GEditor != nullptr)
			{
				GEditor->EndTransaction();
			}
			bOpen = false;
		}

		bool bOpen = false;
	};
}

const FTextBlockStyle& UEcine::Widgets::TextStyle(const FName StyleName)
{
	return FUEcineStyle::Get().GetWidgetStyle<FTextBlockStyle>(StyleName);
}

TSharedRef<SWidget> UEcine::Widgets::MakeSectionHeader(const FText& Title)
{
	return SNew(STextBlock)
		.TextStyle(&TextStyle("UEcine.SectionHeader"))
		.Text(Title)
		.TransformPolicy(ETextTransformPolicy::ToUpper);
}

TSharedRef<SWidget> UEcine::Widgets::MakeRow(const FText& Label, TAttribute<FText> LabelToolTip, const TSharedRef<SWidget>& Control)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(FUEcineStyle::LabelColumnWidth)
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("UEcine.Label"))
				.Text(Label)
				.ToolTipText(MoveTemp(LabelToolTip))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			Control
		];
}

TSharedRef<SWidget> UEcine::Widgets::MakeReadoutRow(const FText& Label, const FText& ToolTip, TAttribute<FText> Value)
{
	return SNew(SHorizontalBox)
		.ToolTipText(ToolTip)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("UEcine.Label"))
			.Text(Label)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("UEcine.Readout"))
			.Text(MoveTemp(Value))
		];
}

TSharedRef<SWidget> UEcine::Widgets::MakeEmptyState(const FText& Message, const FText& ButtonLabel, const FText& ButtonToolTip, FOnClicked OnClicked)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 24.0f, 0.0f, 12.0f)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("UEcine.Body"))
			.Text(Message)
			.AutoWrapText(true)
			.Justification(ETextJustify::Center)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
			.Text(ButtonLabel)
			.ToolTipText(ButtonToolTip)
			.OnClicked(MoveTemp(OnClicked))
		];
}

TSharedRef<SWidget> UEcine::Widgets::MakeWarning(TAttribute<FText> Message)
{
	return SNew(SHorizontalBox)
		.Visibility_Lambda([Message] { return Message.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(0.0f, 1.0f, 6.0f, 0.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.WarningWithColor"))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("UEcine.Body"))
			.Text(Message)
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> UEcine::Widgets::MakeToggleButton(const FText& Label, const FText& ToolTip, TAttribute<bool> IsOn, TFunction<void(bool bTurnOn)> OnToggled)
{
	return SNew(SBox)
		.MinDesiredWidth(36.0f)
		.MinDesiredHeight(FUEcineStyle::MinTargetSize)
		[
			SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("DetailsView.SectionButton"))
			.HAlign(HAlign_Center)
			.ToolTipText(ToolTip)
			.AccessibleText(ToolTip)
			.IsChecked_Lambda([IsOn] { return IsOn.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([OnToggled = MoveTemp(OnToggled)](ECheckBoxState NewState) { OnToggled(NewState == ECheckBoxState::Checked); })
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("UEcine.Label"))
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Text(Label)
			]
		];
}

TSharedRef<SWidget> UEcine::Widgets::MakeIconButton(const FName IconName, const FText& ToolTip, FOnClicked OnClicked)
{
	return SNew(SBox)
		.WidthOverride(FUEcineStyle::MinTargetSize + 4.0f)
		.HeightOverride(FUEcineStyle::MinTargetSize + 4.0f)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ContentPadding(0.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ToolTipText(ToolTip)
			.AccessibleText(ToolTip)
			.OnClicked(MoveTemp(OnClicked))
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush(IconName))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
}

TSharedRef<SWidget> UEcine::Widgets::MakeSpinBox(FSpinBoxArgs Args)
{
	const TSharedRef<FDragTransaction> Drag = MakeShared<FDragTransaction>();
	const TAttribute<float> Value = Args.Value;

	return SNew(SBox)
		.MinDesiredHeight(FUEcineStyle::MinTargetSize)
		[
			SNew(SSpinBox<float>)
			.ToolTipText(Args.ToolTip)
			.AccessibleText(Args.Name)
			.MinValue(Args.MinValue)
			.MaxValue(Args.MaxValue)
			.MinSliderValue(Args.MinSliderValue.IsSet() ? TAttribute<TOptional<float>>(Args.MinSliderValue) : Args.MinValue)
			.MaxSliderValue(Args.MaxSliderValue.IsSet() ? TAttribute<TOptional<float>>(Args.MaxSliderValue) : Args.MaxValue)
			.Delta(Args.Delta)
			.MinFractionalDigits(FMath::Min(1, Args.MaxFractionalDigits))
			.MaxFractionalDigits(Args.MaxFractionalDigits)
			.TypeInterface(Args.TypeInterface)
			.Value(Value)
			.OnBeginSliderMovement_Lambda([Drag, Description = Args.TransactionName, SaveForUndo = MoveTemp(Args.SaveForUndo)]
			{
				Drag->Begin(Description);
				SaveForUndo();
			})
			.OnValueChanged_Lambda([Drag, OnDrag = MoveTemp(Args.OnDrag)](float NewValue)
			{
				if (Drag->bOpen)
				{
					OnDrag(NewValue);
				}
			})
			.OnEndSliderMovement_Lambda([Drag](float) { Drag->End(); })
			.OnValueCommitted_Lambda([Drag, Value, OnCommit = MoveTemp(Args.OnCommit)](float NewValue, ETextCommit::Type)
			{
				// The end of a drag also commits; that value is already applied and recorded.
				if (!Drag->bOpen && !FMath::IsNearlyEqual(NewValue, Value.Get()))
				{
					OnCommit(NewValue);
				}
			})
		];
}

void UEcine::Widgets::Notify(const FText& Message, bool bSucceeded)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 6.0f;
	const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bSucceeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}
