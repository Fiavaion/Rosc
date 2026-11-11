#include "UEcineRenderSubsystem.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Graph/MovieGraphConfig.h"
#include "LevelSequence.h"
#include "MoviePipelinePIEExecutor.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineQueueSubsystem.h"
#include "MovieRenderPipelineDataTypes.h"
#include "UEcineLog.h"
#include "UEcineRenderGraph.h"

#define LOCTEXT_NAMESPACE "UEcineRender"

bool UUEcineRenderSubsystem::StartRender(ULevelSequence* Sequence, TConstArrayView<FUEcineRenderShot> Shots, FText& OutError)
{
	UMoviePipelineQueueSubsystem* EngineQueue = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	if (IsRendering() || EngineQueue->IsRendering())
	{
		OutError = LOCTEXT("AlreadyRendering", "A render is already running. Wait for it to finish, or stop it.");
		return false;
	}
	if (Sequence == nullptr || Shots.IsEmpty())
	{
		OutError = LOCTEXT("NothingToRender", "There is nothing to render: no sequence, or none of its shots.");
		return false;
	}

	UMoviePipelineQueue* NewQueue = NewObject<UMoviePipelineQueue>(this);
	UMoviePipelineExecutorJob* Job = NewQueue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
	Job->JobName = Sequence->GetName();
	Job->Map = FSoftObjectPath(GEditor->GetEditorWorldContext().World());
	// Setting the sequence makes the job's list of shots.
	Job->SetSequence(FSoftObjectPath(Sequence));

	TArray<TObjectPtr<UMovieGraphConfig>> NewGraphs;
	for (UMoviePipelineExecutorShot* JobShot : Job->ShotInfo)
	{
		const FUEcineRenderShot* Shot = Shots.FindByPredicate([JobShot](const FUEcineRenderShot& Candidate) { return Candidate.ShotName == JobShot->OuterName; });
		UE_LOGF(LogUEcine, Verbose, "Render: the sequence has shot '%ls' (camera '%ls'): %ls", *JobShot->OuterName, *JobShot->InnerName, Shot != nullptr ? TEXT("rendered") : TEXT("left out"));
		if (Shot == nullptr)
		{
			JobShot->bEnabled = false;
			continue;
		}

		UMovieGraphConfig* Graph = UEcine::RenderGraph::Build(Shot->Spec, this, OutError);
		if (Graph == nullptr)
		{
			return false;
		}
		// The job needs a graph of its own; each shot then renders with its own.
		if (NewGraphs.IsEmpty())
		{
			Job->SetGraphPreset(Graph);
		}
		JobShot->SetGraphPreset(Graph);
		NewGraphs.Add(Graph);
	}
	if (NewGraphs.IsEmpty())
	{
		OutError = LOCTEXT("ShotsNotFound", "None of the shots to render is in the sequence as the renderer reads it. A shot needs a camera cut to be rendered.");
		return false;
	}

	Queue = NewQueue;
	Graphs = MoveTemp(NewGraphs);
	LastResult = FUEcineRenderResult();
	bExecutorFailed = false;

	Executor = NewObject<UMoviePipelinePIEExecutor>(this);
	// The level is rendered as it stands in the editor, saved or not.
	Executor->SetAllowUsingUnsavedLevels(true);
	Executor->OnIndividualJobWorkFinished().AddUObject(this, &UUEcineRenderSubsystem::HandleJobFinished);
	Executor->OnExecutorFinished().AddUObject(this, &UUEcineRenderSubsystem::HandleExecutorFinished);
	EngineQueue->RenderQueueInstanceWithExecutorInstance(Queue, Executor);
	return true;
}

bool UUEcineRenderSubsystem::IsRendering() const
{
	return Executor != nullptr;
}

void UUEcineRenderSubsystem::CancelRender()
{
	if (Executor != nullptr)
	{
		Executor->CancelAllJobs();
	}
}

float UUEcineRenderSubsystem::GetProgress() const
{
	return Executor != nullptr ? Executor->GetStatusProgress() : 0.0f;
}

void UUEcineRenderSubsystem::HandleJobFinished(FMoviePipelineOutputData Output)
{
	// For a render from a graph the engine fills in the files only: whether it succeeded comes
	// from the executor (LESSON-API-011).
	for (const FMovieGraphRenderOutputData& ShotOutput : Output.GraphData)
	{
		for (const TPair<FMovieGraphRenderDataIdentifier, FMovieGraphRenderLayerOutputData>& LayerOutput : ShotOutput.RenderLayerData)
		{
			for (const FString& File : LayerOutput.Value.FilePaths)
			{
				LastResult.Files.AddUnique(File);
			}
		}
	}
}

void UUEcineRenderSubsystem::HandleExecutorFinished(UMoviePipelineExecutorBase* FinishedExecutor, bool bSuccess)
{
	// The engine reports a failure or a stop at once and may report the end again later, so the
	// render is treated as over only when the engine's queue has let go of the executor.
	bExecutorFailed |= !bSuccess;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UUEcineRenderSubsystem::FinishWhenReleased));
}

bool UUEcineRenderSubsystem::FinishWhenReleased(float DeltaTime)
{
	if (Executor == nullptr)
	{
		return false;
	}
	if (GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>()->IsRendering())
	{
		return true;
	}

	LastResult.bSuccess = !bExecutorFailed;
	Executor = nullptr;
	Queue = nullptr;
	Graphs.Reset();
	RenderFinished.Broadcast(LastResult);
	return false;
}

#undef LOCTEXT_NAMESPACE
