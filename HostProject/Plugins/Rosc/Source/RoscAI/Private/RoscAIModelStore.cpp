// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscAIModelStore.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Paths.h"
#include "RoscAISettings.h"

#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/evp.h>
THIRD_PARTY_INCLUDES_END
#undef UI

#define LOCTEXT_NAMESPACE "RoscAIModelStore"

FString Rosc::AIModelStore::GetFolder()
{
	const FString& Chosen = GetDefault<URoscAISettings>()->ModelsFolder.Path;
	return FPaths::ConvertRelativePathToFull(Chosen.IsEmpty()
		? FString(FPlatformProcess::UserSettingsDir()) / TEXT("Rosc") / TEXT("Models")
		: Chosen);
}

FString Rosc::AIModelStore::GetFile(const FRoscAIModel& Model)
{
	return GetFolder() / Model.FileName;
}

bool Rosc::AIModelStore::IsInstalled(const FRoscAIModel& Model)
{
	return IFileManager::Get().FileSize(*GetFile(Model)) == Model.SizeBytes;
}

FString Rosc::AIModelStore::HashFile(const FString& File)
{
	const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*File));
	if (!Reader.IsValid())
	{
		return FString();
	}

	EVP_MD_CTX* Context = EVP_MD_CTX_new();
	EVP_DigestInit_ex(Context, EVP_sha256(), nullptr);
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(1 << 20);
	for (int64 Left = Reader->TotalSize(); Left > 0 && !Reader->IsError();)
	{
		const int64 Count = FMath::Min<int64>(Left, Buffer.Num());
		Reader->Serialize(Buffer.GetData(), Count);
		EVP_DigestUpdate(Context, Buffer.GetData(), Count);
		Left -= Count;
	}
	uint8 Digest[EVP_MAX_MD_SIZE];
	unsigned int DigestSize = 0;
	EVP_DigestFinal_ex(Context, Digest, &DigestSize);
	EVP_MD_CTX_free(Context);

	return Reader->IsError() ? FString() : BytesToHex(Digest, DigestSize).ToLower();
}

bool Rosc::AIModelStore::Verify(const FRoscAIModel& Model, FText& OutError)
{
	const FString File = GetFile(Model);
	if (!IsInstalled(Model))
	{
		OutError = FText::Format(LOCTEXT("NotInstalled", "{0} is not downloaded yet. Download it in the AI tab."), FText::FromString(Model.Name));
		return false;
	}

	if (HashFile(File) != Model.Sha256)
	{
		OutError = FText::Format(LOCTEXT("WrongFile", "{0} is not the file Rosc lists for {1}: it may be damaged. Remove it and download it again."),
			FText::FromString(File), FText::FromString(Model.Name));
		return false;
	}
	return true;
}

