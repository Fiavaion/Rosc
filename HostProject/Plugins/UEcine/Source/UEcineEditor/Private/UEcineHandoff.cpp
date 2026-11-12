// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineHandoff.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Exporters/FbxExportOption.h"
#include "HAL/FileManager.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "MovieScene.h"
#include "MovieSceneBindingProxy.h"
#include "MovieScenePossessable.h"
#include "ObjectTools.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "SequencerTools.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UEcineFocusEditing.h"
#include "UEcineRig.h"
#include "UEcineSequenceUtils.h"
#include "UEcineShotEditing.h"
#include "UEcineShotMetaData.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

namespace
{
	const TCHAR* const HandoffSuffix = TEXT("_Handoff");

	/** What a camera is doing at one moment of a shot. */
	struct FHandoffSample
	{
		/** In ticks of the shot's own sequence. */
		FFrameNumber Tick;
		/** Of the lens, in the level. */
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float FocalLength = 0.0f;
		float Aperture = 0.0f;
		float FocusDistance = 0.0f;
	};

	/** One camera of a shot, and what it does through the shot. */
	struct FHandoffCamera
	{
		/** The camera's binding in the shot's sequence. */
		FGuid Binding;
		ACineCameraActor* Camera = nullptr;
		TArray<FHandoffSample> Samples;
	};

	/** Plays through a shot of the open sequence and writes down what each of its cameras does. */
	TArray<FHandoffCamera> SampleShotCameras(const ULevelSequence* OpenSequence, const UEcine::ShotEditing::FShot& Shot)
	{
		const UMovieScene* OpenMovieScene = OpenSequence->GetMovieScene();
		const auto ToOpenTicks = [OpenMovieScene](FFrameNumber Frame)
		{
			return ConvertFrameTime(FFrameTime(Frame), OpenMovieScene->GetDisplayRate(), OpenMovieScene->GetTickResolution()).FloorToFrame();
		};

		// Every frame of the shot, then its last moment, so that the movement during the last frame is kept too.
		TArray<FFrameNumber> OpenTicks;
		for (int32 FrameInShot = 0; FrameInShot < Shot.DurationFrames; ++FrameInShot)
		{
			OpenTicks.Add(ToOpenTicks(Shot.StartFrame + FrameInShot));
		}
		if (Shot.DurationFrames > 0)
		{
			OpenTicks.Add(ToOpenTicks(Shot.StartFrame + Shot.DurationFrames) - 1);
		}

		TArray<FHandoffCamera> Cameras;
		for (const FFrameNumber OpenTick : OpenTicks)
		{
			UEcine::SequenceUtils::SetPlayheadTicks(FFrameTime(OpenTick));

			// The cameras are looked for once the shot is playing, so that those it spawns are there.
			if (OpenTick == OpenTicks[0])
			{
				const UMovieScene* ShotMovieScene = Shot.Sequence->GetMovieScene();
				for (const FMovieSceneBinding& Binding : ShotMovieScene->GetBindings())
				{
					for (UObject* Object : UEcine::ShotEditing::ResolveBinding(Shot, Binding.GetObjectGuid()))
					{
						if (ACineCameraActor* Camera = Cast<ACineCameraActor>(Object))
						{
							Cameras.Add({ Binding.GetObjectGuid(), Camera });
							break;
						}
					}
				}
			}

			const FFrameNumber ShotTick = Shot.Section != nullptr ? Shot.Section->OuterToInnerTransform().TransformTime(FFrameTime(OpenTick)).RoundToFrame() : OpenTick;
			for (FHandoffCamera& Baked : Cameras)
			{
				const UCineCameraComponent* Lens = Baked.Camera->GetCineCameraComponent();
				FHandoffSample Sample;
				Sample.Tick = ShotTick;
				Sample.Location = Lens->GetComponentLocation();
				Sample.Rotation = Lens->GetComponentRotation();
				if (!Baked.Samples.IsEmpty())
				{
					// Angles carry on from the frame before, so that a turn through 180 degrees is not played as a spin back the other way.
					const FRotator& Previous = Baked.Samples.Last().Rotation;
					Sample.Rotation = Previous + (Sample.Rotation - Previous).GetNormalized();
				}
				Sample.FocalLength = Lens->CurrentFocalLength;
				Sample.Aperture = Lens->CurrentAperture;
				Sample.FocusDistance = UEcine::FocusEditing::GetLensDistance(Baked.Camera);
				Baked.Samples.Add(Sample);
			}
		}
		return Cameras;
	}

