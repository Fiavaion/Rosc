// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscAIServerBackend.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "ImageUtils.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "RoscAIConsent.h"
#include "RoscAIPictures.h"
#include "RoscAISettings.h"

#define LOCTEXT_NAMESPACE "RoscAIServerBackend"

namespace
{
	const TCHAR* ServerProtocolName = TEXT("rosc-ai/1");
	/** The longest side a picture is sent at. Models work at a fraction of this. */
	constexpr int32 ServerPictureLongSide = 2048;
	constexpr float ServerPollSeconds = 0.25f;
	constexpr float ServerAnswerSeconds = 30.0f;

	TSharedPtr<FJsonObject> ParseServerJson(const TArray<uint8>& Content)
	{
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Content.GetData()), Content.Num());
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get()));
		FJsonSerializer::Deserialize(Reader, Object);
		return Object;
	}

	FText GetServerTaskName(const FString& TaskId)
	{
		if (TaskId == Rosc::AI::GetTaskId(ERoscAITask::Depth))
		{
			return LOCTEXT("TaskDepth", "depth");
		}
		if (TaskId == Rosc::AI::GetTaskId(ERoscAITask::Matte))
		{
			return LOCTEXT("TaskMatte", "matte");
		}
		if (TaskId == Rosc::AI::GetTaskId(ERoscAITask::FocalLength))
		{
			return LOCTEXT("TaskFocal", "focal length");
		}
		return FText::FromString(TaskId);
	}
}

FString FRoscAIRemoteServer::GetUrl() const
{
	FString Url = GetDefault<URoscAISettings>()->ServerUrl.TrimStartAndEnd();
	while (Url.RemoveFromEnd(TEXT("/")))
	{
	}
	return Url;
}

FString FRoscAIRemoteServer::GetKey() const
{
	return GetDefault<URoscAISettings>()->ServerKey;
}

FString FRoscAIRemoteServer::GetHost() const
{
	return Rosc::AIConsent::GetHost(GetUrl());
}

bool FRoscAIRemoteServer::IsUsable(FText& OutWhyNot) const
{
	const FString Url = GetUrl();
	if ((!Url.StartsWith(TEXT("http://")) && !Url.StartsWith(TEXT("https://"))) || GetHost().IsEmpty())
	{
		OutWhyNot = LOCTEXT("NoAddress", "Give the server's address, starting with https:// or http://, under Editor Preferences > Plugins > Rosc AI.");
		return false;
	}
	return true;
}

void FRoscAIRemoteServer::Prepare(TFunction<void(bool, const FText&)> OnReady)
{
	FText WhyNot;
	const bool bUsable = IsUsable(WhyNot);
	OnReady(bUsable, WhyNot);
}

FRoscAIServerBackend::~FRoscAIServerBackend()
{
	FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		Request->CancelRequest();
	}
}

