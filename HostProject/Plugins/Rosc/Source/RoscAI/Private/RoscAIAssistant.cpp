// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscAIAssistant.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "RoscAIConsent.h"
#include "RoscAISettings.h"
#include "RoscCameraDatabase.h"
#include "RoscShotPlan.h"

#define LOCTEXT_NAMESPACE "RoscAIAssistant"

namespace
{
	const TCHAR* ClaudeApiVersion = TEXT("2026-10-21");
	/** Room for the model's thinking and a plan; a plan itself is a few hundred tokens. */
	constexpr int32 ClaudeMaxTokens = 16000;
	/** The answer comes in one piece when the model has finished thinking, so the line is quiet until then. */
	constexpr float ClaudeAnswerSeconds = 180.0f;

	const TCHAR* ShotAssistantSystem = TEXT(
		"You are the shot assistant inside Rosc, a camera department for Unreal Engine used by directors and "
		"directors of photography. The user describes a shot in film language. You turn it into a plan: a short "
		"list of steps from a fixed set. The user reads the plan and applies it with one button, and can undo it "
		"with one undo.\n"
		"\n"
		"There is one request and one plan. You cannot ask the user anything, so where the request leaves something "
		"open, decide as an experienced camera crew would and say what you decided in the summary.\n"
		"\n"
		"You are given three things:\n"
		"- <equipment>: the camera bodies, lens kits and supports that can be chosen, with their ids. For each "
		"support, the axes it has, how far each goes, its top speed and how fast it can gather or lose speed, and "
		"for a rail, arm or telescope where it is when the support is first stood up. Lengths are metres, angles "
		"are degrees.\n"
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
		"- A move is one or more move_to_mark steps, each from where the rig stands to the next mark. Every mark "
		"starts and ends at rest, so give each one, however short, an ease in and an ease out (0.25 each is a "
		"feathered start and stop) unless the request asks for an abrupt move.\n"
		"- Time each mark from the gear. For each axis that moves a distance D in a mark of T seconds with easeIn a "
		"and easeOut b, let k = 1 / (1 - (a + b) / 2). The axis peaks at D * k / T, which must not pass its top "
		"speed, and gathers speed at D * k / (min(a, b) * T * T), which must not pass its top acceleration. Give "
		"the time a crew would give, never less than both allow. When the request asks for a time the gear cannot "
		"make, use the shortest time that works and say so in the summary.\n"
		"- Work out angles from <set>. A subject at aheadM, rightM and upM (its centre; its top and bottom are "
		"heightM / 2 above and below) is atan(rightM / aheadM) to the right of where the camera looks now and "
		"atan(upM / sqrt(aheadM^2 + rightM^2)) above it. After a mark that moves or turns the camera, work out the "
		"next mark from where the camera then stands and points: after a push in of 2 m a subject is 2 m less "
		"ahead, and after a pan the head already points the new way.\n"
		"- A jib or crane arm swings and booms about the pivot on its column, carrying the camera with it. The head "
		"stays level and turns with the arm, so a swing turns the view by the same angle unless the head pans the "
		"other way; extensionM moves the camera along the arm. Keep every axis between its from and to, counting "
		"from where the support stands in <set>, or from standsUpAt when the plan puts the camera on it.\n"
		"- To frame a shot size with a lens, the focal length that holds a height H at a distance D is the sensor "
		"height of the camera's format times D / H. Of a standing person, an extreme close-up holds about 0.15 m, "
		"a close-up 0.3 m, a medium close-up 0.6 m, a medium shot 1 m, a cowboy 1.3 m and a full shot 2.2 m. Put "
		"on the kit's lens nearest the focal length that gives.\n"
		"- A lens is always one the kit has, from its list in <equipment>. When the request asks for a focal length "
		"the kit does not have, such as an 80 from a kit with a 75 and a 100, give the nearest it has "
		"and say so in the summary.\n"
		"- When the plan has a move and the request asks for a shot to keep, add the shot and then bake the move, "
		"in that order. A plan that only sets the camera up needs neither.\n"
		"- These steps cannot light the set, move or animate anything but the camera, time a rack focus, change "
		"frame lines or render. When the request asks for any of that, plan the part these steps can do and say "
		"plainly in the summary what is left for the user. When no part can be done, give no steps.\n"
		"\n"
		"The summary is for the director: one to three sentences in film language, with no ids and no JSON.");

