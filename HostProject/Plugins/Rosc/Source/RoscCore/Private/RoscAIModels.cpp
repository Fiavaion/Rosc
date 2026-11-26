// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscAIModels.h"

#include "Algo/Find.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "RoscJsonEntry.h"
#include "RoscLog.h"

const TCHAR* Rosc::AI::GetTaskId(ERoscAITask Task)
{
	switch (Task)
	{
	case ERoscAITask::Depth: return TEXT("depth");
	case ERoscAITask::Matte: return TEXT("matte");
	case ERoscAITask::FocalLength: return TEXT("focal");
	}
	checkNoEntry();
	return TEXT("");
}

FIntPoint FRoscAIModelInput::GetSizeFor(int32 PictureWidth, int32 PictureHeight) const
{
	if (FixedSize != FIntPoint::ZeroValue)
	{
		return FixedSize;
	}
	check(PictureWidth > 0 && PictureHeight > 0);
	const double Scale = static_cast<double>(ShortSide) / FMath::Min(PictureWidth, PictureHeight);
	const auto Side = [this, Scale](int32 PictureSide)
	{
		return FMath::Max(MultipleOf, FMath::RoundToInt32(PictureSide * Scale / MultipleOf) * MultipleOf);
	};
	return FIntPoint(Side(PictureWidth), Side(PictureHeight));
}

FString FRoscAIModelList::GetListFile()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Rosc"));
	check(Plugin.IsValid());
	return FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("AI") / TEXT("models.json"));
}

const FRoscAIModelList& FRoscAIModelList::Get()
{
	static const FRoscAIModelList List = []
	{
		FRoscAIModelList Loaded;
		TArray<FString> Errors;
		const FString File = GetListFile();
		FString Json;
		if (FFileHelper::LoadFileToString(Json, *File))
		{
			Loaded.AddFromJson(Json, FPaths::GetCleanFilename(File), Errors);
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("%s could not be read."), *File));
		}
		for (const FString& Error : Errors)
		{
			UE_LOG(LogRosc, Warning, TEXT("AI model list: %s"), *Error);
		}
		return Loaded;
	}();
	return List;
}

