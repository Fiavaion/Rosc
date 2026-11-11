#include "UEcineFocusEditing.h"

#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "SceneDepthPickerMode.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineFocus.h"
#include "UEcineSequenceUtils.h"
#include "UEcineWidgets.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

using namespace UEcine::SequenceUtils;

namespace
{
	const FName DepthPickerModuleName("SceneDepthPickerMode");

	/** The camera waiting for a click to focus on. */
	TWeakObjectPtr<ACineCameraActor> PickingCamera;

	/** Changes a camera's focus settings as one undoable step and redraws the viewports. */
	void EditFocus(ACineCameraActor* Camera, const FText& Description, TFunctionRef<void(FCameraFocusSettings&)> Edit)
	{
		UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
		const FScopedTransaction Transaction(Description);
		Lens->Modify();
		FCameraFocusSettings Settings = Lens->FocusSettings;
		Edit(Settings);
		Lens->SetFocusSettings(Settings);
		GEditor->RedrawLevelEditingViewports();
	}

	/** Where the lens itself is focused: where the focus is set, but never nearer than the lens can go. */
	float GetLensDistance(const ACineCameraActor* Camera)
	{
		return FMath::Max(UEcine::FocusEditing::GetFocusDistance(Camera), UEcine::FocusEditing::GetCloseFocus(Camera));
	}

	/** Sets a hand-set focus distance. The Cine Camera adds its focus offset to the manual distance, so it is taken off here. */
	void SetManualFocus(ACineCameraActor* Camera, float DistanceCm, const FText& Description)
	{
		const float Distance = FMath::Max(DistanceCm, UEcine::FocusEditing::GetCloseFocus(Camera));
		EditFocus(Camera, Description, [Distance](FCameraFocusSettings& Settings)
		{
			Settings.FocusMethod = ECameraFocusMethod::Manual;
			Settings.ManualFocusDistance = Distance - Settings.FocusOffset;
		});
	}

	/** Changes the focus marks kept with a camera's package as one undoable step. */
	void EditMarks(ACineCameraActor* Camera, const FText& Description, TFunctionRef<void(UUEcineCameraPackageComponent&)> Edit)
	{
		if (UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(Camera))
		{
			const FScopedTransaction Transaction(Description);
			Package->Modify();
			Edit(*Package);
		}
	}

	bool HasFocusMark(const ACineCameraActor* Camera, int32 MarkIndex)
	{
		const UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(Camera);
		return Package != nullptr && Package->FocusMarksCm.IsValidIndex(MarkIndex);
	}
}

float UEcine::FocusEditing::GetFocusDistance(const ACineCameraActor* Camera)
{
	if (Camera == nullptr)
	{
		return 0.0f;
	}
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	const FCameraFocusSettings& Settings = Lens->FocusSettings;

	// The same measure the Cine Camera uses: along the lens axis, and zero for a subject behind the camera.
	float Distance = Settings.ManualFocusDistance;
	if (Settings.FocusMethod == ECameraFocusMethod::Tracking)
	{
		const AActor* Subject = Settings.TrackingFocusSettings.ActorToTrack.Get();
		const FVector Offset = Settings.TrackingFocusSettings.RelativeOffset;
		const FVector FocusPoint = Subject != nullptr ? Subject->GetActorTransform().TransformPosition(Offset) : Offset;
		Distance = static_cast<float>(FMath::Max(0.0, FVector::DotProduct(Lens->GetForwardVector(), FocusPoint - Lens->GetComponentLocation())));
	}
	return Distance + Settings.FocusOffset;
}

float UEcine::FocusEditing::GetCloseFocus(const ACineCameraActor* Camera)
{
	// The lens keeps its close focus in millimetres.
	return Camera != nullptr ? Camera->GetCineCameraComponent()->LensSettings.MinimumFocusDistance * 0.1f : 0.0f;
}

void UEcine::FocusEditing::SetFocusDistance(ACineCameraActor* Camera, float DistanceCm)
{
	if (Camera != nullptr)
	{
		SetManualFocus(Camera, DistanceCm, LOCTEXT("SetFocus", "Set Focus"));
	}
}

bool UEcine::FocusEditing::FocusOnPoint(ACineCameraActor* Camera, const FVector& WorldPoint, FText& OutError)
{
	if (Camera == nullptr)
	{
		OutError = LOCTEXT("FocusNoCamera", "There is no camera to focus.");
		return false;
	}
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	const double Distance = FVector::DotProduct(Lens->GetForwardVector(), WorldPoint - Lens->GetComponentLocation());
	if (Distance <= 0.0)
	{
		OutError = LOCTEXT("FocusBehindCamera", "That point is behind the camera, so the lens cannot focus on it. Pick something the camera is facing.");
		return false;
	}
	SetManualFocus(Camera, static_cast<float>(Distance), LOCTEXT("PickFocus", "Pick Focus"));
	return true;
}

