// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscWidgets.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDesktopPlatform.h"
#include "Input/DragAndDrop.h"
#include "Misc/Paths.h"
#include "SDropTarget.h"
#include "Styling/AppStyle.h"
#include "RoscEditorSettings.h"
#include "RoscStyle.h"
#include "RoscUnits.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

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
			return Rosc::Units::FormatDistance(Value * 10.0, URoscEditorSettings::GetDistanceUnits()).ToString();
		}

		virtual TOptional<float> FromString(const FString& InString, const float& ExistingValue) override
		{
			// Text the user did not change keeps the exact value, not the rounded one on show.
			if (InString == ToString(ExistingValue))
			{
				return ExistingValue;
			}
			const TOptional<double> Millimetres = Rosc::Units::ParseDistance(InString, URoscEditorSettings::GetDistanceUnits());
			return Millimetres.IsSet() ? TOptional<float>(static_cast<float>(Millimetres.GetValue() / 10.0)) : TOptional<float>();
		}

		virtual bool IsCharacterValid(TCHAR InChar) const override
		{
			// Every character but Tab. A text box types whatever its owner calls valid, Tab included,
			// and Tab has to stay the key that moves on to the next control.
			return InChar != TEXT('\t');
		}
	};
}

const FTextBlockStyle& Rosc::Widgets::TextStyle(const FName StyleName)
{
	return FRoscStyle::Get().GetWidgetStyle<FTextBlockStyle>(StyleName);
}

TSharedRef<SWidget> Rosc::Widgets::MakeSectionHeader(const FText& Title)
{
	return SNew(STextBlock)
		.TextStyle(&TextStyle("Rosc.SectionHeader"))
		.Text(Title)
		.TransformPolicy(ETextTransformPolicy::ToUpper);
}

TSharedRef<SWidget> Rosc::Widgets::MakeRow(const FText& Label, TAttribute<FText> LabelToolTip, const TSharedRef<SWidget>& Control)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(FRoscStyle::LabelColumnWidth)
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("Rosc.Label"))
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

TSharedRef<SWidget> Rosc::Widgets::MakeReadoutRow(const FText& Label, const FText& ToolTip, TAttribute<FText> Value)
{
	return SNew(SHorizontalBox)
		.ToolTipText(ToolTip)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Label"))
			.Text(Label)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Readout"))
			.Text(MoveTemp(Value))
		];
}

TSharedRef<SWidget> Rosc::Widgets::MakeEmptyState(const FText& Message, const FText& ButtonLabel, const FText& ButtonToolTip, FOnClicked OnClicked)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 24.0f, 0.0f, 12.0f)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Body"))
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

