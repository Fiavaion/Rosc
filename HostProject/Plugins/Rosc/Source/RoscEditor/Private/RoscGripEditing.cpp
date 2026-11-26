// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscGripEditing.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieScene.h"
#include "ScopedTransaction.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "RoscCameraPackageComponent.h"
#include "RoscFocusEditing.h"
#include "RoscMove.h"
#include "RoscOperator.h"
#include "RoscOptics.h"
#include "RoscRig.h"
#include "RoscSequenceUtils.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

using namespace Rosc::SequenceUtils;

namespace
{
	/** Where to stand a rig, and how to set it, so its mount is where a camera already is. */
	struct FRigSeat
	{
		FTransform RigTransform;
		FRoscRigPose Pose;
	};

	/**
	 * The dolly zooms of a rig's move, worked out one after another: each starts on the focal
	 * length the one before left the lens at.
	 */
	struct FDollyZoomSolver
	{
		const ARoscRig& Rig;
		/** What the camera is focused on: the thing that keeps its size. */
		FVector Subject = FVector::ZeroVector;
		/** The focal length the lens is on as the next move starts. */
		double FocalLengthMm = 0.0;
		double LensWidestMm = 0.0;
		double LensLongestMm = 0.0;

		FDollyZoomSolver(const ARoscRig& InRig, const ACineCameraActor& Camera)
			: Rig(InRig)
		{
			const UCineCameraComponent* Lens = Camera.GetCineCameraComponent();
			const URoscCameraPackageComponent* Package = URoscCameraPackageComponent::Find(&Camera);
			// The package's focal length, not the lens's: Sequencer may be holding the lens somewhere in an earlier bake.
			FocalLengthMm = Package != nullptr ? Package->Package.FocalLengthMm : Lens->CurrentFocalLength;
			LensWidestMm = Lens->LensSettings.MinFocalLength;
			LensLongestMm = Lens->LensSettings.MaxFocalLength;
			Subject = Rosc::FocusEditing::GetFocusPoint(&Camera);
		}

		/** How far the camera is from the subject with the rig in a pose. */
		double DistanceAt(const FRoscRigPose& Pose) const
		{
			return FVector::Dist((Rig.SolveMount(Pose) * Rig.GetActorTransform()).GetLocation(), Subject);
		}

		/** What the move into a mark asks of the lens at a time from the move's start. */
		double FocalLengthAt(int32 MarkIndex, double TimeSeconds) const
		{
			const FRoscRigMark& From = Rig.Marks[MarkIndex - 1];
			const FRoscRigMark& To = Rig.Marks[MarkIndex];
			const double StartDistance = DistanceAt(From.Pose);
			return StartDistance > UE_KINDA_SMALL_NUMBER
				? Rosc::Optics::DollyZoomFocalLengthMm(FocalLengthMm, StartDistance, DistanceAt(Rosc::Move::Evaluate(From.Pose, To.Pose, To.Arrival, TimeSeconds)))
				: FocalLengthMm;
		}

		double ClampToLens(double Asked) const
		{
			return FMath::Clamp(Asked, LensWidestMm, LensLongestMm);
		}

		/** Moves on past the move into a mark: the lens is left where that move put it. */
		void FinishMove(int32 MarkIndex)
		{
			if (Rig.Marks[MarkIndex].bDollyZoom)
			{
				FocalLengthMm = ClampToLens(FocalLengthAt(MarkIndex, Rig.Marks[MarkIndex].Arrival.Normalized().DurationSeconds));
			}
		}
	};

	/** Rigs whose lift axis is the camera's height straight above the floor. */
	bool StandsUnderCamera(ERoscRigType Type)
	{
		return Type != ERoscRigType::Jib && Type != ERoscRigType::Crane;
	}

