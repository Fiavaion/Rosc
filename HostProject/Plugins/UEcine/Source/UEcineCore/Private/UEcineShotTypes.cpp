// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineShotTypes.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

namespace UEcine::Shots
{
	TConstArrayView<EUEcineShotSize> GetShotSizes()
	{
		static const EUEcineShotSize Sizes[] =
		{
			EUEcineShotSize::Unset,
			EUEcineShotSize::ExtremeWide,
			EUEcineShotSize::Wide,
			EUEcineShotSize::Full,
			EUEcineShotSize::MediumWide,
			EUEcineShotSize::Medium,
			EUEcineShotSize::MediumCloseUp,
			EUEcineShotSize::CloseUp,
			EUEcineShotSize::ExtremeCloseUp,
			EUEcineShotSize::Insert,
			EUEcineShotSize::OverTheShoulder,
			EUEcineShotSize::PointOfView,
			EUEcineShotSize::TwoShot,
		};
		return Sizes;
	}

	FText GetShotSizeName(EUEcineShotSize Size)
	{
		switch (Size)
		{
		case EUEcineShotSize::ExtremeWide: return LOCTEXT("SizeExtremeWide", "Extreme wide");
		case EUEcineShotSize::Wide: return LOCTEXT("SizeWide", "Wide");
		case EUEcineShotSize::Full: return LOCTEXT("SizeFull", "Full");
		case EUEcineShotSize::MediumWide: return LOCTEXT("SizeMediumWide", "Medium wide");
		case EUEcineShotSize::Medium: return LOCTEXT("SizeMedium", "Medium");
		case EUEcineShotSize::MediumCloseUp: return LOCTEXT("SizeMediumCloseUp", "Medium close-up");
		case EUEcineShotSize::CloseUp: return LOCTEXT("SizeCloseUp", "Close-up");
		case EUEcineShotSize::ExtremeCloseUp: return LOCTEXT("SizeExtremeCloseUp", "Extreme close-up");
		case EUEcineShotSize::Insert: return LOCTEXT("SizeInsert", "Insert");
		case EUEcineShotSize::OverTheShoulder: return LOCTEXT("SizeOverTheShoulder", "Over the shoulder");
		case EUEcineShotSize::PointOfView: return LOCTEXT("SizePointOfView", "Point of view");
		case EUEcineShotSize::TwoShot: return LOCTEXT("SizeTwoShot", "Two shot");
		default: return LOCTEXT("SizeUnset", "Not set");
		}
	}

	FText GetShotSizeAbbreviation(EUEcineShotSize Size)
	{
		switch (Size)
		{
		case EUEcineShotSize::ExtremeWide: return LOCTEXT("SizeShortExtremeWide", "EWS");
		case EUEcineShotSize::Wide: return LOCTEXT("SizeShortWide", "WS");
		case EUEcineShotSize::Full: return LOCTEXT("SizeShortFull", "FS");
		case EUEcineShotSize::MediumWide: return LOCTEXT("SizeShortMediumWide", "MWS");
		case EUEcineShotSize::Medium: return LOCTEXT("SizeShortMedium", "MS");
		case EUEcineShotSize::MediumCloseUp: return LOCTEXT("SizeShortMediumCloseUp", "MCU");
		case EUEcineShotSize::CloseUp: return LOCTEXT("SizeShortCloseUp", "CU");
		case EUEcineShotSize::ExtremeCloseUp: return LOCTEXT("SizeShortExtremeCloseUp", "ECU");
		case EUEcineShotSize::Insert: return LOCTEXT("SizeShortInsert", "INS");
		case EUEcineShotSize::OverTheShoulder: return LOCTEXT("SizeShortOverTheShoulder", "OTS");
		case EUEcineShotSize::PointOfView: return LOCTEXT("SizeShortPointOfView", "POV");
		case EUEcineShotSize::TwoShot: return LOCTEXT("SizeShortTwoShot", "2S");
		default: return FText::GetEmpty();
		}
	}
}

#undef LOCTEXT_NAMESPACE
