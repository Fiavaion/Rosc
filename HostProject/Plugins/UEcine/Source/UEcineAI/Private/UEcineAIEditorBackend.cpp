#include "UEcineAIEditorBackend.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "NNE.h"
#include "NNERuntimeGPU.h"
#include "UEcineAIModelStore.h"
#include "UEcineAIPictures.h"
#include "UEcineVision.h"

#define LOCTEXT_NAMESPACE "UEcineAIEditorBackend"

namespace
{
	const TCHAR* EditorBackendRuntimeName = TEXT("NNERuntimeORTDml");

	TWeakInterfacePtr<INNERuntimeGPU> GetEditorBackendRuntime()
	{
		return UE::NNE::GetRuntime<INNERuntimeGPU>(EditorBackendRuntimeName);
	}

	/** One of a model's results, with room for it at the size the model was given its picture. */
	struct FEditorBackendResult
	{
		TArray<float> Values;
		/** The result's own size, read from the model after it has run. */
		FIntPoint Size = FIntPoint::ZeroValue;
	};
}

FUEcineAIEditorBackend::~FUEcineAIEditorBackend()
{
	bCancel = true;
	// The worker may be waiting for the game thread to make its model: let it.
	while (Worker.IsValid() && !Worker.IsReady())
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.001f);
	}
}

bool FUEcineAIEditorBackend::CanRun(EUEcineAITask Task, FText& OutWhyNot) const
{
	const FUEcineAIModel* Model = FUEcineAIModelList::Get().FindForTask(Task);
	if (Model == nullptr)
	{
		OutWhyNot = LOCTEXT("NoModelListed", "UEcine lists no model for this.");
		return false;
	}
	if (!UEcine::AIModelStore::IsInstalled(*Model))
	{
		OutWhyNot = FText::Format(LOCTEXT("NotDownloaded", "{0} is not downloaded yet. Download it below."), FText::FromString(Model->Name));
		return false;
	}
	if (!GetEditorBackendRuntime().IsValid())
	{
		OutWhyNot = LOCTEXT("NoRuntime", "This machine cannot run models in the editor (it needs DirectX 12). Run picture jobs in the helper instead.");
		return false;
	}
	return true;
}

void FUEcineAIEditorBackend::Run(EUEcineAITask Task, const FString& PictureFile, TFunction<void(FUEcineAIOutput&&)> OnDone)
{
	check(IsInGameThread() && (!Worker.IsValid() || Worker.IsReady()));
	const FUEcineAIModel* Model = FUEcineAIModelList::Get().FindForTask(Task);
	check(Model != nullptr);

	bCancel = false;
	Stage = EStage::Reading;
	Worker = Async(EAsyncExecution::Thread, [this, Model = *Model, PictureFile, OnDone = MoveTemp(OnDone)]
	{
		FUEcineAIOutput Output = Work(Model, PictureFile);
		AsyncTask(ENamedThreads::GameThread, [OnDone, Output = MoveTemp(Output)]() mutable { OnDone(MoveTemp(Output)); });
	});
}

void FUEcineAIEditorBackend::GetProgress(FText& OutStage, float& OutFraction) const
{
	switch (Stage.load())
	{
	case EStage::Reading:
		OutStage = LOCTEXT("StageReading", "Reading the picture");
		OutFraction = 0.05f;
		break;
	case EStage::Loading:
		OutStage = LOCTEXT("StageLoading", "Loading the model");
		OutFraction = 0.2f;
		break;
	case EStage::Working:
		OutStage = LOCTEXT("StageWorking", "Working on the picture");
		OutFraction = 0.6f;
		break;
	case EStage::Finishing:
		OutStage = LOCTEXT("StageFinishing", "Finishing");
		OutFraction = 0.95f;
		break;
	}
}

FUEcineAIOutput FUEcineAIEditorBackend::Work(const FUEcineAIModel& Model, const FString& PictureFile)
{
	FUEcineAIOutput Output;
	FImage Picture;
	if (!UEcine::AIPictures::Load(PictureFile, Picture, Output.Error))
	{
		return Output;
	}
	if (bCancel)
	{
		return Output;
	}

	Stage = EStage::Loading;
	if (!LoadModel(Model, Output.Error) || bCancel)
	{
		return Output;
	}

	Stage = EStage::Working;
	if (!RunModel(Model, Picture, Output))
	{
		// A model that failed once is made afresh for the next job.
		LoadedModelId.Reset();
		ModelInstance.Reset();
		LoadedModel.Reset();
		if (Output.Error.IsEmpty())
		{
			Output.Error = FText::Format(LOCTEXT("RunFailed", "{0} could not be run on this machine's graphics card. The log says why. The helper can run it instead."), FText::FromString(Model.Name));
		}
		return Output;
	}
	Output.bSuccess = !bCancel;
	return Output;
}

