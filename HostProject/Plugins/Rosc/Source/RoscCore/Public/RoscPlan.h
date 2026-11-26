// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscUnits.h"

/** Where the camera is at one moment of a shot, seen from above. Positions are world X and Y in centimetres. */
struct FRoscPlanCamera
{
	FVector2D Position = FVector2D::ZeroVector;
	/** Which way the lens points: 0 looks along world X, 90 along world Y. */
	double YawDeg = 0.0;
	/** The angle the lens takes in from side to side. */
	double HorizontalFovDeg = 0.0;
};

/** Something the shot is of, marked on the plan by name. */
struct FRoscPlanSubject
{
	FString Name;
	FVector2D Position = FVector2D::ZeroVector;
};

/** What an overhead plan of a shot shows: where the camera goes, what it runs on and what it looks at. */
struct FRoscPlan
{
	/** The camera through the shot, in time order. The first and last are drawn as cameras; the rest only shape the path. */
	TArray<FRoscPlanCamera> CameraPath;
	/** Dolly track and the like, each as a line through its points. */
	TArray<TArray<FVector2D>> Tracks;
	TArray<FRoscPlanSubject> Subjects;
};

/** What a line of a plan drawing stands for, so each can be given its own look. */
enum class ERoscPlanStroke : uint8
{
	Track,
	/** Where the camera travels. */
	Path,
	/** The edges of what the lens takes in. */
	FieldOfView,
	/** The camera body. */
	Camera,
	/** The scale bar. */
	Scale
};

struct FRoscPlanLine
{
	ERoscPlanStroke Stroke = ERoscPlanStroke::Path;
	TArray<FVector2D> Points;
};

struct FRoscPlanLabel
{
	/** Where the text starts, at the middle of its height. */
	FVector2D Position = FVector2D::ZeroVector;
	FString Text;
};

/**
 * A plan laid out in a picture: positions in pixels from the top left, with world X up the page
 * and world Y to the right, so a camera with no yaw looks up the page.
 */
struct FRoscPlanDrawing
{
	FVector2D Size = FVector2D::ZeroVector;
	TArray<FRoscPlanLine> Lines;
	/** A dot for each subject. */
	TArray<FVector2D> Subjects;
	TArray<FRoscPlanLabel> Labels;
};

namespace Rosc::Plan
{
	/** Radius of a subject's dot, in pixels. */
	inline constexpr double SubjectRadius = 4.0;

	/**
	 * Fits a plan into a picture, to scale, with a scale bar in the user's units. The first camera
	 * position is lettered A and the last B when the camera moves between them. A plan with no
	 * camera gives an empty drawing.
	 */
	ROSCCORE_API FRoscPlanDrawing Layout(const FRoscPlan& Plan, FVector2D PictureSize, ERoscDistanceUnits Units);

	/** The drawing as a standalone SVG picture, black on white, for a printed page. */
	ROSCCORE_API FString WriteSvg(const FRoscPlanDrawing& Drawing, const FString& Description);
}