	FRigSeat SeatRigUnderCamera(const ACineCameraActor* Camera, const FRoscRigPreset& Preset)
	{
		const FRoscRigSpec& Spec = Preset.Spec;
		const FVector CameraLocation = Camera->GetActorLocation();
		const FRotator CameraRotation = Camera->GetActorRotation();

		FRigSeat Seat;
		Seat.Pose = Preset.DefaultPose;

		// Keep the camera's tilt and roll on the head, as far as the head allows.
		const FRoscAxisLimits& TiltLimits = Spec.GetAxis(ERoscRigAxis::Tilt);
		if (TiltLimits.bUsed)
		{
			Seat.Pose.Tilt = FMath::Clamp(CameraRotation.Pitch, TiltLimits.Min, TiltLimits.Max);
		}
		const FRoscAxisLimits& RollLimits = Spec.GetAxis(ERoscRigAxis::Roll);
		if (RollLimits.bUsed)
		{
			Seat.Pose.Roll = FMath::Clamp(CameraRotation.Roll, RollLimits.Min, RollLimits.Max);
		}

		// Stand the rig on the floor under the camera when there is a floor within its reach.
		const FRoscAxisLimits& LiftLimits = Spec.GetAxis(ERoscRigAxis::Lift);
		if (StandsUnderCamera(Spec.Type) && LiftLimits.bUsed)
		{
			FHitResult Floor;
			const FVector Below = CameraLocation - FVector(0.0, 0.0, LiftLimits.Max + 100.0);
			const FCollisionQueryParams Query(SCENE_QUERY_STAT(RoscFindFloor), true, Camera);
			if (Camera->GetWorld()->LineTraceSingleByChannel(Floor, CameraLocation, Below, ECC_Visibility, Query))
			{
				Seat.Pose.Lift = FMath::Clamp(CameraLocation.Z - Floor.ImpactPoint.Z, LiftLimits.Min, LiftLimits.Max);
			}
		}

		// The rig faces the way the camera looks; the head starts with no pan.
		const FTransform MountOnRig = Rosc::Rig::SolveMount(Spec.Type, Seat.Pose, Rosc::Rig::StraightTrackPoint(Seat.Pose.Travel));
		const FQuat RigRotation = FRotator(0.0, CameraRotation.Yaw, 0.0).Quaternion();
		Seat.RigTransform = FTransform(RigRotation, CameraLocation - RigRotation.RotateVector(MountOnRig.GetLocation()));
		return Seat;
	}

	FString MakeRigLabel(const ACineCameraActor* Camera, ERoscRigType Type)
	{
		return FString::Printf(TEXT("%s %s"), *Camera->GetActorLabel(), *Rosc::Rig::GetTypeName(Type).ToString());
	}

	bool HasMoveToBake(const ARoscRig* Rig, FText& OutError)
	{
		if (Rig == nullptr)
		{
			OutError = LOCTEXT("BakeNoRig", "There is no rig to bake a move from.");
			return false;
		}
		if (Rig->Marks.Num() < 2)
		{
			OutError = LOCTEXT("BakeNeedsMarks", "Mark at least two positions before baking a move.");
			return false;
		}
		return true;
	}
}

ARoscRig* Rosc::GripEditing::FindRig(const ACineCameraActor* Camera)
{
	if (Camera == nullptr)
	{
		return nullptr;
	}
	ARoscRig* Rig = Cast<ARoscRig>(Camera->GetAttachParentActor());
	return Rig != nullptr && Rig->GetMountedCamera() == Camera ? Rig : nullptr;
}

FRoscRigPose Rosc::GripEditing::GetPoseOnRig(const ACineCameraActor* Camera, const FRoscRigPreset& Preset)
{
	return Camera != nullptr ? SeatRigUnderCamera(Camera, Preset).Pose : Preset.DefaultPose;
}

