// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

/** What is asked of a language model: who it is, what it is told, and the JSON Schema its answer must fit. */
struct FRoscAssistantQuestion
{
	FString System;
	/** What every question of this kind is told in the same words, sent before User. The model's keeper keeps it, so asking again within minutes costs less. */
	FString Reference;
	FString User;
	FString AnswerSchema;
};

/** A language model's answer to one question. */
struct FRoscAssistantAnswer
{
	bool bSuccess = false;
	/** Why there is no answer. Empty for a question the user gave up. */
	FText Error;
	/** The answer, as JSON that fits the question's schema. */
	FString Json;
	/** What the question and the answer came to, as the model's keeper counts them. InputTokens is all the question, the kept part too. */
	int32 InputTokens = 0;
	int32 OutputTokens = 0;
	/** Of InputTokens, those the keeper stored to keep (billed a little above the rest) and those it had kept (billed far below). */
	int32 CacheWriteTokens = 0;
	int32 CacheReadTokens = 0;

	static FRoscAssistantAnswer Failed(const FText& Why)
	{
		FRoscAssistantAnswer Answer;
		Answer.Error = Why;
		return Answer;
	}
};

/** A language model the shot assistant can ask. Every one of them is on another machine. */
class IRoscAssistantModel
{
public:
	virtual ~IRoscAssistantModel() = default;

	/** The host a question is sent to. */
	virtual FString GetHost() const = 0;
	/** Whether the model can be asked at all; when not, why, in words. */
	virtual bool CanAsk(FText& OutWhyNot) const = 0;
	/** Asks one question; one at a time. OnDone is called on the game thread, also when the question is given up. */
	virtual void Ask(const FRoscAssistantQuestion& Question, TFunction<void(FRoscAssistantAnswer&&)> OnDone) = 0;
	virtual bool IsAsking() const = 0;
	/** Gives up the question being asked. */
	virtual void Cancel() = 0;
};

/**
 * Claude, through the Messages API, with the user's own key and the model named in their
 * settings. The answer is held to the question's schema by the API (structured outputs).
 */
class FRoscClaudeModel : public IRoscAssistantModel, public TSharedFromThis<FRoscClaudeModel>
{
public:
	virtual ~FRoscClaudeModel() override;

	virtual FString GetHost() const override;
	virtual bool CanAsk(FText& OutWhyNot) const override;
	virtual void Ask(const FRoscAssistantQuestion& Question, TFunction<void(FRoscAssistantAnswer&&)> OnDone) override;
	virtual bool IsAsking() const override { return Request.IsValid(); }
	virtual void Cancel() override;

private:
	FRoscAssistantAnswer ReadResponse(int32 Code, const TArray<uint8>& Content) const;
	void Finish(FRoscAssistantAnswer&& Answer);

	FHttpRequestPtr Request;
	TFunction<void(FRoscAssistantAnswer&&)> OnDone;
};

namespace Rosc::AIAssistant
{
	/**
	 * The question that turns a shot described in words into a shot plan.
	 * @param Words What the user typed.
	 * @param SetDescription The camera, its support, what is in the level and the open sequence, as JSON.
	 */
	FRoscAssistantQuestion MakeShotQuestion(const FString& Words, const FString& SetDescription);
}
