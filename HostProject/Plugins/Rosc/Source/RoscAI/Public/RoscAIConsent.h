// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

struct FRoscAIModel;

/**
 * The consent gate: nothing is sent off this machine to a host until the user has agreed to it,
 * for this project and that host. Every path that sends data checks IsGiven itself, so no
 * caller can skip it. What the user agreed to is kept in their own settings for the project.
 */
namespace Rosc::AIConsent
{
	/** The host of an address: "render01" of "https://render01:8750/v1". Empty when there is none. */
	ROSCAI_API FString GetHost(const FString& Url);

	/** True for an address of this machine itself, which nothing leaves the machine to reach. */
	ROSCAI_API bool IsThisMachine(const FString& Host);

	/** True when data may be sent to the host: it is this machine, or the user has agreed. */
	ROSCAI_API bool IsGiven(const FString& Host);

	/** Records that the user agreed, having been told what is sent and where. */
	ROSCAI_API void Give(const FString& Host);

	ROSCAI_API void Withdraw(const FString& Host);

	/** The hosts the user has agreed to, for the AI tab to list. */
	ROSCAI_API TArray<FString> GetHosts();

	/**
	 * True when the user has agreed to the notice shown before a model's first download, as it
	 * reads now (ADR-012). A model is not downloaded until it is.
	 */
	ROSCAI_API bool IsModelNoticeAgreed(const FRoscAIModel& Model);

	/** Records that the user agreed to the model's notice, having been shown it. */
	ROSCAI_API void AgreeToModelNotice(const FRoscAIModel& Model);
}
