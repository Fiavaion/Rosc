#include "UEcineSequenceUtils.h"

#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"

FGuid UEcine::SequenceUtils::FindBinding(ULevelSequence* Sequence, AActor* Actor)
{
	return Sequence->FindBindingFromObject(Actor, MovieSceneHelpers::CreateTransientSharedPlaybackState(Actor->GetWorld(), Sequence));
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