FString FRoscAIModel::GetNoticeSignature() const
{
	FString Notice = FString::Join(TArray<FString>{ Licence, LicenceHolder, LicencePage, LicenceNote }, TEXT("\n"));
	for (const FRoscAITrainingSet& Set : TrainingData)
	{
		Notice += FString::Printf(TEXT("\n%s\t%s\t%s\t%s\t%d"), *Set.Name, *Set.Owner, *Set.Terms, *Set.TermsPage, static_cast<int32>(Set.Commercial));
	}
	// As UTF-8, so the owners' names are hashed letter for letter.
	const FTCHARToUTF8 Utf8(*Notice);
	return FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

bool FRoscAIModelList::AddFromJson(const FString& JsonText, const FString& SourceName, TArray<FString>& OutErrors)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("%s: not valid JSON (%s)."), *SourceName, *Reader->GetErrorMessage()));
		return false;
	}

	bool bAllAdded = true;
	FRoscJsonEntryReader RootReader(Root, SourceName, OutErrors);
	const TArray<TSharedPtr<FJsonObject>> ModelObjects = RootReader.ObjectArray(TEXT("models"), true);
	for (int32 ModelIndex = 0; ModelIndex < ModelObjects.Num(); ++ModelIndex)
	{
		const FString ModelPath = FString::Printf(TEXT("%s models[%d]"), *SourceName, ModelIndex);
		FRoscJsonEntryReader ModelReader(ModelObjects[ModelIndex], ModelPath, OutErrors);

		FRoscAIModel Model;
		Model.Id = ModelReader.RequiredString(TEXT("id"));
		Model.Name = ModelReader.RequiredString(TEXT("name"));
		Model.Licence = ModelReader.RequiredString(TEXT("licence"));
		Model.LicenceHolder = ModelReader.RequiredString(TEXT("licenceHolder"));
		Model.LicencePage = ModelReader.RequiredString(TEXT("licencePage"));
		Model.LicenceNote = ModelReader.OptionalString(TEXT("licenceNote"));

		const TArray<TSharedPtr<FJsonObject>> SetObjects = ModelReader.ObjectArray(TEXT("trainingData"), true);
		for (int32 SetIndex = 0; SetIndex < SetObjects.Num(); ++SetIndex)
		{
			FRoscJsonEntryReader SetReader(SetObjects[SetIndex], FString::Printf(TEXT("%s trainingData[%d]"), *ModelPath, SetIndex), OutErrors);
			FRoscAITrainingSet& Set = Model.TrainingData.AddDefaulted_GetRef();
			Set.Name = SetReader.RequiredString(TEXT("name"));
			Set.Owner = SetReader.RequiredString(TEXT("owner"));
			Set.Terms = SetReader.RequiredString(TEXT("terms"));
			Set.TermsPage = SetReader.OptionalString(TEXT("termsPage"));
			if (!Set.TermsPage.IsEmpty() && !Set.TermsPage.StartsWith(TEXT("https://")))
			{
				SetReader.Fail(TEXT("termsPage"), TEXT("must start with https://"));
			}
			const FString Commercial = SetReader.RequiredString(TEXT("commercial"));
			if (Commercial == TEXT("yes"))
			{
				Set.Commercial = ERoscCommercialUse::Yes;
			}
			else if (Commercial == TEXT("no"))
			{
				Set.Commercial = ERoscCommercialUse::No;
			}
			else if (Commercial == TEXT("unclear"))
			{
				Set.Commercial = ERoscCommercialUse::Unclear;
			}
			else if (!Commercial.IsEmpty())
			{
				SetReader.Fail(TEXT("commercial"), TEXT("must be yes, no or unclear"));
			}
			if (!SetReader.IsValid())
			{
				ModelReader.MarkInvalid();
			}
		}
		Model.Url = ModelReader.RequiredString(TEXT("url"));
		Model.FileName = ModelReader.RequiredString(TEXT("file"));
		Model.SizeBytes = static_cast<int64>(ModelReader.RequiredPositive(TEXT("bytes")));
		Model.Sha256 = ModelReader.RequiredString(TEXT("sha256")).ToLower();
		Model.Output = ModelReader.RequiredString(TEXT("output"));
		Model.MaskOutput = ModelReader.OptionalString(TEXT("maskOutput"));

		const FString TaskId = ModelReader.RequiredString(TEXT("task"));
		const ERoscAITask Tasks[] = { ERoscAITask::Depth, ERoscAITask::Matte, ERoscAITask::FocalLength };
		const ERoscAITask* Task = Algo::FindByPredicate(Tasks, [&TaskId](ERoscAITask Each) { return TaskId == Rosc::AI::GetTaskId(Each); });
		if (Task != nullptr)
		{
			Model.Task = *Task;
			if (Model.Task == ERoscAITask::FocalLength && Model.MaskOutput.IsEmpty())
			{
				ModelReader.Fail(TEXT("maskOutput"), TEXT("is required for a focal model"));
			}
		}
		else if (!TaskId.IsEmpty())
		{
			ModelReader.Fail(TEXT("task"), TEXT("must be depth, matte or focal"));
		}
		if (!Model.Sha256.IsEmpty() && Model.Sha256.Len() != 64)
		{
			ModelReader.Fail(TEXT("sha256"), TEXT("must be 64 hexadecimal digits"));
		}
		// The file name is joined to the models folder: it must not lead out of it.
		if (Model.FileName != FPaths::GetCleanFilename(Model.FileName))
		{
			ModelReader.Fail(TEXT("file"), TEXT("must be a file name without a folder"));
		}
		if (!Model.Url.IsEmpty() && !Model.Url.StartsWith(TEXT("https://")))
		{
			ModelReader.Fail(TEXT("url"), TEXT("must start with https://"));
		}

		if (const TSharedPtr<FJsonObject> InputObject = ModelReader.RequiredObject(TEXT("input")))
		{
			FRoscJsonEntryReader InputReader(InputObject, ModelPath + TEXT(" input"), OutErrors);
			Model.Input.Name = InputReader.RequiredString(TEXT("name"));
			if (InputReader.Has(TEXT("width")) || InputReader.Has(TEXT("height")))
			{
				Model.Input.FixedSize.X = static_cast<int32>(InputReader.RequiredPositive(TEXT("width")));
				Model.Input.FixedSize.Y = static_cast<int32>(InputReader.RequiredPositive(TEXT("height")));
			}
			else
			{
				Model.Input.ShortSide = static_cast<int32>(InputReader.RequiredPositive(TEXT("shortSide")));
				Model.Input.MultipleOf = static_cast<int32>(InputReader.OptionalAtLeast(TEXT("multipleOf"), 1.0, 1.0));
			}
			double Red = 0.0, Green = 0.0, Blue = 0.0;
			if (InputReader.RequiredTriple(TEXT("mean"), Red, Green, Blue))
			{
				Model.Input.Mean = FVector3f(Red, Green, Blue);
			}
			if (InputReader.RequiredTriple(TEXT("std"), Red, Green, Blue))
			{
				if (Red > 0.0 && Green > 0.0 && Blue > 0.0)
				{
					Model.Input.Deviation = FVector3f(Red, Green, Blue);
				}
				else
				{
					InputReader.Fail(TEXT("std"), TEXT("must be three numbers greater than zero"));
				}
			}
			if (!InputReader.IsValid())
			{
				ModelReader.MarkInvalid();
			}
		}

		const TSharedPtr<FJsonObject>* IntegerInputs = nullptr;
		if (ModelObjects[ModelIndex]->TryGetObjectField(TEXT("integerInputs"), IntegerInputs))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Each : (*IntegerInputs)->Values)
			{
				int64 Value = 0;
				if (Each.Value->TryGetNumber(Value))
				{
					Model.IntegerInputs.Add(Each.Key, Value);
				}
				else
				{
					ModelReader.Fail(TEXT("integerInputs"), TEXT("must hold only whole numbers"));
				}
			}
		}

		if (ModelReader.IsValid() && Models.ContainsByPredicate([&Model](const FRoscAIModel& Each) { return Each.Id == Model.Id; }))
		{
			ModelReader.Fail(TEXT("id"), TEXT("is already used by another model"));
		}

		if (ModelReader.IsValid())
		{
			Models.Add(MoveTemp(Model));
		}
		else
		{
			bAllAdded = false;
		}
	}
	return bAllAdded && RootReader.IsValid();
}

const FRoscAIModel* FRoscAIModelList::FindForTask(ERoscAITask Task) const
{
	return Models.FindByPredicate([Task](const FRoscAIModel& Model) { return Model.Task == Task; });
}