void FRoscAIServerBackend::Run(ERoscAITask InTask, const FString& PictureFile, TFunction<void(FRoscAIOutput&&)> InOnDone)
{
	check(IsInGameThread() && !bRunning);
	bRunning = true;
	const int32 Run = ++RunNumber;
	RunningTask = InTask;
	OnDone = MoveTemp(InOnDone);
	JobId.Reset();
	Stage = LOCTEXT("StagePreparing", "Getting ready");
	Fraction = 0.0f;

	Server->Prepare([Weak = AsWeak(), Run, PictureFile](bool bReady, const FText& WhyNot)
	{
		const TSharedPtr<FRoscAIServerBackend> This = Weak.Pin();
		if (!This.IsValid() || !This->bRunning || This->RunNumber != Run)
		{
			return;
		}
		if (!bReady)
		{
			This->Finish(FRoscAIOutput::Failed(WhyNot));
			return;
		}

		This->Stage = LOCTEXT("StageReading", "Reading the picture");
		Async(EAsyncExecution::ThreadPool, [Weak, Run, PictureFile]
		{
			FImage Picture;
			FText Error;
			TArray64<uint8> Png;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (Rosc::AIPictures::Load(PictureFile, Picture, Error))
			{
				Size = FIntPoint(Picture.SizeX, Picture.SizeY);
				const double Scale = FMath::Min(1.0, static_cast<double>(ServerPictureLongSide) / Size.GetMax());
				FImage Sent(FMath::Max(1, FMath::RoundToInt32(Size.X * Scale)), FMath::Max(1, FMath::RoundToInt32(Size.Y * Scale)), ERawImageFormat::BGRA8, EGammaSpace::sRGB);
				FImageCore::ResizeImage(Picture, Sent);
				if (!FImageUtils::CompressImage(Png, TEXT("png"), Sent))
				{
					Error = LOCTEXT("CannotEncode", "The picture could not be made ready to send.");
				}
			}
			AsyncTask(ENamedThreads::GameThread, [Weak, Run, Size, Error, Png = MoveTemp(Png)]() mutable
			{
				const TSharedPtr<FRoscAIServerBackend> This = Weak.Pin();
				if (!This.IsValid() || !This->bRunning || This->RunNumber != Run)
				{
					return;
				}
				if (Png.IsEmpty())
				{
					This->Finish(FRoscAIOutput::Failed(Error));
					return;
				}
				This->PictureSize = Size;
				This->PostJob(MoveTemp(Png));
			});
		});
	});
}

void FRoscAIServerBackend::GetProgress(FText& OutStage, float& OutFraction) const
{
	OutStage = Stage;
	OutFraction = Fraction;
}

void FRoscAIServerBackend::Cancel()
{
	if (!bRunning)
	{
		return;
	}
	if (!JobId.IsEmpty())
	{
		// Tells the server to stop; nothing waits for its answer.
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Stop = FHttpModule::Get().CreateRequest();
		Stop->SetURL(Server->GetUrl() / TEXT("v1/jobs") / JobId);
		Stop->SetVerb(TEXT("DELETE"));
		if (!Server->GetKey().IsEmpty())
		{
			Stop->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Server->GetKey());
		}
		Stop->ProcessRequest();
	}
	Finish(FRoscAIOutput());
}

void FRoscAIServerBackend::CheckHealth(TFunction<void(bool, const FText&)> OnChecked)
{
	FText WhyNot;
	if (!Server->IsUsable(WhyNot) || Server->GetUrl().IsEmpty())
	{
		OnChecked(false, WhyNot);
		return;
	}
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Check = FHttpModule::Get().CreateRequest();
	Check->SetURL(Server->GetUrl() / TEXT("v1/health"));
	Check->SetVerb(TEXT("GET"));
	Check->SetTimeout(ServerAnswerSeconds);
	if (!Server->GetKey().IsEmpty())
	{
		Check->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Server->GetKey());
	}
	Check->OnProcessRequestComplete().BindLambda([Weak = AsWeak(), OnChecked = MoveTemp(OnChecked)](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		const TSharedPtr<FRoscAIServerBackend> This = Weak.Pin();
		if (!This.IsValid())
		{
			return;
		}
		const int32 Code = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
		const TSharedPtr<FJsonObject> Answer = Code == 200 ? ParseServerJson(Response->GetContent()) : nullptr;
		FString Protocol;
		if (!Answer.IsValid() || !Answer->TryGetStringField(TEXT("protocol"), Protocol) || Protocol != ServerProtocolName)
		{
			OnChecked(false, Code == 200
				? FText::Format(LOCTEXT("NotAServer", "What answers at {0} is not a Rosc AI server."), FText::FromString(This->Server->GetHost()))
				: This->DescribeFailure(Code, Response.IsValid() ? Response->GetContent() : TArray<uint8>()));
			return;
		}
		TArray<FString> TaskIds;
		Answer->TryGetStringArrayField(TEXT("tasks"), TaskIds);
		TArray<FText> TaskNames;
		for (const FString& TaskId : TaskIds)
		{
			TaskNames.Add(GetServerTaskName(TaskId));
		}
		OnChecked(true, TaskNames.IsEmpty()
			? FText::Format(LOCTEXT("ServerNoModels", "The server at {0} answers, but has no models yet."), FText::FromString(This->Server->GetHost()))
			: FText::Format(LOCTEXT("ServerReady", "The server at {0} answers and can do: {1}."), FText::FromString(This->Server->GetHost()), FText::Join(LOCTEXT("ListComma", ", "), TaskNames)));
	});
	Check->ProcessRequest();
}

