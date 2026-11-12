// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineAIAssistant.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UEcineAIConsent.h"
#include "UEcineAISettings.h"
#include "UEcineCameraDatabase.h"
#include "UEcineShotPlan.h"

#define LOCTEXT_NAMESPACE "UEcineAIAssistant"

namespace
{
	const TCHAR* ClaudeApiVersion = TEXT("2026-10-21");
	/** Room for the model's thinking and a plan; a plan itself is a few hundred tokens. */
	constexpr int32 ClaudeMaxTokens = 16000;
	/** The answer comes in one piece when the model has finished thinking, so the line is quiet until then. */
	constexpr float ClaudeAnswerSeconds = 180.0f;

	const TCHAR* ShotAssistantSystem = TEXT(
		"You are the shot assistant inside UEcine, a camera department for Unreal Engine used by directors and "
		"directors of photography. The user describes a shot in film language. You turn it into a plan: a short "
		"list of steps from a fixed set. The user reads the plan and applies it with one button, and can undo it "
		"with one undo.\n"
		"\n"
		"There is one request and one plan. You cannot ask the user anything, so where the request leaves something "
		"open, decide as an experienced camera crew would and say what you decided in the summary.\n"
		"\n"
		"You are given three things:\n"
		"- <equipment>: the camera bodies, lens kits and supports that can be chosen, with their ids. For each "
		"support, the axes it has, how far each goes and its top speed. Lengths are metres, angles are degrees.\n"
		"- <set>: the camera the plan is for (its package, exposure, focus and the support it is on, if any), the "
		"things in the level near it by name, with where each is as seen from the camera, and the open sequence.\n"
		"- <request>: what the user typed.\n"
		"\n"
		"How to plan:\n"
		"- Use only ids from <equipment> and subject names from <set>, exactly as written. If the request names "
		"something that is in neither, leave that part out and say so in the summary.\n"
		"- Change only what the request asks for or the shot needs. A step that would change nothing is left out: "
		"do not choose the camera, lens or exposure again when the request does not touch them.\n"
		"- A move needs a support that can make it. If the camera is on none, or on one that cannot make the move, "
		"put it on one first. Choose the smallest support that does the job: a tripod for a pan or tilt, a slider "
		"for a short slide, a dolly for a push in or a tracking move, a jib or crane to rise over something, a "
		"Steadicam or handheld camera to walk with a subject, a drone for height and distance no crane reaches.\n"
		"- A move is one or more move_to_mark steps, each from where the rig stands to the next mark. Work out how "
		"far each axis must go from where things are in <set>. Give the move the time a crew would give it, never "
		"less than the support's top speeds allow, and ease in and out unless the request asks for an abrupt move.\n"
		"- When the plan has a move and the request asks for a shot to keep, add the shot and then bake the move, "
		"in that order. A plan that only sets the camera up needs neither.\n"
		"- These steps cannot light the set, move or animate anything but the camera, time a rack focus, change "
		"frame lines or render. When the request asks for any of that, plan the part these steps can do and say "
		"plainly in the summary what is left for the user. When no part can be done, give no steps.\n"
		"\n"
		"The summary is for the director: one to three sentences in film language, with no ids and no JSON.");

	FString WriteAssistantJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		return Text;
	}

	TSharedPtr<FJsonObject> ReadAssistantJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	FString ToText(const TArray<uint8>& Content)
	{
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Content.GetData()), Content.Num());
		return FString(Text.Length(), Text.Get());
	}
}

FUEcineClaudeModel::~FUEcineClaudeModel()
{
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		Request->CancelRequest();
	}
}

FString FUEcineClaudeModel::GetHost() const
{
	return UEcine::AIConsent::GetHost(GetDefault<UUEcineAISettings>()->AssistantUrl.TrimStartAndEnd());
}

