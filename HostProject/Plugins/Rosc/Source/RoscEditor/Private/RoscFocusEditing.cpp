// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscFocusEditing.h"

#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieSceneSequencePlayer.h"
#include "SceneDepthPickerMode.h"
#include "ScopedTransaction.h"
#include "SLevelViewport.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "RoscCameraPackageComponent.h"
#include "RoscFocus.h"
#include "RoscSequenceUtils.h"
#include "RoscWidgets.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

using namespace Rosc::SequenceUtils;

namespace
{
	const FName DepthPickerModuleName("SceneDepthPickerMode");

	/** The camera waiting for a click to focus on, and whether the click is to be held. */
	TWeakObjectPtr<ACineCameraActor> PickingCamera;
	bool bPickingToHold = false;

	/** Where a point kept on a subject is in the level. With no subject the offset is the place itself. */
	FVector GetPointInLevel(const AActor* Subject, const FVector& Offset)
	{
		return Subject != nullptr ? Subject->GetActorTransform().TransformPosition(Offset) : Offset;
	}

	/** The distance the Cine Camera would focus at for a point: along the lens axis, and zero behind the camera. */
	float MeasureToPoint(const UCineCameraComponent* Lens, const FVector& WorldPoint)
	{
		return static_cast<float>(FMath::Max(0.0, FVector::DotProduct(Lens->GetForwardVector(), WorldPoint - Lens->GetComponentLocation())));
	}

	/** The subject under a point picked in the viewport, found by looking along the view to it. */
	AActor* FindSubjectAt(const UWorld* World, const FVector& WorldPoint)
	{
		if (World == nullptr || GCurrentLevelEditingViewportClient == nullptr)
		{
			return nullptr;
		}
		const FVector Eye = GCurrentLevelEditingViewportClient->GetViewLocation();
		const FVector Along = (WorldPoint - Eye).GetSafeNormal();
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(RoscPickFocus), true);
		// A surface with no collision is not found: the point is then kept as a place in the level.
		const bool bHit = World->LineTraceSingleByChannel(Hit, Eye, WorldPoint + Along * 10.0, ECC_Visibility, Query);
		return bHit && FVector::Dist(Hit.ImpactPoint, WorldPoint) < 25.0 ? Hit.GetActor() : nullptr;
	}

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

	/** Sets a hand-set focus distance. The Cine Camera adds its focus offset to the manual distance, so it is taken off here. */
	void SetManualFocus(ACineCameraActor* Camera, float DistanceCm, const FText& Description)
	{
		const float Distance = FMath::Max(DistanceCm, Rosc::FocusEditing::GetCloseFocus(Camera));
		EditFocus(Camera, Description, [Distance](FCameraFocusSettings& Settings)
		{
			Settings.FocusMethod = ECameraFocusMethod::Manual;
			Settings.ManualFocusDistance = Distance - Settings.FocusOffset;
		});
	}

	/** Changes the focus marks kept with a camera's package as one undoable step. */
	void EditMarks(ACineCameraActor* Camera, const FText& Description, TFunctionRef<void(URoscCameraPackageComponent&)> Edit)
	{
		if (URoscCameraPackageComponent* Package = URoscCameraPackageComponent::Find(Camera))
		{
			const FScopedTransaction Transaction(Description);
			Package->Modify();
			Edit(*Package);
		}
	}

	bool HasFocusMark(const ACineCameraActor* Camera, int32 MarkIndex)
	{
		const URoscCameraPackageComponent* Package = URoscCameraPackageComponent::Find(Camera);
		return Package != nullptr && Package->FocusMarks.IsValidIndex(MarkIndex);
	}

	/** What the focus is on now, as a mark: the point or subject it is kept on, or else its distance. */
	FRoscFocusMark MakeMark(const ACineCameraActor* Camera)
	{
		FRoscFocusMark Mark;
		Mark.DistanceCm = Rosc::FocusEditing::GetLensDistance(Camera);
		const FCameraFocusSettings& Settings = Camera->GetCineCameraComponent()->FocusSettings;
		if (Settings.FocusMethod == ECameraFocusMethod::Tracking)
		{
			Mark.bPoint = true;
			Mark.Subject = Settings.TrackingFocusSettings.ActorToTrack;
			Mark.Offset = Settings.TrackingFocusSettings.RelativeOffset;
		}
		return Mark;
	}

	/** What a rack pulls to: a distance, or a point that is measured again on every frame. */
	struct FRackTarget
	{
		bool bPoint = false;
		float DistanceCm = 0.0f;
		TWeakObjectPtr<AActor> Subject;
		FVector Offset = FVector::ZeroVector;
	};

	bool BakeRackKeys(ACineCameraActor* Camera, const FRackTarget& Target, double RackSeconds, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError);
}

