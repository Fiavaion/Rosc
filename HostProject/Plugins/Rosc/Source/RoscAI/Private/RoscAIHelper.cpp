// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscAIHelper.h"

#include "Async/Async.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "RoscAIModelStore.h"
#include "RoscAISettings.h"

#define LOCTEXT_NAMESPACE "RoscAIHelper"

namespace
{
	/** What the helper prints when it is listening, followed by its address. */
	const TCHAR* HelperReadyMark = TEXT("ROSC-AI READY ");
	/** What it prints when its Python lacks a package, followed by the package. */
	const TCHAR* HelperMissingMark = TEXT("ROSC-AI MISSING ");
	const TCHAR* HelperPackages = TEXT("onnxruntime numpy pillow");
	/** Loading Python and its packages takes a few seconds; a helper that says nothing for this long is not coming. */
	constexpr double HelperStartSeconds = 60.0;

	FString GetHelperScript()
	{
		return FPaths::GetPath(FRoscAIModelList::GetListFile()) / TEXT("rosc_ai_server.py");
	}

	FString GetHelperPython()
	{
		const FString& Chosen = GetDefault<URoscAISettings>()->HelperPython.FilePath;
		return Chosen.IsEmpty() ? FString(TEXT("python")) : FPaths::ConvertRelativePathToFull(Chosen);
	}
}

FRoscAIHelper::~FRoscAIHelper()
{
	Stop();
}

bool FRoscAIHelper::IsUsable(FText& OutWhyNot) const
{
	// A helper that failed is tried again by the next job: the user may have put right what was wrong.
	return true;
}

void FRoscAIHelper::Prepare(TFunction<void(bool, const FText&)> OnReady)
{
	if (State == EState::Ready && FPlatformProcess::IsProcRunning(Process))
	{
		OnReady(true, FText::GetEmpty());
		return;
	}
	if (State == EState::Checking || State == EState::Starting)
	{
		WaitingForReady.Add(MoveTemp(OnReady));
		return;
	}

	// A helper that failed or went away is started afresh.
	Stop();
	WaitingForReady.Add(MoveTemp(OnReady));
	State = EState::Checking;

	// The helper loads whatever is in the models folder, so each file there is checked against the list first.
	TArray<FRoscAIModel> Installed;
	for (const FRoscAIModel& Model : FRoscAIModelList::Get().GetModels())
	{
		if (Rosc::AIModelStore::IsInstalled(Model))
		{
			Installed.Add(Model);
		}
	}
	if (Installed.IsEmpty())
	{
		Fail(LOCTEXT("NoModels", "No model is downloaded yet. Download one below."));
		return;
	}
	Async(EAsyncExecution::ThreadPool, [Weak = AsWeak(), Installed = MoveTemp(Installed)]
	{
		FText Error;
		for (const FRoscAIModel& Model : Installed)
		{
			if (!Rosc::AIModelStore::Verify(Model, Error))
			{
				break;
			}
		}
		AsyncTask(ENamedThreads::GameThread, [Weak, Error]
		{
			const TSharedPtr<FRoscAIHelper> This = Weak.Pin();
			if (!This.IsValid() || This->State != EState::Checking)
			{
				return;
			}
			if (Error.IsEmpty())
			{
				This->Launch();
			}
			else
			{
				This->Fail(Error);
			}
		});
	});
}

FText FRoscAIHelper::GetStatus() const
{
	switch (State)
	{
	case EState::Stopped: return LOCTEXT("StatusStopped", "The helper starts with the first job.");
	case EState::Checking: return LOCTEXT("StatusChecking", "Checking the model files…");
	case EState::Starting: return LOCTEXT("StatusStarting", "The helper is starting…");
	case EState::Ready: return LOCTEXT("StatusReady", "The helper is running.");
	case EState::Failed: return Failure;
	}
	return FText::GetEmpty();
}

void FRoscAIHelper::Stop()
{
	FTSTicker::GetCoreTicker().RemoveTicker(WatchHandle);
	WatchHandle.Reset();
	if (Process.IsValid())
	{
		FPlatformProcess::TerminateProc(Process, true);
		FPlatformProcess::CloseProc(Process);
	}
	FPlatformProcess::ClosePipe(OutputRead, OutputWrite);
	FPlatformProcess::ClosePipe(ErrorRead, ErrorWrite);
	FPlatformProcess::ClosePipe(InputRead, InputWrite);
	OutputRead = OutputWrite = ErrorRead = ErrorWrite = InputRead = InputWrite = nullptr;
	Output.Reset();
	Errors.Reset();
	Url.Reset();
	State = EState::Stopped;
	TellWaiting(false);
}

