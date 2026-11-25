// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscShotTypes.h"

#define LOCTEXT_NAMESPACE "RoscCore"

namespace Rosc::Shots
{
	TConstArrayView<ERoscShotSize> GetShotSizes()
	{
		static const ERoscShotSize Sizes[] =
		{
			ERoscShotSize::Unset,
			ERoscShotSize::ExtremeWide,
			ERoscShotSize::Wide,
			ERoscShotSize::Full,
			ERoscShotSize::MediumWide,
			ERoscShotSize::Medium,
			ERoscShotSize::MediumCloseUp,
			ERoscShotSize::CloseUp,
			ERoscShotSize::ExtremeCloseUp,
			ERoscShotSize::Insert,
			ERoscShotSize::OverTheShoulder,
			ERoscShotSize::PointOfView,
			ERoscShotSize::TwoShot,
		};
		return Sizes;
	}

	FText GetShotSizeName(ERoscShotSize Size)
	{
		switch (Size)
		{
		case ERoscShotSize::ExtremeWide: return LOCTEXT("SizeExtremeWide", "Extreme wide");
		case ERoscShotSize::Wide: return LOCTEXT("SizeWide", "Wide");
		case ERoscShotSize::Full: return LOCTEXT("SizeFull", "Full");
		case ERoscShotSize::MediumWide: return LOCTEXT("SizeMediumWide", "Medium wide");
		case ERoscShotSize::Medium: return LOCTEXT("SizeMedium", "Medium");
		case ERoscShotSize::MediumCloseUp: return LOCTEXT("SizeMediumCloseUp", "Medium close-up");
		case ERoscShotSize::CloseUp: return LOCTEXT("SizeCloseUp", "Close-up");
		case ERoscShotSize::ExtremeCloseUp: return LOCTEXT("SizeExtremeCloseUp", "Extreme close-up");
		case ERoscShotSize::Insert: return LOCTEXT("SizeInsert", "Insert");
		case ERoscShotSize::OverTheShoulder: return LOCTEXT("SizeOverTheShoulder", "Over the shoulder");
		case ERoscShotSize::PointOfView: return LOCTEXT("SizePointOfView", "Point of view");
		case ERoscShotSize::TwoShot: return LOCTEXT("SizeTwoShot", "Two shot");
		default: return LOCTEXT("SizeUnset", "Not set");
		}
	}

	FText GetShotSizeAbbreviation(ERoscShotSize Size)
	{
		switch (Size)
		{
		case ERoscShotSize::ExtremeWide: return LOCTEXT("SizeShortExtremeWide", "EWS");
		case ERoscShotSize::Wide: return LOCTEXT("SizeShortWide", "WS");
		case ERoscShotSize::Full: return LOCTEXT("SizeShortFull", "FS");
		case ERoscShotSize::MediumWide: return LOCTEXT("SizeShortMediumWide", "MWS");
		case ERoscShotSize::Medium: return LOCTEXT("SizeShortMedium", "MS");
		case ERoscShotSize::MediumCloseUp: return LOCTEXT("SizeShortMediumCloseUp", "MCU");
		case ERoscShotSize::CloseUp: return LOCTEXT("SizeShortCloseUp", "CU");
		case ERoscShotSize::ExtremeCloseUp: return LOCTEXT("SizeShortExtremeCloseUp", "ECU");
		case ERoscShotSize::Insert: return LOCTEXT("SizeShortInsert", "INS");
		case ERoscShotSize::OverTheShoulder: return LOCTEXT("SizeShortOverTheShoulder", "OTS");
		case ERoscShotSize::PointOfView: return LOCTEXT("SizeShortPointOfView", "POV");
		case ERoscShotSize::TwoShot: return LOCTEXT("SizeShortTwoShot", "2S");
		default: return FText::GetEmpty();
		}
	}
}

#undef LOCTEXT_NAMESPACE