float Rosc::FocusEditing::GetLensDistance(const ACineCameraActor* Camera)
{
	return FMath::Max(GetFocusDistance(Camera), GetCloseFocus(Camera));
}

float Rosc::FocusEditing::GetFocusDistance(const ACineCameraActor* Camera)
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
		Distance = MeasureToPoint(Lens, GetPointInLevel(Settings.TrackingFocusSettings.ActorToTrack.Get(), Settings.TrackingFocusSettings.RelativeOffset));
	}
	return Distance + Settings.FocusOffset;
}

FVector Rosc::FocusEditing::GetFocusPoint(const ACineCameraActor* Camera)
{
	if (Camera == nullptr)
	{
		return FVector::ZeroVector;
	}
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	const FCameraFocusSettings& Settings = Lens->FocusSettings;
	return Settings.FocusMethod == ECameraFocusMethod::Tracking
		? GetPointInLevel(Settings.TrackingFocusSettings.ActorToTrack.Get(), Settings.TrackingFocusSettings.RelativeOffset)
		: Lens->GetComponentLocation() + Lens->GetForwardVector() * GetLensDistance(Camera);
}

float Rosc::FocusEditing::GetCloseFocus(const ACineCameraActor* Camera)
{
	// The lens keeps its close focus in millimetres.
	return Camera != nullptr ? Camera->GetCineCameraComponent()->LensSettings.MinimumFocusDistance * 0.1f : 0.0f;
}

void Rosc::FocusEditing::SetFocusDistance(ACineCameraActor* Camera, float DistanceCm)
{
	if (Camera != nullptr)
	{
		SetManualFocus(Camera, DistanceCm, LOCTEXT("SetFocus", "Set Focus"));
	}
}

bool Rosc::FocusEditing::FocusOnPoint(ACineCameraActor* Camera, const FVector& WorldPoint, FText& OutError)
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

bool Rosc::FocusEditing::HoldPoint(ACineCameraActor* Camera, AActor* Subject, const FVector& WorldPoint, FText& OutError)
{
	if (Camera == nullptr)
	{
		OutError = LOCTEXT("FocusNoCamera", "There is no camera to focus.");
		return false;
	}
	if (MeasureToPoint(Camera->GetCineCameraComponent(), WorldPoint) <= 0.0f)
	{
		OutError = LOCTEXT("FocusBehindCamera", "That point is behind the camera, so the lens cannot focus on it. Pick something the camera is facing.");
		return false;
	}
	if (Subject == Camera)
	{
		Subject = nullptr;
	}
	const FVector Offset = Subject != nullptr ? Subject->GetActorTransform().InverseTransformPosition(WorldPoint) : WorldPoint;
	EditFocus(Camera, LOCTEXT("HoldFocus", "Pick and Hold Focus"), [Subject, &Offset](FCameraFocusSettings& Settings)
	{
		Settings.FocusMethod = ECameraFocusMethod::Tracking;
		Settings.TrackingFocusSettings.ActorToTrack = Subject;
		Settings.TrackingFocusSettings.RelativeOffset = Offset;
	});
	return true;
}

