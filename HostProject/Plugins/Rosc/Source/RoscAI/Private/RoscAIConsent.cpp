// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscAIConsent.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "RoscAIModels.h"
#include "RoscAISettings.h"

FString Rosc::AIConsent::GetHost(const FString& Url)
{
	return FGenericPlatformHttp::GetUrlDomain(Url).ToLower();
}

bool Rosc::AIConsent::IsThisMachine(const FString& Host)
{
	return Host == TEXT("127.0.0.1") || Host == TEXT("localhost") || Host == TEXT("::1") || Host == TEXT("[::1]");
}

bool Rosc::AIConsent::IsGiven(const FString& Host)
{
	return !Host.IsEmpty() && (IsThisMachine(Host) || GetDefault<URoscAISettings>()->ConsentedHosts.Contains(Host));
}

void Rosc::AIConsent::Give(const FString& Host)
{
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	if (!Host.IsEmpty() && !Settings->ConsentedHosts.Contains(Host))
	{
		Settings->ConsentedHosts.Add(Host);
		Settings->SaveConfig();
	}
}

void Rosc::AIConsent::Withdraw(const FString& Host)
{
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	if (Settings->ConsentedHosts.Remove(Host) > 0)
	{
		Settings->SaveConfig();
	}
}

namespace
{
	FString GetModelNoticeEntry(const FRoscAIModel& Model)
	{
		return Model.Id + TEXT(":") + Model.GetNoticeSignature();
	}
}

bool Rosc::AIConsent::IsModelNoticeAgreed(const FRoscAIModel& Model)
{
	return GetDefault<URoscAISettings>()->AcceptedModelNotices.Contains(GetModelNoticeEntry(Model));
}

void Rosc::AIConsent::AgreeToModelNotice(const FRoscAIModel& Model)
{
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	const FString Entry = GetModelNoticeEntry(Model);
	if (!Settings->AcceptedModelNotices.Contains(Entry))
	{
		// An agreement to the model's older notice is replaced, not kept beside it.
		Settings->AcceptedModelNotices.RemoveAll([&Model](const FString& Each) { return Each.StartsWith(Model.Id + TEXT(":")); });
		Settings->AcceptedModelNotices.Add(Entry);
		Settings->SaveConfig();
	}
}

TArray<FString> Rosc::AIConsent::GetHosts()
{
	return GetDefault<URoscAISettings>()->ConsentedHosts;
}
