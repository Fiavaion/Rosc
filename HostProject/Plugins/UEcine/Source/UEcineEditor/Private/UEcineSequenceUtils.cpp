#include "UEcineSequenceUtils.h"

#include "AssetToolsModule.h"
#include "CineCameraActor.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneSequencePlayer.h"
#include "ObjectTools.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UEcineCameraPackageComponent.h"

FFrameRate UEcine::SequenceUtils::GetFrameRate(double FramesPerSecond)
{
	const int32 Nominal = FMath::RoundToInt32(FramesPerSecond);
	if (FMath::IsNearlyEqual(FramesPerSecond, static_cast<double>(Nominal), 0.001))
	{
		return FFrameRate(Nominal, 1);
	}
	// The NTSC family runs a thousandth slow: 23.976 is 24000/1001, 29.97 is 30000/1001.
	if (FMath::IsNearlyEqual(FramesPerSecond, Nominal * 1000.0 / 1001.0, 0.01))
	{
		return FFrameRate(Nominal * 1000, 1001);
	}
	return FFrameRate(FMath::RoundToInt32(FramesPerSecond * 1000.0), 1000);
}

FFrameRate UEcine::SequenceUtils::GetCameraFrameRate(const ACineCameraActor* Camera)
{
	const UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(Camera);
	return GetFrameRate(Package != nullptr ? Package->Package.FramesPerSecond : 24.0);
}

ULevelSequence* UEcine::SequenceUtils::CreateSequence(const FString& PackageName, FFrameRate DisplayRate)
{
	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();

	FString UniquePackageName;
	FString AssetName;
	AssetTools.CreateUniqueAssetName(PackageName, FString(), UniquePackageName, AssetName);

	ULevelSequence* Sequence = Cast<ULevelSequence>(AssetTools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(UniquePackageName), ULevelSequence::StaticClass(), nullptr));
	if (Sequence != nullptr)
	{
		Sequence->Initialize();
		Sequence->GetMovieScene()->SetDisplayRate(DisplayRate);
	}
	return Sequence;
}

UEcine::SequenceUtils::FBakeTarget UEcine::SequenceUtils::FindOrCreateBakeTarget(const AActor* NamedAfter, const TCHAR* NameSuffix, const ACineCameraActor* Camera)
{
	FBakeTarget Target;
	Target.Sequence = ULevelSequenceEditorBlueprintLibrary::GetFocusedLevelSequence();
	if (Target.Sequence != nullptr)
	{
		Target.StartFrame = ULevelSequenceEditorBlueprintLibrary::GetLocalPosition().Frame.FloorToFrame();
		return Target;
	}

	ULevelSequence* NewSequence = CreateSequence(FString(SequenceFolder) / ObjectTools::SanitizeObjectName(NamedAfter->GetActorLabel() + NameSuffix), GetCameraFrameRate(Camera));
	if (NewSequence != nullptr && ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(NewSequence))
	{
		Target.Sequence = NewSequence;
	}
	return Target;
}

FGuid UEcine::SequenceUtils::FindBinding(ULevelSequence* Sequence, UObject* Object)
{
	return Sequence->FindBindingFromObject(Object, MovieSceneHelpers::CreateTransientSharedPlaybackState(Object->GetWorld(), Sequence));
}

FGuid UEcine::SequenceUtils::FindOrAddBinding(ULevelSequence* Sequence, AActor* Actor)
{
	FGuid Binding = FindBinding(Sequence, Actor);
	if (!Binding.IsValid())
	{
		Binding = Sequence->GetMovieScene()->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
		Sequence->BindPossessableObject(Binding, *Actor, Actor->GetWorld());
	}
	return Binding;
}

FGuid UEcine::SequenceUtils::FindOrAddComponentBinding(ULevelSequence* Sequence, UActorComponent* Component)
{
	FGuid Binding = FindBinding(Sequence, Component);
	if (!Binding.IsValid())
	{
		// A component is bound under its actor, and is found again by its name within that actor.
		AActor* Owner = Component->GetOwner();
		const FGuid OwnerBinding = FindOrAddBinding(Sequence, Owner);
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		Binding = MovieScene->AddPossessable(Component->GetName(), Component->GetClass());
		MovieScene->FindPossessable(Binding)->SetParent(OwnerBinding, MovieScene);
		Sequence->BindPossessableObject(Binding, *Component, Owner);
	}
	return Binding;
}

UMovieSceneFloatTrack* UEcine::SequenceUtils::FindFloatTrack(UMovieScene* MovieScene, const FGuid& Binding, FName PropertyName)
{
	if (const FMovieSceneBinding* Existing = MovieScene->FindBinding(Binding))
	{
		for (UMovieSceneTrack* Track : Existing->GetTracks())
		{
			UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
			if (FloatTrack != nullptr && FloatTrack->GetPropertyName() == PropertyName)
			{
				return FloatTrack;
			}
		}
	}
	return nullptr;
}

UMovieSceneFloatTrack* UEcine::SequenceUtils::FindOrAddFloatTrack(UMovieScene* MovieScene, const FGuid& Binding, FName PropertyName, const FString& PropertyPath, const FText& DisplayName)
{
	UMovieSceneFloatTrack* Track = FindFloatTrack(MovieScene, Binding, PropertyName);
	if (Track == nullptr)
	{
		Track = MovieScene->AddTrack<UMovieSceneFloatTrack>(Binding);
		Track->SetPropertyNameAndPath(PropertyName, PropertyPath);
		Track->SetDisplayName(DisplayName);
	}
	return Track;
}

void UEcine::SequenceUtils::IncludeInPlaybackRange(UMovieScene* MovieScene, const TRange<FFrameNumber>& Range)
{
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	if (PlaybackRange.IsEmpty() || !PlaybackRange.Contains(Range))
	{
		MovieScene->SetPlaybackRange(PlaybackRange.IsEmpty() ? Range : TRange<FFrameNumber>::Hull(PlaybackRange, Range));
	}
}
