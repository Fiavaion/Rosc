// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
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
}

#endif
