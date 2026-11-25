// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Input/Reply.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/NumericTypeInterface.h"

class SWidget;

/** The pieces every department tab is built from, so the tabs look and behave alike. */
namespace Rosc::Widgets
{
	const FTextBlockStyle& TextStyle(FName StyleName);

	TSharedRef<SWidget> MakeSectionHeader(const FText& Title);

	/** A label on the left and a control filling the rest of the row. */
	TSharedRef<SWidget> MakeRow(const FText& Label, TAttribute<FText> LabelToolTip, const TSharedRef<SWidget>& Control);

	/** A derived number the user reads but does not edit. */
	TSharedRef<SWidget> MakeReadoutRow(const FText& Label, const FText& ToolTip, TAttribute<FText> Value);

	/** A centred message with one action, shown when there is nothing to edit yet. */
	TSharedRef<SWidget> MakeEmptyState(const FText& Message, const FText& ButtonLabel, const FText& ButtonToolTip, FOnClicked OnClicked);

	/** A line or two of guidance in the body text. Takes no space while the text is empty. */
	TSharedRef<SWidget> MakeNote(TAttribute<FText> Text);

	/** An icon of the editor's beside a message that says how something stands. Takes no space while the message is empty. */
	TSharedRef<SWidget> MakeStatusLine(FName IconName, TAttribute<FText> Message);

	/** A warning icon beside the message. Takes no space while the message is empty. */
	TSharedRef<SWidget> MakeWarning(TAttribute<FText> Message);

	/** A push button with a text label. */
	TSharedRef<SWidget> MakeButton(TAttribute<FText> Label, TAttribute<FText> ToolTip, FOnClicked OnClicked, TAttribute<bool> IsEnabled = true);

	/** A button that stays lit, and ringed, while its state is on: one lens of a kit, one tab of the panel. */
	TSharedRef<SWidget> MakeToggleButton(TAttribute<FText> Label, const FText& ToolTip, TAttribute<bool> IsOn, TFunction<void(bool bTurnOn)> OnToggled);

	/** A square button showing only an icon. The tooltip is also its name for screen readers. */
	TSharedRef<SWidget> MakeIconButton(FName IconName, const FText& ToolTip, FOnClicked OnClicked);

	/** A drop-down button showing the current choice. Its menu is built each time it opens. */
	TSharedRef<SWidget> MakeComboButton(const FText& Name, const FText& ToolTip, TAttribute<FText> ValueText, FOnGetContent OnGetMenu);

	/** One entry of a drop-down list of choices. */
	struct FChoice
	{
		FText Name;
		TFunction<bool()> IsChosen;
		TFunction<void()> Choose;
	};

	/** A label and a drop-down list of choices, with the current one ticked. */
	TSharedRef<SWidget> MakeChoiceRow(const FText& Label, const FText& ToolTip, TAttribute<FText> ValueText, TArray<FChoice> Choices, TAttribute<bool> IsEnabled = true);

	struct FSpinBoxArgs
	{
		/** Names the control for screen readers. */
		FText Name;
		TAttribute<FText> ToolTip;
		/** The undo step a drag is recorded as. */
		FText TransactionName;
		TAttribute<float> Value;
		TAttribute<TOptional<float>> MinValue;
		TAttribute<TOptional<float>> MaxValue;
		/** The range a drag across the control covers, when that is narrower than the allowed range. */
		TOptional<float> MinSliderValue;
		TOptional<float> MaxSliderValue;
		float Delta = 1.0f;
		/** Above 1, a drag moves the value slowly near the low end of the range and quickly near the high end. */
		float SliderExponent = 1.0f;
		int32 MaxFractionalDigits = 1;
		/** How the value is written and read, when it is more than a plain number. */
		TSharedPtr<INumericTypeInterface<float>> TypeInterface;
		/** Records what the control changes, once, as a drag starts. */
		TFunction<void()> SaveForUndo;
		/** Applies a value during a drag, inside the drag's undo step. */
		TFunction<void(float)> OnDrag;
		/** Applies a typed value as an undo step of its own. */
		TFunction<void(float)> OnCommit;
	};

	/**
	 * A number that can be dragged or typed. A drag changes the value many times; the whole drag
	 * is one undo step, closed when the drag ends or the control goes away.
	 */
	TSharedRef<SWidget> MakeSpinBox(FSpinBoxArgs Args);

	/**
	 * Writes and reads a distance the way the crew says it, in the user's units: 3.2 m or 10' 6".
	 * The value itself stays in centimetres.
	 */
	TSharedRef<INumericTypeInterface<float>> MakeDistanceInterface();

	/**
	 * A box holding a folder's whole path, with Browse and Show in Explorer under it. A folder
	 * dragged from Explorer can be dropped on the box. The box shows the folder in use. What the
	 * user types, picks or drops is handed to ChooseFolder when it is a whole path and not the
	 * folder already in use; an empty path asks for the folder the project keeps for the purpose.
	 */
	TSharedRef<SWidget> MakeFolderControl(const FText& Name, const FText& ToolTip, const FText& BrowseToolTip, TFunction<FString()> GetFolder, TFunction<void(const FString& Folder)> ChooseFolder);

	/**
	 * The same for one file. Extensions are the kinds of file taken, in lower case with no dot;
	 * FileTypeName names them in the file dialog ("Pictures").
	 */
	TSharedRef<SWidget> MakeFileControl(const FText& Name, const FText& ToolTip, const FText& BrowseToolTip, const FText& FileTypeName, TArray<FString> Extensions, TFunction<FString()> GetFile, TFunction<void(const FString& File)> ChooseFile);

	/**
	 * What a path control takes from files dropped on it: the first one, as a whole path. A
	 * folder control takes a folder, or the folder a dropped file is in; a file control takes a
	 * file of one of its kinds (any kind when Extensions is empty). False, with the reason, when
	 * the drop is not something the control takes.
	 */
	bool ResolveDroppedPath(const TArray<FString>& Dropped, bool bFolder, const TArray<FString>& Extensions, FString& OutPath, FText& OutError);

	/**
	 * Keeps keyboard focus in a list whose rows are rebuilt when one is removed. Without it the
	 * focus goes with the removed row's button, and a keyboard user starts again from the top.
	 */
	class FRowFocus
	{
	public:
		/** Called by a row's remove button with the row's place in the list. */
		void RowRemoved(int32 Index) { RemovedIndex = Index; }

		/**
		 * Called when the rows have been rebuilt. After a removal the row that took the removed
		 * one's place is to get the focus, or the last row, or WhenEmpty when no row is left.
		 */
		void RowsRebuilt(TConstArrayView<TSharedRef<SWidget>> Rows, const TSharedPtr<SWidget>& WhenEmpty);

		/** Called each tick: moves the focus once the new rows have been laid out. */
		void Tick();

	private:
		TOptional<int32> RemovedIndex;
		TWeakPtr<SWidget> Pending;
	};

	/** Tells the user how something they asked for turned out, in a toast with a tick or a cross. */
	void Notify(const FText& Message, bool bSucceeded);

	/**
	 * Measures a tab whose content was rebuilt in its Tick. Slate ticks a widget while painting it,
	 * after the frame's measuring pass, so new rows would be drawn once with no height, on top of
	 * each other; that frame stays on screen whenever the next one is late.
	 */
	void MeasureRebuiltContent(SWidget& Tab, const FGeometry& AllottedGeometry);
}