	/** Copies a sequence to its handoff name, in place of the copy from an earlier bake. */
	ULevelSequence* DuplicateForHandoff(ULevelSequence* Sequence)
	{
		ObjectTools::FPackageGroupName CopyName;
		CopyName.PackageName = UEcine::Handoff::GetHandoffPackageName(Sequence);
		CopyName.ObjectName = FPackageName::GetLongPackageAssetName(CopyName.PackageName);
		TSet<UPackage*> PackagesNotLoaded;
		return Cast<ULevelSequence>(ObjectTools::DuplicateSingleObject(Sequence, CopyName, PackagesNotLoaded, false));
	}

	/**
	 * A Cine Camera with the lens, filmback, exposure and look of another, and nothing else of
	 * it: on no rig, with no camera package, focused by hand. It is there to be copied into a
	 * sequence and is then destroyed.
	 */
	ACineCameraActor* SpawnPlainCamera(const ACineCameraActor& Source)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.bTemporaryEditorActor = true;
		ACineCameraActor* Plain = Source.GetWorld()->SpawnActor<ACineCameraActor>(SpawnParameters);
		if (Plain == nullptr)
		{
			return nullptr;
		}
		Plain->SetActorLabel(Source.GetActorLabel());

		// Everything a camera component is set to, and none of where it is or what it is fixed to.
		const UCineCameraComponent* SourceLens = Source.GetCineCameraComponent();
		UCineCameraComponent* Lens = Plain->GetCineCameraComponent();
		for (TFieldIterator<FProperty> Property(UCineCameraComponent::StaticClass()); Property; ++Property)
		{
			if (Property->GetOwnerClass()->IsChildOf(UCameraComponent::StaticClass()) && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
			{
				Property->CopyCompleteValue_InContainer(Lens, SourceLens);
			}
		}
		Lens->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
		Lens->FocusSettings.TrackingFocusSettings = FCameraTrackingFocusSettings();
		Lens->FocusSettings.FocusOffset = 0.0f;
		return Plain;
	}

	FMovieSceneFloatChannel& AddLensChannel(UMovieScene* MovieScene, const FGuid& LensBinding, FName PropertyName, const FString& PropertyPath, const FText& DisplayName)
	{
		UMovieSceneFloatTrack* Track = UEcine::SequenceUtils::FindOrAddFloatTrack(MovieScene, LensBinding, PropertyName, PropertyPath, DisplayName);
		return UEcine::SequenceUtils::FindOrAddSection<UMovieSceneFloatSection>(Track)->GetChannel();
	}

