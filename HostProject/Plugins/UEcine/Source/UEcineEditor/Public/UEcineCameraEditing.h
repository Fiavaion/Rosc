#pragma once

#include "CoreMinimal.h"
#include "UEcineCameraPackage.h"

class ACineCameraActor;
class UUEcineCameraPackageComponent;
class UWorld;

/**
 * The camera department's editing operations. Each one is a single undoable transaction, so
 * the panel, scripts and tests all change cameras the same way.
 */
namespace UEcine::CameraEditing
{
	/**
	 * A sensible starting package: the default body and lens kit, with the lens closest to the
	 * given focal length. Falls back to the first database entries when the defaults are absent.
	 */
	UECINEEDITOR_API FUEcineCameraPackage MakeDefaultPackage(float FocalLengthMm, float TStop);

	/**
	 * Gives a Cine Camera Actor a camera package matched to its current focal length and
	 * aperture, and applies it. Returns the existing component when the actor already has one.
	 */
	UECINEEDITOR_API UUEcineCameraPackageComponent* AddCameraPackage(ACineCameraActor* CameraActor);

	/** Creates a Cine Camera Actor with a camera package in the world's current level and selects it. */
	UECINEEDITOR_API ACineCameraActor* SpawnCamera(UWorld* World, const FTransform& Transform);

	/** SpawnCamera in the level being edited, placed where the active viewport is looking from. */
	UECINEEDITOR_API ACineCameraActor* SpawnCameraAtViewport();

	/**
	 * Changes a camera's package and applies it to the camera as one undoable step.
	 * @param Description What the user did, shown in the undo history ("Change Lens").
	 * @param Edit Makes the change to the package.
	 * @return false, with the package and camera unchanged and the reason in OutError, when the
	 *         edited package cannot be applied.
	 */
	UECINEEDITOR_API bool EditPackage(UUEcineCameraPackageComponent* Component, const FText& Description, TFunctionRef<void(FUEcineCameraPackage&)> Edit, FText& OutError, TArray<FText>* OutWarnings = nullptr);
}