void Rosc::FocusEditing::BeginPickFocus(ACineCameraActor* Camera, bool bHold)
{
	if (Camera == nullptr)
	{
		return;
	}
	PickingCamera = Camera;
	bPickingToHold = bHold;
	// The engine's picker takes a click from the current level viewport only, and calls picking off
	// at a click anywhere else or when that viewport loses focus. In a fresh editor the mode tools
	// name another level viewport (one of the layouts not shown) as the last in focus, and there may
	// be no current viewport: a viewport's first click calls focus off that other one, which the
	// picker takes as its own viewport losing focus, before the click reaches it. So the active
	// viewport is made current and given the focus now, as that first click would have done.
	if (const TSharedPtr<ILevelEditor> LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstLevelEditor())
	{
		if (const TSharedPtr<SLevelViewport> Viewport = LevelEditor->GetActiveViewportInterface())
		{
			FLevelEditorViewportClient& Client = Viewport->GetLevelViewportClient();
			Client.SetCurrentViewport();
			if (Client.GetModeTools()->GetFocusedViewportClient() != &Client)
			{
				Client.ReceivedFocus(Client.Viewport);
			}
		}
	}
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
		const bool bFocused = Location != FVector::ZeroVector
			&& (bPickingToHold ? HoldPoint(Picking, FindSubjectAt(Picking->GetWorld(), Location), Location, Error) : FocusOnPoint(Picking, Location, Error));
		if (!bFocused)
		{
			Rosc::Widgets::Notify(Error, false);
		}
	}));
}

void Rosc::FocusEditing::EndPickFocus()
{
	PickingCamera.Reset();
	if (FSceneDepthPickerModeModule* Picker = FModuleManager::GetModulePtr<FSceneDepthPickerModeModule>(DepthPickerModuleName))
	{
		Picker->EndSceneDepthPickingMode();
	}
}

bool Rosc::FocusEditing::IsPickingFocus(const ACineCameraActor* Camera, bool bHold)
{
	const FSceneDepthPickerModeModule* Picker = FModuleManager::GetModulePtr<FSceneDepthPickerModeModule>(DepthPickerModuleName);
	return Camera != nullptr && PickingCamera == Camera && bPickingToHold == bHold && Picker != nullptr && Picker->IsInSceneDepthPickingMode();
}

void Rosc::FocusEditing::FollowActor(ACineCameraActor* Camera, AActor* Subject)
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

void Rosc::FocusEditing::StopFollowing(ACineCameraActor* Camera)
{
	if (Camera != nullptr && Camera->GetCineCameraComponent()->FocusSettings.FocusMethod == ECameraFocusMethod::Tracking)
	{
		SetManualFocus(Camera, GetLensDistance(Camera), LOCTEXT("StopFollowFocus", "Stop Following Focus"));
	}
}

bool Rosc::FocusEditing::IsFollowing(const ACineCameraActor* Camera)
{
	return Camera != nullptr && Camera->GetCineCameraComponent()->FocusSettings.FocusMethod == ECameraFocusMethod::Tracking;
}

bool Rosc::FocusEditing::IsHoldingPoint(const ACineCameraActor* Camera)
{
	if (!IsFollowing(Camera))
	{
		return false;
	}
	const FCameraTrackingFocusSettings& Tracking = Camera->GetCineCameraComponent()->FocusSettings.TrackingFocusSettings;
	return Tracking.ActorToTrack.IsNull() || !Tracking.RelativeOffset.IsNearlyZero();
}

AActor* Rosc::FocusEditing::GetFollowedActor(const ACineCameraActor* Camera)
{
	if (Camera == nullptr)
	{
		return nullptr;
	}
	const FCameraFocusSettings& Settings = Camera->GetCineCameraComponent()->FocusSettings;
	return Settings.FocusMethod == ECameraFocusMethod::Tracking ? Settings.TrackingFocusSettings.ActorToTrack.Get() : nullptr;
}

int32 Rosc::FocusEditing::AddFocusMark(ACineCameraActor* Camera)
{
	int32 MarkIndex = INDEX_NONE;
	EditMarks(Camera, LOCTEXT("AddFocusMark", "Mark Focus"), [Camera, &MarkIndex](URoscCameraPackageComponent& Package)
	{
		MarkIndex = Package.FocusMarks.Add(MakeMark(Camera));
	});
	return MarkIndex;
}