bool FUEcineClaudeModel::CanAsk(FText& OutWhyNot) const
{
	const UUEcineAISettings* Settings = GetDefault<UUEcineAISettings>();
	const FString Url = Settings->AssistantUrl.TrimStartAndEnd();
	if (Settings->AssistantKey.TrimStartAndEnd().IsEmpty())
	{
		OutWhyNot = LOCTEXT("NoKey", "Give your Claude API key under Editor Preferences > Plugins > UEcine AI.");
		return false;
	}
	if (Settings->AssistantModel.TrimStartAndEnd().IsEmpty())
	{
		OutWhyNot = LOCTEXT("NoModel", "Name the Claude model to ask under Editor Preferences > Plugins > UEcine AI.");
		return false;
	}
	// A key is only ever sent over a line that hides it, except to this machine.
	const bool bHidden = Url.StartsWith(TEXT("https://")) || (Url.StartsWith(TEXT("http://")) && UEcine::AIConsent::IsThisMachine(GetHost()));
	if (!bHidden || GetHost().IsEmpty())
	{
		OutWhyNot = LOCTEXT("NoAddress", "The Messages API address under Editor Preferences > Plugins > UEcine AI must start with https://.");
		return false;
	}
	return true;
}

void FUEcineClaudeModel::Ask(const FUEcineAssistantQuestion& Question, TFunction<void(FUEcineAssistantAnswer&&)> InOnDone)
{
	check(IsInGameThread() && !Request.IsValid());
	const UUEcineAISettings* Settings = GetDefault<UUEcineAISettings>();

	const TSharedPtr<FJsonObject> Schema = ReadAssistantJson(Question.AnswerSchema);
	check(Schema.IsValid());
	const TSharedRef<FJsonObject> Format = MakeShared<FJsonObject>();
	Format->SetStringField(TEXT("type"), TEXT("json_schema"));
	Format->SetObjectField(TEXT("schema"), Schema);
	const TSharedRef<FJsonObject> OutputConfig = MakeShared<FJsonObject>();
	OutputConfig->SetObjectField(TEXT("format"), Format);

	const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("role"), TEXT("user"));
	Message->SetStringField(TEXT("content"), Question.User);

	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Settings->AssistantModel.TrimStartAndEnd());
	Body->SetNumberField(TEXT("max_tokens"), ClaudeMaxTokens);
	Body->SetStringField(TEXT("system"), Question.System);
	const TArray<TSharedPtr<FJsonValue>> Messages = { MakeShared<FJsonValueObject>(Message) };
	Body->SetArrayField(TEXT("messages"), Messages);
	Body->SetObjectField(TEXT("output_config"), OutputConfig);

	OnDone = MoveTemp(InOnDone);
	Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings->AssistantUrl.TrimStartAndEnd());
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("x-api-key"), Settings->AssistantKey.TrimStartAndEnd());
	Request->SetHeader(TEXT("anthropic-version"), ClaudeApiVersion);
	Request->SetContentAsString(WriteAssistantJson(Body));
	Request->SetTimeout(ClaudeAnswerSeconds);
	Request->SetActivityTimeout(ClaudeAnswerSeconds);
	Request->OnProcessRequestComplete().BindLambda([Weak = AsWeak()](FHttpRequestPtr Answered, FHttpResponsePtr Response, bool bConnected)
	{
		const TSharedPtr<FUEcineClaudeModel> This = Weak.Pin();
		if (This.IsValid() && This->Request == Answered)
		{
			const bool bAnswered = bConnected && Response.IsValid();
			This->Finish(This->ReadResponse(bAnswered ? Response->GetResponseCode() : 0, bAnswered ? Response->GetContent() : TArray<uint8>()));
		}
	});
	Request->ProcessRequest();
}

void FUEcineClaudeModel::Cancel()
{
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		Request->CancelRequest();
		Finish(FUEcineAssistantAnswer());
	}
}

void FUEcineClaudeModel::Finish(FUEcineAssistantAnswer&& Answer)
{
	Request.Reset();
	// The caller may ask the next question in the call.
	const TFunction<void(FUEcineAssistantAnswer&&)> Done = MoveTemp(OnDone);
	OnDone = nullptr;
	Done(MoveTemp(Answer));
}

