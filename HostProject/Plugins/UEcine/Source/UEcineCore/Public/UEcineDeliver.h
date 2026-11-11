#pragma once

#include "CoreMinimal.h"

/** The kind of file a render writes. */
enum class EUEcineRenderFormat : uint8
{
	/** Apple ProRes 422 LT in a .mov file: light enough to pass around for review. */
	ProRes422LT,
	/** Apple ProRes 4444 in a .mov file: a finished picture for the grade. */
	ProRes4444,
	/** One OpenEXR file per frame, holding every pass as a layer. */
	MultilayerExr
};

/** A name and a value written into the files that carry metadata, and read by the slate and the burn-ins. */
struct FUEcineMetadataEntry
{
	FString Name;
	FString Value;
};

/**
 * Everything the render of one shot is made of, as plain data. The Movie Render Graph
 * configuration for the shot is generated from this and from nothing else.
 */
struct FUEcineRenderSpec
{
	/** The folder the files are written to. */
	FString OutputDirectory;
	/**
	 * The file's name without its extension. It may hold the renderer's own tokens:
	 * {frame_number} for the frame and {version} for the version of the render.
	 */
	FString FileName;
	/** The picture's size in pixels. */
	FIntPoint Resolution = FIntPoint(1920, 1080);
	EUEcineRenderFormat Format = EUEcineRenderFormat::ProRes422LT;

	/** The picture is written scene-linear, without the display's tone curve: what compositing works in. */
	bool bSceneLinear = false;
	/** Extra passes beside the picture. They need a format that holds layers. */
	bool bDepth = false;
	bool bNormals = false;
	bool bMotionVectors = false;
	/** A matte for every object. */
	bool bMattes = false;

	/** Camera and shot data drawn over the picture. */
	bool bBurnIn = false;
	/**
	 * A slate as the first frame. The renderer can only lengthen a shot at both ends, so a
	 * slated shot is one frame longer at its head (the slate) and one at its tail.
	 */
	bool bSlate = false;

	/** How many moments of the shutter's open time make one frame; more gives smoother motion blur. */
	int32 TemporalSamples = 1;
	/** Frames played before the first one that is kept, so that lighting and exposure have settled. */
	int32 WarmUpFrames = 0;

	TArray<FUEcineMetadataEntry> Metadata;
};

namespace UEcine::Deliver
{
	/** How many frames are added to each end of a shot for a spec: one when it is slated. */
	UECINECORE_API int32 GetHandleFrames(const FUEcineRenderSpec& Spec);

	/** The extension of the files a format is written in, without the dot. */
	UECINECORE_API const TCHAR* GetFileExtension(EUEcineRenderFormat Format);

	/** Metadata that says the file's first frame is a slate. The widget that draws over the picture reads it. */
	inline const TCHAR* const SlateMetadataName = TEXT("uecine/slate");
	/** Metadata that says camera and shot data are drawn over every frame of the picture. */
	inline const TCHAR* const BurnInMetadataName = TEXT("uecine/burnIn");
}
