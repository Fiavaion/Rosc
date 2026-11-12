#include "UEcineAISubsystem.h"

#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "UEcineAIAssistant.h"
#include "UEcineAIBackend.h"
#include "UEcineAIConsent.h"
#include "UEcineAIEditorBackend.h"
#include "UEcineAIHelper.h"
#include "UEcineAIModelStore.h"
#include "UEcineAIServerBackend.h"

#define LOCTEXT_NAMESPACE "UEcineAISubsystem"

void UUEcineAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EditorBackend = MakeShared<FUEcineAIEditorBackend>();
	Helper = MakeShared<FUEcineAIHelper>();
	HelperBackend = MakeShared<FUEcineAIServerBackend>(Helper.ToSharedRef());
	ServerBackend = MakeShared<FUEcineAIServerBackend>(MakeShared<FUEcineAIRemoteServer>());
	AssistantModel = MakeShared<FUEcineClaudeModel>();
}

void UUEcineAISubsystem::Deinitialize()
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

IUEcineAIBackend& UUEcineAISubsystem::GetBackend(EUEcineAIBackend Backend) const
{
	switch (Backend)
	{
	case EUEcineAIBackend::Helper: return *HelperBackend;
	case EUEcineAIBackend::Server: return *ServerBackend;
	default: return *EditorBackend;
	}
}

bool UUEcineAISubsystem::CanRun(EUEcineAITask Task, FText& OutWhyNot) const
{
	return GetBackend(GetDefault<UUEcineAISettings>()->Backend).CanRun(Task, OutWhyNot);
}

FString UUEcineAISubsystem::GetHostNeedingConsent() const
{
	const FString Host = GetBackend(GetDefault<UUEcineAISettings>()->Backend).GetHost();
	return Host.IsEmpty() || UEcine::AIConsent::IsGiven(Host) ? FString() : Host;
}

bool UUEcineAISubsystem::Submit(const FUEcineAIRequest& Request, FText& OutError)
{
	if (!FPaths::FileExists(Request.PictureFile))
	{
		OutError = FText::Format(LOCTEXT("NoPicture", "{0} is not there."), FText::FromString(Request.PictureFile));
		return false;
	}
	const EUEcineAIBackend Backend = GetDefault<UUEcineAISettings>()->Backend;
	if (!GetBackend(Backend).CanRun(Request.Task, OutError))
	{
		return false;
	}
	// The consent gate: every job for a host elsewhere stops here until the user has agreed to that host.
	const FString Host = GetBackend(Backend).GetHost();
	if (!Host.IsEmpty() && !UEcine::AIConsent::IsGiven(Host))
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

void UUEcineAISubsystem::GetProgress(FText& OutStage, float& OutFraction) const
{
	OutStage = FText::GetEmpty();
	OutFraction = 0.0f;
	if (bRunning)
	{
		GetBackend(Running.Backend).GetProgress(OutStage, OutFraction);
	}
}

void UUEcineAISubsystem::CancelAll()
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
		FUEcineAIResult Result;
		Result.Request = Job.Request;
		LastResult = Result;
		JobFinished.Broadcast(LastResult);
	}
}

FString UUEcineAISubsystem::GetResultFolder(const FString& PictureFile)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEcine") / TEXT("AI") / FPaths::GetBaseFilename(PictureFile));
}

