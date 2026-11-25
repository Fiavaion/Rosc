// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "RoscAIBackend.h"

/** A server that speaks rosc-ai/1, and how to reach it. */
class IRoscAIServer
{
public:
	virtual ~IRoscAIServer() = default;

	/** The server's address, such as http://127.0.0.1:50211. Empty until it is known. */
	virtual FString GetUrl() const = 0;
	/** The key the server asks callers for. Empty when it asks for none. */
	virtual FString GetKey() const = 0;
	/** The host a job would be sent to, known before the server is up. */
	virtual FString GetHost() const = 0;
	/** Whether the server can be used at all; when not, why, in words. */
	virtual bool IsUsable(FText& OutWhyNot) const = 0;
	/** Makes the server ready for a job, starting it if need be. OnReady is called on the game thread. */
	virtual void Prepare(TFunction<void(bool bReady, const FText& WhyNot)> OnReady) = 0;
};

/** The server elsewhere: the address and key in the user's settings. */
class FRoscAIRemoteServer : public IRoscAIServer
{
public:
	virtual FString GetUrl() const override;
	virtual FString GetKey() const override;
	virtual FString GetHost() const override;
	virtual bool IsUsable(FText& OutWhyNot) const override;
	virtual void Prepare(TFunction<void(bool, const FText&)> OnReady) override;
};

/**
 * Runs picture jobs on a server that speaks rosc-ai/1: the helper on this machine, or a
 * server elsewhere. The picture goes as a PNG no more than 2048 pixels on its long side, and
 * the result comes back at that size and is brought to the picture's own.
 */
class FRoscAIServerBackend : public IRoscAIBackend, public TSharedFromThis<FRoscAIServerBackend>
{
public:
	explicit FRoscAIServerBackend(const TSharedRef<IRoscAIServer>& InServer) : Server(InServer) {}
	virtual ~FRoscAIServerBackend() override;

	virtual FString GetHost() const override { return Server->GetHost(); }
	virtual bool CanRun(ERoscAITask Task, FText& OutWhyNot) const override { return Server->IsUsable(OutWhyNot); }
	virtual void Run(ERoscAITask Task, const FString& PictureFile, TFunction<void(FRoscAIOutput&&)> OnDone) override;
	virtual void GetProgress(FText& OutStage, float& OutFraction) const override;
	virtual void Cancel() override;

	/** Asks the server what it can do. Calls back on the game thread with whether it answered and what to tell the user. */
	void CheckHealth(TFunction<void(bool bReachable, const FText& Message)> OnChecked);

private:
	using FOnResponse = TFunction<void(int32 Code, const TArray<uint8>& Content)>;

	/** Sends a request to the server. A server that does not answer is code 0. */
	void Send(const TCHAR* Verb, const FString& Path, TArray<uint8>&& Body, FOnResponse OnResponse);
	void PostJob(TArray64<uint8>&& Png);
	bool Poll(float DeltaTime);
	void FetchPicture(const FString& Path);
	void Finish(FRoscAIOutput&& Output);
	/** What to tell the user about an answer that is not the one hoped for. */
	FText DescribeFailure(int32 Code, const TArray<uint8>& Content) const;

	TSharedRef<IRoscAIServer> Server;

	/** The job that is running. Each job has a number, so an answer that comes late is known for one. */
	int32 RunNumber = 0;
	bool bRunning = false;
	ERoscAITask RunningTask = ERoscAITask::Depth;
	FIntPoint PictureSize = FIntPoint::ZeroValue;
	TFunction<void(FRoscAIOutput&&)> OnDone;
	FString JobId;
	FText Stage;
	float Fraction = 0.0f;
	bool bPolling = false;
	FTSTicker::FDelegateHandle PollHandle;
	FHttpRequestPtr Request;
};