TSharedRef<FRoscAIModelDownload> FRoscAIModelDownload::Start(const FRoscAIModel& Model, FOnDone OnDone)
{
	TSharedRef<FRoscAIModelDownload> Download = MakeShareable(new FRoscAIModelDownload(Model));
	Download->OnDone = MoveTemp(OnDone);

	const FString File = Rosc::AIModelStore::GetFile(Model);
	const FString PartPath = File + TEXT(".part");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	Download->PartFile = MakeShareable(IFileManager::Get().CreateFileWriter(*PartPath));
	if (!Download->PartFile.IsValid())
	{
		// Not from inside this call: the caller does not have the download yet.
		AsyncTask(ENamedThreads::GameThread, [Weak = Download.ToWeakPtr(), PartPath]
		{
			if (const TSharedPtr<FRoscAIModelDownload> This = Weak.Pin())
			{
				This->Finish(false, FText::Format(LOCTEXT("CannotWrite", "Could not write to {0}."), FText::FromString(PartPath)));
			}
		});
		return Download;
	}

	Download->Request = FHttpModule::Get().CreateRequest();
	Download->Request->SetURL(Model.Url);
	Download->Request->SetVerb(TEXT("GET"));
	// A model is hundreds of megabytes: no limit on the whole download, only on a connection that has gone quiet.
	Download->Request->SetTimeout(0.0f);
	Download->Request->SetActivityTimeout(60.0f);
	Download->Request->SetResponseBodyReceiveStream(Download->PartFile.ToSharedRef());
	Download->Request->OnRequestProgress64().BindLambda([Weak = Download.ToWeakPtr()](FHttpRequestPtr, uint64, uint64 BytesReceived)
	{
		if (const TSharedPtr<FRoscAIModelDownload> This = Weak.Pin())
		{
			This->Fraction = FMath::Clamp(static_cast<float>(static_cast<double>(BytesReceived) / This->Model.SizeBytes), 0.0f, 1.0f);
		}
	});
	Download->Request->OnProcessRequestComplete().BindLambda([Weak = Download.ToWeakPtr(), File, PartPath](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		const TSharedPtr<FRoscAIModelDownload> This = Weak.Pin();
		if (!This.IsValid())
		{
			return;
		}
		This->PartFile->Close();
		This->PartFile.Reset();
		if (This->bCancelled)
		{
			This->Finish(false, FText::GetEmpty());
			return;
		}
		if (!bConnected || !Response.IsValid() || Response->GetResponseCode() != 200)
		{
			This->Finish(false, FText::Format(LOCTEXT("DownloadFailed", "{0} could not be downloaded ({1}). Check the connection and try again."),
				FText::FromString(This->Model.Name),
				Response.IsValid() ? FText::AsNumber(Response->GetResponseCode(), &FNumberFormattingOptions::DefaultNoGrouping()) : LOCTEXT("NoAnswer", "no answer")));
			return;
		}

		// The whole file is read to check it: off the game thread.
		Async(EAsyncExecution::ThreadPool, [Weak, File, PartPath, Sha256 = This->Model.Sha256, Size = This->Model.SizeBytes]
		{
			const bool bRightFile = IFileManager::Get().FileSize(*PartPath) == Size && Rosc::AIModelStore::HashFile(PartPath) == Sha256;
			const bool bInPlace = bRightFile && IFileManager::Get().Move(*File, *PartPath, true);
			AsyncTask(ENamedThreads::GameThread, [Weak, bRightFile, bInPlace, File]
			{
				if (const TSharedPtr<FRoscAIModelDownload> This = Weak.Pin())
				{
					This->Finish(bInPlace, bInPlace ? FText::GetEmpty()
						: bRightFile ? FText::Format(LOCTEXT("CannotPlace", "Could not write {0}."), FText::FromString(File))
						: FText::Format(LOCTEXT("NotTheFile", "What was downloaded is not the file Rosc lists for {0}, so it was not kept. Try again."), FText::FromString(This->Model.Name)));
				}
			});
		});
	});
	Download->Request->ProcessRequest();
	return Download;
}

FRoscAIModelDownload::~FRoscAIModelDownload()
{
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		Request->OnRequestProgress64().Unbind();
		Request->CancelRequest();
	}
}

void FRoscAIModelDownload::Cancel()
{
	if (Request.IsValid() && !bCancelled)
	{
		bCancelled = true;
		Request->CancelRequest();
	}
}

void FRoscAIModelDownload::Finish(bool bSuccess, const FText& Error)
{
	Fraction = bSuccess ? 1.0f : Fraction;
	Request.Reset();
	if (!bSuccess)
	{
		IFileManager::Get().Delete(*(Rosc::AIModelStore::GetFile(Model) + TEXT(".part")), false, true, true);
	}
	// The owner may let go of this download in the call.
	const TSharedRef<FRoscAIModelDownload> KeepAlive = AsShared();
	OnDone.ExecuteIfBound(bSuccess, Error);
}

#undef LOCTEXT_NAMESPACE