ARoscRig* Rosc::GripEditing::PutCameraOnRig(ACineCameraActor* Camera, const FRoscRigPreset& Preset)
{
	if (Camera == nullptr)
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("PutCameraOnRig", "Put Camera on {0}"), Preset.Name));
	const FRigSeat Seat = SeatRigUnderCamera(Camera, Preset);

	ARoscRig* Rig = FindRig(Camera);
	if (Rig == nullptr)
	{
		Rig = Cast<ARoscRig>(GEditor->AddActor(Camera->GetLevel(), ARoscRig::StaticClass(), Seat.RigTransform, true, RF_Transactional, false));
		if (Rig == nullptr)
		{
			return nullptr;
		}
		Rig->SetSpec(Preset.Spec, Seat.Pose);
		Rig->Operator.Style = Rosc::Operator::GetDefaultStyle(Preset.Spec.Type);
		Camera->Modify();
		Rig->MountCamera(Camera);
		// The support holds the camera: looking through it and flying the viewport, or dragging
		// it, would take it off its mount and leave the support's axes describing nothing.
		Camera->SetLockLocation(true);
	}
	else
	{
		Rig->Modify();
		const bool bTypeChanged = Rig->GetSpec().Type != Preset.Spec.Type;
		Rig->SetActorTransform(Seat.RigTransform);
		Rig->SetSpec(Preset.Spec, Seat.Pose);
		if (bTypeChanged)
		{
			// Marks are positions of the old rig's axes and mean nothing on a different kind of rig.
			Rig->Marks.Reset();
			Rig->Operator.Style = Rosc::Operator::GetDefaultStyle(Preset.Spec.Type);
		}
	}
	Rig->SetActorLabel(MakeRigLabel(Camera, Preset.Spec.Type));

	GEditor->RedrawLevelEditingViewports();
	return Rig;
}

void Rosc::GripEditing::TakeCameraOffRig(ACineCameraActor* Camera)
{
	ARoscRig* Rig = FindRig(Camera);
	if (Rig == nullptr)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("TakeCameraOffRig", "Take Camera off Support"));
	Camera->Modify();
	Camera->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Camera->SetLockLocation(false);
	GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->DestroyActor(Rig);
	GEditor->RedrawLevelEditingViewports();
}

void Rosc::GripEditing::EditRig(ARoscRig* Rig, const FText& Description, TFunctionRef<void(ARoscRig&)> Edit)
{
	if (Rig == nullptr)
	{
		return;
	}
	const FScopedTransaction Transaction(Description);
	Rig->Modify();
	Edit(*Rig);
	GEditor->RedrawLevelEditingViewports();
}

int32 Rosc::GripEditing::AddMark(ARoscRig* Rig)
{
	if (Rig == nullptr)
	{
		return INDEX_NONE;
	}
	EditRig(Rig, LOCTEXT("AddMark", "Mark Position"), [](ARoscRig& EditedRig)
	{
		EditedRig.Marks.AddDefaulted_GetRef().Pose = EditedRig.GetPose();
	});
	return Rig->Marks.Num() - 1;
}

void Rosc::GripEditing::UpdateMark(ARoscRig* Rig, int32 MarkIndex)
{
	if (Rig != nullptr && Rig->Marks.IsValidIndex(MarkIndex))
	{
		EditRig(Rig, LOCTEXT("UpdateMark", "Move Mark Here"), [MarkIndex](ARoscRig& EditedRig) { EditedRig.Marks[MarkIndex].Pose = EditedRig.GetPose(); });
	}
}

void Rosc::GripEditing::RemoveMark(ARoscRig* Rig, int32 MarkIndex)
{
	if (Rig != nullptr && Rig->Marks.IsValidIndex(MarkIndex))
	{
		EditRig(Rig, LOCTEXT("RemoveMark", "Remove Mark"), [MarkIndex](ARoscRig& EditedRig) { EditedRig.Marks.RemoveAt(MarkIndex); });
	}
}

