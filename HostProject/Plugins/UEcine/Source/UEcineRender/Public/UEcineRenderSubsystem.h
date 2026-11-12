// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UEcineDeliver.h"
#include "UEcineRenderSubsystem.generated.h"

class ULevelSequence;
class UMovieGraphConfig;
class UMoviePipelineExecutorBase;
class UMoviePipelinePIEExecutor;
class UMoviePipelineQueue;
struct FMoviePipelineOutputData;

/** One shot of a render: which shot of the sequence, and what is made of it. */
struct FUEcineRenderShot
{
	/**
	 * The shot's name on the sequence's Shot track. Left empty for a sequence that is not cut
	 * into shots, which is rendered as one.
	 */
	FString ShotName;
	FUEcineRenderSpec Spec;
};

/** How a render ended. */
struct FUEcineRenderResult
{
	bool bSuccess = false;
	/** Every file written, in the order written. */
	TArray<FString> Files;
};

/**
 * Runs renders through Movie Render Graph inside the editor, the way the engine's own
 * "Render (Local)" does, from generated graphs. One render runs at a time.
 */
UCLASS()
class UECINERENDER_API UUEcineRenderSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Starts rendering shots of a sequence in the level that is open. A shot of the sequence
	 * that is not listed is not rendered. The render goes on over the following editor frames;
	 * see IsRendering and OnRenderFinished.
	 * @return false with the reason in OutError when the render cannot start.
	 */
	bool StartRender(ULevelSequence* Sequence, TConstArrayView<FUEcineRenderShot> Shots, FText& OutError);

	bool IsRendering() const;

	/**
	 * Stops the render that is running. It ends as a render that did not succeed, over the
	 * following editor frames like any other: see IsRendering and OnRenderFinished. What it had
	 * written by then stays.
	 */
	void CancelRender();

	/** From 0 to 1 while a render runs. */
	float GetProgress() const;

	/** How the last render ended. */
	const FUEcineRenderResult& GetLastResult() const { return LastResult; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRenderFinished, const FUEcineRenderResult&);
	FOnRenderFinished& OnRenderFinished() { return RenderFinished; }

private:
	void HandleJobFinished(FMoviePipelineOutputData Output);
	void HandleExecutorFinished(UMoviePipelineExecutorBase* FinishedExecutor, bool bSuccess);
	/** Watches the render every editor frame: stops it when asked, and ends it once the engine has let go of it. */
	bool WatchRender(float DeltaTime);

	UPROPERTY()
	TObjectPtr<UMoviePipelineQueue> Queue;

	UPROPERTY()
	TObjectPtr<UMoviePipelinePIEExecutor> Executor;

	/** The generated graphs, kept alive while the render uses them. */
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphConfig>> Graphs;

	FUEcineRenderResult LastResult;
	FOnRenderFinished RenderFinished;
	/** The engine has said the render is over. It says so as soon as a render fails, before it has let go of it. */
	bool bExecutorEnded = false;
	bool bExecutorFailed = false;
	bool bStopRequested = false;
	/** How the editor kept time before the render, which keeps its own. */
	bool bFixedTimeStepBefore = false;
	double FixedDeltaTimeBefore = 0.0;
};
