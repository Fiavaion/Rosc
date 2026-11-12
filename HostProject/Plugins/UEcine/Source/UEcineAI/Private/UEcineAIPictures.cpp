// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineAIPictures.h"

#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "UEcineAIModels.h"

#define LOCTEXT_NAMESPACE "UEcineAIPictures"

bool UEcine::AIPictures::Load(const FString& File, FImage& OutPicture, FText& OutError)
{
	if (!FPaths::FileExists(File))
	{
		OutError = FText::Format(LOCTEXT("NoFile", "{0} is not there any more."), FText::FromString(File));
		return false;
	}
	if (!FImageUtils::LoadImage(*File, OutPicture) || OutPicture.SizeX <= 0 || OutPicture.SizeY <= 0)
	{
		OutError = FText::Format(LOCTEXT("NotAPicture", "{0} could not be read as a picture."), FText::FromString(File));
		return false;
	}
	OutPicture.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	return true;
}

TArray<float> UEcine::AIPictures::MakeTensor(const FImage& Picture, const FUEcineAIModelInput& Input, FIntPoint& OutSize)
{
	OutSize = Input.GetSizeFor(Picture.SizeX, Picture.SizeY);
	FImage Sized(OutSize.X, OutSize.Y, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	FImageCore::ResizeImage(Picture, Sized);

	const TArrayView64<const FColor> Pixels = Sized.AsBGRA8();
	const int64 PlaneSize = Pixels.Num();
	TArray<float> Tensor;
	Tensor.SetNumUninitialized(PlaneSize * 3);
	for (int64 Index = 0; Index < PlaneSize; ++Index)
	{
		const FColor& Pixel = Pixels[Index];
		Tensor[Index] = (Pixel.R / 255.0f - Input.Mean.X) / Input.Deviation.X;
		Tensor[PlaneSize + Index] = (Pixel.G / 255.0f - Input.Mean.Y) / Input.Deviation.Y;
		Tensor[PlaneSize * 2 + Index] = (Pixel.B / 255.0f - Input.Mean.Z) / Input.Deviation.Z;
	}
	return Tensor;
}

TArray<float> UEcine::AIPictures::Resize(TConstArrayView<float> Values, FIntPoint Size, FIntPoint NewSize)
{
	check(Values.Num() == Size.X * Size.Y);
	if (Size == NewSize)
	{
		return TArray<float>(Values);
	}
	const FImageView Source(const_cast<float*>(Values.GetData()), Size.X, Size.Y, 1, ERawImageFormat::R32F, EGammaSpace::Linear);
	FImage Sized(NewSize.X, NewSize.Y, ERawImageFormat::R32F, EGammaSpace::Linear);
	FImageCore::ResizeImage(Source, Sized, FImageCore::EResizeImageFilter::Bilinear);
	return TArray<float>(Sized.AsR32F());
}

FImage UEcine::AIPictures::MakeGrey16(TConstArrayView<uint16> Values, FIntPoint Size)
{
	check(Values.Num() == Size.X * Size.Y);
	FImage Picture(Size.X, Size.Y, ERawImageFormat::G16, EGammaSpace::Linear);
	FMemory::Memcpy(Picture.AsG16().GetData(), Values.GetData(), Values.NumBytes());
	return Picture;
}

FImage UEcine::AIPictures::MakeGrey8(TConstArrayView<uint8> Values, FIntPoint Size)
{
	check(Values.Num() == Size.X * Size.Y);
	FImage Picture(Size.X, Size.Y, ERawImageFormat::G8, EGammaSpace::Linear);
	FMemory::Memcpy(Picture.AsG8().GetData(), Values.GetData(), Values.NumBytes());
	return Picture;
}

#undef LOCTEXT_NAMESPACE
