#include "UEcineCameraTarget.h"

#include "CineCameraActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Selection.h"
#include "UEcineCameraPackageComponent.h"

FUEcineCameraTarget::FUEcineCameraTarget()
{
	SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &FUEcineCameraTarget::OnSelectionChanged);

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

FUEcineCameraTarget::~FUEcineCameraTarget()
{
	USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
}

ACineCameraActor* FUEcineCameraTarget::GetCamera() const
{
	return Camera.Get();
}

UUEcineCameraPackageComponent* FUEcineCameraTarget::GetPackageComponent() const
{
	return UUEcineCameraPackageComponent::Find(Camera.Get());
}

void FUEcineCameraTarget::SetCamera(ACineCameraActor* NewCamera)
{
	Camera = NewCamera;
	if (NewCamera != nullptr && GEditor != nullptr && !NewCamera->IsSelected())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(NewCamera, true, true);
	}
}

void FUEcineCameraTarget::OnSelectionChanged(UObject* SelectionObject)
{
	TargetSelectedCamera();
}

void FUEcineCameraTarget::TargetSelectedCamera()
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