bool FUEcineAIEditorBackend::LoadModel(const FUEcineAIModel& Model, FText& OutError)
{
	if (LoadedModelId == Model.Id)
	{
		return true;
	}
	LoadedModelId.Reset();
	ModelInstance.Reset();
	LoadedModel.Reset();

	if (!UEcine::AIModelStore::Verify(Model, OutError))
	{
		return false;
	}
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *UEcine::AIModelStore::GetFile(Model)))
	{
		OutError = FText::Format(LOCTEXT("CannotRead", "{0} could not be read."), FText::FromString(UEcine::AIModelStore::GetFile(Model)));
		return false;
	}

	// The engine's model object is made on the game thread, like any other object.
	TPromise<void> Made;
	AsyncTask(ENamedThreads::GameThread, [this, &Bytes, &Made]
	{
		if (INNERuntimeGPU* Runtime = GetEditorBackendRuntime().Get())
		{
			ModelData.Reset(NewObject<UNNEModelData>());
			ModelData->Init(TEXT("onnx"), Bytes);
			if (Runtime->CanCreateModelGPU(ModelData.Get()) == INNERuntimeGPU::ECanCreateModelGPUStatus::Ok)
			{
				LoadedModel = Runtime->CreateModelGPU(ModelData.Get());
			}
		}
		Made.SetValue();
	});
	Made.GetFuture().Wait();
	Bytes.Empty();

	// Making the instance compiles the model for the graphics card: the slow part of a first run.
	ModelInstance = LoadedModel.IsValid() ? LoadedModel->CreateModelInstanceGPU() : nullptr;
	if (!ModelInstance.IsValid())
	{
		LoadedModel.Reset();
		OutError = FText::Format(LOCTEXT("CannotLoad", "{0} could not be loaded on this machine's graphics card. The helper can run it instead."), FText::FromString(Model.Name));
		return false;
	}
	LoadedModelId = Model.Id;
	return true;
}

