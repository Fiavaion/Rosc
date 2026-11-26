// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscAISubsystem.h"

#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "RoscAIAssistant.h"
#include "RoscAIBackend.h"
#include "RoscAIConsent.h"
#include "RoscAIEditorBackend.h"
#include "RoscAIHelper.h"
#include "RoscAIModelStore.h"
#include "RoscAIServerBackend.h"

#define LOCTEXT_NAMESPACE "RoscAISubsystem"

void URoscAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EditorBackend = MakeShared<FRoscAIEditorBackend>();
	Helper = MakeShared<FRoscAIHelper>();
	HelperBackend = MakeShared<FRoscAIServerBackend>(Helper.ToSharedRef());
	ServerBackend = MakeShared<FRoscAIServerBackend>(MakeShared<FRoscAIRemoteServer>());
	AssistantModel = MakeShared<FRoscClaudeModel>();
}

void URoscAISubsystem::Deinitialize()
{
	Waiting.Reset();
	bRunning = false;
	++RunNumber;
	AssistantModel.Reset();
	Download.Reset();
	ServerBackend.Reset();
	HelperBackend.Reset();
	Helper.Reset();
	EditorBackend.Reset();
	Super::Deinitialize();
}

IRoscAIBackend& URoscAISubsystem::GetBackend(ERoscAIBackend Backend) const
{
	switch (Backend)
	{
	case ERoscAIBackend::Helper: return *HelperBackend;
	case ERoscAIBackend::Server: return *ServerBackend;
	default: return *EditorBackend;
	}
}

bool URoscAISubsystem::CanRun(ERoscAITask Task, FText& OutWhyNot) const
{
	return GetBackend(GetDefault<URoscAISettings>()->Backend).CanRun(Task, OutWhyNot);
}

FString URoscAISubsystem::GetHostNeedingConsent() const
{
	const FString Host = GetBackend(GetDefault<URoscAISettings>()->Backend).GetHost();
	return Host.IsEmpty() || Rosc::AIConsent::IsGiven(Host) ? FString() : Host;
}

bool URoscAISubsystem::Submit(const FRoscAIRequest& Request, FText& OutError)
{
	if (!FPaths::FileExists(Request.PictureFile))
	{
		OutError = FText::Format(LOCTEXT("NoPicture", "{0} is not there."), FText::FromString(Request.PictureFile));
		return false;
	}
	const ERoscAIBackend Backend = GetDefault<URoscAISettings>()->Backend;
	if (!GetBackend(Backend).CanRun(Request.Task, OutError))
	{
		return false;
	}
	// The consent gate: every job for a host elsewhere stops here until the user has agreed to that host.
	const FString Host = GetBackend(Backend).GetHost();
	if (!Host.IsEmpty() && !Rosc::AIConsent::IsGiven(Host))
	{
		OutError = FText::Format(LOCTEXT("NoConsent", "You have not agreed to send pictures to {0}."), FText::FromString(Host));
		return false;
	}

	Waiting.Add({ Request, Backend });
	if (!bRunning)
	{
		StartNext();
	}
	return true;
}

void URoscAISubsystem::GetProgress(FText& OutStage, float& OutFraction) const
{
	OutStage = FText::GetEmpty();
	OutFraction = 0.0f;
	if (bRunning)
	{
		GetBackend(Running.Backend).GetProgress(OutStage, OutFraction);
	}
}

void URoscAISubsystem::CancelAll()
{
	// The jobs that wait end first, in their order; the running job ends when its backend lets go of it.
	const TArray<FWaitingJob> GivenUp = MoveTemp(Waiting);
	Waiting.Reset();
	if (bRunning)
	{
		GetBackend(Running.Backend).Cancel();
	}
	for (const FWaitingJob& Job : GivenUp)
	{
		FRoscAIResult Result;
		Result.Request = Job.Request;
		LastResult = Result;
		JobFinished.Broadcast(LastResult);
	}
}

FString URoscAISubsystem::GetResultFolder(const FString& PictureFile)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Rosc") / TEXT("AI") / FPaths::GetBaseFilename(PictureFile));
}

