// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "CineCameraActor.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "RoscAIConsent.h"
#include "RoscAISettings.h"
#include "RoscAISubsystem.h"
#include "RoscCameraEditing.h"
#include "RoscGripEditing.h"
#include "RoscRig.h"
#include "RoscShotAssistant.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The shot assistant's evals: requests a director or DOP would type, each with the set it is
 * made on and what a good plan for it has. A case is graded in code: the checks it lists are
 * made on the plan as the model wrote it, and the plan is then read against the set as the
 * panel would read it, where a step that cannot be carried out fails the case.
 *
 * The cases are a file outside the plugin, and asking the model costs money, so the run is
 * asked for on the editor's command line (see Rosc.Evals.ShotAssistant below).
 */
namespace
{
	struct FShotEvalSubject
	{
		FString Name;
		/** The middle of it, with the camera looking along X from above the origin. */
		FVector Centre = FVector::ZeroVector;
		FVector Size = FVector::ZeroVector;
	};

	struct FShotEvalCase
	{
		FString Id;
		FString Request;
		double CameraHeightCm = 160.0;
		/** The support the camera starts on. Empty for none. */
		FString SupportId;
		TArray<FShotEvalSubject> Subjects;
		TArray<TSharedPtr<FJsonValue>> Checks;
		/** How many warnings the plan may draw when it is read against the set. INDEX_NONE for any number. */
		int32 MaxWarnings = INDEX_NONE;
	};