TSharedRef<SWidget> Rosc::Widgets::MakeStatusLine(const FName IconName, TAttribute<FText> Message)
{
	// What the icon's shape says, in a word, for a screen reader and for whoever points at it.
	const FText Kind = IconName == FName("Icons.WarningWithColor") ? LOCTEXT("StatusWarning", "Warning")
		: IconName == FName("Icons.ErrorWithColor") ? LOCTEXT("StatusProblem", "Problem")
		: IconName == FName("Icons.SuccessWithColor") ? LOCTEXT("StatusDone", "Done")
		: LOCTEXT("StatusNote", "Note");
	return SNew(SHorizontalBox)
		.Visibility_Lambda([Message] { return Message.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(0.0f, 1.0f, 6.0f, 0.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush(IconName))
			.ToolTipText(Kind)
			.AccessibleText(Kind)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Body"))
			.Text(Message)
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> Rosc::Widgets::MakeWarning(TAttribute<FText> Message)
{
	return MakeStatusLine("Icons.WarningWithColor", MoveTemp(Message));
}

TSharedRef<SWidget> Rosc::Widgets::MakeButton(TAttribute<FText> Label, TAttribute<FText> ToolTip, FOnClicked OnClicked, TAttribute<bool> IsEnabled)
{
	return SNew(SBox)
		.MinDesiredHeight(FRoscStyle::MinTargetSize)
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

TSharedRef<SWidget> Rosc::Widgets::MakeNote(TAttribute<FText> Text)
{
	return SNew(STextBlock)
		.TextStyle(&TextStyle("Rosc.Body"))
		.AutoWrapText(true)
		.Visibility_Lambda([Text] { return Text.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		.Text(Text);
}

TSharedRef<SWidget> Rosc::Widgets::MakeToggleButton(TAttribute<FText> Label, const FText& ToolTip, TAttribute<bool> IsOn, TFunction<void(bool bTurnOn)> OnToggled)
{
	return SNew(SBox)
		.MinDesiredWidth(36.0f)
		.MinDesiredHeight(FRoscStyle::MinTargetSize)
		[
			SNew(SCheckBox)
			.Style(&FRoscStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Rosc.Toggle"))
			.HAlign(HAlign_Center)
			.ToolTipText(ToolTip)
			.AccessibleText(ToolTip)
			.IsChecked_Lambda([IsOn] { return IsOn.Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([OnToggled = MoveTemp(OnToggled)](ECheckBoxState NewState) { OnToggled(NewState == ECheckBoxState::Checked); })
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("Rosc.Label"))
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Text(MoveTemp(Label))
			]
		];
}

TSharedRef<SWidget> Rosc::Widgets::MakeIconButton(const FName IconName, const FText& ToolTip, FOnClicked OnClicked)
{
	return SNew(SBox)
		.WidthOverride(FRoscStyle::MinTargetSize + 4.0f)
		.HeightOverride(FRoscStyle::MinTargetSize + 4.0f)
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

TSharedRef<SWidget> Rosc::Widgets::MakeComboButton(const FText& Name, const FText& ToolTip, TAttribute<FText> ValueText, FOnGetContent OnGetMenu)
{
	return SNew(SBox)
		.MinDesiredHeight(FRoscStyle::MinTargetSize)
		[
			SNew(SComboButton)
			.ToolTipText(ToolTip)
			.AccessibleText(Name)
			.OnGetMenuContent(MoveTemp(OnGetMenu))
			.ButtonContent()
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("Rosc.Value"))
				.Text(MoveTemp(ValueText))
			]
		];
}

TSharedRef<SWidget> Rosc::Widgets::MakeChoiceRow(const FText& Label, const FText& ToolTip, TAttribute<FText> ValueText, TArray<FChoice> Choices, TAttribute<bool> IsEnabled)
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

TSharedRef<SWidget> Rosc::Widgets::MakeSpinBox(FSpinBoxArgs Args)
{
	const TSharedRef<FDragTransaction> Drag = MakeShared<FDragTransaction>();
	const TAttribute<float> Value = Args.Value;

	return SNew(SBox)
		.MinDesiredHeight(FRoscStyle::MinTargetSize)
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

TSharedRef<INumericTypeInterface<float>> Rosc::Widgets::MakeDistanceInterface()
{
	return MakeShared<FDistanceTypeInterface>();
}

void Rosc::Widgets::Notify(const FText& Message, bool bSucceeded)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 6.0f;
	const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bSucceeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

void Rosc::Widgets::MeasureRebuiltContent(SWidget& Tab, const FGeometry& AllottedGeometry)
{
	// The geometry's scale is the layout scale the frame's measuring pass gave the tab.
	Tab.SlatePrepass(AllottedGeometry.Scale);
}

bool Rosc::Widgets::ResolveDroppedPath(const TArray<FString>& Dropped, const bool bFolder, const TArray<FString>& Extensions, FString& OutPath, FText& OutError)
{
	if (Dropped.IsEmpty())
	{
		OutError = LOCTEXT("DropNothing", "Nothing was dropped.");
		return false;
	}
	const FString Path = FPaths::ConvertRelativePathToFull(Dropped[0]);
	const bool bIsFolder = IFileManager::Get().DirectoryExists(*Path);
	if (bFolder)
	{
		OutPath = bIsFolder ? Path : FPaths::GetPath(Path);
		return true;
	}
	if (bIsFolder)
	{
		OutError = LOCTEXT("DropFolderOnFile", "Drop a file here, not a folder.");
		return false;
	}
	if (!Extensions.IsEmpty() && !Extensions.Contains(FPaths::GetExtension(Path).ToLower()))
	{
		OutError = FText::Format(LOCTEXT("DropWrongKind", "{0} is not a kind of file this takes. It takes: {1}."),
			FText::FromString(FPaths::GetCleanFilename(Path)), FText::FromString(FString::Join(Extensions, TEXT(", "))));
		return false;
	}
	OutPath = Path;
	return true;
}

namespace
{
	TSharedRef<SWidget> MakePathControl(const bool bFolder, const FText& Name, const FText& ToolTip, const FText& BrowseToolTip, const FText& FileTypeName, TArray<FString> Extensions, TFunction<FString()> GetPath, TFunction<void(const FString& Path)> ChoosePath)
	{
		using namespace Rosc::Widgets;

		const auto Choose = [GetPath, ChoosePath = MoveTemp(ChoosePath)](const FString& Path)
		{
			// The box shows the path in use, which for a folder is the project's own until another is chosen.
			if (Path == GetPath())
			{
				return;
			}
			if (!Path.IsEmpty() && FPaths::IsRelative(Path))
			{
				Notify(LOCTEXT("PathNotWhole", "Give the whole path, from its drive on."), false);
				return;
			}
			ChoosePath(Path);
		};

		const FText BoxToolTip = FText::Format(bFolder
			? LOCTEXT("FolderBoxToolTip", "{0} Browse picks it, or drag a folder here from Explorer.")
			: LOCTEXT("FileBoxToolTip", "{0} Browse picks it, or drag the file here from Explorer."), ToolTip);

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SDropTarget)
				.OnAllowDrop_Lambda([](TSharedPtr<FDragDropOperation> Operation)
				{
					return Operation.IsValid() && Operation->IsOfType<FExternalDragOperation>() && StaticCastSharedPtr<FExternalDragOperation>(Operation)->HasFiles();
				})
				.OnDropped_Lambda([bFolder, Extensions, Choose](const FGeometry&, const FDragDropEvent& Event)
				{
					const TSharedPtr<FExternalDragOperation> Operation = Event.GetOperationAs<FExternalDragOperation>();
					if (Operation.IsValid() && Operation->HasFiles())
					{
						FString Path;
						FText Error;
						if (ResolveDroppedPath(Operation->GetFiles(), bFolder, Extensions, Path, Error))
						{
							Choose(Path);
						}
						else
						{
							Notify(Error, false);
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SBox)
					.MinDesiredHeight(FRoscStyle::MinTargetSize)
					[
						SNew(SEditableTextBox)
						.ToolTipText(BoxToolTip)
						.AccessibleText(Name)
						.Text_Lambda([GetPath] { return FText::FromString(GetPath()); })
						.OnTextCommitted_Lambda([Choose](const FText& Text, ETextCommit::Type CommitType)
						{
							// Escape gives up what was typed. A path copied in Explorer comes in quotes.
							if (CommitType != ETextCommit::OnCleared)
							{
								Choose(Text.ToString().TrimStartAndEnd().TrimQuotes());
							}
						})
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(4.0f, 4.0f))
				+ SWrapBox::Slot()
				[
					MakeButton(LOCTEXT("BrowseButton", "Browse"), BrowseToolTip, FOnClicked::CreateLambda([bFolder, BrowseToolTip, FileTypeName, Extensions, GetPath, Choose]
					{
						IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
						if (DesktopPlatform == nullptr)
						{
							return FReply::Handled();
						}
						const void* Window = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
						if (bFolder)
						{
							FString Folder;
							if (DesktopPlatform->OpenDirectoryDialog(Window, BrowseToolTip.ToString(), GetPath(), Folder))
							{
								Choose(Folder);
							}
							return FReply::Handled();
						}
						const FString Pattern = TEXT("*.") + FString::Join(Extensions, TEXT(";*."));
						TArray<FString> Files;
						if (DesktopPlatform->OpenFileDialog(Window, BrowseToolTip.ToString(), FPaths::GetPath(GetPath()), FString(),
							FString::Printf(TEXT("%s (%s)|%s"), *FileTypeName.ToString(), *Pattern, *Pattern), EFileDialogFlags::None, Files) && !Files.IsEmpty())
						{
							Choose(FPaths::ConvertRelativePathToFull(Files[0]));
						}
						return FReply::Handled();
					}))
				]
				+ SWrapBox::Slot()
				[
					MakeButton(LOCTEXT("ShowInExplorerButton", "Show in Explorer"),
						bFolder ? LOCTEXT("ShowFolderToolTip", "Open this folder in Explorer.") : LOCTEXT("ShowFileToolTip", "Open the folder this file is in, in Explorer."),
						FOnClicked::CreateLambda([bFolder, GetPath]
						{
							const FString Path = GetPath();
							if (bFolder ? IFileManager::Get().DirectoryExists(*Path) : IFileManager::Get().FileExists(*Path))
							{
								FPlatformProcess::ExploreFolder(*Path);
							}
							else if (Path.IsEmpty())
							{
								Notify(LOCTEXT("ShowNothing", "Nothing is chosen yet."), false);
							}
							else
							{
								Notify(bFolder
									? LOCTEXT("ShowNoFolder", "This folder is not there yet. It is made when the first file is written to it.")
									: LOCTEXT("ShowNoFile", "There is no file at this path."), false);
							}
							return FReply::Handled();
						}))
				]
			];
	}
}

TSharedRef<SWidget> Rosc::Widgets::MakeFolderControl(const FText& Name, const FText& ToolTip, const FText& BrowseToolTip, TFunction<FString()> GetFolder, TFunction<void(const FString& Folder)> ChooseFolder)
{
	return MakePathControl(true, Name, ToolTip, BrowseToolTip, FText::GetEmpty(), TArray<FString>(), MoveTemp(GetFolder), MoveTemp(ChooseFolder));
}

TSharedRef<SWidget> Rosc::Widgets::MakeFileControl(const FText& Name, const FText& ToolTip, const FText& BrowseToolTip, const FText& FileTypeName, TArray<FString> Extensions, TFunction<FString()> GetFile, TFunction<void(const FString& File)> ChooseFile)
{
	return MakePathControl(false, Name, ToolTip, BrowseToolTip, FileTypeName, MoveTemp(Extensions), MoveTemp(GetFile), MoveTemp(ChooseFile));
}

namespace
{
	/** The first widget inside another that takes keyboard focus and can be used now. */
	TSharedPtr<SWidget> FindFirstControl(const TSharedRef<SWidget>& Widget)
	{
		if (!Widget->GetVisibility().IsVisible() || !Widget->IsEnabled())
		{
			return nullptr;
		}
		if (Widget->SupportsKeyboardFocus())
		{
			return Widget;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (const TSharedPtr<SWidget> Found = FindFirstControl(Children->GetChildAt(Index)))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

void Rosc::Widgets::FRowFocus::RowsRebuilt(TConstArrayView<TSharedRef<SWidget>> Rows, const TSharedPtr<SWidget>& WhenEmpty)
{
	if (RemovedIndex.IsSet())
	{
		Pending = Rows.IsEmpty() ? WhenEmpty : TSharedPtr<SWidget>(Rows[FMath::Min(*RemovedIndex, Rows.Num() - 1)]);
		RemovedIndex.Reset();
	}
}

void Rosc::Widgets::FRowFocus::Tick()
{
	if (const TSharedPtr<SWidget> Row = Pending.Pin())
	{
		if (const TSharedPtr<SWidget> Control = FindFirstControl(Row.ToSharedRef()))
		{
			FSlateApplication::Get().SetKeyboardFocus(Control, EFocusCause::SetDirectly);
		}
	}
	Pending.Reset();
}

#undef LOCTEXT_NAMESPACE
