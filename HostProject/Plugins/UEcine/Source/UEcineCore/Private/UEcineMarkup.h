#pragma once

#include "CoreMinimal.h"

namespace UEcine::Markup
{
	/** Text made safe to place inside HTML or SVG, as content or inside a quoted attribute. */
	inline FString Escape(const FString& Text)
	{
		return Text
			.Replace(TEXT("&"), TEXT("&amp;"))
			.Replace(TEXT("<"), TEXT("&lt;"))
			.Replace(TEXT(">"), TEXT("&gt;"))
			.Replace(TEXT("\""), TEXT("&quot;"))
			.Replace(TEXT("'"), TEXT("&#39;"));
	}
}