	/** The fields each kind of step has in a written plan, with the JSON type of each, from the plan's own schema. */
	const TMap<FString, TMap<FString, FString>>& GetShotEvalStepFields()
	{
		static const TMap<FString, TMap<FString, FString>> Fields = []
		{
			TMap<FString, TMap<FString, FString>> Result;
			TSharedPtr<FJsonObject> Schema;
			verify(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Rosc::ShotPlan::GetJsonSchema()), Schema));
			const TSharedPtr<FJsonObject> Steps = Schema->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("steps"));
			for (const TSharedPtr<FJsonValue>& StepValue : Steps->GetObjectField(TEXT("items"))->GetArrayField(TEXT("anyOf")))
			{
				const TSharedPtr<FJsonObject> Properties = StepValue->AsObject()->GetObjectField(TEXT("properties"));
				TMap<FString, FString>& StepFields = Result.Add(Properties->GetObjectField(TEXT("step"))->GetArrayField(TEXT("enum"))[0]->AsString());
				for (const auto& Field : Properties->Values)
				{
					FString Type;
					Field.Value->AsObject()->TryGetStringField(TEXT("type"), Type);
					StepFields.Add(FString(Field.Key), Type);
				}
			}
			return Result;
		}();
		return Fields;
	}

	FString FormatShotEvalNumber(double Value)
	{
		return FString::Printf(TEXT("%g"), Value);
	}

	/** "min" and "max" of a check; either may be left out. */
	struct FShotEvalRange
	{
		TOptional<double> Min;
		TOptional<double> Max;

		explicit FShotEvalRange(const FJsonObject& Object)
		{
			double Value = 0.0;
			if (Object.TryGetNumberField(TEXT("min"), Value))
			{
				Min = Value;
			}
			if (Object.TryGetNumberField(TEXT("max"), Value))
			{
				Max = Value;
			}
		}

		bool Holds(double Value) const
		{
			return (!Min.IsSet() || Value >= *Min) && (!Max.IsSet() || Value <= *Max);
		}

		FString Describe() const
		{
			if (Min.IsSet() && Max.IsSet())
			{
				return *Min == *Max
					? FString::Printf(TEXT("exactly %s"), *FormatShotEvalNumber(*Min))
					: FString::Printf(TEXT("from %s to %s"), *FormatShotEvalNumber(*Min), *FormatShotEvalNumber(*Max));
			}
			return Min.IsSet() ? FString::Printf(TEXT("at least %s"), *FormatShotEvalNumber(*Min)) : FString::Printf(TEXT("at most %s"), *FormatShotEvalNumber(Max.Get(0.0)));
		}
	};

	/**
	 * What is wrong with a check as it is written. Empty when it can be made. A check is one of:
	 *   { "step"?, "count": { "min"?, "max"? } }          how many steps of that kind, or of any kind
	 *   { "step", "field", "oneOf": [text...] }           the last such step's field, whatever its case
	 *   { "step", "field", "is": true|false }             the last such step's field
	 *   { "step", "field", "range": { "min"?, "max"? } }  the last such step's field
	 *   { "step", "field", "sum": { "min"?, "max"? } }    the field added up over all such steps
	 *   { "step", "field", "largest": { "min"?, "max"? } } of all such steps, the field largest in size, its sign kept
	 */
	FString FindShotEvalCheckFault(const FJsonObject& Check)
	{
		FString StepId;
		const bool bHasStep = Check.TryGetStringField(TEXT("step"), StepId);
		const TMap<FString, FString>* Fields = bHasStep ? GetShotEvalStepFields().Find(StepId) : nullptr;
		if (bHasStep && Fields == nullptr)
		{
			return FString::Printf(TEXT("no kind of step is called \"%s\""), *StepId);
		}

		const TSharedPtr<FJsonObject>* Range = nullptr;
		const bool bCount = Check.TryGetObjectField(TEXT("count"), Range);
		FString Field;
		if (!Check.TryGetStringField(TEXT("field"), Field))
		{
			return bCount ? FString() : FString(TEXT("needs \"count\", or a \"field\" with \"oneOf\", \"is\", \"range\", \"sum\" or \"largest\""));
		}
		if (bCount)
		{
			return TEXT("has both \"count\" and \"field\"");
		}
		if (Fields == nullptr)
		{
			return TEXT("a \"field\" needs the \"step\" it is a field of");
		}
		const FString* Type = Fields->Find(Field);
		if (Type == nullptr)
		{
			return FString::Printf(TEXT("a %s step has no field \"%s\""), *StepId, *Field);
		}

		const TArray<TSharedPtr<FJsonValue>>* OneOf = nullptr;
		bool bIs = false;
		if (Check.TryGetArrayField(TEXT("oneOf"), OneOf))
		{
			return *Type == TEXT("string") ? FString() : FString::Printf(TEXT("\"oneOf\" is for text, and %s.%s is not text"), *StepId, *Field);
		}
		if (Check.TryGetBoolField(TEXT("is"), bIs))
		{
			return *Type == TEXT("boolean") ? FString() : FString::Printf(TEXT("\"is\" is for yes or no, and %s.%s is not"), *StepId, *Field);
		}
		if (Check.TryGetObjectField(TEXT("range"), Range) || Check.TryGetObjectField(TEXT("sum"), Range) || Check.TryGetObjectField(TEXT("largest"), Range))
		{
			return *Type == TEXT("number") || *Type == TEXT("integer") ? FString() : FString::Printf(TEXT("\"range\", \"sum\" and \"largest\" are for numbers, and %s.%s is not one"), *StepId, *Field);
		}
		return TEXT("a \"field\" needs \"oneOf\", \"is\", \"range\", \"sum\" or \"largest\"");
	}

	/**
	 * Reads the cases file. Everything wrong with it is found here, before anything is asked of
	 * a model: each fault names its case.
	 */
	TArray<FShotEvalCase> ReadShotEvalCases(const FString& JsonText, TArray<FString>& OutFaults)
	{
		TArray<FShotEvalCase> Cases;
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* CaseValues = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("cases"), CaseValues))
		{
			OutFaults.Add(TEXT("The cases file is not JSON with a \"cases\" list."));
			return Cases;
		}

		for (int32 CaseIndex = 0; CaseIndex < CaseValues->Num(); ++CaseIndex)
		{
			const TSharedPtr<FJsonObject> Object = (*CaseValues)[CaseIndex]->AsObject();
			FShotEvalCase Case;
			if (!Object.IsValid() || !Object->TryGetStringField(TEXT("id"), Case.Id) || Case.Id.IsEmpty())
			{
				OutFaults.Add(FString::Printf(TEXT("Case %d has no \"id\"."), CaseIndex + 1));
				continue;
			}
			const auto Fault = [&OutFaults, &Case](const FString& What) { OutFaults.Add(FString::Printf(TEXT("Case \"%s\": %s."), *Case.Id, *What)); };
			if (Cases.ContainsByPredicate([&Case](const FShotEvalCase& Other) { return Other.Id == Case.Id; }))
			{
				Fault(TEXT("another case has the same id"));
			}
			if (!Object->TryGetStringField(TEXT("request"), Case.Request) || Case.Request.TrimStartAndEnd().IsEmpty())
			{
				Fault(TEXT("it has no \"request\""));
			}
			double MaxWarnings = 0.0;
			if (Object->TryGetNumberField(TEXT("maxWarnings"), MaxWarnings))
			{
				Case.MaxWarnings = FMath::RoundToInt32(MaxWarnings);
			}

			const TSharedPtr<FJsonObject>* Set = nullptr;
			if (Object->TryGetObjectField(TEXT("set"), Set))
			{
				double CameraHeightM = 0.0;
				if ((*Set)->TryGetNumberField(TEXT("cameraHeightM"), CameraHeightM))
				{
					Case.CameraHeightCm = CameraHeightM * 100.0;
				}
				if ((*Set)->TryGetStringField(TEXT("supportId"), Case.SupportId) && Rosc::Rig::FindPreset(Case.SupportId) == nullptr)
				{
					Fault(FString::Printf(TEXT("there is no support \"%s\" for its set"), *Case.SupportId));
				}
				const TArray<TSharedPtr<FJsonValue>>* SubjectValues = nullptr;
				if ((*Set)->TryGetArrayField(TEXT("subjects"), SubjectValues))
				{
					for (const TSharedPtr<FJsonValue>& SubjectValue : *SubjectValues)
					{
						const TSharedPtr<FJsonObject> SubjectObject = SubjectValue->AsObject();
						FShotEvalSubject Subject;
						double AheadM = 0.0;
						double RightM = 0.0;
						double HeightM = 0.0;
						double WidthM = 0.0;
						if (!SubjectObject.IsValid() || !SubjectObject->TryGetStringField(TEXT("name"), Subject.Name) || Subject.Name.IsEmpty()
							|| !SubjectObject->TryGetNumberField(TEXT("aheadM"), AheadM) || !SubjectObject->TryGetNumberField(TEXT("rightM"), RightM)
							|| !SubjectObject->TryGetNumberField(TEXT("heightM"), HeightM) || !SubjectObject->TryGetNumberField(TEXT("widthM"), WidthM)
							|| HeightM <= 0.0 || WidthM <= 0.0)
						{
							Fault(TEXT("a subject of its set needs \"name\", \"aheadM\", \"rightM\", and \"heightM\" and \"widthM\" above zero"));
							continue;
						}
						// It stands on the floor.
						Subject.Centre = FVector(AheadM * 100.0, RightM * 100.0, HeightM * 50.0);
						Subject.Size = FVector(WidthM * 100.0, WidthM * 100.0, HeightM * 100.0);
						Case.Subjects.Add(Subject);
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* CheckValues = nullptr;
			if (!Object->TryGetArrayField(TEXT("checks"), CheckValues) || CheckValues->IsEmpty())
			{
				Fault(TEXT("it has no \"checks\", so any plan would pass it"));
			}
			else
			{
				Case.Checks = *CheckValues;
				for (int32 CheckIndex = 0; CheckIndex < Case.Checks.Num(); ++CheckIndex)
				{
					const TSharedPtr<FJsonObject> Check = Case.Checks[CheckIndex]->AsObject();
					const FString CheckFault = Check.IsValid() ? FindShotEvalCheckFault(*Check) : FString(TEXT("is not an object"));
					if (!CheckFault.IsEmpty())
					{
						Fault(FString::Printf(TEXT("check %d %s"), CheckIndex + 1, *CheckFault));
					}
				}
			}
			Cases.Add(MoveTemp(Case));
		}
		return Cases;
	}

	/** Makes a case's checks on a plan as the model wrote it. Each one that does not hold is told; none means the plan passes them. */
	TArray<FString> GradeShotPlan(const TArray<TSharedPtr<FJsonValue>>& Checks, const FString& PlanJson)
	{
		TArray<FString> Failures;
		TSharedPtr<FJsonObject> Plan;
		const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PlanJson), Plan) || !Plan.IsValid() || !Plan->TryGetArrayField(TEXT("steps"), Steps))
		{
			Failures.Add(TEXT("the plan is not JSON with steps"));
			return Failures;
		}

		for (const TSharedPtr<FJsonValue>& CheckValue : Checks)
		{
			const FJsonObject& Check = *CheckValue->AsObject();
			FString StepId;
			Check.TryGetStringField(TEXT("step"), StepId);
			TArray<TSharedPtr<FJsonObject>> Matching;
			for (const TSharedPtr<FJsonValue>& StepValue : *Steps)
			{
				const TSharedPtr<FJsonObject> Step = StepValue->AsObject();
				if (StepId.IsEmpty() || Step->GetStringField(TEXT("step")) == StepId)
				{
					Matching.Add(Step);
				}
			}

			const TSharedPtr<FJsonObject>* RangeObject = nullptr;
			if (Check.TryGetObjectField(TEXT("count"), RangeObject))
			{
				const FShotEvalRange Range(**RangeObject);
				if (!Range.Holds(Matching.Num()))
				{
					Failures.Add(FString::Printf(TEXT("%s: %d in the plan, expected %s"), StepId.IsEmpty() ? TEXT("steps") : *StepId, Matching.Num(), *Range.Describe()));
				}
				continue;
			}

			const FString Field = Check.GetStringField(TEXT("field"));
			const FString Name = StepId + TEXT(".") + Field;
			if (Check.TryGetObjectField(TEXT("sum"), RangeObject))
			{
				double Sum = 0.0;
				for (const TSharedPtr<FJsonObject>& Step : Matching)
				{
					Sum += Step->GetNumberField(Field);
				}
				const FShotEvalRange Range(**RangeObject);
				if (!Range.Holds(Sum))
				{
					Failures.Add(FString::Printf(TEXT("%s adds up to %s over the plan, expected %s"), *Name, *FormatShotEvalNumber(Sum), *Range.Describe()));
				}
				continue;
			}
			if (Matching.IsEmpty())
			{
				Failures.Add(FString::Printf(TEXT("%s: the plan has no %s step"), *Name, *StepId));
				continue;
			}
			if (Check.TryGetObjectField(TEXT("largest"), RangeObject))
			{
				// A move split over marks, such as a settle onto the subject and then the move asked
				// for, is measured by its biggest mark.
				double Largest = 0.0;
				for (const TSharedPtr<FJsonObject>& Step : Matching)
				{
					const double Value = Step->GetNumberField(Field);
					Largest = FMath::Abs(Value) > FMath::Abs(Largest) ? Value : Largest;
				}
				const FShotEvalRange Range(**RangeObject);
				if (!Range.Holds(Largest))
				{
					Failures.Add(FString::Printf(TEXT("%s is %s at its largest, expected %s"), *Name, *FormatShotEvalNumber(Largest), *Range.Describe()));
				}
				continue;
			}

			const FJsonObject& Last = *Matching.Last();
			const TArray<TSharedPtr<FJsonValue>>* OneOf = nullptr;
			bool bExpected = false;
			if (Check.TryGetArrayField(TEXT("oneOf"), OneOf))
			{
				const FString Value = Last.GetStringField(Field);
				TArray<FString> Allowed;
				for (const TSharedPtr<FJsonValue>& AllowedValue : *OneOf)
				{
					Allowed.Add(AllowedValue->AsString());
				}
				if (!Allowed.ContainsByPredicate([&Value](const FString& Each) { return Each.Equals(Value, ESearchCase::IgnoreCase); }))
				{
					Failures.Add(FString::Printf(TEXT("%s is \"%s\", expected one of: %s"), *Name, *Value, *FString::Join(Allowed, TEXT(", "))));
				}
			}
			else if (Check.TryGetBoolField(TEXT("is"), bExpected))
			{
				if (Last.GetBoolField(Field) != bExpected)
				{
					Failures.Add(FString::Printf(TEXT("%s is %s, expected %s"), *Name, bExpected ? TEXT("false") : TEXT("true"), bExpected ? TEXT("true") : TEXT("false")));
				}
			}
			else
			{
				const FShotEvalRange Range(*Check.GetObjectField(TEXT("range")));
				const double Value = Last.GetNumberField(Field);
				if (!Range.Holds(Value))
				{
					Failures.Add(FString::Printf(TEXT("%s is %s, expected %s"), *Name, *FormatShotEvalNumber(Value), *Range.Describe()));
				}
			}
		}
		return Failures;
	}

	/** How one case went. */
	struct FShotEvalOutcome
	{
		FString Id;
		FString Request;
		/** "pass", "fail", or "error" when no plan came back to grade. */
		FString Status;
		/** Why no plan came back. */
		FString Error;
		TArray<FString> Failures;
		/** What the gear cannot do about the plan, as the panel would tell the user. */
		TArray<FString> Warnings;
		FString SetDescription;
		FString PlanJson;
		/** InputTokens is the whole request, the kept part too, as FRoscShotPlanResult counts it. */
		int32 InputTokens = 0;
		int32 OutputTokens = 0;
		int32 CacheWriteTokens = 0;
		int32 CacheReadTokens = 0;
		/** From the request going out to its answer coming back. */
		double Seconds = 0.0;
	};

	/** Asks for a plan as URoscAISubsystem::PlanShot does. */
	using FAskForShotPlan = TFunction<bool(const FString& Words, const FString& SetDescription, FText& OutError, TFunction<void(const FRoscShotPlanResult&)> OnDone)>;

	/** A run of the cases, one at a time, over as many editor frames as the answers take. */
	struct FShotEvalRun
	{
		TArray<FShotEvalCase> Cases;
		FAskForShotPlan Ask;
		/** The model asked and the effort it was asked at, for the report. */
		FString Model;
		FString Effort;
		FString ReportFile;
		/** Called once, when every case has been through. */
		TFunction<void(const FShotEvalRun&)> Then;

		TArray<FShotEvalOutcome> Outcomes;
		bool bWaiting = false;
		double AskedAt = 0.0;
		TOptional<FRoscShotPlanResult> Answer;
		TWeakObjectPtr<ACineCameraActor> Camera;
		TArray<TWeakObjectPtr<AActor>> SetActors;

		int32 Count(const TCHAR* Status) const
		{
			return Outcomes.FilterByPredicate([Status](const FShotEvalOutcome& Outcome) { return Outcome.Status == Status; }).Num();
		}

		FString DescribeScore() const
		{
			int32 InputTokens = 0;
			int32 OutputTokens = 0;
			for (const FShotEvalOutcome& Outcome : Outcomes)
			{
				InputTokens += Outcome.InputTokens;
				OutputTokens += Outcome.OutputTokens;
			}
			return FString::Printf(TEXT("%d of %d cases passed, %d failed, %d came back with no plan. Tokens: %d in, %d out."),
				Count(TEXT("pass")), Outcomes.Num(), Count(TEXT("fail")), Count(TEXT("error")), InputTokens, OutputTokens);
		}
	};

	/** Stands a case's set up in the level: a floor, the camera looking along X, the things in front of it. */
	void BuildShotEvalSet(UWorld* World, const FShotEvalCase& Case, FShotEvalRun& Run)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const auto AddBox = [World, Cube, &Run](const FString& Name, const FVector& Centre, const FVector& Size)
		{
			AStaticMeshActor* Box = World->SpawnActor<AStaticMeshActor>(Centre, FRotator::ZeroRotator);
			Box->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Box->SetActorScale3D(Size / 100.0);
			Box->SetActorLabel(Name);
			Run.SetActors.Add(Box);
		};
		// Too large to be a subject: the floor is the set, not something in it.
		AddBox(TEXT("Floor"), FVector(0.0, 0.0, -5.0), FVector(20000.0, 20000.0, 10.0));
		for (const FShotEvalSubject& Subject : Case.Subjects)
		{
			AddBox(Subject.Name, Subject.Centre, Subject.Size);
		}

		ACineCameraActor* Camera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, Case.CameraHeightCm)));
		Camera->SetActorLabel(TEXT("A Cam"));
		if (!Case.SupportId.IsEmpty())
		{
			Rosc::GripEditing::PutCameraOnRig(Camera, *Rosc::Rig::FindPreset(Case.SupportId));
		}
		Run.Camera = Camera;
	}

	void ClearShotEvalSet(UWorld* World, FShotEvalRun& Run)
	{
		// A new camera is selected, and the next one cannot be while a destroyed one still is.
		GEditor->SelectNone(false, true, false);
		if (ACineCameraActor* Camera = Run.Camera.Get())
		{
			if (ARoscRig* Rig = Rosc::GripEditing::FindRig(Camera))
			{
				World->EditorDestroyActor(Rig, false);
			}
			World->EditorDestroyActor(Camera, false);
		}
		for (const TWeakObjectPtr<AActor>& Actor : Run.SetActors)
		{
			if (Actor.IsValid())
			{
				World->EditorDestroyActor(Actor.Get(), false);
			}
		}
		Run.Camera.Reset();
		Run.SetActors.Reset();
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc shot assistant evals")));
	}

	TArray<TSharedPtr<FJsonValue>> MakeShotEvalTextValues(const TArray<FString>& Texts)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Text : Texts)
		{
			Values.Add(MakeShared<FJsonValueString>(Text));
		}
		return Values;
	}

	/** Writes the run so far. Written after every case, so that a run that is cut short keeps what it paid for. */
	void WriteShotEvalReport(const FShotEvalRun& Run)
	{
		TArray<TSharedPtr<FJsonValue>> CaseValues;
		int32 InputTokens = 0;
		int32 OutputTokens = 0;
		int32 CacheWriteTokens = 0;
		int32 CacheReadTokens = 0;
		for (const FShotEvalOutcome& Outcome : Run.Outcomes)
		{
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("id"), Outcome.Id);
			Object->SetStringField(TEXT("request"), Outcome.Request);
			Object->SetStringField(TEXT("status"), Outcome.Status);
			if (!Outcome.Error.IsEmpty())
			{
				Object->SetStringField(TEXT("error"), Outcome.Error);
			}
			Object->SetArrayField(TEXT("failures"), MakeShotEvalTextValues(Outcome.Failures));
			Object->SetArrayField(TEXT("warnings"), MakeShotEvalTextValues(Outcome.Warnings));
			Object->SetNumberField(TEXT("inputTokens"), Outcome.InputTokens);
			Object->SetNumberField(TEXT("outputTokens"), Outcome.OutputTokens);
			Object->SetNumberField(TEXT("cacheWriteTokens"), Outcome.CacheWriteTokens);
			Object->SetNumberField(TEXT("cacheReadTokens"), Outcome.CacheReadTokens);
			Object->SetNumberField(TEXT("seconds"), FMath::RoundToDouble(Outcome.Seconds * 100.0) / 100.0);
			TSharedPtr<FJsonObject> Parsed;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Outcome.PlanJson), Parsed) && Parsed.IsValid())
			{
				Object->SetObjectField(TEXT("plan"), Parsed);
			}
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Outcome.SetDescription), Parsed) && Parsed.IsValid())
			{
				Object->SetObjectField(TEXT("set"), Parsed);
			}
			CaseValues.Add(MakeShared<FJsonValueObject>(Object));
			InputTokens += Outcome.InputTokens;
			OutputTokens += Outcome.OutputTokens;
			CacheWriteTokens += Outcome.CacheWriteTokens;
			CacheReadTokens += Outcome.CacheReadTokens;
		}

		const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("model"), Run.Model);
		Report->SetStringField(TEXT("effort"), Run.Effort);
		Report->SetStringField(TEXT("written"), FDateTime::UtcNow().ToIso8601());
		Report->SetNumberField(TEXT("cases"), Run.Cases.Num());
		Report->SetNumberField(TEXT("passed"), Run.Count(TEXT("pass")));
		Report->SetNumberField(TEXT("failed"), Run.Count(TEXT("fail")));
		Report->SetNumberField(TEXT("errors"), Run.Count(TEXT("error")));
		Report->SetNumberField(TEXT("inputTokens"), InputTokens);
		Report->SetNumberField(TEXT("outputTokens"), OutputTokens);
		Report->SetNumberField(TEXT("cacheWriteTokens"), CacheWriteTokens);
		Report->SetNumberField(TEXT("cacheReadTokens"), CacheReadTokens);
		Report->SetArrayField(TEXT("results"), CaseValues);

		FString Text;
		FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
		FFileHelper::SaveStringToFile(Text, *Run.ReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	/** Grades the answer to the case whose set is standing. */
	void GradeShotEvalAnswer(FShotEvalRun& Run, const FShotEvalCase& Case, const FRoscShotPlanResult& Answer)
	{
		FShotEvalOutcome& Outcome = Run.Outcomes.Last();
		Outcome.InputTokens = Answer.InputTokens;
		Outcome.OutputTokens = Answer.OutputTokens;
		Outcome.CacheWriteTokens = Answer.CacheWriteTokens;
		Outcome.CacheReadTokens = Answer.CacheReadTokens;
		Outcome.Seconds = FPlatformTime::Seconds() - Run.AskedAt;
		Outcome.PlanJson = Answer.PlanJson;
		if (!Answer.bSuccess)
		{
			// A request that did not come back with a plan is not a plan that was graded and found wanting.
			Outcome.Status = TEXT("error");
			Outcome.Error = Answer.Error.ToString();
			return;
		}

		Outcome.Failures = GradeShotPlan(Case.Checks, Answer.PlanJson);
		const TArray<Rosc::ShotAssistant::FPlanLine> Lines = Rosc::ShotAssistant::Review(Answer.Plan, Run.Camera.Get());
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (!Lines[Index].Problem.IsEmpty())
			{
				Outcome.Failures.Add(FString::Printf(TEXT("step %d cannot be carried out: %s"), Index + 1, *Lines[Index].Problem.ToString()));
			}
			for (const FText& Warning : Lines[Index].Warnings)
			{
				Outcome.Warnings.Add(FString::Printf(TEXT("step %d: %s"), Index + 1, *Warning.ToString()));
			}
		}
		if (Case.MaxWarnings != INDEX_NONE && Outcome.Warnings.Num() > Case.MaxWarnings)
		{
			Outcome.Failures.Add(FString::Printf(TEXT("the plan draws %d warnings, expected at most %d"), Outcome.Warnings.Num(), Case.MaxWarnings));
		}
		Outcome.Status = Outcome.Failures.IsEmpty() ? TEXT("pass") : TEXT("fail");
	}

	/**
	 * Takes the run one step on: grades an answer that has come, or asks for the next case's.
	 * @return true when every case has been through.
	 */
	bool StepShotEvalRun(const TSharedRef<FShotEvalRun>& Run)
	{
		if (Run->bWaiting)
		{
			return false;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (Run->Answer.IsSet())
		{
			GradeShotEvalAnswer(*Run, Run->Cases[Run->Outcomes.Num() - 1], *Run->Answer);
			Run->Answer.Reset();
			ClearShotEvalSet(World, *Run);
			WriteShotEvalReport(*Run);
			return false;
		}
		if (Run->Outcomes.Num() == Run->Cases.Num())
		{
			Run->Then(*Run);
			return true;
		}

		const FShotEvalCase& Case = Run->Cases[Run->Outcomes.Num()];
		BuildShotEvalSet(World, Case, *Run);
		FShotEvalOutcome& Outcome = Run->Outcomes.AddDefaulted_GetRef();
		Outcome.Id = Case.Id;
		Outcome.Request = Case.Request;
		Outcome.SetDescription = Rosc::ShotAssistant::DescribeSet(Run->Camera.Get());

		Run->bWaiting = true;
		Run->AskedAt = FPlatformTime::Seconds();
		FText Error;
		const bool bAsked = Run->Ask(Case.Request, Outcome.SetDescription, Error, [WeakRun = Run.ToWeakPtr()](const FRoscShotPlanResult& Result)
		{
			if (const TSharedPtr<FShotEvalRun> Pinned = WeakRun.Pin())
			{
				Pinned->Answer = Result;
				Pinned->bWaiting = false;
			}
		});
		if (!bAsked)
		{
			FRoscShotPlanResult NotAsked;
			NotAsked.Error = Error;
			Run->Answer = NotAsked;
			Run->bWaiting = false;
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRunShotEvalCommand, TSharedRef<FShotEvalRun>, Run);

	bool FRunShotEvalCommand::Update()
	{
		return StepShotEvalRun(Run);
	}

	FString GetShotEvalReportFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Rosc/Evals"));
	}

	int32 CountShotEvalActors(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

/**
 * The evals' own workings, with a stand-in for the model: a plan that should pass does, an
 * empty plan and a wrong one fail for the reasons they should, a request that is not answered
 * is counted apart, and a cases file with mistakes in it is refused before anything is asked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscShotEvalHarnessTest, "Rosc.Editor.ShotEval.Harness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscShotEvalHarnessTest::RunTest(const FString& Parameters)
{
	// A cases file with a mistake of every kind.
	{
		TArray<FString> Faults;
		ReadShotEvalCases(TEXT("not json"), Faults);
		TestEqual(TEXT("A file that is not JSON is refused"), Faults.Num(), 1);

		Faults.Reset();
		const TArray<FShotEvalCase> Read = ReadShotEvalCases(TEXT(R"({ "cases": [
			{ "request": "No id" },
			{ "id": "a", "request": "", "checks": [ { "count": { "max": 0 } } ] },
			{ "id": "a", "request": "Twice", "set": { "supportId": "hoverboard", "subjects": [ { "name": "Anna", "aheadM": 3 } ] }, "checks": [
				{ "step": "teleport", "count": { "min": 1 } },
				{ "step": "choose_lens" },
				{ "step": "choose_lens", "field": "aperture", "range": { "min": 1 } },
				{ "step": "choose_lens", "field": "lensKitId", "range": { "min": 1 } },
				{ "step": "choose_lens", "field": "focalLengthMm", "oneOf": [ "50" ] },
				{ "step": "focus_on", "field": "subject", "is": true },
				{ "field": "subject", "oneOf": [ "Anna" ] },
				{ "step": "focus_on", "field": "follow", "count": { "min": 1 } },
				{ "step": "focus_on", "field": "follow" } ] },
			{ "id": "b", "request": "No checks" } ] })"), Faults);
		const FString All = FString::Join(Faults, TEXT("\n"));
		AddInfo(All);
		TestEqual(TEXT("Cases with an id are read, so that all their faults are told at once"), Read.Num(), 3);
		TestEqual(TEXT("Every fault is told"), Faults.Num(), 15);
		for (const TCHAR* Expected : {
			TEXT("Case 1 has no \"id\"."),
			TEXT("Case \"a\": it has no \"request\"."),
			TEXT("Case \"a\": another case has the same id."),
			TEXT("Case \"a\": there is no support \"hoverboard\" for its set."),
			TEXT("Case \"a\": a subject of its set needs"),
			TEXT("Case \"a\": check 1 no kind of step is called \"teleport\"."),
			TEXT("Case \"a\": check 2 needs \"count\", or a \"field\""),
			TEXT("Case \"a\": check 3 a choose_lens step has no field \"aperture\"."),
			TEXT("Case \"a\": check 4 \"range\", \"sum\" and \"largest\" are for numbers, and choose_lens.lensKitId is not one."),
			TEXT("Case \"a\": check 5 \"oneOf\" is for text, and choose_lens.focalLengthMm is not text."),
			TEXT("Case \"a\": check 6 \"is\" is for yes or no, and focus_on.subject is not."),
			TEXT("Case \"a\": check 7 a \"field\" needs the \"step\" it is a field of."),
			TEXT("Case \"a\": check 8 has both \"count\" and \"field\"."),
			TEXT("Case \"a\": check 9 a \"field\" needs \"oneOf\", \"is\", \"range\", \"sum\" or \"largest\"."),
			TEXT("Case \"b\": it has no \"checks\", so any plan would pass it.") })
		{
			TestTrue(Expected, All.Contains(Expected));
		}
	}

	// Four cases that ask the same of a plan, and a stand-in model that answers each differently.
	const FString Checks = TEXT(R"("set": { "cameraHeightM": 1.5, "subjects": [ { "name": "Anna", "aheadM": 6, "rightM": 0.8, "heightM": 1.8, "widthM": 0.5 } ] },
		"maxWarnings": 0, "checks": [
			{ "count": { "min": 3, "max": 6 } },
			{ "step": "put_on_support", "count": { "min": 1, "max": 1 } },
			{ "step": "put_on_support", "field": "supportId", "oneOf": [ "crane-22ft", "crane-30ft", "crane-50ft" ] },
			{ "step": "move_to_mark", "field": "boomDeg", "sum": { "min": 5 } },
			{ "step": "move_to_mark", "field": "extensionM", "largest": { "min": 0.5, "max": 2 } },
			{ "step": "move_to_mark", "field": "durationSeconds", "range": { "min": 6, "max": 10 } },
			{ "step": "focus_on", "field": "subject", "oneOf": [ "anna" ] },
			{ "step": "focus_on", "field": "follow", "is": true },
			{ "step": "bake_move", "count": { "max": 0 } } ])");
	TArray<FString> Faults;
	const TArray<FShotEvalCase> Cases = ReadShotEvalCases(FString::Printf(TEXT(R"({ "cases": [
		{ "id": "good", "request": "good", %s },
		{ "id": "empty", "request": "empty", %s },
		{ "id": "wrong", "request": "wrong", %s },
		{ "id": "unanswered", "request": "unanswered", %s } ] })"), *Checks, *Checks, *Checks, *Checks), Faults);
	if (!TestEqual(*FString::Printf(TEXT("The test's cases are read (%s)"), *FString::Join(Faults, TEXT(" "))), Cases.Num(), 4) || !TestTrue(TEXT("With no faults"), Faults.IsEmpty()))
	{
		return false;
	}
	TestEqual(TEXT("A subject stands on the floor"), Cases[0].Subjects[0].Centre, FVector(600.0, 80.0, 90.0));

	const TSharedRef<FShotEvalRun> Run = MakeShared<FShotEvalRun>();
	Run->Cases = Cases;
	Run->Model = TEXT("stand-in");
	Run->ReportFile = GetShotEvalReportFolder() / TEXT("harness-test.json");
	const int32 ActorsBefore = CountShotEvalActors(GEditor->GetEditorWorldContext().World());
	Run->Ask = [this](const FString& Words, const FString& SetDescription, FText& OutError, TFunction<void(const FRoscShotPlanResult&)> OnDone)
	{
		TestTrue(TEXT("The model is told of the case's set as it stands in the level"), SetDescription.Contains(TEXT("\"name\":\"Anna\"")) && SetDescription.Contains(TEXT("\"aheadM\":6")));
		if (Words == TEXT("unanswered"))
		{
			OutError = FText::FromString(TEXT("The stand-in has no key."));
			return false;
		}
		FRoscShotPlanResult Result;
		Result.InputTokens = 1000;
		Result.OutputTokens = 200;
		Result.PlanJson = Words == TEXT("good")
			? TEXT(R"({ "summary": "Up on the crane to Anna.", "steps": [
				{ "step": "put_on_support", "supportId": "crane-30ft" },
				{ "step": "move_to_mark", "travelM": 0, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 4, "extensionM": 0,
				  "panDeg": 0, "tiltDeg": -5, "rollDeg": 0, "durationSeconds": 20, "easeIn": 0.25, "easeOut": 0.25 },
				{ "step": "move_to_mark", "travelM": 0, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 12, "extensionM": 1,
				  "panDeg": 0, "tiltDeg": -5, "rollDeg": 0, "durationSeconds": 8, "easeIn": 0.25, "easeOut": 0.25 },
				{ "step": "focus_on", "subject": "Anna", "follow": true } ] })")
			: Words == TEXT("empty")
			? TEXT(R"({ "summary": "Nothing can be done.", "steps": [] })")
			: TEXT(R"({ "summary": "On the dolly, fast.", "steps": [
				{ "step": "put_on_support", "supportId": "dolly" },
				{ "step": "move_to_mark", "travelM": 30, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 0, "extensionM": 0,
				  "panDeg": 0, "tiltDeg": 0, "rollDeg": 0, "durationSeconds": 2, "easeIn": 0.25, "easeOut": 0.25 },
				{ "step": "focus_on", "subject": "Ben", "follow": false },
				{ "step": "bake_move" } ] })");
		FString Fault;
		Result.bSuccess = Rosc::ShotPlan::Parse(Result.PlanJson, Result.Plan, Fault);
		TestTrue(*FString::Printf(TEXT("The stand-in's plan is a plan (%s)"), *Fault), Result.bSuccess);
		if (Words == TEXT("good"))
		{
			// As the model does: the answer comes in a later frame.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Result, OnDone](float) { OnDone(Result); return false; }));
		}
		else
		{
			OnDone(Result);
		}
		return true;
	};
	Run->Then = [this, ActorsBefore](const FShotEvalRun& Done)
	{
		ON_SCOPE_EXIT { IFileManager::Get().Delete(*Done.ReportFile); };
		for (const FShotEvalOutcome& Outcome : Done.Outcomes)
		{
			AddInfo(FString::Printf(TEXT("%s: %s %s%s"), *Outcome.Id, *Outcome.Status, *Outcome.Error, *FString::Join(Outcome.Failures, TEXT(" | "))));
		}
		if (!TestEqual(TEXT("Every case went through"), Done.Outcomes.Num(), 4))
		{
			return;
		}
		const FShotEvalOutcome& Good = Done.Outcomes[0];
		TestEqual(*FString::Printf(TEXT("A plan that has what is asked passes (%s)"), *FString::Join(Good.Failures, TEXT(" | "))), *Good.Status, TEXT("pass"));
		TestEqual(TEXT("Its tokens are counted"), Good.InputTokens, 1000);

		const FShotEvalOutcome& Empty = Done.Outcomes[1];
		TestEqual(TEXT("A plan with nothing in it fails"), *Empty.Status, TEXT("fail"));
		TestEqual(TEXT("On every check that asks for something"), Empty.Failures.Num(), 8);
		TestTrue(TEXT("With the count it has"), Empty.Failures.Contains(TEXT("steps: 0 in the plan, expected from 3 to 6")));
		TestTrue(TEXT("And the step it lacks"), Empty.Failures.Contains(TEXT("focus_on.subject: the plan has no focus_on step")));
		TestTrue(TEXT("A sum over no steps is zero"), Empty.Failures.Contains(TEXT("move_to_mark.boomDeg adds up to 0 over the plan, expected at least 5")));

		const FShotEvalOutcome& Wrong = Done.Outcomes[2];
		TestEqual(TEXT("A wrong plan fails"), *Wrong.Status, TEXT("fail"));
		for (const TCHAR* Expected : {
			TEXT("put_on_support.supportId is \"dolly\", expected one of: crane-22ft, crane-30ft, crane-50ft"),
			TEXT("move_to_mark.boomDeg adds up to 0 over the plan, expected at least 5"),
			TEXT("move_to_mark.extensionM is 0 at its largest, expected from 0.5 to 2"),
			TEXT("move_to_mark.durationSeconds is 2, expected from 6 to 10"),
			TEXT("focus_on.subject is \"Ben\", expected one of: anna"),
			TEXT("focus_on.follow is false, expected true"),
			TEXT("bake_move: 1 in the plan, expected at most 0"),
			TEXT("step 3 cannot be carried out: Nothing near the camera is called \"Ben\".") })
		{
			TestTrue(Expected, Wrong.Failures.Contains(Expected));
		}
		TestTrue(TEXT("What the gear cannot do about it is kept, as the panel would say it"), Wrong.Warnings.Num() >= 1 && Wrong.Warnings[0].StartsWith(TEXT("step 2: ")));
		TestTrue(TEXT("And more of that than the case allows fails it too"), Wrong.Failures.ContainsByPredicate([](const FString& Failure) { return Failure.Contains(TEXT("warnings, expected at most 0")); }));

		const FShotEvalOutcome& Unanswered = Done.Outcomes[3];
		TestEqual(TEXT("A request that is not answered is an error, not a failed plan"), *Unanswered.Status, TEXT("error"));
		TestEqual(TEXT("With the reason"), *Unanswered.Error, TEXT("The stand-in has no key."));
		TestEqual(TEXT("The score counts each apart"), *Done.DescribeScore(), TEXT("1 of 4 cases passed, 2 failed, 1 came back with no plan. Tokens: 3000 in, 600 out."));

		FString ReportText;
		TSharedPtr<FJsonObject> Report;
		if (TestTrue(TEXT("The report is written"), FFileHelper::LoadFileToString(ReportText, *Done.ReportFile))
			&& TestTrue(TEXT("As JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ReportText), Report) && Report.IsValid()))
		{
			TestEqual(TEXT("With the model"), *Report->GetStringField(TEXT("model")), TEXT("stand-in"));
			TestEqual(TEXT("The passes"), Report->GetIntegerField(TEXT("passed")), 1);
			TestEqual(TEXT("The failures"), Report->GetIntegerField(TEXT("failed")), 2);
			TestEqual(TEXT("The errors"), Report->GetIntegerField(TEXT("errors")), 1);
			TestEqual(TEXT("The tokens"), Report->GetIntegerField(TEXT("inputTokens")), 3000);
			const TArray<TSharedPtr<FJsonValue>>& Results = Report->GetArrayField(TEXT("results"));
			if (TestEqual(TEXT("And every case"), Results.Num(), 4))
			{
				const FJsonObject& First = *Results[0]->AsObject();
				TestTrue(TEXT("Each with its plan"), First.GetObjectField(TEXT("plan"))->GetArrayField(TEXT("steps")).Num() == 4);
				TestTrue(TEXT("And the set the model was told of"), First.GetObjectField(TEXT("set"))->HasField(TEXT("camera")));
			}
		}
		TestEqual(TEXT("Every set is cleared away again"), CountShotEvalActors(GEditor->GetEditorWorldContext().World()), ActorsBefore);
	};
	ADD_LATENT_AUTOMATION_COMMAND(FRunShotEvalCommand(Run));
	return true;
}

/**
 * The shot assistant's evals. Asked for on the editor's command line:
 *   -RoscEvals=<cases file>   reads the cases and stands up each one's set; nothing is sent.
 *   -RoscEvalsLive            also asks the model in the user's settings for each case's plan.
 *   -RoscEvalsModel=<id>      asks that model instead, for this run only.
 *   -RoscEvalsEffort=<effort> default, low, medium or high, for this run only.
 * The live run uses the user's own key and what they have agreed to send, and it is billed to
 * them. Its score is a measurement, written to Saved/Rosc/Evals; the test fails only when
 * the run itself cannot be made.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscShotAssistantEvalsTest, "Rosc.Evals.ShotAssistant", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscShotAssistantEvalsTest::RunTest(const FString& Parameters)
{
	FString CasesFile;
	if (!FParse::Value(FCommandLine::Get(), TEXT("RoscEvals="), CasesFile))
	{
		AddInfo(TEXT("Skipped: the evals are asked for with -RoscEvals=<cases file> on the command line."));
		return true;
	}
	FString CasesText;
	if (!TestTrue(*FString::Printf(TEXT("The cases file %s is read"), *CasesFile), FFileHelper::LoadFileToString(CasesText, *CasesFile)))
	{
		return false;
	}
	TArray<FString> Faults;
	const TArray<FShotEvalCase> Cases = ReadShotEvalCases(CasesText, Faults);
	for (const FString& Fault : Faults)
	{
		AddError(Fault);
	}
	if (!Faults.IsEmpty() || !TestTrue(TEXT("The cases file has cases"), !Cases.IsEmpty()))
	{
		return false;
	}

	const bool bLive = FParse::Param(FCommandLine::Get(), TEXT("RoscEvalsLive"));
	URoscAISubsystem* Assistant = GEditor->GetEditorSubsystem<URoscAISubsystem>();

	// A run may ask another model, or at another effort, than the settings name. The settings are put back when it ends, and never saved.
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	const FString ModelBefore = Settings->AssistantModel;
	const ERoscAssistantEffort EffortBefore = Settings->AssistantEffort;
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("RoscEvalsModel="), Override) && !Override.IsEmpty())
	{
		Settings->AssistantModel = Override;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("RoscEvalsEffort="), Override) && !Override.IsEmpty())
	{
		const TMap<FString, ERoscAssistantEffort> Efforts = {
			{ TEXT("default"), ERoscAssistantEffort::ModelDefault }, { TEXT("low"), ERoscAssistantEffort::Low },
			{ TEXT("medium"), ERoscAssistantEffort::Medium }, { TEXT("high"), ERoscAssistantEffort::High } };
		const ERoscAssistantEffort* Effort = Efforts.Find(Override.ToLower());
		if (!Effort)
		{
			AddError(FString::Printf(TEXT("-RoscEvalsEffort=%s is not one of default, low, medium or high."), *Override));
			Settings->AssistantModel = ModelBefore;
			return false;
		}
		Settings->AssistantEffort = *Effort;
	}
	const auto PutSettingsBack = [Settings, ModelBefore, EffortBefore]
	{
		Settings->AssistantModel = ModelBefore;
		Settings->AssistantEffort = EffortBefore;
	};

	if (bLive)
	{
		FText WhyNot;
		if (!Assistant->CanPlanShot(WhyNot))
		{
			AddError(FString::Printf(TEXT("The model cannot be asked. %s"), *WhyNot.ToString()));
			PutSettingsBack();
			return false;
		}
		const FString Host = Assistant->GetAssistantHostNeedingConsent();
		if (!Host.IsEmpty())
		{
			AddError(FString::Printf(TEXT("You have not agreed to send shot descriptions to %s. Agree in the Rosc panel's AI tab first."), *Host));
			PutSettingsBack();
			return false;
		}
	}

	const TSharedRef<FShotEvalRun> Run = MakeShared<FShotEvalRun>();
	Run->Cases = Cases;
	Run->Model = bLive ? Settings->AssistantModel : FString(TEXT("none (the sets were stood up and nothing was sent)"));
	Run->Effort = Settings->AssistantEffort == ERoscAssistantEffort::Low ? TEXT("low")
		: Settings->AssistantEffort == ERoscAssistantEffort::Medium ? TEXT("medium")
		: Settings->AssistantEffort == ERoscAssistantEffort::High ? TEXT("high")
		: TEXT("default");
	Run->ReportFile = GetShotEvalReportFolder() / FString::Printf(TEXT("shot-assistant-%s.json"), bLive ? *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) : TEXT("dry-run"));
	if (bLive)
	{
		Run->Ask = [Assistant](const FString& Words, const FString& SetDescription, FText& OutError, TFunction<void(const FRoscShotPlanResult&)> OnDone)
		{
			return Assistant->PlanShot(Words, SetDescription, OutError, MoveTemp(OnDone));
		};
	}
	else
	{
		Run->Ask = [](const FString&, const FString&, FText& OutError, TFunction<void(const FRoscShotPlanResult&)>)
		{
			OutError = FText::FromString(TEXT("Not asked: add -RoscEvalsLive to the command line to ask the model."));
			return false;
		};
	}
	Run->Then = [this, bLive, PutSettingsBack](const FShotEvalRun& Done)
	{
		PutSettingsBack();
		for (const FShotEvalOutcome& Outcome : Done.Outcomes)
		{
			if (bLive)
			{
				AddInfo(FString::Printf(TEXT("%s: %s %s%s"), *Outcome.Id, *Outcome.Status, *Outcome.Error, *FString::Join(Outcome.Failures, TEXT(" | "))));
			}
			TestTrue(*FString::Printf(TEXT("The set of case \"%s\" stands and can be described"), *Outcome.Id), Outcome.SetDescription.Contains(TEXT("\"camera\"")));
		}
		AddInfo(bLive ? Done.DescribeScore() : FString::Printf(TEXT("%d cases read and their sets stood up. Nothing was sent."), Done.Outcomes.Num()));
		AddInfo(FString::Printf(TEXT("Report: %s"), *Done.ReportFile));
	};
	ADD_LATENT_AUTOMATION_COMMAND(FRunShotEvalCommand(Run));
	return true;
}

#endif
