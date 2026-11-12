// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

/** What is asked of a language model: who it is, what it is told, and the JSON Schema its answer must fit. */
struct FUEcineAssistantQuestion
{
	FString System;
	FString User;
	FString AnswerSchema;
};

/** A language model's answer to one question. */
struct FUEcineAssistantAnswer
{
	bool bSuccess = false;
	/** Why there is no answer. Empty for a question the user gave up. */
	FText Error;
	/** The answer, as JSON that fits the question's schema. */
	FString Json;
	/** What the question and the answer came to, as the model's keeper counts them. */
	int32 InputTokens = 0;
	int32 OutputTokens = 0;

	static FUEcineAssistantAnswer Failed(const FText& Why)
	{
		FUEcineAssistantAnswer Answer;
		Answer.Error = Why;
		return Answer;
	}
};

/** A language model the shot assistant can ask. Every one of them is on another machine. */
class IUEcineAssistantModel
{
public:
	virtual ~IUEcineAssistantModel() = default;

	/** The host a question is sent to. */
	virtual FString GetHost() const = 0;
	/** Whether the model can be asked at all; when not, why, in words. */
	virtual bool CanAsk(FText& OutWhyNot) const = 0;
	/** Asks one question; one at a time. OnDone is called on the game thread, also when the question is given up. */
	virtual void Ask(const FUEcineAssistantQuestion& Question, TFunction<void(FUEcineAssistantAnswer&&)> OnDone) = 0;
	virtual bool IsAsking() const = 0;
	/** Gives up the question being asked. */
	virtual void Cancel() = 0;
};

/**
 * Claude, through the Messages API, with the user's own key and the model named in their
 * settings. The answer is held to the question's schema by the API (structured outputs).
 */
class FUEcineClaudeModel : public IUEcineAssistantModel, public TSharedFromThis<FUEcineClaudeModel>
{
public:
	virtual ~FUEcineClaudeModel() override;

	virtual FString GetHost() const override;
	virtual bool CanAsk(FText& OutWhyNot) const override;
	virtual void Ask(const FUEcineAssistantQuestion& Question, TFunction<void(FUEcineAssistantAnswer&&)> OnDone) override;
	virtual bool IsAsking() const override { return Request.IsValid(); }
	virtual void Cancel() override;

private:
	FUEcineAssistantAnswer ReadResponse(int32 Code, const TArray<uint8>& Content) const;
	void Finish(FUEcineAssistantAnswer&& Answer);

	FHttpRequestPtr Request;
	TFunction<void(FUEcineAssistantAnswer&&)> OnDone;
};

namespace UEcine::AIAssistant
{
	/**
	 * The question that turns a shot described in words into a shot plan.
	 * @param Words What the user typed.
	 * @param SetDescription The camera, its support, what is in the level and the open sequence, as JSON.
	 */
	FUEcineAssistantQuestion MakeShotQuestion(const FString& Words, const FString& SetDescription);
}