void UEcine::FocusEditing::BeginPickFocus(ACineCameraActor* Camera)
{
	if (Camera == nullptr)
	{
		return;
	}
	PickingCamera = Camera;
	FSceneDepthPickerModeModule& Picker = FModuleManager::LoadModuleChecked<FSceneDepthPickerModeModule>(DepthPickerModuleName);
	Picker.BeginSceneDepthPickingMode(FOnSceneDepthLocationSelected::CreateLambda([](FVector Location)
	{
		ACineCameraActor* Picking = PickingCamera.Get();
		PickingCamera.Reset();
		if (Picking == nullptr)
		{
			return;
		}
		// A click on empty sky comes back as the origin.
		FText Error = LOCTEXT("PickedNothing", "There is nothing there to focus on. Pick a surface in the level.");
		if (Location == FVector::ZeroVector || !FocusOnPoint(Picking, Location, Error))
		{
			UEcine::Widgets::Notify(Error, false);
		}
	}));
}

void UEcine::FocusEditing::EndPickFocus()
{
	PickingCamera.Reset();
	if (FSceneDepthPickerModeModule* Picker = FModuleManager::GetModulePtr<FSceneDepthPickerModeModule>(DepthPickerModuleName))
	{
		Picker->EndSceneDepthPickingMode();
	}
}

bool UEcine::FocusEditing::IsPickingFocus(const ACineCameraActor* Camera)
{
	const FSceneDepthPickerModeModule* Picker = FModuleManager::GetModulePtr<FSceneDepthPickerModeModule>(DepthPickerModuleName);
	return Camera != nullptr && PickingCamera == Camera && Picker != nullptr && Picker->IsInSceneDepthPickingMode();
}

void UEcine::FocusEditing::FollowActor(ACineCameraActor* Camera, AActor* Subject)
{
	if (Camera == nullptr || Subject == nullptr || Subject == Camera)
	{
		return;
	}
	EditFocus(Camera, LOCTEXT("FollowFocus", "Follow Focus"), [Subject](FCameraFocusSettings& Settings)
	{
		Settings.FocusMethod = ECameraFocusMethod::Tracking;
		Settings.TrackingFocusSettings.ActorToTrack = Subject;
		Settings.TrackingFocusSettings.RelativeOffset = FVector::ZeroVector;
	});
}

void UEcine::FocusEditing::StopFollowing(ACineCameraActor* Camera)
{
	if (Camera != nullptr && Camera->GetCineCameraComponent()->FocusSettings.FocusMethod == ECameraFocusMethod::Tracking)
	{
		SetManualFocus(Camera, GetLensDistance(Camera), LOCTEXT("StopFollowFocus", "Stop Following Focus"));
	}
}

AActor* UEcine::FocusEditing::GetFollowedActor(const ACineCameraActor* Camera)
{
	if (Camera == nullptr)
	{
		return nullptr;
	}
	const FCameraFocusSettings& Settings = Camera->GetCineCameraComponent()->FocusSettings;
	return Settings.FocusMethod == ECameraFocusMethod::Tracking ? Settings.TrackingFocusSettings.ActorToTrack.Get() : nullptr;
}

int32 UEcine::FocusEditing::AddFocusMark(ACineCameraActor* Camera)
{
	int32 MarkIndex = INDEX_NONE;
	EditMarks(Camera, LOCTEXT("AddFocusMark", "Mark Focus"), [Camera, &MarkIndex](UUEcineCameraPackageComponent& Package)
	{
		MarkIndex = Package.FocusMarksCm.Add(GetLensDistance(Camera));
	});
	return MarkIndex;
}

void UEcine::FocusEditing::UpdateFocusMark(ACineCameraActor* Camera, int32 MarkIndex)
{
	if (HasFocusMark(Camera, MarkIndex))
	{
		EditMarks(Camera, LOCTEXT("UpdateFocusMark", "Move Focus Mark"), [Camera, MarkIndex](UUEcineCameraPackageComponent& Package)
		{
			Package.FocusMarksCm[MarkIndex] = GetLensDistance(Camera);
		});
	}
}

void UEcine::FocusEditing::RemoveFocusMark(ACineCameraActor* Camera, int32 MarkIndex)
{
	if (HasFocusMark(Camera, MarkIndex))
	{
		EditMarks(Camera, LOCTEXT("RemoveFocusMark", "Remove Focus Mark"), [MarkIndex](UUEcineCameraPackageComponent& Package)
		{
			Package.FocusMarksCm.RemoveAt(MarkIndex);
		});
	}
}

void UEcine::FocusEditing::GoToFocusMark(ACineCameraActor* Camera, int32 MarkIndex)
{
	if (HasFocusMark(Camera, MarkIndex))
	{
		SetManualFocus(Camera, UUEcineCameraPackageComponent::Find(Camera)->FocusMarksCm[MarkIndex], LOCTEXT("GoToFocusMark", "Go to Focus Mark"));
	}
}