void URoscAISubsystem::StartNext()
{
	check(!bRunning && !Waiting.IsEmpty());
	Running = Waiting[0];
	Waiting.RemoveAt(0);
	bRunning = true;
	const int32 Run = ++RunNumber;

	GetBackend(Running.Backend).Run(Running.Request.Task, Running.Request.PictureFile, [WeakThis = TWeakObjectPtr<URoscAISubsystem>(this), Run](FRoscAIOutput&& Output)
	{
		URoscAISubsystem* This = WeakThis.Get();
		if (This == nullptr || !This->bRunning || This->RunNumber != Run)
		{
			return;
		}

		FRoscAIResult Result;
		Result.Request = This->Running.Request;
		Result.Error = Output.Error;
		Result.bSuccess = Output.bSuccess;
		if (Output.bSuccess && Result.Request.Task == ERoscAITask::FocalLength)
		{
			Result.FieldOfViewDeg = Output.FieldOfViewDeg;
		}
		else if (Output.bSuccess)
		{
			const FString Folder = GetResultFolder(Result.Request.PictureFile);
			Result.File = Folder / FString::Printf(TEXT("%s_%s.png"), *FPaths::GetBaseFilename(Result.Request.PictureFile), Rosc::AI::GetTaskId(Result.Request.Task));
			IFileManager::Get().MakeDirectory(*Folder, true);
			if (!FImageUtils::SaveImageByExtension(*Result.File, Output.Picture))
			{
				Result.bSuccess = false;
				Result.Error = FText::Format(LOCTEXT("CannotWrite", "Could not write {0}."), FText::FromString(Result.File));
				Result.File.Reset();
			}
		}
		This->Finish(MoveTemp(Result));
	});
}

void URoscAISubsystem::Finish(FRoscAIResult&& Result)
{
	bRunning = false;
	LastResult = MoveTemp(Result);
	JobFinished.Broadcast(LastResult);
	if (!bRunning && !Waiting.IsEmpty())
	{
		StartNext();
	}
}

bool URoscAISubsystem::DownloadModel(const FRoscAIModel& Model)
{
	if (Download.IsValid())
	{
		return false;
	}
	DownloadError = FText::GetEmpty();
	Download = FRoscAIModelDownload::Start(Model, FRoscAIModelDownload::FOnDone::CreateWeakLambda(this, [this](bool bSuccess, const FText& Error)
	{
		DownloadError = Error;
		Download.Reset();
	}));
	return true;
}

const FRoscAIModel* URoscAISubsystem::GetDownloadingModel() const
{
	return Download.IsValid() ? &Download->GetModel() : nullptr;
}

float URoscAISubsystem::GetDownloadFraction() const
{
	return Download.IsValid() ? Download->GetFraction() : 0.0f;
}

void URoscAISubsystem::CancelDownload()
{
	if (Download.IsValid())
	{
		Download->Cancel();
	}
}

FText URoscAISubsystem::GetHelperStatus() const
{
	return Helper->GetStatus();
}

void URoscAISubsystem::CheckServer(TFunction<void(bool, const FText&)> OnChecked)
{
	ServerBackend->CheckHealth(MoveTemp(OnChecked));
}

bool URoscAISubsystem::CanPlanShot(FText& OutWhyNot) const
{
	return AssistantModel->CanAsk(OutWhyNot);
}

FString URoscAISubsystem::GetAssistantHostNeedingConsent() const
{
	const FString Host = AssistantModel->GetHost();
	return Host.IsEmpty() || Rosc::AIConsent::IsGiven(Host) ? FString() : Host;
}

bool URoscAISubsystem::PlanShot(const FString& Words, const FString& SetDescription, FText& OutError, TFunction<void(const FRoscShotPlanResult&)> OnDone)
{
	if (Words.TrimStartAndEnd().IsEmpty())
	{
		OutError = LOCTEXT("NoWords", "Describe the shot first.");
		return false;
	}
	if (AssistantModel->IsAsking())
	{
		OutError = LOCTEXT("AlreadyPlanning", "The shot assistant is still working on the last request.");
		return false;
	}
	if (!AssistantModel->CanAsk(OutError))
	{
		return false;
	}
	// The consent gate: the user's words and the set's description stop here until the user has agreed to that host.
	const FString Host = AssistantModel->GetHost();
	if (!Rosc::AIConsent::IsGiven(Host))
	{
		OutError = FText::Format(LOCTEXT("NoAssistantConsent", "You have not agreed to send shot descriptions to {0}."), FText::FromString(Host));
		return false;
	}

	AssistantModel->Ask(Rosc::AIAssistant::MakeShotQuestion(Words, SetDescription), [OnDone = MoveTemp(OnDone)](FRoscAssistantAnswer&& Answer)
	{
		FRoscShotPlanResult Result;
		Result.Error = Answer.Error;
		Result.InputTokens = Answer.InputTokens;
		Result.OutputTokens = Answer.OutputTokens;
		if (Answer.bSuccess)
		{
			FString Fault;
			Result.PlanJson = Answer.Json;
			Result.bSuccess = Rosc::ShotPlan::Parse(Answer.Json, Result.Plan, Fault);
			if (!Result.bSuccess)
			{
				Result.Error = FText::Format(LOCTEXT("OddPlan", "The plan that came back could not be read. {0}"), FText::FromString(Fault));
			}
		}
		OnDone(Result);
	});
	return true;
}

bool URoscAISubsystem::IsPlanningShot() const
{
	return AssistantModel->IsAsking();
}

void URoscAISubsystem::CancelShotPlan()
{
	AssistantModel->Cancel();
}

#undef LOCTEXT_NAMESPACE
