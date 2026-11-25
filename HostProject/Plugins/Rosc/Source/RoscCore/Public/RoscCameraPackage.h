// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoscCameraPackage.generated.h"

class FRoscCameraDatabase;
struct FRoscCameraBody;
struct FRoscLensKit;

/**
 * What the camera department has chosen for one camera: body, recording format, lens and
 * exposure settings, in film terms. Ids refer to entries in the camera database.
 */
USTRUCT(BlueprintType)
struct FRoscCameraPackage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FString BodyId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FString FormatId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lens")
	FString LensKitId;

	/**
	 * Width over height of the frame the shot is delivered in, such as 2.39. Zero delivers the
	 * whole recording format.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0"))
	float DeliveryAspect = 0.0f;

	/** Which lens of the kit is on the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lens")
	int32 LensIndex = 0;

	/** Current focal length. Fixed by the lens for a prime; the zoom position for a zoom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lens", meta = (ForceUnits = mm))
	float FocalLengthMm = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (DisplayName = "T-stop"))
	float TStop = 2.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (DisplayName = "ISO"))
	int32 Iso = 800;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (ClampMin = "1", ClampMax = "360", Units = deg))
	float ShutterAngleDeg = 180.0f;

	/** Optical density of the neutral density filter in front of the lens. 0 is no filter; each 0.3 is one stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (DisplayName = "ND", ClampMin = "0"))
	float NdDensity = 0.0f;

	/** Frame rate the shot is recorded at; with the shutter angle it sets the exposure time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (ClampMin = "1"))
	float FramesPerSecond = 24.0f;

	/**
	 * When on, ISO, shutter, T-stop and ND set how bright the image is, as on a real camera, and
	 * the scene must be lit at real-world light levels. When off, they only affect depth of field
	 * and motion blur, and the scene's own exposure settings are left alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	bool bPhysicalExposure = false;

	/** Brightens (positive) or darkens the image in stops when physical exposure is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (EditCondition = "bPhysicalExposure"))
	float ExposureCompensationStops = 0.0f;

	/** Chooses a body, its first recording format and its native ISO. */
	ROSCCORE_API void SelectBody(const FRoscCameraBody& Body);

	/** Chooses a lens kit and the lens in it closest to the current focal length. */
	ROSCCORE_API void SelectLensKit(const FRoscLensKit& Kit);

	/** Puts a lens of the kit on the camera, keeping the T-stop within what the lens can do. */
	ROSCCORE_API void SelectLens(const FRoscLensKit& Kit, int32 NewLensIndex);
};

/** A camera package turned into the numbers a camera needs, with limits applied. */
struct FRoscResolvedCamera
{
	double SensorWidthMm = 0.0;
	double SensorHeightMm = 0.0;
	double Squeeze = 1.0;

	double MinFocalLengthMm = 0.0;
	double MaxFocalLengthMm = 0.0;
	double FocalLengthMm = 0.0;

	/** Widest aperture of the lens, the smallest T-number. */
	double WidestTStop = 0.0;
	double MinApertureTStop = 0.0;
	double TStop = 0.0;

	double CloseFocusMm = 0.0;
	/** Zero when the kit does not state it. */
	int32 IrisBlades = 0;

	/** Shutter speed as the engine wants it: the X in 1/X seconds. */
	double ShutterSpeedReciprocal = 0.0;
	/** Fraction of the frame interval the shutter is open. */
	double MotionBlurAmount = 0.0;

	bool bPhysicalExposure = false;
	int32 Iso = 0;
	/** Exposure offset in stops: compensation minus the light lost to the ND filter. */
	double ExposureBiasStops = 0.0;

	/** Things the user should know that do not stop the package being applied, such as vignetting. */
	TArray<FText> Warnings;
};

/** A frame shape a shot can be delivered in. */
struct FRoscDeliveryAspect
{
	/** Width over height. Zero is the whole recording format. */
	float Aspect = 0.0f;
	FText Name;
};

namespace Rosc
{
	/** The delivery frames offered, the whole recording format first. */
	ROSCCORE_API TConstArrayView<FRoscDeliveryAspect> GetDeliveryAspects();

	/** What a delivery aspect is called: the name of an offered one, or "2.2:1" for any other. */
	ROSCCORE_API FText GetDeliveryAspectName(float Aspect);

	/**
	 * Looks up the package's ids in the database and applies the lens limits.
	 * @return false when an id cannot be found; OutError then says which one.
	 */
	ROSCCORE_API bool ResolveCameraPackage(const FRoscCameraDatabase& Database, const FRoscCameraPackage& Package, FRoscResolvedCamera& OutCamera, FText& OutError);
}