void FRoscAIServerBackend::Send(const TCHAR* Verb, const FString& Path, TArray<uint8>&& Body, FOnResponse OnResponse)
{
	Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Server->GetUrl() + Path);
	Request->SetVerb(Verb);
	Request->SetTimeout(ServerAnswerSeconds);
	if (!Server->GetKey().IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Server->GetKey());
	}
	if (!Body.IsEmpty())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("image/png"));
		Request->SetContent(MoveTemp(Body));
	}
	Request->OnProcessRequestComplete().BindLambda([Weak = AsWeak(), Run = RunNumber, OnResponse = MoveTemp(OnResponse)](FHttpRequestPtr Answered, FHttpResponsePtr Response, bool bConnected)
	{
		const TSharedPtr<FRoscAIServerBackend> This = Weak.Pin();
		if (This.IsValid() && This->bRunning && This->RunNumber == Run)
		{
			// This request is over: what follows must not reach into it while it is still calling back.
			if (This->Request == Answered)
			{
				This->Request.Reset();
			}
			const bool bAnswered = bConnected && Response.IsValid();
			OnResponse(bAnswered ? Response->GetResponseCode() : 0, bAnswered ? Response->GetContent() : TArray<uint8>());
		}
	});
	Request->ProcessRequest();
}

void FRoscAIServerBackend::PostJob(TArray64<uint8>&& Png)
{
	Stage = LOCTEXT("StageSending", "Sending the picture");
	Fraction = 0.05f;
	Send(TEXT("POST"), FString::Printf(TEXT("/v1/jobs?task=%s"), Rosc::AI::GetTaskId(RunningTask)), TArray<uint8>(Png), [this](int32 Code, const TArray<uint8>& Content)
	{
		const TSharedPtr<FJsonObject> Answer = Code == 200 ? ParseServerJson(Content) : nullptr;
		if (!Answer.IsValid() || !Answer->TryGetStringField(TEXT("id"), JobId) || JobId.IsEmpty())
		{
			Finish(FRoscAIOutput::Failed(DescribeFailure(Code, Content)));
			return;
		}
		bPolling = false;
		PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FRoscAIServerBackend::Poll), ServerPollSeconds);
	});
}

bool FRoscAIServerBackend::Poll(float DeltaTime)
{
	if (bPolling)
	{
		return true;
	}
	bPolling = true;
	Send(TEXT("GET"), TEXT("/v1/jobs/") + JobId, TArray<uint8>(), [this](int32 Code, const TArray<uint8>& Content)
	{
		bPolling = false;
		const TSharedPtr<FJsonObject> Status = Code == 200 ? ParseServerJson(Content) : nullptr;
		FString State;
		if (!Status.IsValid() || !Status->TryGetStringField(TEXT("state"), State))
		{
			Finish(FRoscAIOutput::Failed(DescribeFailure(Code, Content)));
			return;
		}

		FString StageWords;
		if (Status->TryGetStringField(TEXT("stage"), StageWords))
		{
			Stage = FText::FromString(StageWords);
		}
		double Done = 0.0;
		if (Status->TryGetNumberField(TEXT("fraction"), Done))
		{
			Fraction = FMath::Clamp(static_cast<float>(Done), 0.0f, 1.0f);
		}

		if (State == TEXT("done"))
		{
			const TSharedPtr<FJsonObject>* Result = nullptr;
			FString PicturePath;
			FRoscAIOutput Output;
			if (Status->TryGetObjectField(TEXT("result"), Result) && (*Result)->TryGetNumberField(TEXT("fovDegrees"), Output.FieldOfViewDeg)
				&& Output.FieldOfViewDeg > 0.0 && Output.FieldOfViewDeg < 180.0 && RunningTask == ERoscAITask::FocalLength)
			{
				Output.bSuccess = true;
				Finish(MoveTemp(Output));
			}
			else if (Result != nullptr && (*Result)->TryGetStringField(TEXT("picture"), PicturePath) && PicturePath.StartsWith(TEXT("/")) && RunningTask != ERoscAITask::FocalLength)
			{
				FetchPicture(PicturePath);
			}
			else
			{
				Finish(FRoscAIOutput::Failed(LOCTEXT("OddResult", "The server gave a result Rosc does not understand.")));
			}
		}
		else if (State == TEXT("failed"))
		{
			FString Why;
			Status->TryGetStringField(TEXT("error"), Why);
			Finish(FRoscAIOutput::Failed(Why.IsEmpty() ? LOCTEXT("FailedNoReason", "The server could not do the job, and did not say why.") : FText::FromString(Why)));
		}
		else if (State == TEXT("cancelled"))
		{
			Finish(FRoscAIOutput());
		}
	});
	return true;
}