	/**
	 * Adds a plain camera to a shot's copy, spawned by the sequence, with a key at every sample.
	 * @return the new camera's binding, or an invalid guid when it could not be made.
	 */
	FGuid AddBakedCamera(ULevelSequence* ShotCopy, const FHandoffCamera& Baked)
	{
		ACineCameraActor* Plain = SpawnPlainCamera(*Baked.Camera);
		if (Plain == nullptr)
		{
			return FGuid();
		}
		ON_SCOPE_EXIT { Plain->GetWorld()->DestroyActor(Plain); };

		UMovieScene* MovieScene = ShotCopy->GetMovieScene();
		// Asked of the sequence as the engine's own scripting does: through the base type, where making a spawnable is public.
		UMovieSceneSequence* SpawningSequence = ShotCopy;
		const FGuid CameraBinding = SpawningSequence->CreateSpawnable(Plain);
		if (!CameraBinding.IsValid())
		{
			return FGuid();
		}

		// The lens is found again by its name inside whichever camera the sequence spawns.
		UCineCameraComponent* Lens = Plain->GetCineCameraComponent();
		const FGuid LensBinding = MovieScene->AddPossessable(Lens->GetName(), Lens->GetClass());
		MovieScene->FindPossessable(LensBinding)->SetParent(CameraBinding, MovieScene);
		ShotCopy->BindPossessableObject(LensBinding, *Lens, Plain);

		UMovieScene3DTransformSection* TransformSection = UEcine::SequenceUtils::FindOrAddSection<UMovieScene3DTransformSection>(MovieScene->AddTrack<UMovieScene3DTransformTrack>(CameraBinding));
		// Channels are location X Y Z, rotation roll pitch yaw, then scale X Y Z.
		const TArrayView<FMovieSceneDoubleChannel*> Transform = TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();

		FMovieSceneFloatChannel& FocalLength = AddLensChannel(MovieScene, LensBinding,
			GET_MEMBER_NAME_CHECKED(UCineCameraComponent, CurrentFocalLength), TEXT("CurrentFocalLength"), LOCTEXT("HandoffFocalLengthTrack", "Focal Length"));
		FMovieSceneFloatChannel& Aperture = AddLensChannel(MovieScene, LensBinding,
			GET_MEMBER_NAME_CHECKED(UCineCameraComponent, CurrentAperture), TEXT("CurrentAperture"), LOCTEXT("HandoffApertureTrack", "Aperture"));
		FMovieSceneFloatChannel& FocusDistance = AddLensChannel(MovieScene, LensBinding,
			GET_MEMBER_NAME_CHECKED(FCameraFocusSettings, ManualFocusDistance), TEXT("FocusSettings.ManualFocusDistance"), LOCTEXT("HandoffFocusTrack", "Focus Distance"));

		for (const FHandoffSample& Sample : Baked.Samples)
		{
			Transform[0]->AddLinearKey(Sample.Tick, Sample.Location.X);
			Transform[1]->AddLinearKey(Sample.Tick, Sample.Location.Y);
			Transform[2]->AddLinearKey(Sample.Tick, Sample.Location.Z);
			Transform[3]->AddLinearKey(Sample.Tick, Sample.Rotation.Roll);
			Transform[4]->AddLinearKey(Sample.Tick, Sample.Rotation.Pitch);
			Transform[5]->AddLinearKey(Sample.Tick, Sample.Rotation.Yaw);
			FocalLength.AddLinearKey(Sample.Tick, Sample.FocalLength);
			Aperture.AddLinearKey(Sample.Tick, Sample.Aperture);
			FocusDistance.AddLinearKey(Sample.Tick, Sample.FocusDistance);
		}
		return CameraBinding;
	}

	/**
	 * Turns the copy of a shot into one that needs no UEcine: its cameras become plain baked
	 * cameras, and its rigs and shot list data are taken out.
	 * @param OutBakedCameras The plain camera's binding in the copy for each camera's binding in the shot.
	 */
	bool MakeShotCopyPlain(ULevelSequence* ShotCopy, TConstArrayView<FHandoffCamera> Cameras, TMap<FGuid, FGuid>& OutBakedCameras)
	{
		UMovieScene* MovieScene = ShotCopy->GetMovieScene();
		ShotCopy->RemoveMetaDataByClass(UUEcineShotMetaData::StaticClass());

		TSet<FGuid> BindingsToRemove;
		for (const FHandoffCamera& Baked : Cameras)
		{
			const FGuid BakedBinding = AddBakedCamera(ShotCopy, Baked);
			if (!BakedBinding.IsValid())
			{
				return false;
			}
			OutBakedCameras.Add(Baked.Binding, BakedBinding);
			BindingsToRemove.Add(Baked.Binding);
		}

		if (UMovieSceneTrack* CameraCuts = MovieScene->GetCameraCutTrack())
		{
			for (UMovieSceneSection* Section : CameraCuts->GetAllSections())
			{
				UMovieSceneCameraCutSection* Cut = CastChecked<UMovieSceneCameraCutSection>(Section);
				if (const FGuid* BakedBinding = OutBakedCameras.Find(Cut->GetCameraBindingID().GetGuid()))
				{
					Cut->SetCameraBindingID(UE::MovieScene::FRelativeObjectBindingID(*BakedBinding));
				}
			}
		}

		// The old cameras and the rigs go, with whatever is bound inside them.
		for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
		{
			const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
			const UClass* Class = Possessable.GetPossessedObjectClass();
			if (Class != nullptr && Class->IsChildOf(AUEcineRig::StaticClass()))
			{
				BindingsToRemove.Add(Possessable.GetGuid());
			}
		}
		for (bool bFoundMore = true; bFoundMore;)
		{
			bFoundMore = false;
			for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
			{
				const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
				if (BindingsToRemove.Contains(Possessable.GetParent()) && !BindingsToRemove.Contains(Possessable.GetGuid()))
				{
					BindingsToRemove.Add(Possessable.GetGuid());
					bFoundMore = true;
				}
			}
		}
		for (const FGuid& Binding : BindingsToRemove)
		{
			ShotCopy->UnbindPossessableObjects(Binding);
			if (!MovieScene->RemovePossessable(Binding))
			{
				MovieScene->RemoveSpawnable(Binding);
			}
		}
		return true;
	}

