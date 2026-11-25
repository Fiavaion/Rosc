// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoscDeliver.h"

class UMovieGraphConfig;

/** Turns a render spec into a Movie Render Graph configuration made of the engine's own nodes. */
namespace Rosc::RenderGraph
{
	/**
	 * Builds the graph for one shot.
	 * @param Outer What owns the graph; it has to keep the graph alive until the render is over.
	 * @return null with the reason in OutError when the graph cannot be built, for example when
	 *         the engine plugin a format needs is not enabled.
	 */
	ROSCRENDER_API UMovieGraphConfig* Build(const FRoscRenderSpec& Spec, UObject* Outer, FText& OutError);

	/** The name of the graph branch the picture is rendered on. */
	inline const TCHAR* const PictureBranchName = TEXT("Picture");
}