	/** The API's name for an effort, or null to leave it to the model. */
	const TCHAR* GetEffortName(ERoscAssistantEffort Effort)
	{
		switch (Effort)
		{
		case ERoscAssistantEffort::Low: return TEXT("low");
		case ERoscAssistantEffort::Medium: return TEXT("medium");
		case ERoscAssistantEffort::High: return TEXT("high");
		default: return nullptr;
		}
	}

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

FRoscClaudeModel::~FRoscClaudeModel()
{
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		Request->CancelRequest();
	}
}

FString FRoscClaudeModel::GetHost() const
{
	return Rosc::AIConsent::GetHost(GetDefault<URoscAISettings>()->AssistantUrl.TrimStartAndEnd());
}

bool FRoscClaudeModel::CanAsk(FText& OutWhyNot) const
{
	const URoscAISettings* Settings = GetDefault<URoscAISettings>();
	const FString Url = Settings->AssistantUrl.TrimStartAndEnd();
	if (Settings->AssistantKey.TrimStartAndEnd().IsEmpty())
	{
		OutWhyNot = LOCTEXT("NoKey", "Give your Claude API key under Editor Preferences > Plugins > Rosc AI.");
		return false;
	}
	if (Settings->AssistantModel.TrimStartAndEnd().IsEmpty())
	{
		OutWhyNot = LOCTEXT("NoModel", "Name the Claude model to ask under Editor Preferences > Plugins > Rosc AI.");
		return false;
	}
	// A key is only ever sent over a line that hides it, except to this machine.
	const bool bHidden = Url.StartsWith(TEXT("https://")) || (Url.StartsWith(TEXT("http://")) && Rosc::AIConsent::IsThisMachine(GetHost()));
	if (!bHidden || GetHost().IsEmpty())
	{
		OutWhyNot = LOCTEXT("NoAddress", "The Messages API address under Editor Preferences > Plugins > Rosc AI must start with https://.");
		return false;
	}
	return true;
}

void FRoscClaudeModel::Ask(const FRoscAssistantQuestion& Question, TFunction<void(FRoscAssistantAnswer&&)> InOnDone)
{
	check(IsInGameThread() && !Request.IsValid());
	const URoscAISettings* Settings = GetDefault<URoscAISettings>();

	const TSharedPtr<FJsonObject> Schema = ReadAssistantJson(Question.AnswerSchema);
	check(Schema.IsValid());
	const TSharedRef<FJsonObject> Format = MakeShared<FJsonObject>();
	Format->SetStringField(TEXT("type"), TEXT("json_schema"));
	Format->SetObjectField(TEXT("schema"), Schema);
	const TSharedRef<FJsonObject> OutputConfig = MakeShared<FJsonObject>();
	OutputConfig->SetObjectField(TEXT("format"), Format);
	if (const TCHAR* Effort = GetEffortName(Settings->AssistantEffort))
	{
		OutputConfig->SetStringField(TEXT("effort"), Effort);
	}

	// The reference goes first and is marked to be kept, so the next question within minutes reads it back cheaply.
	TArray<TSharedPtr<FJsonValue>> Content;
	if (!Question.Reference.IsEmpty())
	{
		const TSharedRef<FJsonObject> Keep = MakeShared<FJsonObject>();
		Keep->SetStringField(TEXT("type"), TEXT("ephemeral"));
		const TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
		Reference->SetStringField(TEXT("type"), TEXT("text"));
		Reference->SetStringField(TEXT("text"), Question.Reference);
		Reference->SetObjectField(TEXT("cache_control"), Keep);
		Content.Add(MakeShared<FJsonValueObject>(Reference));
	}
	const TSharedRef<FJsonObject> Words = MakeShared<FJsonObject>();
	Words->SetStringField(TEXT("type"), TEXT("text"));
	Words->SetStringField(TEXT("text"), Question.User);
	Content.Add(MakeShared<FJsonValueObject>(Words));

	const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("role"), TEXT("user"));
	Message->SetArrayField(TEXT("content"), Content);

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
		const TSharedPtr<FRoscClaudeModel> This = Weak.Pin();
		if (This.IsValid() && This->Request == Answered)
		{
			const bool bAnswered = bConnected && Response.IsValid();
			This->Finish(This->ReadResponse(bAnswered ? Response->GetResponseCode() : 0, bAnswered ? Response->GetContent() : TArray<uint8>()));
		}
	});
	Request->ProcessRequest();
}

