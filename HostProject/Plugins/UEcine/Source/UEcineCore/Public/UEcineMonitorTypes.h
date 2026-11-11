#pragma once

#include "CoreMinimal.h"
#include "UEcineMonitorTypes.generated.h"

/** Colours offered for lines and highlights drawn over the picture. */
UENUM()
enum class EUEcineMonitorColour : uint8
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
struct UECINECORE_API FUEcineMonitorSettings
{
	GENERATED_BODY()

	/** Replace the picture with exposure bands (see UEcine::Monitor::GetFalseColourBands). */
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
	EUEcineMonitorColour PeakingColour = EUEcineMonitorColour::Red;

	/** Smallest step in signal, in percent, between a pixel and its neighbours that counts as sharp detail. */
	UPROPERTY()
	float PeakingThresholdPercent = 8.0f;

	/** Signal level of every column of the picture, drawn bottom left. */
	UPROPERTY()
	bool bWaveform = false;

	/** How much of the picture sits at each signal level, drawn bottom right. */
	UPROPERTY()
	bool bHistogram = false;

	/** True when any tool that changes the picture's pixels is on. */
	bool UsesPicturePass() const
	{
		return bFalseColour || bZebras || bPeaking || bWaveform || bHistogram;
	}
};

/** One exposure band of the false colour tool: every signal level in [FromPercent, ToPercent) is shown in Colour. */
struct FUEcineFalseColourBand
{
	float FromPercent = 0.0f;
	float ToPercent = 0.0f;
	/** The colour as it appears on screen. */
	FColor Colour = FColor::Black;
	FText Name;
};

/** Where the scopes sit in a picture, in pixels from its top left corner. A scope that is off has an empty rectangle. */
struct FUEcineScopeLayout
{
	FIntRect Waveform;
	FIntRect Histogram;
};

namespace UEcine::Monitor
{
	/** The colour as it appears on screen. */
	UECINECORE_API FColor GetColour(EUEcineMonitorColour Colour);

	/**
	 * Signal level of a displayed pixel in percent: 0 is black, 100 is the brightest the display shows.
	 * This is the luma of the picture as it is seen, after tone mapping, which is what an on-set monitor measures.
	 */
	UECINECORE_API float GetSignalPercent(const FColor& DisplayedColour);

	/** The false colour bands from darkest to brightest. Levels between bands are shown in grey. */
	UECINECORE_API TConstArrayView<FUEcineFalseColourBand> GetFalseColourBands();

	/** The band a signal level falls in, or null when it is shown in grey. */
	UECINECORE_API const FUEcineFalseColourBand* FindFalseColourBand(float SignalPercent);

	/** Places the scopes along the bottom of a picture of the given size. */
	UECINECORE_API FUEcineScopeLayout GetScopeLayout(FIntPoint PictureSize, bool bWaveform, bool bHistogram);
}
