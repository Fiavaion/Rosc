#include "UEcineWidgets.h"

#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "UEcineEditorSettings.h"
#include "UEcineStyle.h"
#include "UEcineUnits.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
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

	/** A distance in centimetres, written and read in the user's units. */
	class FDistanceTypeInterface : public TDefaultNumericTypeInterface<float>
	{
	public:
		virtual FString ToString(const float& Value) const override
		{
			return UEcine::Units::FormatDistance(Value * 10.0, UUEcineEditorSettings::GetDistanceUnits()).ToString();
		}

		virtual TOptional<float> FromString(const FString& InString, const float& ExistingValue) override
		{
			// Text the user did not change keeps the exact value, not the rounded one on show.
			if (InString == ToString(ExistingValue))
			{
				return ExistingValue;
			}
			const TOptional<double> Millimetres = UEcine::Units::ParseDistance(InString, UUEcineEditorSettings::GetDistanceUnits());
			return Millimetres.IsSet() ? TOptional<float>(static_cast<float>(Millimetres.GetValue() / 10.0)) : TOptional<float>();
		}

		virtual bool IsCharacterValid(TCHAR InChar) const override
		{
			return true;
		}
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

TSharedRef<SWidget> UEcine::Widgets::MakeButton(TAttribute<FText> Label, TAttribute<FText> ToolTip, FOnClicked OnClicked, TAttribute<bool> IsEnabled)
{
	return SNew(SBox)
		.MinDesiredHeight(FUEcineStyle::MinTargetSize)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Text(MoveTemp(Label))
			.ToolTipText(MoveTemp(ToolTip))
			.IsEnabled(MoveTemp(IsEnabled))
			.OnClicked(MoveTemp(OnClicked))
		];
}

TSharedRef<SWidget> UEcine::Widgets::MakeNote(TAttribute<FText> Text)
{
	return SNew(STextBlock)
		.TextStyle(&TextStyle("UEcine.Body"))
		.AutoWrapText(true)
		.Visibility_Lambda([Text] { return Text.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		.Text(Text);
}

TSharedRef<SWidget> UEcine::Widgets::MakeToggleButton(TAttribute<FText> Label, const FText& ToolTip, TAttribute<bool> IsOn, TFunction<void(bool bTurnOn)> OnToggled)
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
				.Text(MoveTemp(Label))
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

TSharedRef<SWidget> UEcine::Widgets::MakeComboButton(const FText& Name, const FText& ToolTip, TAttribute<FText> ValueText, FOnGetContent OnGetMenu)
{
	return SNew(SBox)
		.MinDesiredHeight(FUEcineStyle::MinTargetSize)
		[
			SNew(SComboButton)
			.ToolTipText(ToolTip)
			.AccessibleText(Name)
			.OnGetMenuContent(MoveTemp(OnGetMenu))
			.ButtonContent()
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("UEcine.Value"))
				.Text(MoveTemp(ValueText))
			]
		];
}

TSharedRef<SWidget> UEcine::Widgets::MakeChoiceRow(const FText& Label, const FText& ToolTip, TAttribute<FText> ValueText, TArray<FChoice> Choices, TAttribute<bool> IsEnabled)
{
	const TSharedRef<SWidget> Combo = MakeComboButton(Label, ToolTip, MoveTemp(ValueText), FOnGetContent::CreateLambda([Choices = MoveTemp(Choices)]
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		for (const FChoice& Choice : Choices)
		{
			MenuBuilder.AddMenuEntry(
				Choice.Name,
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda(Choice.Choose), FCanExecuteAction(), FIsActionChecked::CreateLambda(Choice.IsChosen)),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
		return MenuBuilder.MakeWidget();
	}));
	Combo->SetEnabled(MoveTemp(IsEnabled));
	return MakeRow(Label, ToolTip, Combo);
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
			.SliderExponent(Args.SliderExponent)
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

TSharedRef<INumericTypeInterface<float>> UEcine::Widgets::MakeDistanceInterface()
{
	return MakeShared<FDistanceTypeInterface>();
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