void Rosc::GripEditing::GoToMark(ARoscRig* Rig, int32 MarkIndex)
{
	if (Rig != nullptr && Rig->Marks.IsValidIndex(MarkIndex))
	{
		EditRig(Rig, LOCTEXT("GoToMark", "Go to Mark"), [MarkIndex](ARoscRig& EditedRig) { EditedRig.SetPose(EditedRig.Marks[MarkIndex].Pose); });
	}
}

bool Rosc::GripEditing::CanDollyZoom(const ARoscRig* Rig)
{
	if (Rig == nullptr)
	{
		return false;
	}
	const ERoscRigType Type = Rig->GetSpec().Type;
	return Type == ERoscRigType::Dolly || Type == ERoscRigType::Slider;
}

bool Rosc::GripEditing::PlanDollyZoom(const ARoscRig* Rig, int32 MarkIndex, FDollyZoom& OutPlan)
{
	const ACineCameraActor* Camera = Rig != nullptr ? Rig->GetMountedCamera() : nullptr;
	if (Camera == nullptr || MarkIndex < 1 || !Rig->Marks.IsValidIndex(MarkIndex) || !Rig->Marks[MarkIndex].bDollyZoom || !CanDollyZoom(Rig))
	{
		return false;
	}

	FDollyZoomSolver Solver(*Rig, *Camera);
	for (int32 Earlier = 1; Earlier < MarkIndex; ++Earlier)
	{
		Solver.FinishMove(Earlier);
	}
	const double DurationSeconds = Rig->Marks[MarkIndex].Arrival.Normalized().DurationSeconds;
	OutPlan.StartFocalLengthMm = Solver.FocalLengthMm;
	OutPlan.EndFocalLengthMm = Solver.FocalLengthAt(MarkIndex, DurationSeconds);
	OutPlan.WidestMm = OutPlan.LongestMm = OutPlan.StartFocalLengthMm;
	const int32 Steps = 32;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const double Asked = Solver.FocalLengthAt(MarkIndex, DurationSeconds * Step / Steps);
		OutPlan.WidestMm = FMath::Min(OutPlan.WidestMm, Asked);
		OutPlan.LongestMm = FMath::Max(OutPlan.LongestMm, Asked);
	}
	OutPlan.LensWidestMm = Solver.LensWidestMm;
	OutPlan.LensLongestMm = Solver.LensLongestMm;
	OutPlan.bFocusHeld = Rosc::FocusEditing::IsFollowing(Camera);
	return true;
}

