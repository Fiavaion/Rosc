// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FAutomationTestBase;
struct FImage;

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Readers for the files a render writes, for the tests that check a delivery: enough of
 * OpenEXR and QuickTime to say what is in a file without decoding its pictures.
 */
namespace Rosc::RenderedFiles
{
	/** What the header of an OpenEXR file says. */
	struct FExrHeader
	{
		/** Every channel, as "layer.channel" or just "channel"; for a file in parts, each part's channels. */
		TArray<FString> Channels;
		/** Every attribute that is text, by name. */
		TMap<FString, FString> Text;
		/** The picture's size in pixels, from the first part. */
		FIntPoint Size = FIntPoint::ZeroValue;
		int32 PartCount = 0;
	};

	/** False when the file is not there or is not an OpenEXR file. */
	bool ReadExrHeader(const FString& Filename, FExrHeader& OutHeader);

	/** The layers of an OpenEXR file: what comes before the last dot of each channel name; "" for the plain picture. */
	TArray<FString> GetExrLayers(const FExrHeader& Header);

	/**
	 * The pixels of one layer of an OpenEXR file (a name GetExrLayers gives), as red, green, blue
	 * and alpha. Reads files stored in rows, unpacked or packed with ZIP, which is how a VFX
	 * handoff is written. False when the file, the layer or the packing is not one it reads.
	 */
	bool ReadExrLayer(const FString& Filename, const FString& Layer, FImage& OutPicture);

	/** How many pictures the video track of a QuickTime file holds, or -1 when the file is not there or has none. */
	int32 CountMovFrames(const FString& Filename);

	/**
	 * The engine logs an error for every graph render started in the editor: it asks for the
	 * version number before it has the graph to read it from (LESSON-API-010). The render is not
	 * affected, and a test that renders expects the message.
	 */
	void ExpectRenderLogNoise(FAutomationTestBase& Test);
}

#endif
