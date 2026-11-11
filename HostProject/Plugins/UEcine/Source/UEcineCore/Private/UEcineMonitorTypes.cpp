#include "UEcineMonitorTypes.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

namespace UEcine::Monitor
{
	FColor GetColour(EUEcineMonitorColour Colour)
	{
		switch (Colour)
		{
		case EUEcineMonitorColour::Red: return FColor(255, 48, 48);
		case EUEcineMonitorColour::Green: return FColor(64, 230, 64);
		case EUEcineMonitorColour::Blue: return FColor(72, 140, 255);
		case EUEcineMonitorColour::Yellow: return FColor(255, 224, 32);
		default: return FColor(255, 255, 255);
		}
	}

	FText GetColourName(EUEcineMonitorColour Colour)
	{
		switch (Colour)
		{
		case EUEcineMonitorColour::Red: return LOCTEXT("ColourRed", "Red");
		case EUEcineMonitorColour::Green: return LOCTEXT("ColourGreen", "Green");
		case EUEcineMonitorColour::Blue: return LOCTEXT("ColourBlue", "Blue");
		case EUEcineMonitorColour::Yellow: return LOCTEXT("ColourYellow", "Yellow");
		default: return LOCTEXT("ColourWhite", "White");
		}
	}

	FIntRect GetDeliveryFrame(FIntPoint PictureSize, float DeliveryAspect)
	{
		FIntPoint FrameSize = PictureSize;
		if (DeliveryAspect > 0.0f && PictureSize.X > 0 && PictureSize.Y > 0)
		{
			// Wider than the picture: bars above and below. Narrower: bars at the sides.
			if (DeliveryAspect * PictureSize.Y > PictureSize.X)
			{
				FrameSize.Y = FMath::Min(PictureSize.Y, FMath::RoundToInt32(PictureSize.X / DeliveryAspect));
			}
			else
			{
				FrameSize.X = FMath::Min(PictureSize.X, FMath::RoundToInt32(PictureSize.Y * DeliveryAspect));
			}
		}
		const FIntPoint Min = (PictureSize - FrameSize) / 2;
		return FIntRect(Min, Min + FrameSize);
	}

	FIntRect GetSafeArea(const FIntRect& Frame, float Fraction)
	{
		const FIntPoint AreaSize(FMath::RoundToInt32(Frame.Width() * Fraction), FMath::RoundToInt32(Frame.Height() * Fraction));
		const FIntPoint Min = Frame.Min + (Frame.Size() - AreaSize) / 2;
		return FIntRect(Min, Min + AreaSize);
	}

	FText FormatDataReadout(const FUEcineCameraData& Data, EUEcineDistanceUnits Units)
	{
		using UEcine::Units::FormatNumber;

		TArray<FText> Parts;
		Parts.Add(FText::FromString(Data.CameraName));
		Parts.Add(FText::Format(LOCTEXT("ReadoutFocalLength", "{0} mm"), FormatNumber(Data.FocalLengthMm, 0)));
		Parts.Add(FText::Format(LOCTEXT("ReadoutTStop", "T{0}"), FormatNumber(Data.TStop, 1)));
		if (Data.bHasExposure)
		{
			Parts.Add(FText::Format(LOCTEXT("ReadoutIso", "ISO {0}"), FormatNumber(Data.Iso, 0)));
			Parts.Add(FText::Format(LOCTEXT("ReadoutShutter", "{0}°"), FormatNumber(Data.ShutterAngleDeg, 1)));
			Parts.Add(FText::Format(LOCTEXT("ReadoutFrameRate", "{0} fps"), FormatNumber(Data.FramesPerSecond, 3)));
			if (Data.NdDensity > 0.0)
			{
				Parts.Add(FText::Format(LOCTEXT("ReadoutNd", "ND {0}"), FormatNumber(Data.NdDensity, 1)));
			}
		}
		Parts.Add(FText::Format(LOCTEXT("ReadoutFocus", "Focus {0}"), UEcine::Units::FormatDistance(Data.FocusDistanceMm, Units)));
		return FText::Join(FText::FromString(TEXT("   ")), Parts);
	}