void UEcine::FocusEditing::SetRackTime(ACineCameraActor* Camera, float Seconds)
{
	EditMarks(Camera, LOCTEXT("SetRackTime", "Change Rack Time"), [Seconds](UUEcineCameraPackageComponent& Package)
	{
		Package.RackSeconds = FMath::Clamp(Seconds, 0.1f, 60.0f);
	});
}

bool UEcine::FocusEditing::BakeRack(ACineCameraActor* Camera, float ToDistanceCm, double RackSeconds, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError)
{
	if (Camera == nullptr)
	{
		OutError = LOCTEXT("RackNoCamera", "There is no camera to rack focus on.");
		return false;
	}
	if (Sequence == nullptr || Sequence->GetMovieScene() == nullptr)
	{
		OutError = LOCTEXT("RackNoSequence", "There is no sequence to bake the rack into.");
		return false;
	}

	UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const auto ToTick = [&DisplayRate, &TickResolution](FFrameNumber Frame)
	{
		return ConvertFrameTime(FFrameTime(Frame), DisplayRate, TickResolution).RoundToFrame();
	};

	// A key on every frame from the one the rack starts on to the first one at or after its end.
	const FUEcineMoveTiming Timing = UEcine::Focus::MakeRackTiming(RackSeconds);
	const FFrameNumber EndFrame = StartFrame + FMath::CeilToInt32(Timing.DurationSeconds * DisplayRate.AsDecimal() - 1.e-6);
	const TRange<FFrameNumber> RackRange = TRange<FFrameNumber>::Inclusive(ToTick(StartFrame), ToTick(EndFrame));

	const FScopedTransaction Transaction(LOCTEXT("BakeRack", "Rack Focus"));
	Sequence->Modify();
	MovieScene->Modify();

	const FGuid LensBinding = FindOrAddComponentBinding(Sequence, Lens);
	UMovieSceneFloatTrack* Track = FindOrAddFloatTrack(MovieScene, LensBinding,
		GET_MEMBER_NAME_CHECKED(FCameraFocusSettings, ManualFocusDistance), TEXT("FocusSettings.ManualFocusDistance"), LOCTEXT("FocusTrackName", "Focus Distance"));
	Track->Modify();
	UMovieSceneFloatSection* Section = FindOrAddSection<UMovieSceneFloatSection>(Track);
	Section->Modify();
	FMovieSceneFloatChannel& Channel = Section->GetChannel();

	// The rack leaves from what the sequence already has the focus doing on that frame, or, with
	// no focus keyed yet, from where the lens is now.
	const float CloseFocus = GetCloseFocus(Camera);
	const float FocusOffset = Lens->FocusSettings.FocusOffset;
	float KeyedDistance = 0.0f;
	const float FromDistance = Channel.Evaluate(FFrameTime(RackRange.GetLowerBoundValue()), KeyedDistance)
		? FMath::Max(KeyedDistance + FocusOffset, CloseFocus)
		: GetLensDistance(Camera);
	const float ToDistance = FMath::Max(ToDistanceCm, CloseFocus);

	// Keys drive the hand-set distance, so the camera has to be focused by hand.
	if (Lens->FocusSettings.FocusMethod != ECameraFocusMethod::Manual)
	{
		SetManualFocus(Camera, FromDistance, LOCTEXT("RackSetsFocusByHand", "Set Focus"));
	}

	DeleteKeysInRange(Channel, RackRange);
	for (FFrameNumber Frame = StartFrame; Frame <= EndFrame; ++Frame)
	{
		const double Distance = UEcine::Focus::RackDistance(FromDistance, ToDistance, Timing, DisplayRate.AsSeconds(FFrameTime(Frame - StartFrame)));
		Channel.AddLinearKey(ToTick(Frame), static_cast<float>(Distance) - FocusOffset);
	}
	Section->MarkAsChanged();

	IncludeInPlaybackRange(MovieScene, RackRange);
	ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	return true;
}

ULevelSequence* UEcine::FocusEditing::RackToFocusMark(ACineCameraActor* Camera, int32 MarkIndex, FText& OutError)
{
	// Checked first so that a refused rack does not leave a new, empty sequence behind.
	if (!HasFocusMark(Camera, MarkIndex))
	{
		OutError = LOCTEXT("RackNoMark", "There is no such focus mark to rack to.");
		return nullptr;
	}

	const FBakeTarget Target = FindOrCreateBakeTarget(Camera, TEXT("_Focus"), Camera);
	if (Target.Sequence == nullptr)
	{
		OutError = LOCTEXT("RackNoNewSequence", "A new sequence could not be made for the rack.");
		return nullptr;
	}
	const UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(Camera);
	return BakeRack(Camera, Package->FocusMarksCm[MarkIndex], Package->RackSeconds, Target.Sequence, Target.StartFrame, OutError) ? Target.Sequence : nullptr;
}

#undef LOCTEXT_NAMESPACE
