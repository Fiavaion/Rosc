#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class ACineCameraActor;
class UUEcineCameraPackageComponent;

/**
 * The camera the UEcine panel is working on. It follows the level editor selection: selecting
 * a Cine Camera Actor makes it the target, and selecting anything else keeps the last camera,
 * so the panel does not go blank while the user adjusts lights or set dressing.
 */
class FUEcineCameraTarget
{
public:
	FUEcineCameraTarget();
	~FUEcineCameraTarget();

	/** Null when there is no camera, or the targeted camera has been deleted. */
	ACineCameraActor* GetCamera() const;

	/** The targeted camera's package, or null when it has none yet. */
	UUEcineCameraPackageComponent* GetPackageComponent() const;

	/** Targets a camera and selects it in the level editor. */
	void SetCamera(ACineCameraActor* NewCamera);

private:
	void OnSelectionChanged(UObject* SelectionObject);
	void TargetSelectedCamera();

	TWeakObjectPtr<ACineCameraActor> Camera;
	FDelegateHandle SelectionChangedHandle;
};
