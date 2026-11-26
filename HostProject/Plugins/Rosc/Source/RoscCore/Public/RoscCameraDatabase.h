// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/** One recording mode of a camera body: the area of the sensor it uses. */
struct FRoscRecordingFormat
{
	FString Id;
	FString Name;
	double SensorWidthMm = 0.0;
	double SensorHeightMm = 0.0;
	/** Recorded photosites. Zero for film gauges, which have no fixed resolution. */
	FIntPoint Resolution = FIntPoint::ZeroValue;
};

struct FRoscCameraBody
{
	FString Id;
	FString Manufacturer;
	FString Model;
	FString Mount;
	/** Native ISO ratings; the first is the default. Empty for film cameras. */
	TArray<int32> BaseIsos;
	TArray<FRoscRecordingFormat> Formats;

	ROSCCORE_API FString GetDisplayName() const;
	ROSCCORE_API const FRoscRecordingFormat* FindFormat(const FString& FormatId) const;
};

struct FRoscLens
{
	/** Focal length of a prime, or the wide end of a zoom. */
	double FocalLengthMm = 0.0;
	/** Long end of a zoom. Equal to FocalLengthMm for a prime. */
	double MaxFocalLengthMm = 0.0;
	/** Widest aperture. */
	double TStop = 0.0;
	/** Minimum focus distance, measured from the image plane, in metres. */
	double CloseFocusM = 0.0;

	bool IsZoom() const { return MaxFocalLengthMm > FocalLengthMm; }
	/** "35mm T1.8" for a prime, "24-290mm T2.8" for a zoom. */
	ROSCCORE_API FString GetDisplayName() const;
};

/** A matched set of lenses from one manufacturer line. */
struct FRoscLensKit
{
	FString Id;
	FString Manufacturer;
	FString Name;
	/** Anamorphic squeeze factor; 1 for spherical lenses. */
	double Squeeze = 1.0;
	/** Diameter of the image the lenses project. A format with a larger diagonal will vignette. */
	double ImageCircleMm = 0.0;
	/** Smallest aperture the iris closes to. */
	double MinApertureTStop = 22.0;
	/** Number of iris blades, which shapes out-of-focus highlights. Zero when unknown. */
	int32 IrisBlades = 0;
	TArray<FRoscLens> Lenses;

	bool IsAnamorphic() const { return Squeeze > 1.0; }
	ROSCCORE_API FString GetDisplayName() const;
	/** True when the kit's image circle covers the format's sensor diagonal. */
	ROSCCORE_API bool CoversFormat(const FRoscRecordingFormat& Format) const;
};

/**
 * Camera bodies and lens kits, read from JSON files so the list can grow without recompiling
 * and productions can add their own equipment.
 */
class FRoscCameraDatabase
{
public:
	/** Schema version this build reads. */
	static constexpr int32 SchemaVersion = 1;

	/**
	 * The database shipped with the plugin, plus any files in the project's
	 * Config/Rosc/Database folder. Loaded on first use; problems are written to the log.
	 */
	ROSCCORE_API static const FRoscCameraDatabase& Get();

	/**
	 * Parses the text of one database file and adds its camera bodies and lens kits.
	 * An entry that fails validation is skipped and reported in OutErrors with the source name
	 * and the field at fault; valid entries in the same file are still added.
	 * @return true when the file added no errors.
	 */
	ROSCCORE_API bool AddFromJson(const FString& JsonText, const FString& SourceName, TArray<FString>& OutErrors);

	/** Adds every .json file in a folder, in name order. A missing folder adds nothing. */
	ROSCCORE_API void AddFromDirectory(const FString& Directory, TArray<FString>& OutErrors);

	const TArray<FRoscCameraBody>& GetBodies() const { return Bodies; }
	const TArray<FRoscLensKit>& GetLensKits() const { return LensKits; }

	ROSCCORE_API const FRoscCameraBody* FindBody(const FString& BodyId) const;
	ROSCCORE_API const FRoscLensKit* FindLensKit(const FString& KitId) const;

private:
	TArray<FRoscCameraBody> Bodies;
	TArray<FRoscLensKit> LensKits;
};
