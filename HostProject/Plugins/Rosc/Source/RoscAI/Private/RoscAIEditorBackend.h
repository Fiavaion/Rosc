// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "NNEModelData.h"
#include "RoscAIBackend.h"
#include "UObject/StrongObjectPtr.h"
#include <atomic>

namespace UE::NNE
{
	class IModelGPU;
	class IModelInstanceGPU;
}

/**
 * Runs models inside the editor through the engine's neural network runtime (ONNX Runtime on
 * DirectML). The work is done on a thread of its own, so the editor stays live (ADR-006).
 */
class FRoscAIEditorBackend : public IRoscAIBackend
{
public:
	virtual ~FRoscAIEditorBackend() override;

	virtual FString GetHost() const override { return FString(); }
	virtual bool CanRun(ERoscAITask Task, FText& OutWhyNot) const override;
	virtual void Run(ERoscAITask Task, const FString& PictureFile, TFunction<void(FRoscAIOutput&&)> OnDone) override;
	virtual void GetProgress(FText& OutStage, float& OutFraction) const override;
	virtual void Cancel() override { bCancel = true; }

private:
	enum class EStage : uint8
	{
		Reading,
		Loading,
		Working,
		Finishing
	};

	/** The whole of a job, on the worker thread. */
	FRoscAIOutput Work(const FRoscAIModel& Model, const FString& PictureFile);
	/** Brings the model into memory, checked against its hash and compiled for the graphics card. */
	bool LoadModel(const FRoscAIModel& Model, FText& OutError);
	bool RunModel(const FRoscAIModel& Model, const FImage& Picture, FRoscAIOutput& Output);
	/** Gives back the memory the model holds, on the graphics card and off it. */
	void ReleaseModel();

	/**
	 * The model, in memory only while its job runs. The matte's model takes about 10 GB of
	 * graphics memory, and an editor left holding it lost its graphics device at the next render
	 * (LESSON-API-020). Used by the worker, and by the game thread only while the worker
	 * waits for it.
	 */
	TStrongObjectPtr<UNNEModelData> ModelData;
	TSharedPtr<UE::NNE::IModelGPU> LoadedModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> ModelInstance;

	TFuture<void> Worker;
	std::atomic<EStage> Stage = EStage::Reading;
	std::atomic<bool> bCancel = false;
};
