// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscCameraPackage.h"

class ACineCameraActor;
class URoscCameraPackageComponent;
class UWorld;

/**
 * The camera department's editing operations. Each one is a single undoable transaction, so
 * the panel, scripts and tests all change cameras the same way.
 */
namespace Rosc::CameraEditing
{
	/**
	 * A sensible starting package: the default body and lens kit, with the lens closest to the
	 * given focal length. Falls back to the first database entries when the defaults are absent.
	 */
	ROSCEDITOR_API FRoscCameraPackage MakeDefaultPackage(float FocalLengthMm, float TStop);

	/**
	 * Gives a Cine Camera Actor a camera package matched to its current focal length and
	 * aperture, and applies it. Returns the existing component when the actor already has one.
	 */
	ROSCEDITOR_API URoscCameraPackageComponent* AddCameraPackage(ACineCameraActor* CameraActor);

	/** Creates a Cine Camera Actor with a camera package in the world's current level and selects it. */
	ROSCEDITOR_API ACineCameraActor* SpawnCamera(UWorld* World, const FTransform& Transform);

	/** SpawnCamera in the level being edited, placed where the active viewport is looking from. */
	ROSCEDITOR_API ACineCameraActor* SpawnCameraAtViewport();

	/**
	 * Changes a camera's package and applies it to the camera as one undoable step.
	 * @param Description What the user did, shown in the undo history ("Change Lens").
	 * @param Edit Makes the change to the package.
	 * @param OutNotice Set when the change did something to tell the user once, as it happens: a
	 *        new lens moved a hand-set focus nearer than it can focus out to its close focus.
	 * @return false, with the package and camera unchanged and the reason in OutError, when the
	 *         edited package cannot be applied.
	 */
	ROSCEDITOR_API bool EditPackage(URoscCameraPackageComponent* Component, const FText& Description, TFunctionRef<void(FRoscCameraPackage&)> Edit, FText& OutError, FText* OutNotice = nullptr);

	/** What a field of view comes to on a camera, and the lens of its kit that comes nearest. */
	struct FLensMatch
	{
		/** The focal length that gives the field of view on the camera's recording format, with the kit's squeeze. */
		double FocalLengthMm = 0.0;
		/** The kit's nearest lens, as the kit names it, and the focal length it would be at: a zoom is set between its ends. */
		FString LensName;
		double LensFocalLengthMm = 0.0;
	};

	/**
	 * Finds the lens that would see a horizontal field of view on the camera, such as the one a
	 * reference picture was taken with.
	 * @return false when the camera has no package that can be worked out, or the angle is not one a lens has.
	 */
	ROSCEDITOR_API bool MatchLensToFieldOfView(const ACineCameraActor* Camera, double HorizontalFieldOfViewDeg, FLensMatch& OutMatch);

	/** Puts the lens MatchLensToFieldOfView finds on the camera, as one undoable step. OutNotice is as for EditPackage. */
	ROSCEDITOR_API bool PutOnLensForFieldOfView(ACineCameraActor* Camera, double HorizontalFieldOfViewDeg, FText& OutError, FText* OutNotice = nullptr);
}
