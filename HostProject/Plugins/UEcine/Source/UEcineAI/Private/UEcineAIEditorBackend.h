#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "NNEModelData.h"
#include "UEcineAIBackend.h"
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
class FUEcineAIEditorBackend : public IUEcineAIBackend
{
public:
	virtual ~FUEcineAIEditorBackend() override;

	virtual FString GetHost() const override { return FString(); }
	virtual bool CanRun(EUEcineAITask Task, FText& OutWhyNot) const override;
	virtual void Run(EUEcineAITask Task, const FString& PictureFile, TFunction<void(FUEcineAIOutput&&)> OnDone) override;
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
	FUEcineAIOutput Work(const FUEcineAIModel& Model, const FString& PictureFile);
	/** Brings the model into memory when it is not the one already there. */
	bool LoadModel(const FUEcineAIModel& Model, FText& OutError);
	bool RunModel(const FUEcineAIModel& Model, const FImage& Picture, FUEcineAIOutput& Output);

	/**
	 * The model in memory: one at a time, as each takes a few hundred megabytes. Used by the
	 * worker, and by the game thread only while the worker waits for it.
	 */
	FString LoadedModelId;
	TStrongObjectPtr<UNNEModelData> ModelData;
	TSharedPtr<UE::NNE::IModelGPU> LoadedModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> ModelInstance;

	TFuture<void> Worker;
	std::atomic<EStage> Stage = EStage::Reading;
	std::atomic<bool> bCancel = false;
};
