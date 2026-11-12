#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UEcineCameraDatabase.h"
#include "UEcineShotPlan.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> ReadShotPlanTestJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	/** The names of an object's fields, in order of name. */
	TArray<FString> GetShotPlanTestFields(const FJsonObject& Object)
	{
		TArray<FString> Fields;
		for (const auto& Field : Object.Values)
		{
			Fields.Add(FString(Field.Key));
		}
		Fields.Sort();
		return Fields;
	}

	/** A written plan with one step of every kind. */
	const TCHAR* EveryStepPlan = TEXT(R"({
		"summary": "A slow crane up from the table as Anna stands, on the 32 mm.",
		"steps": [
			{ "step": "choose_camera", "bodyId": "arri-alexa-35", "formatId": "4.6k-16x9" },
			{ "step": "choose_lens", "lensKitId": "cooke-s4i", "focalLengthMm": 32 },
			{ "step": "set_exposure", "tStop": 2.8, "iso": 800, "shutterAngleDeg": 172.8, "ndStops": 2 },
			{ "step": "put_on_support", "supportId": "crane-30ft" },
			{ "step": "move_to_mark", "travelM": 0, "sidewaysM": 0, "liftM": 0, "swingDeg": -10, "boomDeg": 25, "extensionM": 1.5,
			  "panDeg": 5, "tiltDeg": -12, "rollDeg": 0, "durationSeconds": 8, "easeIn": 0.25, "easeOut": 0.3 },
			{ "step": "focus_on", "subject": "Anna", "follow": true },
			{ "step": "focus_at", "distanceM": 3.2 },
			{ "step": "add_shot", "scene": "12", "size": "medium_close_up", "move": "Crane up", "notes": "As she stands" },
			{ "step": "bake_move" }
		] })");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineShotPlanReadTest, "UEcine.Core.ShotPlan.Read", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineShotPlanReadTest::RunTest(const FString& Parameters)
{
	FUEcineShotPlan Plan;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("A plan with every kind of step is read (%s)"), *Error), UEcine::ShotPlan::Parse(EveryStepPlan, Plan, Error))
		|| !TestEqual(TEXT("Every step is kept, in order"), Plan.Steps.Num(), 9))
	{
		return false;
	}
	TestTrue(TEXT("The summary is kept"), Plan.Summary.StartsWith(TEXT("A slow crane up")));
	for (int32 Index = 0; Index < Plan.Steps.Num(); ++Index)
	{
		TestEqual(TEXT("Each step is of the kind in its place"), static_cast<int32>(Plan.Steps[Index].Kind), Index);
	}

	TestEqual(TEXT("Body"), *Plan.Steps[0].BodyId, TEXT("arri-alexa-35"));
	TestEqual(TEXT("Format"), *Plan.Steps[0].FormatId, TEXT("4.6k-16x9"));
	TestEqual(TEXT("Lens kit"), *Plan.Steps[1].LensKitId, TEXT("cooke-s4i"));
	TestEqual(TEXT("Focal length"), Plan.Steps[1].FocalLengthMm, 32.0);
	TestEqual(TEXT("T-stop"), Plan.Steps[2].TStop, 2.8);
	TestEqual(TEXT("ISO"), Plan.Steps[2].Iso, 800);
	TestEqual(TEXT("Shutter angle"), Plan.Steps[2].ShutterAngleDeg, 172.8);
	TestEqual(TEXT("ND"), Plan.Steps[2].NdStops, 2.0);
	TestEqual(TEXT("Support"), *Plan.Steps[3].SupportId, TEXT("crane-30ft"));

	const FUEcineShotStep& Move = Plan.Steps[4];
	TestEqual(TEXT("Swing is in degrees"), Move.Change.Swing, -10.0);
	TestEqual(TEXT("Boom is in degrees"), Move.Change.Boom, 25.0);
	TestEqual(TEXT("The telescope's metres become centimetres"), Move.Change.Extension, 150.0);
	TestEqual(TEXT("Pan"), Move.Change.Pan, 5.0);
	TestEqual(TEXT("Tilt"), Move.Change.Tilt, -12.0);
	TestEqual(TEXT("An axis that stays is zero"), Move.Change.Travel, 0.0);
	TestEqual(TEXT("Duration"), Move.Timing.DurationSeconds, 8.0);
	TestEqual(TEXT("Ease in"), Move.Timing.EaseInFraction, 0.25);
	TestEqual(TEXT("Ease out"), Move.Timing.EaseOutFraction, 0.3);

	TestEqual(TEXT("Subject"), *Plan.Steps[5].Subject, TEXT("Anna"));
	TestTrue(TEXT("Follow"), Plan.Steps[5].bFollow);
	TestEqual(TEXT("A focus distance in metres becomes centimetres"), Plan.Steps[6].FocusDistanceCm, 320.0);
	TestEqual(TEXT("Scene"), *Plan.Steps[7].Scene, TEXT("12"));
	TestTrue(TEXT("Shot size"), Plan.Steps[7].Size == EUEcineShotSize::MediumCloseUp);
	TestEqual(TEXT("Move"), *Plan.Steps[7].Move, TEXT("Crane up"));
	TestEqual(TEXT("Notes"), *Plan.Steps[7].Notes, TEXT("As she stands"));

	// A plan that can do nothing is still a plan.
	TestTrue(TEXT("A plan with no steps is read"), UEcine::ShotPlan::Parse(TEXT(R"({ "summary": "Lighting is not something these steps can do.", "steps": [] })"), Plan, Error));
	TestEqual(TEXT("And has none"), Plan.Steps.Num(), 0);

	// Faults are named with the step and the field, and nothing of a faulty plan is kept.
	const auto TestFault = [this](const TCHAR* What, const TCHAR* Json, const TCHAR* ExpectedError)
	{
		FUEcineShotPlan Faulty;
		FString Fault;
		TestFalse(What, UEcine::ShotPlan::Parse(Json, Faulty, Fault));
		TestEqual(What, *Fault, ExpectedError);
		TestEqual(TEXT("Nothing of a faulty plan is kept"), Faulty.Steps.Num(), 0);
	};
	TestFault(TEXT("A kind of step that does not exist"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "bake_move" }, { "step": "light_the_set" } ] })"),
		TEXT("Step 2: \"step\" must name a kind of step; \"light_the_set\" is not one."));
	TestFault(TEXT("A field left out"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "choose_lens", "lensKitId": "cooke-s4i" } ] })"),
		TEXT("Step 1 (choose_lens): \"focalLengthMm\" is required and must be a number."));
	TestFault(TEXT("A field of the wrong type"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "focus_on", "subject": "Anna", "follow": "yes" } ] })"),
		TEXT("Step 1 (focus_on): \"follow\" is required and must be true or false."));
	TestFault(TEXT("An ISO that is not a whole number"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "set_exposure", "tStop": 2.8, "iso": 800.5, "shutterAngleDeg": 180, "ndStops": 0 } ] })"),
		TEXT("Step 1 (set_exposure): \"iso\" is required and must be a whole number."));
	TestFault(TEXT("A shutter angle past a full turn"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "set_exposure", "tStop": 2.8, "iso": 800, "shutterAngleDeg": 400, "ndStops": 0 } ] })"),
		TEXT("Step 1 (set_exposure): \"shutterAngleDeg\" must be from 1 to 360."));
	TestFault(TEXT("A move that takes no time"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "move_to_mark", "travelM": 3, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 0, "extensionM": 0,
			"panDeg": 0, "tiltDeg": 0, "rollDeg": 0, "durationSeconds": 0, "easeIn": 0.25, "easeOut": 0.25 } ] })"),
		TEXT("Step 1 (move_to_mark): \"durationSeconds\" must be greater than zero."));
	TestFault(TEXT("Eases that are longer than the move"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "move_to_mark", "travelM": 3, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 0, "extensionM": 0,
			"panDeg": 0, "tiltDeg": 0, "rollDeg": 0, "durationSeconds": 6, "easeIn": 0.6, "easeOut": 0.6 } ] })"),
		TEXT("Step 1 (move_to_mark): \"easeOut\" and \"easeIn\" together must not pass 1."));
	TestFault(TEXT("A shot size that does not exist"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "add_shot", "scene": "", "size": "huge", "move": "", "notes": "" } ] })"),
		TEXT("Step 1 (add_shot): \"size\" must be a shot size."));
	TestFault(TEXT("A subject with no name"),
		TEXT(R"({ "summary": "", "steps": [ { "step": "focus_on", "subject": "", "follow": false } ] })"),
		TEXT("Step 1 (focus_on): \"subject\" must not be empty."));
	TestFault(TEXT("No list of steps"), TEXT(R"({ "summary": "Nothing" })"), TEXT("The plan must have a \"summary\" and a list of \"steps\"."));

	FString Fault;
	TestFalse(TEXT("Text that is not JSON is not a plan"), UEcine::ShotPlan::Parse(TEXT("Put it on a crane."), Plan, Fault));
	TestTrue(TEXT("And the fault says so"), Fault.StartsWith(TEXT("The plan is not valid JSON")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineShotPlanSchemaTest, "UEcine.Core.ShotPlan.Schema", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineShotPlanSchemaTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Schema = ReadShotPlanTestJson(UEcine::ShotPlan::GetJsonSchema());
	if (!TestTrue(TEXT("The schema is JSON"), Schema.IsValid()))
	{
		return false;
	}

	// What a model held to a schema needs of it: closed objects with every field required.
	const auto TestClosed = [this](const FJsonObject& Object, const FString& Name)
	{
		bool bOpen = true;
		TestTrue(*(Name + TEXT(" takes no fields but its own")), Object.TryGetBoolField(TEXT("additionalProperties"), bOpen) && !bOpen);
		TArray<FString> Required;
		Object.TryGetStringArrayField(TEXT("required"), Required);
		const TArray<FString> Fields = GetShotPlanTestFields(*Object.GetObjectField(TEXT("properties")));
		Required.Sort();
		TestTrue(*(Name + TEXT(" requires every field")), Required == Fields);
		for (const FString& Field : Fields)
		{
			TestFalse(*FString::Printf(TEXT("%s says what \"%s\" means"), *Name, *Field),
				Object.GetObjectField(TEXT("properties"))->GetObjectField(Field)->GetStringField(TEXT("description")).IsEmpty());
		}
	};
	TestClosed(*Schema, TEXT("A plan"));

	const TArray<TSharedPtr<FJsonValue>>& StepSchemas = Schema->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("steps"))->GetObjectField(TEXT("items"))->GetArrayField(TEXT("anyOf"));
	TestEqual(TEXT("The schema has every kind of step"), StepSchemas.Num(), static_cast<int32>(EUEcineShotStep::BakeMove) + 1);
	TArray<FString> StepIds;
	for (const TSharedPtr<FJsonValue>& StepSchema : StepSchemas)
	{
		const FJsonObject& Step = *StepSchema->AsObject();
		const FString Id = Step.GetObjectField(TEXT("properties"))->GetObjectField(TEXT("step"))->GetArrayField(TEXT("enum"))[0]->AsString();
		StepIds.Add(Id);
		TestClosed(Step, Id);
	}
	for (int32 Kind = 0; Kind <= static_cast<int32>(EUEcineShotStep::BakeMove); ++Kind)
	{
		const TCHAR* Id = UEcine::ShotPlan::GetStepId(static_cast<EUEcineShotStep>(Kind));
		TestTrue(*FString::Printf(TEXT("The schema has \"%s\""), Id), StepIds.Contains(Id));
	}

	// The written plan of the reading test is what the schema describes: the same fields, no more.
	const TSharedPtr<FJsonObject> Written = ReadShotPlanTestJson(EveryStepPlan);
	const TArray<TSharedPtr<FJsonValue>>& WrittenSteps = Written->GetArrayField(TEXT("steps"));
	for (int32 Index = 0; Index < WrittenSteps.Num() && Index < StepSchemas.Num(); ++Index)
	{
		const TArray<FString> WrittenFields = GetShotPlanTestFields(*WrittenSteps[Index]->AsObject());
		const TArray<FString> SchemaFields = GetShotPlanTestFields(*StepSchemas[Index]->AsObject()->GetObjectField(TEXT("properties")));
		TestTrue(*FString::Printf(TEXT("\"%s\" is read with the fields the schema gives it"), *StepIds[Index]), WrittenFields == SchemaFields);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineShotPlanEquipmentTest, "UEcine.Core.ShotPlan.Equipment", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineShotPlanEquipmentTest::RunTest(const FString& Parameters)
{
	// Numbers are written as a crew would say them, whatever a computer makes of 2.8.
	using UEcine::ShotPlan::MakeNumber;
	TestEqual(TEXT("A T-stop held as a float"), *MakeNumber(2.8f, 1)->AsString(), TEXT("2.8"));
	TestEqual(TEXT("A whole number has no decimals"), *MakeNumber(6.0, 2)->AsString(), TEXT("6"));
	TestEqual(TEXT("A hundred keeps its zeros"), *MakeNumber(100.0, 2)->AsString(), TEXT("100"));
	TestEqual(TEXT("Rounded to the decimals asked for"), *MakeNumber(1234.5678, 2)->AsString(), TEXT("1234.57"));
	TestEqual(TEXT("Small numbers keep their place"), *MakeNumber(0.05, 2)->AsString(), TEXT("0.05"));
	TestEqual(TEXT("Nothing is not minus nothing"), *MakeNumber(-0.001, 2)->AsString(), TEXT("0"));
	TestEqual(TEXT("Negative numbers stay negative"), *MakeNumber(-12.5, 1)->AsString(), TEXT("-12.5"));

	const FString Text = UEcine::ShotPlan::DescribeEquipment(FUEcineCameraDatabase::Get());
	TestFalse(TEXT("No number of the equipment list trails a computer's rounding after it"), Text.Contains(TEXT("00000")) || Text.Contains(TEXT("99999")));
	const TSharedPtr<FJsonObject> Equipment = ReadShotPlanTestJson(Text);
	if (!TestTrue(TEXT("The equipment list is JSON"), Equipment.IsValid()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("The equipment list is %d characters"), Text.Len()));

	const auto Find = [&Equipment](const TCHAR* List, const TCHAR* Id) -> TSharedPtr<FJsonObject>
	{
		for (const TSharedPtr<FJsonValue>& Value : Equipment->GetArrayField(List))
		{
			if (Value->AsObject()->GetStringField(TEXT("id")) == Id)
			{
				return Value->AsObject();
			}
		}
		return nullptr;
	};

	const TSharedPtr<FJsonObject> Body = Find(TEXT("bodies"), TEXT("arri-alexa-35"));
	if (TestTrue(TEXT("A shipped body is listed"), Body.IsValid()))
	{
		TestEqual(TEXT("By the name a crew knows it by"), *Body->GetStringField(TEXT("name")), TEXT("ARRI ALEXA 35"));
		TestTrue(TEXT("With its formats"), Body->GetArrayField(TEXT("formats")).Num() > 1);
	}
	const TSharedPtr<FJsonObject> Kit = Find(TEXT("lensKits"), TEXT("cooke-s4i"));
	if (TestTrue(TEXT("A shipped lens kit is listed"), Kit.IsValid()))
	{
		TArray<FString> Lenses;
		Kit->TryGetStringArrayField(TEXT("lenses"), Lenses);
		TestTrue(TEXT("With its lenses as a crew writes them"), Lenses.Contains(TEXT("32mm T2")));
		TestFalse(TEXT("A spherical kit has no squeeze to tell of"), Kit->HasField(TEXT("anamorphicSqueeze")));
	}

	const TSharedPtr<FJsonObject> Crane = Find(TEXT("supports"), TEXT("crane-30ft"));
	if (TestTrue(TEXT("A crane is listed"), Crane.IsValid()))
	{
		const TSharedPtr<FJsonObject> Axes = Crane->GetObjectField(TEXT("axes"));
		TestEqual(TEXT("Its telescope starts at 2.8 m"), Axes->GetObjectField(TEXT("extensionM"))->GetNumberField(TEXT("from")), 2.8);
		TestEqual(TEXT("And reaches 9.1 m"), Axes->GetObjectField(TEXT("extensionM"))->GetNumberField(TEXT("to")), 9.1);
		TestEqual(TEXT("At 1.8 m a second"), Axes->GetObjectField(TEXT("extensionM"))->GetNumberField(TEXT("topSpeedPerSecond")), 1.8);
		TestEqual(TEXT("Its boom is in degrees"), Axes->GetObjectField(TEXT("boomDeg"))->GetNumberField(TEXT("to")), 50.0);
		TestTrue(TEXT("Its pivot height is fixed during a shot"), Axes->GetObjectField(TEXT("liftM"))->GetBoolField(TEXT("fixedDuringShot")));
		TestFalse(TEXT("It has no track"), Axes->HasField(TEXT("travelM")));
	}
	const TSharedPtr<FJsonObject> Steadicam = Find(TEXT("supports"), TEXT("steadicam"));
	if (TestTrue(TEXT("A Steadicam is listed"), Steadicam.IsValid()))
	{
		const TSharedPtr<FJsonObject> Forward = Steadicam->GetObjectField(TEXT("axes"))->GetObjectField(TEXT("travelM"));
		TestFalse(TEXT("It goes as far as its operator walks"), Forward->HasField(TEXT("to")));
		TestEqual(TEXT("At walking pace"), Forward->GetNumberField(TEXT("topSpeedPerSecond")), 3.0);
	}
	return true;
}

#endif