void Rosc::FocusEditing::UpdateFocusMark(ACineCameraActor* Camera, int32 MarkIndex)
{
	if (HasFocusMark(Camera, MarkIndex))
	{
		EditMarks(Camera, LOCTEXT("UpdateFocusMark", "Move Focus Mark"), [Camera, MarkIndex](URoscCameraPackageComponent& Package)
		{
			Package.FocusMarks[MarkIndex] = MakeMark(Camera);
		});
	}
}

void Rosc::FocusEditing::RemoveFocusMark(ACineCameraActor* Camera, int32 MarkIndex)
{
	if (HasFocusMark(Camera, MarkIndex))
	{
		EditMarks(Camera, LOCTEXT("RemoveFocusMark", "Remove Focus Mark"), [MarkIndex](URoscCameraPackageComponent& Package)
		{
			Package.FocusMarks.RemoveAt(MarkIndex);
		});
	}
}

void Rosc::FocusEditing::GoToFocusMark(ACineCameraActor* Camera, int32 MarkIndex)
{
	if (!HasFocusMark(Camera, MarkIndex))
	{
		return;
	}
	const FRoscFocusMark Mark = URoscCameraPackageComponent::Find(Camera)->FocusMarks[MarkIndex];
	if (Mark.bPoint)
	{
		EditFocus(Camera, LOCTEXT("GoToFocusMark", "Go to Focus Mark"), [&Mark](FCameraFocusSettings& Settings)
		{
			Settings.FocusMethod = ECameraFocusMethod::Tracking;
			Settings.TrackingFocusSettings.ActorToTrack = Mark.Subject;
			Settings.TrackingFocusSettings.RelativeOffset = Mark.Offset;
		});
	}
	else
	{
		SetManualFocus(Camera, Mark.DistanceCm, LOCTEXT("GoToFocusMark", "Go to Focus Mark"));
	}
}

float Rosc::FocusEditing::GetFocusMarkDistance(const ACineCameraActor* Camera, int32 MarkIndex)
{
	if (!HasFocusMark(Camera, MarkIndex))
	{
		return 0.0f;
	}
	const FRoscFocusMark& Mark = URoscCameraPackageComponent::Find(Camera)->FocusMarks[MarkIndex];
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	return Mark.bPoint
		? FMath::Max(MeasureToPoint(Lens, GetPointInLevel(Mark.Subject.Get(), Mark.Offset)) + Lens->FocusSettings.FocusOffset, GetCloseFocus(Camera))
		: Mark.DistanceCm;
}

void Rosc::FocusEditing::SetRackTime(ACineCameraActor* Camera, float Seconds)
{
	EditMarks(Camera, LOCTEXT("SetRackTime", "Change Rack Time"), [Seconds](URoscCameraPackageComponent& Package)
	{
		Package.RackSeconds = FMath::Clamp(Seconds, 0.1f, 60.0f);
	});
}

bool Rosc::FocusEditing::BakeRack(ACineCameraActor* Camera, float ToDistanceCm, double RackSeconds, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError)
{
	FRackTarget Target;
	Target.DistanceCm = ToDistanceCm;
	return BakeRackKeys(Camera, Target, RackSeconds, Sequence, StartFrame, OutError);
}

bool Rosc::FocusEditing::BakeRackToPoint(ACineCameraActor* Camera, AActor* Subject, const FVector& Offset, double RackSeconds, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError)
{
	FRackTarget Target;
	Target.bPoint = true;
	Target.Subject = Subject;
	Target.Offset = Offset;
	return BakeRackKeys(Camera, Target, RackSeconds, Sequence, StartFrame, OutError);
}

