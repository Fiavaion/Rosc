#include "UEcineRenderSubsystem.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Graph/MovieGraphConfig.h"
#include "LevelSequence.h"
#include "Misc/App.h"
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
		// A sequence that is not cut into shots is one shot to the renderer too, under a name of the renderer's own.
		const FUEcineRenderShot* Shot = Shots.FindByPredicate([JobShot](const FUEcineRenderShot& Candidate) { return Candidate.ShotName.IsEmpty() || Candidate.ShotName == JobShot->OuterName; });
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
	bExecutorEnded = bExecutorFailed = bStopRequested = false;
	bFixedTimeStepBefore = FApp::UseFixedTimeStep();
	FixedDeltaTimeBefore = FApp::GetFixedDeltaTime();

	Executor = NewObject<UMoviePipelinePIEExecutor>(this);
	// The level is rendered as it stands in the editor, saved or not.
	Executor->SetAllowUsingUnsavedLevels(true);
	Executor->OnIndividualJobWorkFinished().AddUObject(this, &UUEcineRenderSubsystem::HandleJobFinished);
	Executor->OnExecutorFinished().AddUObject(this, &UUEcineRenderSubsystem::HandleExecutorFinished);
	EngineQueue->RenderQueueInstanceWithExecutorInstance(Queue, Executor);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UUEcineRenderSubsystem::WatchRender));
	return true;
}

bool UUEcineRenderSubsystem::IsRendering() const
{
	return Executor != nullptr;
}

void UUEcineRenderSubsystem::CancelRender()
{
	// The render runs in a play session the editor starts a frame after the render does, and a
	// render stopped before then would leave that session running. So the stop waits for it:
	// see WatchRender.
	bStopRequested |= Executor != nullptr;
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
	// The engine reports a failure at once, while its play session still runs, and reports the
	// end again when it has let go: WatchRender decides when the render is over (LESSON-API-014).
	bExecutorEnded = true;
	bExecutorFailed |= !bSuccess;
}

bool UUEcineRenderSubsystem::WatchRender(float DeltaTime)
{
	if (Executor == nullptr)
	{
		return false;
	}

	if (!bExecutorEnded && !Executor->IsRendering() && !GEditor->IsPlaySessionInProgress())
	{
		// The editor did not start the play session the render runs in, and the engine's executor
		// would wait for it for good: it would take the next play session for its own. It is told
		// to give up, which makes it report its end.
		FEditorDelegates::PostPIEStarted.RemoveAll(Executor);
		Executor->CancelAllJobs();
	}

	// Ending the play session is how the engine stops a render too (it is what Escape does): the
	// render writes out what it has and the executor reports its end. A render that failed without
	// ending its session is ended the same way.
	if ((bStopRequested || bExecutorEnded) && GEditor->PlayWorld != nullptr && !GEditor->ShouldEndPlayMap())
	{
		GEditor->RequestEndPlayMap();
	}

	if (!bExecutorEnded || Executor->IsRendering() || GEditor->IsPlaySessionInProgress())
	{
		return true;
	}

	// The engine puts the editor's time keeping back as its play session ends, and not when the
	// session never started.
	FApp::SetUseFixedTimeStep(bFixedTimeStepBefore);
	FApp::SetFixedDeltaTime(FixedDeltaTimeBefore);

	LastResult.bSuccess = !bExecutorFailed && !bStopRequested;
	Executor = nullptr;
	Queue = nullptr;
	Graphs.Reset();
	RenderFinished.Broadcast(LastResult);
	return false;
}

#undef LOCTEXT_NAMESPACE
