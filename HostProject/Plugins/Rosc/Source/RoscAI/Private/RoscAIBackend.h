// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "ImageCore.h"
#include "RoscAIModels.h"

/** What a backend made of a picture. */
struct FRoscAIOutput
{
	bool bSuccess = false;
	/** Why not. Empty for a job that was given up. */
	FText Error;
	/** The depth map (16-bit grey) or matte (8-bit grey), at the picture's size. */
	FImage Picture;
	/** The horizontal field of view, for a focal length job. */
	double FieldOfViewDeg = 0.0;

	static FRoscAIOutput Failed(const FText& Why)
	{
		FRoscAIOutput Output;
		Output.Error = Why;
		return Output;
	}
};

/**
 * Somewhere a picture job can be run: inside the editor, in the helper on this machine, or on
 * a server elsewhere. A backend runs one job at a time and is used from the game thread.
 */
class IRoscAIBackend
{
public:
	virtual ~IRoscAIBackend() = default;

	/**
	 * The host pictures are sent to. Empty when they never leave this process. The job
	 * subsystem holds every job for a host until the user has agreed to it.
	 */
	virtual FString GetHost() const = 0;

	/** Whether a task can be run now; when not, why, in words the AI tab shows. */
	virtual bool CanRun(ERoscAITask Task, FText& OutWhyNot) const = 0;

	/** Starts a job. OnDone is called on the game thread, once, also for a job that fails or is given up. */
	virtual void Run(ERoscAITask Task, const FString& PictureFile, TFunction<void(FRoscAIOutput&&)> OnDone) = 0;

	/** What the running job is doing, in words, and how far along it is from 0 to 1. */
	virtual void GetProgress(FText& OutStage, float& OutFraction) const = 0;

	/** Gives the running job up. It still ends through OnDone. */
	virtual void Cancel() = 0;
};