FUEcineAssistantAnswer FUEcineClaudeModel::ReadResponse(int32 Code, const TArray<uint8>& Content) const
{
	const FText Host = FText::FromString(GetHost());
	if (Code == 0)
	{
		return FUEcineAssistantAnswer::Failed(FText::Format(LOCTEXT("NoAnswer", "{0} did not answer. Check the connection and try again."), Host));
	}

	const TSharedPtr<FJsonObject> Answer = ReadAssistantJson(ToText(Content));
	if (Code != 200)
	{
		if (Code == 401 || Code == 403)
		{
			return FUEcineAssistantAnswer::Failed(FText::Format(LOCTEXT("WrongKey", "{0} did not accept the key. Check it under Editor Preferences > Plugins > UEcine AI."), Host));
		}
		if (Code == 429)
		{
			return FUEcineAssistantAnswer::Failed(FText::Format(LOCTEXT("TooMany", "{0} is taking no more requests from this key for now. Try again in a moment."), Host));
		}
		if (Code >= 500)
		{
			return FUEcineAssistantAnswer::Failed(FText::Format(LOCTEXT("Busy", "{0} is busy. Try again in a moment."), Host));
		}
		// What is left is a request the API would not take; it says why.
		const TSharedPtr<FJsonObject>* Error = nullptr;
		FString Why;
		if (Answer.IsValid() && Answer->TryGetObjectField(TEXT("error"), Error) && (*Error)->TryGetStringField(TEXT("message"), Why) && !Why.IsEmpty())
		{
			return FUEcineAssistantAnswer::Failed(FText::Format(LOCTEXT("Refused", "{0} would not take the request: {1}"), Host, FText::FromString(Why)));
		}
		return FUEcineAssistantAnswer::Failed(FText::Format(LOCTEXT("OddCode", "{0} gave an answer UEcine does not understand ({1})."), Host, FText::AsNumber(Code, &FNumberFormattingOptions::DefaultNoGrouping())));
	}

	const FText Odd = FText::Format(LOCTEXT("OddAnswer", "{0} gave an answer UEcine does not understand."), Host);
	if (!Answer.IsValid())
	{
		return FUEcineAssistantAnswer::Failed(Odd);
	}

	FUEcineAssistantAnswer Result;
	if (const TSharedPtr<FJsonObject>* Usage = nullptr; Answer->TryGetObjectField(TEXT("usage"), Usage))
	{
		(*Usage)->TryGetNumberField(TEXT("input_tokens"), Result.InputTokens);
		(*Usage)->TryGetNumberField(TEXT("output_tokens"), Result.OutputTokens);
	}

	// An answer that was declined or cut short is not held to the schema, so it is not read as one.
	FString StopReason;
	Answer->TryGetStringField(TEXT("stop_reason"), StopReason);
	if (StopReason == TEXT("refusal"))
	{
		Result.Error = LOCTEXT("Declined", "The model declined this request. Describe the shot another way.");
		return Result;
	}
	if (StopReason == TEXT("max_tokens"))
	{
		Result.Error = LOCTEXT("CutShort", "The answer was cut short before it was finished. Ask for less in one go.");
		return Result;
	}

	const TArray<TSharedPtr<FJsonValue>>* Blocks = nullptr;
	if (Answer->TryGetArrayField(TEXT("content"), Blocks))
	{
		for (const TSharedPtr<FJsonValue>& Block : *Blocks)
		{
			const TSharedPtr<FJsonObject>* BlockObject = nullptr;
			FString Type;
			if (Block->TryGetObject(BlockObject) && (*BlockObject)->TryGetStringField(TEXT("type"), Type) && Type == TEXT("text")
				&& (*BlockObject)->TryGetStringField(TEXT("text"), Result.Json) && !Result.Json.IsEmpty())
			{
				Result.bSuccess = true;
				return Result;
			}
		}
	}
	Result.Error = Odd;
	return Result;
}

FUEcineAssistantQuestion UEcine::AIAssistant::MakeShotQuestion(const FString& Words, const FString& SetDescription)
{
	FUEcineAssistantQuestion Question;
	Question.System = ShotAssistantSystem;
	Question.User = FString::Printf(TEXT("<equipment>\n%s\n</equipment>\n\n<set>\n%s\n</set>\n\n<request>\n%s\n</request>"),
		*UEcine::ShotPlan::DescribeEquipment(FUEcineCameraDatabase::Get()), *SetDescription, *Words.TrimStartAndEnd());
	Question.AnswerSchema = UEcine::ShotPlan::GetJsonSchema();
	return Question;
}

#undef LOCTEXT_NAMESPACE