void FRoscAIHelper::Launch()
{
	const FString Script = GetHelperScript();
	if (!FPaths::FileExists(Script))
	{
		Fail(FText::Format(LOCTEXT("NoScript", "The helper's script is missing from the plugin: {0}"), FText::FromString(Script)));
		return;
	}

	Key = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Arguments = FString::Printf(TEXT("\"%s\" --models-folder \"%s\" --models-list \"%s\" --key %s --stop-with-input"),
		*Script, *Rosc::AIModelStore::GetFolder(), *FRoscAIModelList::GetListFile(), *Key);

	verify(FPlatformProcess::CreatePipe(OutputRead, OutputWrite));
	verify(FPlatformProcess::CreatePipe(ErrorRead, ErrorWrite));
	verify(FPlatformProcess::CreatePipe(InputRead, InputWrite, true));
	Process = FPlatformProcess::CreateProc(*GetHelperPython(), *Arguments, false, true, true, nullptr, 0, nullptr, OutputWrite, InputRead, ErrorWrite);
	if (!Process.IsValid())
	{
		Fail(FText::Format(LOCTEXT("NoPython", "Python could not be started ({0}). Give the python.exe to use under Editor Preferences > Plugins > Rosc AI."), FText::FromString(GetHelperPython())));
		return;
	}
	State = EState::Starting;
	StartedAt = FPlatformTime::Seconds();
	WatchHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FRoscAIHelper::Watch), 0.1f);
}

bool FRoscAIHelper::Watch(float DeltaTime)
{
	Output += FPlatformProcess::ReadPipe(OutputRead);
	Errors += FPlatformProcess::ReadPipe(ErrorRead);

	if (State == EState::Starting)
	{
		int32 MarkAt = Output.Find(HelperReadyMark);
		int32 LineEnd = INDEX_NONE;
		if (MarkAt != INDEX_NONE && Output.Mid(MarkAt).FindChar(TEXT('\n'), LineEnd))
		{
			MarkAt += FCString::Strlen(HelperReadyMark);
			Url = Output.Mid(MarkAt, LineEnd - FCString::Strlen(HelperReadyMark)).TrimStartAndEnd();
			State = EState::Ready;
			TellWaiting(true);
			return true;
		}
	}

	if (!FPlatformProcess::IsProcRunning(Process))
	{
		Errors += FPlatformProcess::ReadPipe(ErrorRead);
		const int32 MissingAt = Errors.Find(HelperMissingMark);
		if (MissingAt != INDEX_NONE)
		{
			FString Package = Errors.Mid(MissingAt + FCString::Strlen(HelperMissingMark));
			int32 LineEnd = INDEX_NONE;
			if (Package.FindChar(TEXT('\n'), LineEnd))
			{
				Package.LeftInline(LineEnd);
			}
			Fail(FText::Format(LOCTEXT("MissingPackage", "The helper's Python ({0}) has no {1}. Install what the helper needs with: python -m pip install {2}"),
				FText::FromString(GetHelperPython()), FText::FromString(Package.TrimStartAndEnd()), FText::FromString(HelperPackages)));
		}
		else
		{
			// The last thing Python said is what went wrong.
			TArray<FString> Lines;
			Errors.ParseIntoArrayLines(Lines);
			Fail(FText::Format(LOCTEXT("Stopped", "The helper stopped. {0}"), FText::FromString(Lines.IsEmpty() ? FString() : Lines.Last())));
		}
		return false;
	}
	if (State == EState::Starting && FPlatformTime::Seconds() - StartedAt > HelperStartSeconds)
	{
		Fail(LOCTEXT("TooSlow", "The helper did not start. Check the Python given under Editor Preferences > Plugins > Rosc AI."));
		return false;
	}
	return true;
}

void FRoscAIHelper::Fail(const FText& Why)
{
	const TArray<TFunction<void(bool, const FText&)>> Waiting = MoveTemp(WaitingForReady);
	WaitingForReady.Reset();
	Stop();
	State = EState::Failed;
	Failure = Why;
	for (const TFunction<void(bool, const FText&)>& Each : Waiting)
	{
		Each(false, Why);
	}
}

void FRoscAIHelper::TellWaiting(bool bReady)
{
	const TArray<TFunction<void(bool, const FText&)>> Waiting = MoveTemp(WaitingForReady);
	WaitingForReady.Reset();
	for (const TFunction<void(bool, const FText&)>& Each : Waiting)
	{
		Each(bReady, bReady ? FText::GetEmpty() : LOCTEXT("StoppedBeforeReady", "The helper was stopped."));
	}
}

#undef LOCTEXT_NAMESPACE