void FRoscAIServerBackend::FetchPicture(const FString& Path)
{
	FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
	PollHandle.Reset();
	Stage = LOCTEXT("StageFetching", "Fetching the result");
	Send(TEXT("GET"), Path, TArray<uint8>(), [this](int32 Code, const TArray<uint8>& Content)
	{
		FImage Received;
		if (Code != 200 || !FImageUtils::DecompressImage(Content.GetData(), Content.Num(), Received) || Received.SizeX <= 0 || Received.SizeY <= 0)
		{
			Finish(FRoscAIOutput::Failed(Code == 200 ? LOCTEXT("OddPicture", "The server's result could not be read as a picture.") : DescribeFailure(Code, Content)));
			return;
		}
		const ERawImageFormat::Type Format = RunningTask == ERoscAITask::Depth ? ERawImageFormat::G16 : ERawImageFormat::G8;
		Received.ChangeFormat(Format, EGammaSpace::Linear);

		FRoscAIOutput Output;
		Output.bSuccess = true;
		if (FIntPoint(Received.SizeX, Received.SizeY) == PictureSize)
		{
			Output.Picture = MoveTemp(Received);
		}
		else
		{
			Output.Picture.Init(PictureSize.X, PictureSize.Y, Format, EGammaSpace::Linear);
			FImageCore::ResizeImage(Received, Output.Picture, FImageCore::EResizeImageFilter::Bilinear);
		}
		Finish(MoveTemp(Output));
	});
}

void FRoscAIServerBackend::Finish(FRoscAIOutput&& Output)
{
	FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
	PollHandle.Reset();
	bRunning = false;
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		Request->CancelRequest();
		Request.Reset();
	}
	// The caller may start the next job in the call.
	const TFunction<void(FRoscAIOutput&&)> Done = MoveTemp(OnDone);
	OnDone = nullptr;
	Done(MoveTemp(Output));
}

FText FRoscAIServerBackend::DescribeFailure(int32 Code, const TArray<uint8>& Content) const
{
	const FText Host = FText::FromString(Server->GetHost());
	if (Code == 0)
	{
		return FText::Format(LOCTEXT("NoAnswer", "The server at {0} did not answer."), Host);
	}
	if (Code == 401)
	{
		return FText::Format(LOCTEXT("WrongKey", "The server at {0} did not accept the key."), Host);
	}
	FString Why;
	if (const TSharedPtr<FJsonObject> Answer = ParseServerJson(Content); Answer.IsValid() && Answer->TryGetStringField(TEXT("error"), Why) && !Why.IsEmpty())
	{
		return FText::FromString(Why);
	}
	return FText::Format(LOCTEXT("OddAnswer", "The server at {0} gave an answer Rosc does not understand ({1})."), Host, FText::AsNumber(Code, &FNumberFormattingOptions::DefaultNoGrouping()));
}

#undef LOCTEXT_NAMESPACE