bool FUEcineAIEditorBackend::RunModel(const FUEcineAIModel& Model, const FImage& Picture, FUEcineAIOutput& Output)
{
	using namespace UE::NNE;

	FIntPoint InputSize;
	TArray<float> PictureTensor = UEcine::AIPictures::MakeTensor(Picture, Model.Input, InputSize);
	const int32 PixelCount = InputSize.X * InputSize.Y;

	// What the model takes, in the order it takes it.
	TArray<FTensorShape> InputShapes;
	TArray<FTensorBindingCPU> InputBindings;
	TArray<int64> Integers;
	Integers.Reserve(Model.IntegerInputs.Num());
	for (const FTensorDesc& Input : ModelInstance->GetInputTensorDescs())
	{
		if (Input.GetName() == Model.Input.Name && Input.GetDataType() == ENNETensorDataType::Float)
		{
			InputShapes.Add(FTensorShape::Make({ 1u, 3u, static_cast<uint32>(InputSize.Y), static_cast<uint32>(InputSize.X) }));
			InputBindings.Add({ PictureTensor.GetData(), static_cast<uint64>(PictureTensor.NumBytes()) });
		}
		else if (const int64* Integer = Model.IntegerInputs.Find(Input.GetName()); Integer != nullptr && Input.GetDataType() == ENNETensorDataType::Int64)
		{
			// A single number: a shape with no sides.
			InputShapes.Add(FTensorShape::Make(TConstArrayView<uint32>()));
			InputBindings.Add({ &Integers.Add_GetRef(*Integer), sizeof(int64) });
		}
		else
		{
			Output.Error = FText::Format(LOCTEXT("UnknownInput", "{0} takes \"{1}\", which UEcine's list does not describe."), FText::FromString(Model.Name), FText::FromString(Input.GetName()));
			return false;
		}
	}
	if (ModelInstance->SetInputTensorShapes(InputShapes) != IModelInstanceGPU::ESetInputTensorShapesStatus::Ok)
	{
		return false;
	}

	// What it gives back: room for the results UEcine reads, none for the others.
	FEditorBackendResult Main;
	FEditorBackendResult Mask;
	const int32 MainChannels = Model.Task == EUEcineAITask::FocalLength ? 3 : 1;
	const TConstArrayView<FTensorDesc> OutputDescs = ModelInstance->GetOutputTensorDescs();
	TArray<FTensorBindingCPU> OutputBindings;
	int32 MainIndex = INDEX_NONE;
	int32 MaskIndex = INDEX_NONE;
	for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
	{
		FTensorBindingCPU& Binding = OutputBindings.Add_GetRef({ nullptr, 0 });
		if (OutputDescs[Index].GetDataType() != ENNETensorDataType::Float)
		{
			continue;
		}
		if (OutputDescs[Index].GetName() == Model.Output)
		{
			MainIndex = Index;
			Main.Values.SetNumZeroed(PixelCount * MainChannels);
			Binding = { Main.Values.GetData(), static_cast<uint64>(Main.Values.NumBytes()) };
		}
		else if (!Model.MaskOutput.IsEmpty() && OutputDescs[Index].GetName() == Model.MaskOutput)
		{
			MaskIndex = Index;
			Mask.Values.SetNumZeroed(PixelCount);
			Binding = { Mask.Values.GetData(), static_cast<uint64>(Mask.Values.NumBytes()) };
		}
	}
	if (MainIndex == INDEX_NONE || (!Model.MaskOutput.IsEmpty() && MaskIndex == INDEX_NONE))
	{
		Output.Error = FText::Format(LOCTEXT("NoOutput", "{0} does not give the result UEcine's list names."), FText::FromString(Model.Name));
		return false;
	}

	if (ModelInstance->RunSync(InputBindings, OutputBindings) != IModelInstanceGPU::ERunSyncStatus::Ok)
	{
		return false;
	}
	Stage = EStage::Finishing;

	// A result is a grid of numbers, or of triples of numbers, with ones in front: 1 x H x W, 1 x 1 x H x W, 1 x H x W x 3.
	const TConstArrayView<FTensorShape> OutputShapes = ModelInstance->GetOutputTensorShapes();
	const auto ReadSize = [&OutputShapes, PixelCount](int32 Index, int32 Channels, FEditorBackendResult& Result)
	{
		if (!OutputShapes.IsValidIndex(Index))
		{
			return false;
		}
		TArray<uint32> Sides(OutputShapes[Index].GetData());
		if (Channels > 1 && (Sides.IsEmpty() || Sides.Pop() != static_cast<uint32>(Channels)))
		{
			return false;
		}
		Sides.RemoveAll([](uint32 Side) { return Side == 1u; });
		if (Sides.Num() != 2 || static_cast<int64>(Sides[0]) * Sides[1] > PixelCount)
		{
			return false;
		}
		Result.Size = FIntPoint(Sides[1], Sides[0]);
		Result.Values.SetNum(Result.Size.X * Result.Size.Y * Channels);
		return true;
	};
	if (!ReadSize(MainIndex, MainChannels, Main) || (MaskIndex != INDEX_NONE && (!ReadSize(MaskIndex, 1, Mask) || Mask.Size != Main.Size)))
	{
		Output.Error = FText::Format(LOCTEXT("OddOutput", "{0} gave a result of a shape UEcine does not expect."), FText::FromString(Model.Name));
		return false;
	}

	const FIntPoint PictureSize(Picture.SizeX, Picture.SizeY);
	switch (Model.Task)
	{
	case EUEcineAITask::Depth:
		Output.Picture = UEcine::AIPictures::MakeGrey16(UEcine::Vision::NearnessToGrey16(UEcine::AIPictures::Resize(Main.Values, Main.Size, PictureSize)), PictureSize);
		break;
	case EUEcineAITask::Matte:
		Output.Picture = UEcine::AIPictures::MakeGrey8(UEcine::Vision::LogitsToGrey8(UEcine::AIPictures::Resize(Main.Values, Main.Size, PictureSize)), PictureSize);
		break;
	case EUEcineAITask::FocalLength:
		{
			const TOptional<double> FieldOfView = UEcine::Vision::FieldOfViewFromPoints(Main.Values, Mask.Values, Main.Size.X, Main.Size.Y);
			if (!FieldOfView.IsSet())
			{
				Output.Error = LOCTEXT("NothingToJudge", "The picture has too little in it to judge the lens from.");
				return false;
			}
			Output.FieldOfViewDeg = FieldOfView.GetValue();
		}
		break;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