	/** The camera a shot is seen through, of those sampled: the camera of its first camera cut, or failing that the first. */
	const FHandoffCamera* FindCameraSeenThrough(const UEcine::ShotEditing::FShot& Shot, TConstArrayView<FHandoffCamera> Cameras)
	{
		const UMovieSceneCameraCutSection* FirstCut = nullptr;
		if (const UMovieSceneTrack* CameraCuts = Shot.Sequence->GetMovieScene()->GetCameraCutTrack())
		{
			for (const UMovieSceneSection* Section : CameraCuts->GetAllSections())
			{
				const UMovieSceneCameraCutSection* Cut = Cast<UMovieSceneCameraCutSection>(Section);
				if (Cut != nullptr && Cut->HasStartFrame() && (FirstCut == nullptr || Cut->GetInclusiveStartFrame() < FirstCut->GetInclusiveStartFrame()))
				{
					FirstCut = Cut;
				}
			}
		}
		if (FirstCut != nullptr)
		{
			const FGuid CutBinding = FirstCut->GetCameraBindingID().GetGuid();
			if (const FHandoffCamera* Camera = Cameras.FindByPredicate([&CutBinding](const FHandoffCamera& Candidate) { return Candidate.Binding == CutBinding; }))
			{
				return Camera;
			}
		}
		return Cameras.IsEmpty() ? nullptr : &Cameras[0];
	}
}

FString UEcine::Handoff::GetHandoffPackageName(const ULevelSequence* Sequence)
{
	return Sequence->GetOutermost()->GetName() + HandoffSuffix;
}

bool UEcine::Handoff::IsHandoffCopy(const ULevelSequence* Sequence)
{
	return Sequence != nullptr && Sequence->GetName().EndsWith(HandoffSuffix);
}

