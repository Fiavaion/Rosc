// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscUnits.h"

#define LOCTEXT_NAMESPACE "RoscCore"

namespace Rosc::Units
{
	FText FormatNumber(double Value, int32 MaxDecimals)
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(false);
		Options.SetMaximumFractionalDigits(MaxDecimals);
		return FText::AsNumber(Value, &Options);
	}

	FText FormatDistance(double Millimetres, ERoscDistanceUnits Units)
	{
		if (Millimetres <= -0.5)
		{
			return FText::Format(LOCTEXT("NegativeDistance", "-{0}"), FormatDistance(-Millimetres, Units));
		}
		Millimetres = FMath::Max(Millimetres, 0.0);

		if (Units == ERoscDistanceUnits::Imperial)
		{
			const int64 TotalInches = FMath::RoundToInt64(Millimetres / 25.4);
			const int64 Feet = TotalInches / 12;
			const int64 Inches = TotalInches % 12;
			if (Feet == 0)
			{
				return FText::Format(LOCTEXT("Inches", "{0}\""), FText::AsNumber(Inches));
			}
			if (Feet >= 100)
			{
				return FText::Format(LOCTEXT("Feet", "{0}'"), FormatNumber(static_cast<double>(FMath::RoundToInt64(Millimetres / 304.8)), 0));
			}
			return FText::Format(LOCTEXT("FeetInches", "{0}' {1}\""), FText::AsNumber(Feet), FText::AsNumber(Inches));
		}

		if (Millimetres < 1000.0)
		{
			return FText::Format(LOCTEXT("Centimetres", "{0} cm"), FormatNumber(Millimetres / 10.0, 1));
		}
		const double Metres = Millimetres / 1000.0;
		return FText::Format(LOCTEXT("Metres", "{0} m"), FormatNumber(Metres, Metres < 10.0 ? 2 : 1));
	}

	TOptional<double> ParseDistance(const FString& Text, ERoscDistanceUnits Units)
	{
		FString Trimmed = Text.TrimStartAndEnd();
		const bool bNegative = Trimmed.RemoveFromStart(TEXT("-"));
		if (Trimmed.IsEmpty())
		{
			return {};
		}

		// One or more numbers, each followed by its unit: 10' 6", 3.2 m, 1 m 20 cm.
		double Millimetres = 0.0;
		bool bLastWasFeet = false;
		const TCHAR* Cursor = *Trimmed;
		while (*Cursor != 0)
		{
			const TCHAR* NumberStart = Cursor;
			while (FChar::IsDigit(*Cursor) || *Cursor == TEXT('.'))
			{
				++Cursor;
			}
			const FString NumberText = FString::ConstructFromPtrSize(NumberStart, UE_PTRDIFF_TO_INT32(Cursor - NumberStart));
			// IsNumeric alone accepts an empty string and a lone point.
			if (!NumberText.IsNumeric() || NumberText.FindLastCharByPredicate([](TCHAR Character) { return FChar::IsDigit(Character); }) == INDEX_NONE)
			{
				return {};
			}
			const double Number = FCString::Atod(*NumberText);
			while (FChar::IsWhitespace(*Cursor))
			{
				++Cursor;
			}

			const TCHAR* UnitStart = Cursor;
			while (*Cursor != 0 && !FChar::IsDigit(*Cursor) && !FChar::IsWhitespace(*Cursor) && *Cursor != TEXT('.'))
			{
				++Cursor;
			}
			const FString Unit = FString::ConstructFromPtrSize(UnitStart, UE_PTRDIFF_TO_INT32(Cursor - UnitStart)).ToLower();
			while (FChar::IsWhitespace(*Cursor))
			{
				++Cursor;
			}
			const bool bIsLast = *Cursor == 0;

			double MillimetresPerUnit = 0.0;
			if (Unit == TEXT("'") || Unit == TEXT("ft"))
			{
				MillimetresPerUnit = 304.8;
			}
			else if (Unit == TEXT("\"") || Unit == TEXT("in"))
			{
				MillimetresPerUnit = 25.4;
			}
			else if (Unit == TEXT("m"))
			{
				MillimetresPerUnit = 1000.0;
			}
			else if (Unit == TEXT("cm"))
			{
				MillimetresPerUnit = 10.0;
			}
			else if (Unit == TEXT("mm"))
			{
				MillimetresPerUnit = 1.0;
			}
			else if (Unit.IsEmpty() && bIsLast && bLastWasFeet)
			{
				// 4' 6 means four feet six inches.
				MillimetresPerUnit = 25.4;
			}
			else if (Unit.IsEmpty() && bIsLast && Millimetres == 0.0)
			{
				MillimetresPerUnit = Units == ERoscDistanceUnits::Imperial ? 304.8 : 1000.0;
			}
			else
			{
				return {};
			}

			Millimetres += Number * MillimetresPerUnit;
			bLastWasFeet = MillimetresPerUnit == 304.8;
		}
		return bNegative ? -Millimetres : Millimetres;
	}
}

#undef LOCTEXT_NAMESPACE
