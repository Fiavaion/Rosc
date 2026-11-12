#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tests/UEcineAITestUtils.h"
#include "UEcineAIConsent.h"
#include "UEcineAISubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace UEcine::AITests;

	/** A stand-in for the Messages API, and what a test of the shot assistant keeps between editor frames. */
	struct FAssistantTestState
	{
		FSettingsBefore SettingsBefore;
		FStandInServer Server;

		/** What the stand-in answers with next. */
		FString Answer;
		EHttpServerResponseCodes AnswerCode = EHttpServerResponseCodes::Ok;

		/** What the stand-in was last sent. */
		int32 Asked = 0;
		FString SentKey;
		FString SentVersion;
		FString SentContentType;
		TSharedPtr<FJsonObject> SentBody;

		TArray<FUEcineShotPlanResult> Results;
	};

	/** A message as the Messages API gives it: the model's thinking, then its answer as text. */
	FString MakeAssistantTestMessage(const FString& Text, const TCHAR* StopReason)
	{
		const TSharedRef<FJsonObject> Thinking = MakeShared<FJsonObject>();
		Thinking->SetStringField(TEXT("type"), TEXT("thinking"));
		Thinking->SetStringField(TEXT("thinking"), TEXT("A crane, to rise over the table."));
		const TSharedRef<FJsonObject> Answer = MakeShared<FJsonObject>();
		Answer->SetStringField(TEXT("type"), TEXT("text"));
		Answer->SetStringField(TEXT("text"), Text);
		const TSharedRef<FJsonObject> Usage = MakeShared<FJsonObject>();
		Usage->SetNumberField(TEXT("input_tokens"), 4321);
		Usage->SetNumberField(TEXT("output_tokens"), 210);

		const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("id"), TEXT("msg_stand_in"));
		Message->SetStringField(TEXT("type"), TEXT("message"));
		Message->SetStringField(TEXT("role"), TEXT("assistant"));
		const TArray<TSharedPtr<FJsonValue>> Content = { MakeShared<FJsonValueObject>(Thinking), MakeShared<FJsonValueObject>(Answer) };
		Message->SetArrayField(TEXT("content"), Content);
		Message->SetStringField(TEXT("stop_reason"), StopReason);
		Message->SetObjectField(TEXT("usage"), Usage);

		FString Written;
		FJsonSerializer::Serialize(Message, TJsonWriterFactory<>::Create(&Written));
		return Written;
	}

	const TCHAR* AssistantTestPlan = TEXT(R"({
		"summary": "On the 30 ft crane, rising over the table as Anna stands.",
		"steps": [
			{ "step": "put_on_support", "supportId": "crane-30ft" },
			{ "step": "focus_on", "subject": "Anna", "follow": true } ] })");

	const TCHAR* AssistantTestWords = TEXT("Crane up over the table as Anna stands");
	const TCHAR* AssistantTestSet = TEXT(R"({"camera":{"name":"A Cam"},"subjects":[{"name":"Anna"}]})");

	/** Asks for a plan and waits for the request to end, then goes on. */
	void AskThen(FAutomationTestBase& Test, const TSharedRef<FAssistantTestState>& State, const TCHAR* What, TFunction<void(const FUEcineShotPlanResult&)> Then)
	{
		const TSharedRef<int32> ResultsBefore = MakeShared<int32>(INDEX_NONE);
		WaitThen(Test, What, 30.0, [&Test, State, ResultsBefore, What = FString(What)]
		{
			if (*ResultsBefore == INDEX_NONE)
			{
				// The request is made when its turn comes, with the answer the step before set up.
				*ResultsBefore = State->Results.Num();
				FText Error;
				UUEcineAISubsystem* Jobs = GEditor->GetEditorSubsystem<UUEcineAISubsystem>();
				if (!Test.TestTrue(*FString::Printf(TEXT("The request is made for %s (%s)"), *What, *Error.ToString()),
					Jobs->PlanShot(AssistantTestWords, AssistantTestSet, Error, [State](const FUEcineShotPlanResult& Result) { State->Results.Add(Result); })))
				{
					State->Results.AddDefaulted();
				}
			}
			return State->Results.Num() > *ResultsBefore;
		}, [State, Then = MoveTemp(Then)]
		{
			Then(State->Results.IsEmpty() ? FUEcineShotPlanResult() : State->Results.Last());
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineAIAssistantTest, "UEcine.AI.Assistant", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineAIAssistantTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FAssistantTestState> State = MakeShared<FAssistantTestState>();
	State->Server.Answer(EHttpServerRequestVerbs::VERB_POST, TEXT("/v1/messages"), [State](const FHttpServerRequest& Request)
	{
		++State->Asked;
		State->SentKey = FStandInServer::GetHeader(Request, TEXT("x-api-key"));
		State->SentVersion = FStandInServer::GetHeader(Request, TEXT("anthropic-version"));
		State->SentContentType = FStandInServer::GetHeader(Request, TEXT("Content-Type"));
		const FUTF8ToTCHAR Body(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		State->SentBody.Reset();
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Body.Length(), Body.Get())), State->SentBody);
		return FStandInServer::Json(State->Answer, State->AnswerCode);
	});

	UUEcineAISettings* Settings = GetMutableDefault<UUEcineAISettings>();
	UUEcineAISubsystem* Jobs = GEditor->GetEditorSubsystem<UUEcineAISubsystem>();
	Jobs->CancelShotPlan();
	const FString StandInUrl = State->Server.GetUrl() / TEXT("v1/messages");
	FText Error;
	const auto NothingHappens = [State](const FUEcineShotPlanResult& Result) { State->Results.Add(Result); };

	// What stands in the way of a request, before anything is sent.
	Settings->AssistantUrl = StandInUrl;
	Settings->AssistantModel = TEXT("claude-opus-5-5");
	Settings->AssistantKey.Reset();
	TestFalse(TEXT("With no key the assistant cannot be asked"), Jobs->PlanShot(AssistantTestWords, AssistantTestSet, Error, NothingHappens));
	TestTrue(TEXT("And the reason says where to give one"), Error.ToString().Contains(TEXT("Claude API key")) && Error.ToString().Contains(TEXT("Editor Preferences")));

	Settings->AssistantKey = TEXT("stand-in-key");
	TestFalse(TEXT("A shot with no description is not asked for"), Jobs->PlanShot(TEXT("  "), AssistantTestSet, Error, NothingHappens));

	Settings->AssistantUrl = TEXT("http://uecine-assistant-test.invalid/v1/messages");
	TestFalse(TEXT("A key is not sent in the clear to another machine"), Jobs->PlanShot(AssistantTestWords, AssistantTestSet, Error, NothingHappens));
	TestTrue(TEXT("And the reason says so"), Error.ToString().Contains(TEXT("https://")));

	const FString Elsewhere = TEXT("uecine-assistant-test.invalid");
	Settings->AssistantUrl = FString::Printf(TEXT("https://%s/v1/messages"), *Elsewhere);
	UEcine::AIConsent::Withdraw(Elsewhere);
	TestEqual(TEXT("A host elsewhere is named as needing the user's agreement"), *Jobs->GetAssistantHostNeedingConsent(), *Elsewhere);
	TestFalse(TEXT("And nothing is asked of it until they agree"), Jobs->PlanShot(AssistantTestWords, AssistantTestSet, Error, NothingHappens));
	TestTrue(TEXT("The reason names the host"), Error.ToString().Contains(Elsewhere));
	TestFalse(TEXT("No request is under way"), Jobs->IsPlanningShot());
	TestEqual(TEXT("Nothing was sent and nothing came back"), State->Asked + State->Results.Num(), 0);

	Settings->AssistantUrl = StandInUrl;
	TestTrue(TEXT("A stand-in on this machine needs no agreement"), Jobs->GetAssistantHostNeedingConsent().IsEmpty());

	// A plan comes back.
	State->Answer = MakeAssistantTestMessage(AssistantTestPlan, TEXT("end_turn"));
	AskThen(*this, State, TEXT("a plan"), [this, State](const FUEcineShotPlanResult& Result)
	{
		TestEqual(TEXT("The key goes in its header"), *State->SentKey, TEXT("stand-in-key"));
		TestEqual(TEXT("The API version is named"), *State->SentVersion, TEXT("2026-10-21"));
		TestTrue(TEXT("The request is JSON"), State->SentContentType.StartsWith(TEXT("application/json")));
		if (TestTrue(TEXT("And reads as JSON"), State->SentBody.IsValid()))
		{
			const FJsonObject& Body = *State->SentBody;
			TestEqual(TEXT("The model in the settings is asked"), *Body.GetStringField(TEXT("model")), TEXT("claude-opus-5-5"));
			TestTrue(TEXT("With room to answer"), Body.GetIntegerField(TEXT("max_tokens")) >= 4000);
			TestTrue(TEXT("The assistant is told who it is"), Body.GetStringField(TEXT("system")).Contains(TEXT("shot assistant")));
			// This model takes neither of these, and refuses a request that has them.
			TestFalse(TEXT("No sampling settings are sent"), Body.HasField(TEXT("temperature")) || Body.HasField(TEXT("top_p")));
			TestFalse(TEXT("No thinking budget is sent"), Body.HasField(TEXT("thinking")));

			const TArray<TSharedPtr<FJsonValue>>& Messages = Body.GetArrayField(TEXT("messages"));
			if (TestEqual(TEXT("One message is sent"), Messages.Num(), 1))
			{
				const FJsonObject& Message = *Messages[0]->AsObject();
				TestEqual(TEXT("From the user"), *Message.GetStringField(TEXT("role")), TEXT("user"));
				const FString Content = Message.GetStringField(TEXT("content"));
				TestTrue(TEXT("With the user's words"), Content.Contains(FString::Printf(TEXT("<request>\n%s\n</request>"), AssistantTestWords)));
				TestTrue(TEXT("The set's description"), Content.Contains(FString::Printf(TEXT("<set>\n%s\n</set>"), AssistantTestSet)));
				TestTrue(TEXT("And the equipment it can choose from"), Content.Contains(TEXT("<equipment>")) && Content.Contains(TEXT("\"crane-30ft\"")) && Content.Contains(TEXT("\"arri-alexa-35\"")));
			}

			const TSharedPtr<FJsonObject>* Format = nullptr;
			if (TestTrue(TEXT("The answer is held to a schema"), Body.GetObjectField(TEXT("output_config"))->TryGetObjectField(TEXT("format"), Format)))
			{
				TestEqual(TEXT("A JSON Schema"), *(*Format)->GetStringField(TEXT("type")), TEXT("json_schema"));
				TestTrue(TEXT("The shot plan's"), (*Format)->GetObjectField(TEXT("schema"))->GetObjectField(TEXT("properties"))->HasField(TEXT("steps")));
			}
		}

		TestTrue(*FString::Printf(TEXT("The plan is read (%s)"), *Result.Error.ToString()), Result.bSuccess);
		TestEqual(TEXT("With its steps"), Result.Plan.Steps.Num(), 2);
		TestTrue(TEXT("And its summary"), Result.Plan.Summary.StartsWith(TEXT("On the 30 ft crane")));
		TestEqual(TEXT("What the request came to is kept"), Result.InputTokens, 4321);
		TestEqual(TEXT("And what the answer came to"), Result.OutputTokens, 210);

		State->Answer = MakeAssistantTestMessage(TEXT(""), TEXT("refusal"));
	});

	// Answers that are not plans, each told to the user in words.
	AskThen(*this, State, TEXT("a request the model declines"), [this, State](const FUEcineShotPlanResult& Result)
	{
		TestFalse(TEXT("A declined request gives no plan"), Result.bSuccess);
		TestEqual(TEXT("And says so"), *Result.Error.ToString(), TEXT("The model declined this request. Describe the shot another way."));
		State->Answer = MakeAssistantTestMessage(TEXT(R"({"summary": "On the 30 ft cra)"), TEXT("max_tokens"));
	});
	AskThen(*this, State, TEXT("an answer that is cut short"), [this, State](const FUEcineShotPlanResult& Result)
	{
		TestFalse(TEXT("An answer cut short gives no plan"), Result.bSuccess);
		TestEqual(TEXT("And says so"), *Result.Error.ToString(), TEXT("The answer was cut short before it was finished. Ask for less in one go."));
		State->Answer = MakeAssistantTestMessage(TEXT(R"({"summary": "", "steps": [ { "step": "light_the_set" } ] })"), TEXT("end_turn"));
	});
	AskThen(*this, State, TEXT("a plan with a step that does not exist"), [this, State](const FUEcineShotPlanResult& Result)
	{
		TestFalse(TEXT("A plan that cannot be read is not a plan"), Result.bSuccess);
		TestEqual(TEXT("And the fault is named"), *Result.Error.ToString(),
			TEXT("The plan that came back could not be read. Step 1: \"step\" must name a kind of step; \"light_the_set\" is not one."));
		State->Answer = TEXT(R"({"type": "error", "error": {"type": "authentication_error", "message": "invalid x-api-key"}})");
		State->AnswerCode = EHttpServerResponseCodes::Denied;
	});
	AskThen(*this, State, TEXT("a key that is not accepted"), [this, State](const FUEcineShotPlanResult& Result)
	{
		TestFalse(TEXT("A key that is not accepted gives no plan"), Result.bSuccess);
		TestEqual(TEXT("And says where to check it"), *Result.Error.ToString(),
			TEXT("127.0.0.1 did not accept the key. Check it under Editor Preferences > Plugins > UEcine AI."));
		State->Answer = TEXT(R"({"type": "error", "error": {"type": "invalid_request_error", "message": "model: claude-opus-9 is not a model."}})");
		State->AnswerCode = EHttpServerResponseCodes::BadRequest;
	});
	AskThen(*this, State, TEXT("a request the API will not take"), [this, State](const FUEcineShotPlanResult& Result)
	{
		TestFalse(TEXT("A request the API will not take gives no plan"), Result.bSuccess);
		TestEqual(TEXT("And gives the API's reason"), *Result.Error.ToString(), TEXT("127.0.0.1 would not take the request: model: claude-opus-9 is not a model."));
		State->Answer = TEXT(R"({"type": "error", "error": {"type": "overloaded_error", "message": "Overloaded"}})");
		State->AnswerCode = static_cast<EHttpServerResponseCodes>(529);
	});
	AskThen(*this, State, TEXT("a busy API"), [this, State](const FUEcineShotPlanResult& Result)
	{
		TestFalse(TEXT("A busy API gives no plan"), Result.bSuccess);
		TestEqual(TEXT("And says to try again"), *Result.Error.ToString(), TEXT("127.0.0.1 is busy. Try again in a moment."));
		TestEqual(TEXT("Every request reached the stand-in once"), State->Asked, 7);
		State->SettingsBefore.PutBack();
	});
	return true;
}

#endif
