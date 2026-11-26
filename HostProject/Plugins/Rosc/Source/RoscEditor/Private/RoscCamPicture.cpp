// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscCamPicture.h"

#include "Async/Async.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "TextureResource.h"

namespace
{
	/** Good enough to frame and focus by, and small enough for 24 frames a second on a home network. */
	constexpr int32 JpegQuality = 70;
}

FRoscCamPicture::FRoscCamPicture(int32 InWidth, int32 InHeight, double InFramesPerSecond)
	: Width(InWidth)
	, Height(InHeight)
	, FramesPerSecond(InFramesPerSecond)
	, InFlight(MakeShared<FInFlight, ESPMode::ThreadSafe>())
{
	// The coding task runs on a worker thread, where a module must not be loaded for the first time.
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	InFlight->Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("RoscCamPicture"));
}

FRoscCamPicture::~FRoscCamPicture()
{
	if (Capture != nullptr && IsValid(Capture))
	{
		Capture->DestroyComponent();
	}
	// Render commands still queued hold the shared state; let them finish before it is let go.
	FlushRenderingCommands();
}

void FRoscCamPicture::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Target);
	Collector.AddReferencedObject(Capture);
}

void FRoscCamPicture::EnsureCapture(UWorld* World)
{
	if (Target == nullptr)
	{
		Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		// Eight bits that hold the picture as displayed, like a viewport.
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->TargetGamma = 2.2f;
		Target->InitAutoFormat(Width, Height);
		Target->UpdateResourceImmediate(true);
	}
	if (Capture != nullptr && CaptureWorld.Get() == World)
	{
		return;
	}
	if (Capture != nullptr && IsValid(Capture))
	{
		Capture->DestroyComponent();
	}
	Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
	Capture->TextureTarget = Target;
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	// Without a rendering state of its own the capture has no exposure (see the storyboard frames).
	Capture->bAlwaysPersistRenderingState = true;
	Capture->RegisterComponentWithWorld(World);
	CaptureWorld = World;
}

void FRoscCamPicture::Update(const ACineCameraActor* Camera, double NowSeconds)
{
	if (!FApp::CanEverRender() || Camera == nullptr || Camera->GetWorld() == nullptr)
	{
		return;
	}

	const TSharedRef<FInFlight, ESPMode::ThreadSafe> Shared = InFlight;
	if (Shared->bBusy)
	{
		// Look for the copy on the render thread; when it is there, take the pixels and code them.
		if (Shared->bCopyQueued.exchange(false))
		{
			const int32 PictureWidth = Width;
			const int32 PictureHeight = Height;
			ENQUEUE_RENDER_COMMAND(RoscCamPictureRead)([Shared, PictureWidth, PictureHeight](FRHICommandListImmediate&)
			{
				if (!Shared->Readback->IsReady())
				{
					Shared->bCopyQueued = true;
					return;
				}
				int32 RowPitch = 0;
				const FColor* Rows = static_cast<const FColor*>(Shared->Readback->Lock(RowPitch));
				TArray<FColor> Pixels;
				if (Rows != nullptr)
				{
					Pixels.SetNumUninitialized(PictureWidth * PictureHeight);
					for (int32 Row = 0; Row < PictureHeight; ++Row)
					{
						FMemory::Memcpy(&Pixels[Row * PictureWidth], Rows + Row * RowPitch, PictureWidth * sizeof(FColor));
					}
				}
				Shared->Readback->Unlock();
				AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Shared, Pixels = MoveTemp(Pixels), PictureWidth, PictureHeight]
				{
					TArray64<uint8> Jpeg;
					if (Pixels.Num() == PictureWidth * PictureHeight
						&& FImageUtils::CompressImage(Jpeg, TEXT("jpg"), FImageView(Pixels.GetData(), PictureWidth, PictureHeight), JpegQuality))
					{
						FScopeLock Guard(&Shared->Lock);
						Shared->LatestJpeg = TArray<uint8>(Jpeg.GetData(), IntCastChecked<int32>(Jpeg.Num()));
						Shared->bHasLatest = true;
					}
					Shared->bBusy = false;
				});
			});
		}
		return;
	}
	if (NowSeconds < NextFrameSeconds)
	{
		return;
	}
	NextFrameSeconds = FMath::Max(NextFrameSeconds + 1.0 / FramesPerSecond, NowSeconds);

	EnsureCapture(Camera->GetWorld());
	FMinimalViewInfo View;
	Camera->GetCineCameraComponent()->GetCameraView(0.0f, View);
	Capture->SetCameraView(View);
	Capture->PostProcessSettings = View.PostProcessSettings;
	Capture->PostProcessBlendWeight = View.PostProcessBlendWeight;
	Capture->CaptureScene();

	Shared->bBusy = true;
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(RoscCamPictureCopy)([Shared, Resource](FRHICommandListImmediate& RHICmdList)
	{
		Shared->Readback->EnqueueCopy(RHICmdList, Resource->GetRenderTargetTexture());
		Shared->bCopyQueued = true;
	});
}

bool FRoscCamPicture::TakeFrame(TArray<uint8>& OutJpeg)
{
	FScopeLock Guard(&InFlight->Lock);
	if (!InFlight->bHasLatest)
	{
		return false;
	}
	OutJpeg = MoveTemp(InFlight->LatestJpeg);
	InFlight->bHasLatest = false;
	return true;
}