bool Rosc::GripEditing::BakeMove(ARoscRig* Rig, ULevelSequence* Sequence, FFrameNumber StartFrame, FText& OutError)
{
	if (!HasMoveToBake(Rig, OutError))
	{
		return false;
	}
	if (Sequence == nullptr || Sequence->GetMovieScene() == nullptr)
	{
		OutError = LOCTEXT("BakeNoSequence", "There is no sequence to bake the move into.");
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const double TicksPerSecond = TickResolution.AsDecimal();
	const double DurationSeconds = Rosc::Move::GetTotalDuration(Rig->Marks);

	const FFrameNumber StartTick = ConvertFrameTime(FFrameTime(StartFrame), DisplayRate, TickResolution).RoundToFrame();
	const FFrameNumber EndTick = StartTick + FMath::RoundToInt32(DurationSeconds * TicksPerSecond);
	const TRange<FFrameNumber> MoveRange = TRange<FFrameNumber>::Inclusive(StartTick, EndTick);

	const FScopedTransaction Transaction(LOCTEXT("BakeMove", "Bake Move to Sequencer"));
	Sequence->Modify();
	MovieScene->Modify();

	// The designed move: a few keys per axis, with the slopes that draw the eases exactly.
	const FRoscRigSpec Spec = Rig->GetSpec();
	const FGuid RigBinding = FindOrAddBinding(Sequence, Rig);
	for (int32 AxisIndex = 0; AxisIndex < RoscRigAxisCount; ++AxisIndex)
	{
		const ERoscRigAxis Axis = static_cast<ERoscRigAxis>(AxisIndex);
		if (!Spec.Axes[AxisIndex].bUsed)
		{
			continue;
		}

		UMovieSceneFloatTrack* Track = FindOrAddFloatTrack(MovieScene, RigBinding, ARoscRig::GetAxisPropertyName(Axis), ARoscRig::GetAxisPropertyName(Axis).ToString(), Rosc::Rig::GetAxisName(Spec.Type, Axis));
		Track->Modify();
		UMovieSceneFloatSection* Section = FindOrAddSection<UMovieSceneFloatSection>(Track);
		Section->Modify();

		FMovieSceneFloatChannel& Channel = Section->GetChannel();
		DeleteKeysInRange(Channel, MoveRange);
		for (const FRoscMoveKey& Key : Rosc::Move::MakeKeysForMarks(Rig->Marks, Axis))
		{
			// Sequencer stores slopes per tick.
			FMovieSceneTangentData Tangent;
			Tangent.ArriveTangent = static_cast<float>(Key.ArriveSlopePerSecond / TicksPerSecond);
			Tangent.LeaveTangent = static_cast<float>(Key.LeaveSlopePerSecond / TicksPerSecond);
			const FFrameNumber KeyTick = StartTick + FMath::RoundToInt32(Key.TimeSeconds * TicksPerSecond);
			Channel.AddCubicKey(KeyTick, static_cast<float>(Key.Value), RCTM_Break, Tangent);
		}
		Section->MarkAsChanged();
	}

	// The operator's hand: a key on every frame of the camera's transform relative to the mount.
	if (ACineCameraActor* Camera = Rig->GetMountedCamera())
	{
		const FGuid CameraBinding = FindOrAddBinding(Sequence, Camera);
		UMovieScene3DTransformTrack* Track = MovieScene->FindTrack<UMovieScene3DTransformTrack>(CameraBinding);
		if (Track == nullptr && !Rig->Operator.IsStill())
		{
			Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(CameraBinding);
		}
		if (Track != nullptr)
		{
			Track->Modify();
			UMovieScene3DTransformSection* Section = FindOrAddSection<UMovieScene3DTransformSection>(Track);
			Section->Modify();

			// Channels are location X Y Z, rotation roll pitch yaw, then scale X Y Z.
			const TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			for (int32 ChannelIndex = 0; ChannelIndex < 6; ++ChannelIndex)
			{
				DeleteKeysInRange(*Channels[ChannelIndex], MoveRange);
			}

			if (!Rig->Operator.IsStill())
			{
				const FFrameNumber EndFrame = ConvertFrameTime(FFrameTime(EndTick), TickResolution, DisplayRate).CeilToFrame();
				for (FFrameNumber Frame = StartFrame; Frame <= EndFrame; ++Frame)
				{
					const FFrameNumber Tick = ConvertFrameTime(FFrameTime(Frame), DisplayRate, TickResolution).RoundToFrame();
					const FTransform Offset = Rosc::Operator::Offset(Rig->Operator, DisplayRate.AsSeconds(FFrameTime(Frame - StartFrame)));
					const FVector Location = Offset.GetLocation();
					const FRotator Rotation = Offset.Rotator();
					Channels[0]->AddLinearKey(Tick, Location.X);
					Channels[1]->AddLinearKey(Tick, Location.Y);
					Channels[2]->AddLinearKey(Tick, Location.Z);
					Channels[3]->AddLinearKey(Tick, Rotation.Roll);
					Channels[4]->AddLinearKey(Tick, Rotation.Pitch);
					Channels[5]->AddLinearKey(Tick, Rotation.Yaw);
				}
			}
			Section->MarkAsChanged();
		}
	}

	// A dolly zoom: a key on every frame of the lens's focal length while the camera travels.
	// A move baked earlier with a dolly zoom that has since been switched off loses its keys.
	if (ACineCameraActor* Camera = Rig->GetMountedCamera())
	{
		UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
		const FName FocalLengthName = GET_MEMBER_NAME_CHECKED(UCineCameraComponent, CurrentFocalLength);
		const bool bAnyDollyZoom = CanDollyZoom(Rig) && Rig->Marks.ContainsByPredicate([](const FRoscRigMark& Mark) { return Mark.bDollyZoom; });
		UMovieSceneFloatTrack* Track = bAnyDollyZoom
			? FindOrAddFloatTrack(MovieScene, FindOrAddComponentBinding(Sequence, Lens), FocalLengthName, FocalLengthName.ToString(), LOCTEXT("ZoomTrackName", "Focal Length"))
			: FindFloatTrack(MovieScene, FindBinding(Sequence, Lens), FocalLengthName);
		if (Track != nullptr)
		{
			Track->Modify();
			UMovieSceneFloatSection* Section = FindOrAddSection<UMovieSceneFloatSection>(Track);
			Section->Modify();
			FMovieSceneFloatChannel& Channel = Section->GetChannel();
			DeleteKeysInRange(Channel, MoveRange);

			FDollyZoomSolver Solver(*Rig, *Camera);
			const double FramesPerSecond = DisplayRate.AsDecimal();
			double MoveStartSeconds = 0.0;
			for (int32 MarkIndex = 1; bAnyDollyZoom && MarkIndex < Rig->Marks.Num(); ++MarkIndex)
			{
				const double MoveSeconds = Rig->Marks[MarkIndex].Arrival.Normalized().DurationSeconds;
				if (Rig->Marks[MarkIndex].bDollyZoom)
				{
					const FFrameNumber FirstFrame = StartFrame + FMath::CeilToInt32(MoveStartSeconds * FramesPerSecond - 1.e-6);
					const FFrameNumber LastFrame = StartFrame + FMath::CeilToInt32((MoveStartSeconds + MoveSeconds) * FramesPerSecond - 1.e-6);
					for (FFrameNumber Frame = FirstFrame; Frame <= LastFrame; ++Frame)
					{
						const double IntoMove = FMath::Clamp(DisplayRate.AsSeconds(FFrameTime(Frame - StartFrame)) - MoveStartSeconds, 0.0, MoveSeconds);
						const FFrameNumber Tick = ConvertFrameTime(FFrameTime(Frame), DisplayRate, TickResolution).RoundToFrame();
						Channel.AddLinearKey(Tick, static_cast<float>(Solver.ClampToLens(Solver.FocalLengthAt(MarkIndex, IntoMove))));
					}
				}
				Solver.FinishMove(MarkIndex);
				MoveStartSeconds += MoveSeconds;
			}
			Section->MarkAsChanged();
		}
	}

	// What was baked, so that the Grip tab can say when the marks have moved on since.
	Rig->Modify();
	Rig->BakedMarks = Rig->Marks;
	Rig->BakedOperator = Rig->Operator;

	IncludeInPlaybackRange(MovieScene, MoveRange);
	ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	return true;
}

ULevelSequence* Rosc::GripEditing::BakeMoveToSequencer(ARoscRig* Rig, FText& OutError)
{
	// Checked first so that a refused bake does not leave a new, empty sequence behind.
	if (!HasMoveToBake(Rig, OutError))
	{
		return nullptr;
	}

	const ACineCameraActor* Camera = Rig->GetMountedCamera();
	const FBakeTarget Target = FindOrCreateBakeTarget(Camera != nullptr ? static_cast<const AActor*>(Camera) : Rig, TEXT("_Move"), Camera);
	if (Target.Sequence == nullptr)
	{
		OutError = LOCTEXT("BakeNoNewSequence", "A new sequence could not be made for the move.");
		return nullptr;
	}
	return BakeMove(Rig, Target.Sequence, Target.StartFrame, OutError) ? Target.Sequence : nullptr;
}

#undef LOCTEXT_NAMESPACE
