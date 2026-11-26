// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "UObject/WeakObjectPtr.h"

class ACineCameraActor;
class FRHIGPUTextureReadback;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * The camera's view for the RoscCam app (ADR-014): captured as the storyboard frames are, with the
 * camera's exposure and without the monitor tools, read back from the graphics card without
 * waiting for it, and coded as JPEG off the game thread. One frame is in flight at a time.
 */
class FRoscCamPicture : public FGCObject
{
public:
	FRoscCamPicture(int32 InWidth, int32 InHeight, double InFramesPerSecond);
	virtual ~FRoscCamPicture() override;

	/** Takes the camera's view when the next frame is due, and moves the frames in flight along. */
	void Update(const ACineCameraActor* Camera, double NowSeconds);

	/** The newest frame coded since the last call, if there is one. */
	bool TakeFrame(TArray<uint8>& OutJpeg);

	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FRoscCamPicture"); }

private:
	void EnsureCapture(UWorld* World);

	const int32 Width;
	const int32 Height;
	const double FramesPerSecond;
	double NextFrameSeconds = 0.0;

	TObjectPtr<UTextureRenderTarget2D> Target = nullptr;
	TObjectPtr<USceneCaptureComponent2D> Capture = nullptr;
	TWeakObjectPtr<UWorld> CaptureWorld;

	/** Shared with the render thread and the coding task, so it outlives either. */
	struct FInFlight
	{
		TUniquePtr<FRHIGPUTextureReadback> Readback;
		std::atomic<bool> bCopyQueued = false;
		std::atomic<bool> bBusy = false;
		FCriticalSection Lock;
		TArray<uint8> LatestJpeg;
		bool bHasLatest = false;
	};
	TSharedRef<FInFlight, ESPMode::ThreadSafe> InFlight;
};
