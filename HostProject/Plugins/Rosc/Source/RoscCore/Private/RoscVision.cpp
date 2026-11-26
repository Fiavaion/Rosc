// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscVision.h"

TArray<uint16> Rosc::Vision::NearnessToGrey16(TConstArrayView<float> Nearness)
{
	float Low = TNumericLimits<float>::Max();
	float High = TNumericLimits<float>::Lowest();
	for (const float Value : Nearness)
	{
		Low = FMath::Min(Low, Value);
		High = FMath::Max(High, Value);
	}
	const float Scale = 65535.0f / FMath::Max(High - Low, UE_SMALL_NUMBER);

	TArray<uint16> Grey;
	Grey.SetNumUninitialized(Nearness.Num());
	for (int32 Index = 0; Index < Nearness.Num(); ++Index)
	{
		Grey[Index] = static_cast<uint16>(FMath::RoundToInt32((Nearness[Index] - Low) * Scale));
	}
	return Grey;
}

TArray<uint8> Rosc::Vision::LogitsToGrey8(TConstArrayView<float> Logits)
{
	TArray<uint8> Grey;
	Grey.SetNumUninitialized(Logits.Num());
	for (int32 Index = 0; Index < Logits.Num(); ++Index)
	{
		Grey[Index] = static_cast<uint8>(FMath::RoundToInt32(255.0f / (1.0f + FMath::Exp(-Logits[Index]))));
	}
	return Grey;
}

TOptional<double> Rosc::Vision::FieldOfViewFromPoints(TConstArrayView<float> PointsXyz, TConstArrayView<float> Mask, int32 Width, int32 Height)
{
	check(Width > 0 && Height > 0 && Mask.Num() == Width * Height && PointsXyz.Num() == Mask.Num() * 3);

	// The picture's corners are one unit from its centre, whatever its shape.
	const double Aspect = static_cast<double>(Width) / Height;
	const double SpanX = Aspect / FMath::Sqrt(1.0 + Aspect * Aspect);
	const double SpanY = 1.0 / FMath::Sqrt(1.0 + Aspect * Aspect);

	struct FSample
	{
		FVector2d Pixel;
		FVector3d Point;
	};
	// A grid of samples says as much as every pixel would.
	constexpr int32 GridSize = 64;
	const int32 Columns = FMath::Min(Width, GridSize);
	const int32 Rows = FMath::Min(Height, GridSize);
	TArray<FSample> Samples;
	Samples.Reserve(Columns * Rows);
	double NearestZ = TNumericLimits<double>::Max();
	double FarthestZ = TNumericLimits<double>::Lowest();
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const int32 Y = Rows > 1 ? FMath::RoundToInt32(static_cast<double>(Row) * (Height - 1) / (Rows - 1)) : 0;
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const int32 X = Columns > 1 ? FMath::RoundToInt32(static_cast<double>(Column) * (Width - 1) / (Columns - 1)) : 0;
			const int32 Index = Y * Width + X;
			if (Mask[Index] > 0.5f)
			{
				FSample& Sample = Samples.AddDefaulted_GetRef();
				Sample.Pixel = FVector2d((2.0 * (X + 0.5) / Width - 1.0) * SpanX, (2.0 * (Y + 0.5) / Height - 1.0) * SpanY);
				Sample.Point = FVector3d(PointsXyz[Index * 3], PointsXyz[Index * 3 + 1], PointsXyz[Index * 3 + 2]);
				NearestZ = FMath::Min(NearestZ, Sample.Point.Z);
				FarthestZ = FMath::Max(FarthestZ, Sample.Point.Z);
			}
		}
	}
	if (Samples.Num() < 2)
	{
		return {};
	}

	// The best focal length for a shift, and how far off their pixels the points then land.
	const auto Fit = [&Samples](double Shift, double& OutError)
	{
		double Across = 0.0;
		double Squares = 0.0;
		for (const FSample& Sample : Samples)
		{
			const FVector2d Projected = FVector2d(Sample.Point.X, Sample.Point.Y) / (Sample.Point.Z + Shift);
			Across += Projected.Dot(Sample.Pixel);
			Squares += Projected.SquaredLength();
		}
		const double Focal = Squares > 0.0 ? Across / Squares : 0.0;
		OutError = 0.0;
		for (const FSample& Sample : Samples)
		{
			const FVector2d Projected = FVector2d(Sample.Point.X, Sample.Point.Y) / (Sample.Point.Z + Shift);
			OutError += (Projected * Focal - Sample.Pixel).SquaredLength();
		}
		return Focal;
	};

	// Every point has to stay in front of the camera, which bounds the shift from below.
	const double Depth = FarthestZ - NearestZ + UE_DOUBLE_SMALL_NUMBER;
	double Low = -NearestZ + 1e-3 * Depth;
	double High = Low + 4.0 * Depth + 4.0 * FMath::Abs(FarthestZ);
	const double GoldenRatio = (FMath::Sqrt(5.0) - 1.0) / 2.0;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		const double First = High - GoldenRatio * (High - Low);
		const double Second = Low + GoldenRatio * (High - Low);
		double FirstError = 0.0;
		double SecondError = 0.0;
		Fit(First, FirstError);
		Fit(Second, SecondError);
		if (FirstError < SecondError)
		{
			High = Second;
		}
		else
		{
			Low = First;
		}
	}
	double Error = 0.0;
	const double Focal = Fit((Low + High) / 2.0, Error);
	if (!FMath::IsFinite(Focal) || Focal <= 0.0)
	{
		return {};
	}
	return FMath::RadiansToDegrees(2.0 * FMath::Atan(SpanX / Focal));
}