	float GetSignalPercent(const FColor& DisplayedColour)
	{
		// Rec. 709 luma weights on the displayed (gamma-encoded) values.
		return (0.2126f * DisplayedColour.R + 0.7152f * DisplayedColour.G + 0.0722f * DisplayedColour.B) * (100.0f / 255.0f);
	}

	TConstArrayView<FUEcineFalseColourBand> GetFalseColourBands()
	{
		// The picture is measured as displayed, so the bands are set against what the engine's default tone
		// curve does to a scene. Measured in UE 5.8 (UEcine.Monitor.Picture prints the figures): an 18 % grey
		// card shows at 46 % signal, a quarter of a stop either side at 41 % and 51 %; one stop over grey shows
		// at 67 %, a quarter of a stop either side at 62 % and 71 %; four stops over reaches 96 %.
		// A project that replaces the tone curve moves these levels, and the bands then need its own figures.
		static const TArray<FUEcineFalseColourBand> Bands =
		{
			{ 0.0f, 2.0f, FColor(112, 32, 160), LOCTEXT("BandCrushed", "Crushed black") },
			{ 2.0f, 5.0f, FColor(32, 96, 232), LOCTEXT("BandNearBlack", "Near black") },
			{ 41.0f, 52.0f, FColor(40, 168, 64), LOCTEXT("BandMiddleGrey", "Middle grey") },
			{ 61.0f, 72.0f, FColor(244, 136, 176), LOCTEXT("BandOneStopOver", "One stop over grey") },
			{ 94.0f, 99.0f, FColor(248, 220, 40), LOCTEXT("BandNearClipping", "Near clipping") },
			{ 99.0f, 101.0f, FColor(228, 32, 32), LOCTEXT("BandClipped", "Clipped white") }
		};
		return Bands;
	}

	FText GetFalseColourRange(const FUEcineFalseColourBand& Band)
	{
		// The top band runs past 100 % so that it takes in 100 % itself; a legend stops where the signal does.
		return FText::Format(LOCTEXT("FalseColourRange", "{0}-{1} %"),
			FText::AsNumber(FMath::RoundToInt32(Band.FromPercent)), FText::AsNumber(FMath::Min(100, FMath::RoundToInt32(Band.ToPercent))));
	}

	const FUEcineFalseColourBand* FindFalseColourBand(float SignalPercent)
	{
		for (const FUEcineFalseColourBand& Band : GetFalseColourBands())
		{
			if (SignalPercent >= Band.FromPercent && SignalPercent < Band.ToPercent)
			{
				return &Band;
			}
		}
		return nullptr;
	}

	FUEcineScopeLayout GetScopeLayout(FIntPoint PictureSize, bool bWaveform, bool bHistogram)
	{
		FUEcineScopeLayout Layout;
		const int32 ScopeCount = (bWaveform ? 1 : 0) + (bHistogram ? 1 : 0);
		if (ScopeCount == 0)
		{
			return Layout;
		}

		// A quarter of the picture height, three to two, shrunk to fit a narrow picture.
		const int32 Height = FMath::Clamp(FMath::RoundToInt32(PictureSize.Y * 0.25f), 64, 512);
		const int32 Margin = FMath::Max(8, Height / 16);
		const int32 Width = FMath::Min(Height * 3 / 2, (PictureSize.X - Margin * (ScopeCount + 1)) / ScopeCount);
		if (Width < 64 || PictureSize.Y < Height + Margin * 2)
		{
			return Layout;
		}

		const int32 Top = PictureSize.Y - Margin - Height;
		if (bWaveform)
		{
			Layout.Waveform = FIntRect(Margin, Top, Margin + Width, Top + Height);
		}
		if (bHistogram)
		{
			Layout.Histogram = FIntRect(PictureSize.X - Margin - Width, Top, PictureSize.X - Margin, Top + Height);
		}
		return Layout;
	}
}

#undef LOCTEXT_NAMESPACE
