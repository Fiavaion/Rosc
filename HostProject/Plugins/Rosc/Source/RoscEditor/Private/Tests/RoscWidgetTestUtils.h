// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Reading what a panel shows, for the tests that drive it. */
namespace Rosc::WidgetTests
{
	/** True when a line of text that is shown somewhere under a widget has these words in it. */
	inline bool ShowsText(const TSharedRef<SWidget>& Widget, const TCHAR* Words)
	{
		if (!Widget->GetVisibility().IsVisible())
		{
			return false;
		}
		if (Widget->GetType() == FName("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Words))
		{
			return true;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (ShowsText(Children->GetChildAt(Index), Words))
			{
				return true;
			}
		}
		return false;
	}

	/** The words a widget shows, its lines of text one after the other. */
	inline FString GetShownText(const TSharedRef<SWidget>& Widget)
	{
		FString Text;
		if (Widget->GetType() == FName("STextBlock"))
		{
			Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
		}
		FChildren* Children = Widget->GetAllChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			const FString ChildText = GetShownText(Children->GetChildAt(Index));
			Text += (Text.IsEmpty() || ChildText.IsEmpty()) ? ChildText : TEXT("\n") + ChildText;
		}
		return Text;
	}

	/** Every button under a widget that shows the label, in the order they are laid out. */
	inline void FindButtonsShowing(const TSharedRef<SWidget>& Widget, const TCHAR* Label, TArray<TSharedRef<SButton>>& OutButtons)
	{
		if (Widget->GetType() == FName("SButton") && GetShownText(Widget) == Label)
		{
			OutButtons.Add(StaticCastSharedRef<SButton>(Widget));
			return;
		}
		FChildren* Children = Widget->GetAllChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			FindButtonsShowing(Children->GetChildAt(Index), Label, OutButtons);
		}
	}

	/** The first button under a widget that shows the label, or none. */
	inline TSharedPtr<SButton> FindButtonShowing(const TSharedRef<SWidget>& Widget, const TCHAR* Label)
	{
		TArray<TSharedRef<SButton>> Buttons;
		FindButtonsShowing(Widget, Label, Buttons);
		return Buttons.IsEmpty() ? nullptr : TSharedPtr<SButton>(Buttons[0]);
	}
}

#endif
