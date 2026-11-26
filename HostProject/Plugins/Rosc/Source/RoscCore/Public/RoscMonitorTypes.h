// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscUnits.h"
#include "RoscMonitorTypes.generated.h"

/** Colours offered for lines and highlights drawn over the picture. */
UENUM()
enum class ERoscMonitorColour : uint8
{
	White,
	Red,
	Green,
	Blue,
	Yellow
};

/**
 * Which monitor tools are switched on and how they are set. This describes the view, like the
 * buttons on an on-set monitor: it is a per-user preference and never part of the shot.
 */
USTRUCT()
struct ROSCCORE_API FRoscMonitorSettings
{
	GENERATED_BODY()

	/** Replace the picture with exposure bands (see Rosc::Monitor::GetFalseColourBands). */
	UPROPERTY()
	bool bFalseColour = false;

	/** Stripe every part of the picture at or above the zebra level. */
	UPROPERTY()
	bool bZebras = false;

	/** Signal level, in percent, at which zebras start. */
	UPROPERTY()
	float ZebraLevelPercent = 95.0f;

	/** Outline fine detail that is in focus. */
	UPROPERTY()
	bool bPeaking = false;

	UPROPERTY()
	ERoscMonitorColour PeakingColour = ERoscMonitorColour::Red;

	/** Smallest step in signal, in percent, between a pixel and its neighbours that counts as sharp detail. */
	UPROPERTY()
	float PeakingThresholdPercent = 8.0f;

	/** Signal level of every column of the picture, drawn bottom left. */
	UPROPERTY()
	bool bWaveform = false;

	/** How much of the picture sits at each signal level, drawn bottom right. */
	UPROPERTY()
	bool bHistogram = false;

	/** Outline the frame the shot is delivered in, when that is not the whole picture. */
	UPROPERTY()
	bool bFrameLines = false;

	/** Outline the action safe and title safe areas of the delivery frame. */
	UPROPERTY()
	bool bSafeAreas = false;

	/** A cross at the centre of the frame. */
	UPROPERTY()
	bool bCentreMark = false;

	/** The colour of the frame lines, safe areas and centre mark. */
	UPROPERTY()
	ERoscMonitorColour LineColour = ERoscMonitorColour::White;

	/** A line of camera data over the picture: camera, lens, exposure and focus. */
	UPROPERTY()
	bool bDataReadout = false;

	/** True when any tool is on, so there is something to draw over the picture. */
	bool IsAnyToolOn() const
	{
		return bFalseColour || bZebras || bPeaking || bWaveform || bHistogram || bFrameLines || bSafeAreas || bCentreMark || bDataReadout;
	}

	/** True when a tool is on that draws the delivery frame or the data of the camera a view looks through. */
	bool IsAnyCameraToolOn() const
	{
		return bFrameLines || bSafeAreas || bCentreMark || bDataReadout;
	}
};

/** One exposure band of the false colour tool: every signal level in [FromPercent, ToPercent) is shown in Colour. */
struct FRoscFalseColourBand
{
	float FromPercent = 0.0f;
	float ToPercent = 0.0f;
	/** The colour as it appears on screen. */
	FColor Colour = FColor::Black;
	FText Name;
};

/** Where the scopes sit in a picture, in pixels from its top left corner. A scope that is off has an empty rectangle. */
struct FRoscScopeLayout
{
	FIntRect Waveform;
	FIntRect Histogram;
};

/** What the data readout says about the camera a view is looking through. */
struct FRoscCameraData
{
	FString CameraName;
	double FocalLengthMm = 0.0;
	double TStop = 0.0;
	double FocusDistanceMm = 0.0;

	/** False for a camera with no camera package, which has no ISO, shutter angle or frame rate to show. */
	bool bHasExposure = false;
	int32 Iso = 0;
	double ShutterAngleDeg = 0.0;
	double FramesPerSecond = 0.0;
	/** Zero is no filter, which the readout leaves out. */
	double NdDensity = 0.0;
};

namespace Rosc::Monitor
{
	/** Share of the delivery frame's width and height inside which the action is kept. */
	inline constexpr float ActionSafeFraction = 0.93f;
	/** Share of the delivery frame's width and height inside which titles are kept. */
	inline constexpr float TitleSafeFraction = 0.9f;

	/** The colour as it appears on screen. */
	ROSCCORE_API FColor GetColour(ERoscMonitorColour Colour);

	ROSCCORE_API FText GetColourName(ERoscMonitorColour Colour);

	/**
	 * Where the delivery frame sits in a picture, in pixels from its top left corner: the largest
	 * rectangle of the delivery aspect that fits, centred. The whole picture when the aspect is zero.
	 */
	ROSCCORE_API FIntRect GetDeliveryFrame(FIntPoint PictureSize, float DeliveryAspect);

	/** The middle of a frame, taking the given share of its width and height. */
	ROSCCORE_API FIntRect GetSafeArea(const FIntRect& Frame, float Fraction);

	/** One line of camera data, as a monitor shows it: "A Cam   35 mm   T2.8   ISO 800   180°   24 fps   ND 0.6   Focus 3.05 m". */
	ROSCCORE_API FText FormatDataReadout(const FRoscCameraData& Data, ERoscDistanceUnits Units);

	/**
	 * Signal level of a displayed pixel in percent: 0 is black, 100 is the brightest the display shows.
	 * This is the luma of the picture as it is seen, after tone mapping, which is what an on-set monitor measures.
	 */
	ROSCCORE_API float GetSignalPercent(const FColor& DisplayedColour);

	/** The false colour bands from darkest to brightest. Levels between bands are shown in grey. */
	ROSCCORE_API TConstArrayView<FRoscFalseColourBand> GetFalseColourBands();

	/** The signal levels a band covers, as a legend gives them: "41-52 %". */
	ROSCCORE_API FText GetFalseColourRange(const FRoscFalseColourBand& Band);

	/** The band a signal level falls in, or null when it is shown in grey. */
	ROSCCORE_API const FRoscFalseColourBand* FindFalseColourBand(float SignalPercent);

	/** Places the scopes along the bottom of a picture of the given size. */
	ROSCCORE_API FRoscScopeLayout GetScopeLayout(FIntPoint PictureSize, bool bWaveform, bool bHistogram);
}