namespace
{
bool BakeRackKeys(ACineCameraActor* Camera, const FRackTarget& Target, double RackSeconds, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError)
{
	using namespace Rosc::FocusEditing;
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
	const FRoscMoveTiming Timing = Rosc::Focus::MakeRackTiming(RackSeconds);
	const FFrameNumber EndFrame = StartFrame + FMath::CeilToInt32(Timing.DurationSeconds * DisplayRate.AsDecimal() - 1.e-6);

	// A point is kept sharp after the rack as well, to the last frame the sequence plays.
	FFrameNumber LastFrame = EndFrame;
	if (Target.bPoint && MovieScene->GetPlaybackRange().HasUpperBound())
	{
		const FFrameNumber LastPlayed = ConvertFrameTime(FFrameTime(UE::MovieScene::DiscreteExclusiveUpper(MovieScene->GetPlaybackRange()) - 1), TickResolution, DisplayRate).FloorToFrame();
		LastFrame = FMath::Max(EndFrame, LastPlayed);
	}
	const TRange<FFrameNumber> RackRange = TRange<FFrameNumber>::Inclusive(ToTick(StartFrame), ToTick(LastFrame));

	// Sequencer is told of the rack after the undo step closes: told inside it, Sequencer keeps the step open.
	TOptional<FScopedTransaction> Transaction(InPlace, LOCTEXT("BakeRack", "Rack Focus"));
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

	// Where the rack is pulling to on each frame. A point is measured from where the camera and
	// the subject are on that frame, read by putting Sequencer's playhead there.
	TArray<float> ToDistances;
	ToDistances.Init(FMath::Max(Target.DistanceCm, CloseFocus), LastFrame.Value - StartFrame.Value + 1);
	if (Target.bPoint)
	{
		const bool bReadFrames = ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence() == Sequence;
		const FMovieSceneSequencePlaybackParams PlayheadBefore = ULevelSequenceEditorBlueprintLibrary::GetLocalPosition(EMovieSceneTimeUnit::TickResolution);
		for (int32 Index = 0; Index < ToDistances.Num(); ++Index)
		{
			if (bReadFrames)
			{
				ULevelSequenceEditorBlueprintLibrary::SetLocalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(ToTick(StartFrame + Index)), EUpdatePositionMethod::Scrub), EMovieSceneTimeUnit::TickResolution);
			}
			ToDistances[Index] = FMath::Max(MeasureToPoint(Lens, GetPointInLevel(Target.Subject.Get(), Target.Offset)) + FocusOffset, CloseFocus);
		}
		if (bReadFrames)
		{
			ULevelSequenceEditorBlueprintLibrary::SetLocalPosition(FMovieSceneSequencePlaybackParams(PlayheadBefore.Frame, EUpdatePositionMethod::Scrub), EMovieSceneTimeUnit::TickResolution);
		}
	}

	// Keys drive the hand-set distance, so the camera has to be focused by hand.
	if (Lens->FocusSettings.FocusMethod != ECameraFocusMethod::Manual)
	{
		SetManualFocus(Camera, FromDistance, LOCTEXT("RackSetsFocusByHand", "Set Focus"));
	}

	DeleteKeysInRange(Channel, RackRange);
	for (FFrameNumber Frame = StartFrame; Frame <= LastFrame; ++Frame)
	{
		const float ToDistance = ToDistances[Frame.Value - StartFrame.Value];
		const double Distance = Frame <= EndFrame
			? Rosc::Focus::RackDistance(FromDistance, ToDistance, Timing, DisplayRate.AsSeconds(FFrameTime(Frame - StartFrame)))
			: ToDistance;
		Channel.AddLinearKey(ToTick(Frame), static_cast<float>(Distance) - FocusOffset);
	}
	Section->MarkAsChanged();

	IncludeInPlaybackRange(MovieScene, RackRange);
	Transaction.Reset();
	ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	return true;
}
}

ULevelSequence* Rosc::FocusEditing::RackToFocusMark(ACineCameraActor* Camera, int32 MarkIndex, FText& OutError)
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
	const URoscCameraPackageComponent* Package = URoscCameraPackageComponent::Find(Camera);
	const FRoscFocusMark Mark = Package->FocusMarks[MarkIndex];
	const bool bBaked = Mark.bPoint
		? BakeRackToPoint(Camera, Mark.Subject.Get(), Mark.Offset, Package->RackSeconds, Target.Sequence, Target.StartFrame, OutError)
		: BakeRack(Camera, Mark.DistanceCm, Package->RackSeconds, Target.Sequence, Target.StartFrame, OutError);
	return bBaked ? Target.Sequence : nullptr;
}

#undef LOCTEXT_NAMESPACE
