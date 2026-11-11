#include "UEcineUnits.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

namespace UEcine::Units
{
	FText FormatNumber(double Value, int32 MaxDecimals)
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(false);
		Options.SetMaximumFractionalDigits(MaxDecimals);
		return FText::AsNumber(Value, &Options);
	}

	FText FormatDistance(double Millimetres, EUEcineDistanceUnits Units)
	{
		if (Units == EUEcineDistanceUnits::Imperial)
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
}

#undef LOCTEXT_NAMESPACE
