// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscCameraTarget.h"

#include "CineCameraActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Selection.h"
#include "RoscCameraPackageComponent.h"

FRoscCameraTarget::FRoscCameraTarget()
{
	SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &FRoscCameraTarget::OnSelectionChanged);

	TargetSelectedCamera();
	if (!Camera.IsValid() && GEditor != nullptr)
	{
		// Nothing selected: start on the first camera in the level, if there is one.
		if (const UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			for (TActorIterator<ACineCameraActor> It(World); It; ++It)
			{
				Camera = *It;
				break;
			}
		}
	}
}

FRoscCameraTarget::~FRoscCameraTarget()
{
	USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
}

ACineCameraActor* FRoscCameraTarget::GetCamera() const
{
	return Camera.Get();
}

URoscCameraPackageComponent* FRoscCameraTarget::GetPackageComponent() const
{
	return URoscCameraPackageComponent::Find(Camera.Get());
}

void FRoscCameraTarget::SetCamera(ACineCameraActor* NewCamera)
{
	Camera = NewCamera;
	if (NewCamera != nullptr && GEditor != nullptr && !NewCamera->IsSelected())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(NewCamera, true, true);
	}
}

void FRoscCameraTarget::OnSelectionChanged(UObject* SelectionObject)
{
	TargetSelectedCamera();
}

void FRoscCameraTarget::TargetSelectedCamera()
{
	if (GEditor == nullptr)
	{
		return;
	}
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		if (ACineCameraActor* SelectedCamera = Cast<ACineCameraActor>(*It))
		{
			Camera = SelectedCamera;
			return;
		}
	}
}
