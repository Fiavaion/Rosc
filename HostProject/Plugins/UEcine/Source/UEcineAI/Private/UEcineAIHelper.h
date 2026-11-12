#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "UEcineAIServerBackend.h"

/**
 * The helper on this machine: UEcine's own server script (Resources/AI/uecine_ai_server.py),
 * started with the user's Python when a job first needs it and stopped with the editor. It
 * listens on this machine only, on a port and with a key made for this run of the editor.
 */
class FUEcineAIHelper : public IUEcineAIServer, public TSharedFromThis<FUEcineAIHelper>
{
public:
	virtual ~FUEcineAIHelper() override;

	virtual FString GetUrl() const override { return Url; }
	virtual FString GetKey() const override { return Key; }
	virtual FString GetHost() const override { return TEXT("127.0.0.1"); }
	virtual bool IsUsable(FText& OutWhyNot) const override;
	virtual void Prepare(TFunction<void(bool, const FText&)> OnReady) override;

	/** How the helper stands, in words. */
	FText GetStatus() const;

	/** Stops the helper. The next job starts it again, with the Python and models folder the settings then give. */
	void Stop();

private:
	enum class EState : uint8
	{
		Stopped,
		Checking,
		Starting,
		Ready,
		Failed
	};

	void Launch();
	bool Watch(float DeltaTime);
	void Fail(const FText& Why);
	void TellWaiting(bool bReady);

	EState State = EState::Stopped;
	FText Failure;
	FString Url;
	FString Key;

	FProcHandle Process;
	/** The helper's standard output and error, read here. */
	void* OutputRead = nullptr;
	void* OutputWrite = nullptr;
	void* ErrorRead = nullptr;
	void* ErrorWrite = nullptr;
	/** The helper's standard input. It is never written to: the helper stops when it closes, as it does when the editor goes away. */
	void* InputRead = nullptr;
	void* InputWrite = nullptr;
	FString Output;
	FString Errors;
	double StartedAt = 0.0;

	FTSTicker::FDelegateHandle WatchHandle;
	TArray<TFunction<void(bool, const FText&)>> WaitingForReady;
};
