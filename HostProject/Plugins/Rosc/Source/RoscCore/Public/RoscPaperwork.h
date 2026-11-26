// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/** A picture placed in the paperwork, such as a storyboard frame. */
struct FRoscPaperworkPicture
{
	FString Caption;
	/** The picture as a JPEG file. */
	TArray<uint8> Jpeg;
};

/**
 * Everything the paperwork says about one shot, already written the way it is to be read
 * ("35 mm", "T2.8", "3.05 m"). A field left empty is left out or left blank.
 */
struct FRoscShotReport
{
	FString Scene;
	FString Shot;
	FString Size;
	FString Move;
	FString Notes;
	/** Timecode of the first frame of the shot. */
	FString Start;
	FString Duration;

	FString Camera;
	FString Body;
	FString Format;
	FString LensKit;
	FString FocalLength;
	FString TStop;
	FString Iso;
	FString Shutter;
	FString FrameRate;
	FString Nd;
	FString Focus;
	FString Support;

	TArray<FRoscPaperworkPicture> Storyboard;
	/** The overhead plan as an SVG picture, or empty when there is none. */
	FString PlanSvg;
};

/** A sequence's shots, ready to be written out as a shot list and a camera report. */
struct FRoscPaperwork
{
	/** What the paperwork is of, usually the name of the sequence. */
	FString Title;
	FString Date;
	/** The language the text is in, as an IETF tag such as "en", for the page's language attribute. */
	FString Language = TEXT("en");
	TArray<FRoscShotReport> Shots;
};

namespace Rosc::Paperwork
{
	/** The shot list as a spreadsheet: one line per shot, with a heading line. */
	ROSCCORE_API FString WriteShotListCsv(const FRoscPaperwork& Paperwork);

	/** The camera report as a spreadsheet: one line per shot with its camera, lens and exposure. */
	ROSCCORE_API FString WriteCameraReportCsv(const FRoscPaperwork& Paperwork);

	/**
	 * The shot list, then each shot with its storyboard, overhead plan and camera report, as one web
	 * page with everything inside it. It prints, or saves as a PDF, from any browser.
	 */
	ROSCCORE_API FString WriteHtml(const FRoscPaperwork& Paperwork);
}