void UUEcineAISubsystem::StartNext()
{
	check(!bRunning && !Waiting.IsEmpty());
	Running = Waiting[0];
	Waiting.RemoveAt(0);
	bRunning = true;
	const int32 Run = ++RunNumber;

	GetBackend(Running.Backend).Run(Running.Request.Task, Running.Request.PictureFile, [WeakThis = TWeakObjectPtr<UUEcineAISubsystem>(this), Run](FUEcineAIOutput&& Output)
	{
		UUEcineAISubsystem* This = WeakThis.Get();
		if (This == nullptr || !This->bRunning || This->RunNumber != Run)
		{
			return;
		}

		FUEcineAIResult Result;
		Result.Request = This->Running.Request;
		Result.Error = Output.Error;
		Result.bSuccess = Output.bSuccess;
		if (Output.bSuccess && Result.Request.Task == EUEcineAITask::FocalLength)
		{
			Result.FieldOfViewDeg = Output.FieldOfViewDeg;
		}
		else if (Output.bSuccess)
		{
			const FString Folder = GetResultFolder(Result.Request.PictureFile);
			Result.File = Folder / FString::Printf(TEXT("%s_%s.png"), *FPaths::GetBaseFilename(Result.Request.PictureFile), UEcine::AI::GetTaskId(Result.Request.Task));
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

void UUEcineAISubsystem::Finish(FUEcineAIResult&& Result)
{
	bRunning = false;
	LastResult = MoveTemp(Result);
	JobFinished.Broadcast(LastResult);
	if (!bRunning && !Waiting.IsEmpty())
	{
		StartNext();
	}
}

bool UUEcineAISubsystem::DownloadModel(const FUEcineAIModel& Model)
{
	if (Download.IsValid())
	{
		return false;
	}
	DownloadError = FText::GetEmpty();
	Download = FUEcineAIModelDownload::Start(Model, FUEcineAIModelDownload::FOnDone::CreateWeakLambda(this, [this](bool bSuccess, const FText& Error)
	{
		DownloadError = Error;
		Download.Reset();
	}));
	return true;
}

const FUEcineAIModel* UUEcineAISubsystem::GetDownloadingModel() const
{
	return Download.IsValid() ? &Download->GetModel() : nullptr;
}

float UUEcineAISubsystem::GetDownloadFraction() const
{
	return Download.IsValid() ? Download->GetFraction() : 0.0f;
}

void UUEcineAISubsystem::CancelDownload()
{
	if (Download.IsValid())
	{
		Download->Cancel();
	}
}

FText UUEcineAISubsystem::GetHelperStatus() const
{
	return Helper->GetStatus();
}

void UUEcineAISubsystem::CheckServer(TFunction<void(bool, const FText&)> OnChecked)
{
	ServerBackend->CheckHealth(MoveTemp(OnChecked));
}

bool UUEcineAISubsystem::CanPlanShot(FText& OutWhyNot) const
{
	return AssistantModel->CanAsk(OutWhyNot);
}

FString UUEcineAISubsystem::GetAssistantHostNeedingConsent() const
{
	const FString Host = AssistantModel->GetHost();
	return Host.IsEmpty() || UEcine::AIConsent::IsGiven(Host) ? FString() : Host;
}

bool UUEcineAISubsystem::PlanShot(const FString& Words, const FString& SetDescription, FText& OutError, TFunction<void(const FUEcineShotPlanResult&)> OnDone)
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
	if (!UEcine::AIConsent::IsGiven(Host))
	{
		OutError = FText::Format(LOCTEXT("NoAssistantConsent", "You have not agreed to send shot descriptions to {0}."), FText::FromString(Host));
		return false;
	}

	AssistantModel->Ask(UEcine::AIAssistant::MakeShotQuestion(Words, SetDescription), [OnDone = MoveTemp(OnDone)](FUEcineAssistantAnswer&& Answer)
	{
		FUEcineShotPlanResult Result;
		Result.Error = Answer.Error;
		Result.InputTokens = Answer.InputTokens;
		Result.OutputTokens = Answer.OutputTokens;
		if (Answer.bSuccess)
		{
			FString Fault;
			Result.PlanJson = Answer.Json;
			Result.bSuccess = UEcine::ShotPlan::Parse(Answer.Json, Result.Plan, Fault);
			if (!Result.bSuccess)
			{
				Result.Error = FText::Format(LOCTEXT("OddPlan", "The plan that came back could not be read. {0}"), FText::FromString(Fault));
			}
		}
		OnDone(Result);
	});
	return true;
}

bool UUEcineAISubsystem::IsPlanningShot() const
{
	return AssistantModel->IsAsking();
}

void UUEcineAISubsystem::CancelShotPlan()
{
	AssistantModel->Cancel();
}

#undef LOCTEXT_NAMESPACE
