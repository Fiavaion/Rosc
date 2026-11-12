// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineAIConsent.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "UEcineAISettings.h"

FString UEcine::AIConsent::GetHost(const FString& Url)
{
	return FGenericPlatformHttp::GetUrlDomain(Url).ToLower();
}

bool UEcine::AIConsent::IsThisMachine(const FString& Host)
{
	return Host == TEXT("127.0.0.1") || Host == TEXT("localhost") || Host == TEXT("::1") || Host == TEXT("[::1]");
}

bool UEcine::AIConsent::IsGiven(const FString& Host)
{
	return !Host.IsEmpty() && (IsThisMachine(Host) || GetDefault<UUEcineAISettings>()->ConsentedHosts.Contains(Host));
}

void UEcine::AIConsent::Give(const FString& Host)
{
	UUEcineAISettings* Settings = GetMutableDefault<UUEcineAISettings>();
	if (!Host.IsEmpty() && !Settings->ConsentedHosts.Contains(Host))
	{
		Settings->ConsentedHosts.Add(Host);
		Settings->SaveConfig();
	}
}

void UEcine::AIConsent::Withdraw(const FString& Host)
{
	UUEcineAISettings* Settings = GetMutableDefault<UUEcineAISettings>();
	if (Settings->ConsentedHosts.Remove(Host) > 0)
	{
		Settings->SaveConfig();
	}
}

TArray<FString> UEcine::AIConsent::GetHosts()
{
	return GetDefault<UUEcineAISettings>()->ConsentedHosts;
}
