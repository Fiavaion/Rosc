#pragma once

#include "CoreMinimal.h"

/**
 * The consent gate: nothing is sent off this machine to a host until the user has agreed to it,
 * for this project and that host. Every path that sends data checks IsGiven itself, so no
 * caller can skip it. What the user agreed to is kept in their own settings for the project.
 */
namespace UEcine::AIConsent
{
	/** The host of an address: "render01" of "https://render01:8750/v1". Empty when there is none. */
	UECINEAI_API FString GetHost(const FString& Url);

	/** True for an address of this machine itself, which nothing leaves the machine to reach. */
	UECINEAI_API bool IsThisMachine(const FString& Host);

	/** True when data may be sent to the host: it is this machine, or the user has agreed. */
	UECINEAI_API bool IsGiven(const FString& Host);

	/** Records that the user agreed, having been told what is sent and where. */
	UECINEAI_API void Give(const FString& Host);

	UECINEAI_API void Withdraw(const FString& Host);

	/** The hosts the user has agreed to, for the AI tab to list. */
	UECINEAI_API TArray<FString> GetHosts();
}