void FRoscClaudeModel::Cancel()
{
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		Request->CancelRequest();
		Finish(FRoscAssistantAnswer());
	}
}

void FRoscClaudeModel::Finish(FRoscAssistantAnswer&& Answer)
{
	Request.Reset();
	// The caller may ask the next question in the call.
	const TFunction<void(FRoscAssistantAnswer&&)> Done = MoveTemp(OnDone);
	OnDone = nullptr;
	Done(MoveTemp(Answer));
}

FRoscAssistantAnswer FRoscClaudeModel::ReadResponse(int32 Code, const TArray<uint8>& Content) const
{
	const FText Host = FText::FromString(GetHost());
	if (Code == 0)
	{
		return FRoscAssistantAnswer::Failed(FText::Format(LOCTEXT("NoAnswer", "{0} did not answer. Check the connection and try again."), Host));
	}

	const TSharedPtr<FJsonObject> Answer = ReadAssistantJson(ToText(Content));
	if (Code != 200)
	{
		if (Code == 401 || Code == 403)
		{
			return FRoscAssistantAnswer::Failed(FText::Format(LOCTEXT("WrongKey", "{0} did not accept the key. Check it under Editor Preferences > Plugins > Rosc AI."), Host));
		}
		if (Code == 429)
		{
			return FRoscAssistantAnswer::Failed(FText::Format(LOCTEXT("TooMany", "{0} is taking no more requests from this key for now. Try again in a moment."), Host));
		}
		if (Code >= 500)
		{
			return FRoscAssistantAnswer::Failed(FText::Format(LOCTEXT("Busy", "{0} is busy. Try again in a moment."), Host));
		}
		// What is left is a request the API would not take; it says why.
		const TSharedPtr<FJsonObject>* Error = nullptr;
		FString Why;
		if (Answer.IsValid() && Answer->TryGetObjectField(TEXT("error"), Error) && (*Error)->TryGetStringField(TEXT("message"), Why) && !Why.IsEmpty())
		{
			return FRoscAssistantAnswer::Failed(FText::Format(LOCTEXT("Refused", "{0} would not take the request: {1}"), Host, FText::FromString(Why)));
		}
		return FRoscAssistantAnswer::Failed(FText::Format(LOCTEXT("OddCode", "{0} gave an answer Rosc does not understand ({1})."), Host, FText::AsNumber(Code, &FNumberFormattingOptions::DefaultNoGrouping())));
	}

	const FText Odd = FText::Format(LOCTEXT("OddAnswer", "{0} gave an answer Rosc does not understand."), Host);
	if (!Answer.IsValid())
	{
		return FRoscAssistantAnswer::Failed(Odd);
	}

	FRoscAssistantAnswer Result;
	if (const TSharedPtr<FJsonObject>* Usage = nullptr; Answer->TryGetObjectField(TEXT("usage"), Usage))
	{
		// input_tokens counts only what was neither stored nor read back.
		int32 Uncached = 0;
		(*Usage)->TryGetNumberField(TEXT("input_tokens"), Uncached);
		(*Usage)->TryGetNumberField(TEXT("cache_creation_input_tokens"), Result.CacheWriteTokens);
		(*Usage)->TryGetNumberField(TEXT("cache_read_input_tokens"), Result.CacheReadTokens);
		(*Usage)->TryGetNumberField(TEXT("output_tokens"), Result.OutputTokens);
		Result.InputTokens = Uncached + Result.CacheWriteTokens + Result.CacheReadTokens;
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

FRoscAssistantQuestion Rosc::AIAssistant::MakeShotQuestion(const FString& Words, const FString& SetDescription)
{
	FRoscAssistantQuestion Question;
	Question.System = ShotAssistantSystem;
	Question.Reference = FString::Printf(TEXT("<equipment>\n%s\n</equipment>"), *Rosc::ShotPlan::DescribeEquipment(FRoscCameraDatabase::Get()));
	Question.User = FString::Printf(TEXT("<set>\n%s\n</set>\n\n<request>\n%s\n</request>"), *SetDescription, *Words.TrimStartAndEnd());
	Question.AnswerSchema = Rosc::ShotPlan::GetJsonSchema();
	return Question;
}

#undef LOCTEXT_NAMESPACE