ULevelSequence* UEcine::Handoff::BakeForHandoff(FText& OutError, TArray<FBakedShot>* OutShots)
{
	ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		OutError = LOCTEXT("HandoffNoSequence", "No sequence is open in Sequencer. Open the sequence to hand off.");
		return nullptr;
	}
	if (IsHandoffCopy(OpenSequence))
	{
		OutError = LOCTEXT("HandoffOfHandoff", "This sequence is a handoff copy already. Open the sequence it was made from.");
		return nullptr;
	}

	// What the cameras do is read from the sequences being worked on, before anything is copied.
	const TArray<ShotEditing::FShot> Shots = ShotEditing::GetShots();
	TArray<TArray<FHandoffCamera>> CamerasOfEachShot;
	TMap<ULevelSequence*, TArray<FHandoffCamera>> CamerasOfShots;
	{
		FScopedSlowTask Progress(static_cast<float>(Shots.Num()), LOCTEXT("HandoffProgress", "Baking cameras for handoff"));
		Progress.MakeDialog();
		const SequenceUtils::FScopedPlayhead PutPlayheadBack;
		for (const ShotEditing::FShot& Shot : Shots)
		{
			Progress.EnterProgressFrame();
			const TArray<FHandoffCamera>& Cameras = CamerasOfEachShot.Add_GetRef(SampleShotCameras(OpenSequence, Shot));
			// A shot's sequence used twice in the cut is baked as it plays the first time.
			if (!CamerasOfShots.Contains(Shot.Sequence))
			{
				CamerasOfShots.Add(Shot.Sequence, Cameras);
			}
		}
	}

	const auto FailToCopy = [&OutError](const ULevelSequence* Sequence)
	{
		OutError = FText::Format(LOCTEXT("HandoffCopyFailed", "The handoff copy of {0} could not be made."), FText::FromString(Sequence->GetName()));
		return nullptr;
	};

	// The open sequence is copied first: its copy from an earlier bake plays the shots' old copies, which can only be replaced once it has gone.
	TMap<ULevelSequence*, ULevelSequence*> Copies;
	ULevelSequence* OpenCopy = DuplicateForHandoff(OpenSequence);
	if (OpenCopy == nullptr)
	{
		return FailToCopy(OpenSequence);
	}
	Copies.Add(OpenSequence, OpenCopy);

	TMap<ULevelSequence*, TMap<FGuid, FGuid>> BakedCamerasOfShots;
	for (const TPair<ULevelSequence*, TArray<FHandoffCamera>>& ShotCameras : CamerasOfShots)
	{
		ULevelSequence* ShotCopy = Copies.FindRef(ShotCameras.Key);
		if (ShotCopy == nullptr)
		{
			ShotCopy = DuplicateForHandoff(ShotCameras.Key);
			if (ShotCopy == nullptr)
			{
				return FailToCopy(ShotCameras.Key);
			}
			Copies.Add(ShotCameras.Key, ShotCopy);
		}
		if (!MakeShotCopyPlain(ShotCopy, ShotCameras.Value, BakedCamerasOfShots.Add(ShotCameras.Key)))
		{
			return FailToCopy(ShotCameras.Key);
		}
	}

	if (OutShots != nullptr)
	{
		OutShots->Reset();
		for (int32 ShotIndex = 0; ShotIndex < Shots.Num(); ++ShotIndex)
		{
			const ShotEditing::FShot& Shot = Shots[ShotIndex];
			FBakedShot& Baked = OutShots->AddDefaulted_GetRef();
			Baked.Name = Shot.Name;
			Baked.Copy = Copies.FindRef(Shot.Sequence);
			if (const FHandoffCamera* Camera = FindCameraSeenThrough(Shot, CamerasOfEachShot[ShotIndex]))
			{
				Baked.Camera = BakedCamerasOfShots.FindChecked(Shot.Sequence).FindRef(Camera->Binding);
				// One sample to a frame; the sample after those is the shot's last moment, which is no frame of its own.
				for (int32 FrameInShot = 0; FrameInShot < Shot.DurationFrames && FrameInShot < Camera->Samples.Num(); ++FrameInShot)
				{
					const FHandoffSample& Sample = Camera->Samples[FrameInShot];
					FUEcineCameraSample& Frame = Baked.Samples.AddDefaulted_GetRef();
					Frame.Frame = Shot.StartFrame.Value + FrameInShot;
					Frame.Location = Sample.Location;
					Frame.Rotation = Sample.Rotation;
					Frame.FocalLengthMm = Sample.FocalLength;
					Frame.TStop = Sample.Aperture;
					Frame.FocusDistanceCm = Sample.FocusDistance;
				}
			}
		}
	}

	// The copy of the cut plays the copies of the shots.
	if (const UMovieSceneCinematicShotTrack* ShotTrack = OpenCopy->GetMovieScene()->FindTrack<UMovieSceneCinematicShotTrack>())
	{
		for (UMovieSceneSection* Section : ShotTrack->GetAllSections())
		{
			UMovieSceneSubSection* ShotSection = CastChecked<UMovieSceneSubSection>(Section);
			if (ULevelSequence* const* ShotCopy = Copies.Find(Cast<ULevelSequence>(ShotSection->GetSequence())))
			{
				ShotSection->SetSequence(*ShotCopy);
			}
		}
	}
	return OpenCopy;
}

bool UEcine::Handoff::ExportCameraFbx(ULevelSequence* CutCopy, const FBakedShot& Shot, const FString& Filename)
{
	if (CutCopy == nullptr || Shot.Copy == nullptr || !Shot.Camera.IsValid())
	{
		return false;
	}

	// The user's own FBX export choices, with the keys at the frames of the whole sequence.
	UFbxExportOption* Options = NewObject<UFbxExportOption>();
	const FGCObjectScopeGuard KeepOptions(Options);
	Options->bExportLocalTime = false;

	// The engine plays the copy in the level with a player of its own, so Sequencer is left alone.
	const FSequencerExportFBXParams Export(GEditor->GetEditorWorldContext().World(), Shot.Copy, CutCopy,
		{ FMovieSceneBindingProxy(Shot.Camera, Shot.Copy) }, {}, Options, Filename);
	return USequencerToolsFunctionLibrary::ExportLevelSequenceFBX(Export) && IFileManager::Get().FileExists(*Filename);
}

#undef LOCTEXT_NAMESPACE
