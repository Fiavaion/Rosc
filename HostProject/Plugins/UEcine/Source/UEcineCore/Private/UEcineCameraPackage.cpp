#include "UEcineCameraPackage.h"

#include "UEcineCameraDatabase.h"
#include "UEcineExposure.h"
#include "UEcineOptics.h"
#include "UEcineUnits.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

void FUEcineCameraPackage::SelectBody(const FUEcineCameraBody& Body)
{
	BodyId = Body.Id;
	FormatId = Body.Formats.IsEmpty() ? FString() : Body.Formats[0].Id;
	if (!Body.BaseIsos.IsEmpty())
	{
		Iso = Body.BaseIsos[0];
	}
}

void FUEcineCameraPackage::SelectLensKit(const FUEcineLensKit& Kit)
{
	LensKitId = Kit.Id;

	// Focal lengths are compared as ratios: 40 mm is closer to 35 mm than 50 mm is to 40 mm.
	int32 ClosestIndex = 0;
	double ClosestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Kit.Lenses.Num(); ++Index)
	{
		const FUEcineLens& Lens = Kit.Lenses[Index];
		const double NearestFocal = FMath::Clamp<double>(FocalLengthMm, Lens.FocalLengthMm, Lens.MaxFocalLengthMm);
		const double Distance = FMath::Abs(FMath::Loge(NearestFocal / FocalLengthMm));
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			ClosestIndex = Index;
		}
	}
	SelectLens(Kit, ClosestIndex);
}

void FUEcineCameraPackage::SelectLens(const FUEcineLensKit& Kit, int32 NewLensIndex)
{
	if (!Kit.Lenses.IsValidIndex(NewLensIndex))
	{
		return;
	}
	const FUEcineLens& Lens = Kit.Lenses[NewLensIndex];
	LensIndex = NewLensIndex;
	FocalLengthMm = static_cast<float>(FMath::Clamp<double>(FocalLengthMm, Lens.FocalLengthMm, Lens.MaxFocalLengthMm));
	TStop = static_cast<float>(FMath::Clamp<double>(TStop, Lens.TStop, Kit.MinApertureTStop));
}

bool UEcine::ResolveCameraPackage(const FUEcineCameraDatabase& Database, const FUEcineCameraPackage& Package, FUEcineResolvedCamera& OutCamera, FText& OutError)
{
	const FUEcineCameraBody* Body = Database.FindBody(Package.BodyId);
	if (Body == nullptr)
	{
		OutError = FText::Format(LOCTEXT("UnknownBody", "The camera body \"{0}\" is not in the camera database."), FText::FromString(Package.BodyId));
		return false;
	}
	const FUEcineRecordingFormat* Format = Body->FindFormat(Package.FormatId);
	if (Format == nullptr)
	{
		OutError = FText::Format(LOCTEXT("UnknownFormat", "{0} has no recording format \"{1}\"."), FText::FromString(Body->GetDisplayName()), FText::FromString(Package.FormatId));
		return false;
	}
	const FUEcineLensKit* Kit = Database.FindLensKit(Package.LensKitId);
	if (Kit == nullptr)
	{
		OutError = FText::Format(LOCTEXT("UnknownKit", "The lens kit \"{0}\" is not in the camera database."), FText::FromString(Package.LensKitId));
		return false;
	}
	if (!Kit->Lenses.IsValidIndex(Package.LensIndex))
	{
		OutError = FText::Format(LOCTEXT("UnknownLens", "{0} has no lens number {1}."), FText::FromString(Kit->GetDisplayName()), FText::AsNumber(Package.LensIndex + 1));
		return false;
	}
	const FUEcineLens& Lens = Kit->Lenses[Package.LensIndex];

	FUEcineResolvedCamera Camera;
	Camera.SensorWidthMm = Format->SensorWidthMm;
	Camera.SensorHeightMm = Format->SensorHeightMm;
	Camera.Squeeze = Kit->Squeeze;

	Camera.MinFocalLengthMm = Lens.FocalLengthMm;
	Camera.MaxFocalLengthMm = Lens.MaxFocalLengthMm;
	Camera.FocalLengthMm = FMath::Clamp<double>(Package.FocalLengthMm, Lens.FocalLengthMm, Lens.MaxFocalLengthMm);

	Camera.WidestTStop = Lens.TStop;
	Camera.MinApertureTStop = Kit->MinApertureTStop;
	Camera.TStop = FMath::Clamp<double>(Package.TStop, Lens.TStop, Kit->MinApertureTStop);

	Camera.CloseFocusMm = Lens.CloseFocusM * 1000.0;
	Camera.IrisBlades = Kit->IrisBlades;

	const double ShutterTime = UEcine::Exposure::ShutterTimeSeconds(Package.ShutterAngleDeg, Package.FramesPerSecond);
	Camera.ShutterSpeedReciprocal = 1.0 / ShutterTime;
	Camera.MotionBlurAmount = UEcine::Exposure::ShutterOpenFraction(Package.ShutterAngleDeg);

	Camera.bPhysicalExposure = Package.bPhysicalExposure;
	Camera.Iso = Package.Iso;
	Camera.ExposureBiasStops = Package.ExposureCompensationStops - UEcine::Exposure::NdStops(Package.NdDensity);

	if (!Kit->CoversFormat(*Format))
	{
		FNumberFormattingOptions OneDecimal;
		OneDecimal.SetMaximumFractionalDigits(1);
		Camera.Warnings.Add(FText::Format(
			LOCTEXT("ImageCircleTooSmall", "{0} lenses project a {1} mm image circle, smaller than the {2} mm diagonal of {3}. A real camera would vignette; choose a smaller recording format or a larger-format lens kit."),
			FText::FromString(Kit->GetDisplayName()),
			FText::AsNumber(Kit->ImageCircleMm, &OneDecimal),
			FText::AsNumber(UEcine::Optics::SensorDiagonalMm(Format->SensorWidthMm, Format->SensorHeightMm), &OneDecimal),
			FText::FromString(Format->Name)));
	}

	OutCamera = MoveTemp(Camera);
	return true;
}

TConstArrayView<FUEcineDeliveryAspect> UEcine::GetDeliveryAspects()
{
	static const TArray<FUEcineDeliveryAspect> Aspects =
	{
		{ 0.0f, LOCTEXT("DeliverFullFormat", "Full recording format") },
		{ 2.39f, LOCTEXT("Deliver239", "2.39:1 Scope") },
		{ 2.0f, LOCTEXT("Deliver200", "2:1") },
		{ 1.85f, LOCTEXT("Deliver185", "1.85:1 Flat") },
		{ 16.0f / 9.0f, LOCTEXT("Deliver178", "16:9") },
		{ 1.66f, LOCTEXT("Deliver166", "1.66:1") },
		{ 4.0f / 3.0f, LOCTEXT("Deliver133", "4:3") },
		{ 1.0f, LOCTEXT("Deliver100", "1:1 Square") },
		{ 0.8f, LOCTEXT("Deliver080", "4:5 Portrait") },
		{ 9.0f / 16.0f, LOCTEXT("Deliver056", "9:16 Vertical") }
	};
	return Aspects;
}

FText UEcine::GetDeliveryAspectName(float Aspect)
{
	for (const FUEcineDeliveryAspect& Offered : GetDeliveryAspects())
	{
		if (FMath::IsNearlyEqual(Offered.Aspect, Aspect, 0.005f))
		{
			return Offered.Name;
		}
	}
	return FText::Format(LOCTEXT("DeliverOther", "{0}:1"), UEcine::Units::FormatNumber(Aspect, 2));
}

#undef LOCTEXT_NAMESPACE
