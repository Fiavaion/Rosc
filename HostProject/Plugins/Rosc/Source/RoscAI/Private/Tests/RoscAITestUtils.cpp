// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscAITestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "HttpPath.h"
#include "HttpRouteHandle.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint32 StandInServerPort = 48741;

	/** The routes of the stand-in server that is up. A test that ended badly may have left its own behind. */
	TArray<FHttpRouteHandle> StandInServerRoutes;

	void UnbindStandInServerRoutes(IHttpRouter& Router)
	{
		for (const FHttpRouteHandle& Route : StandInServerRoutes)
		{
			Router.UnbindRoute(Route);
		}
		StandInServerRoutes.Reset();
	}
}

FString Rosc::AITests::WriteTestStill()
{
	// Seen from the camera: X to the right, Y down, Z ahead. Lengths in metres.
	struct FBall
	{
		FVector3d Centre;
		double Radius;
		FLinearColor Colour;
	};
	const FBall Balls[] = {
		{ FVector3d(0.3, 0.9, 5.0), 0.6, FLinearColor(0.8f, 0.25f, 0.2f) },
		{ FVector3d(-2.5, 0.7, 11.0), 0.8, FLinearColor(0.2f, 0.6f, 0.3f) } };
	const double FloorY = 1.5;
	const FVector3d ToLight = FVector3d(-0.4, -0.8, -0.45).GetSafeNormal();
	const double FocalPixels = StillWidth / (2.0 * FMath::Tan(FMath::DegreesToRadians(StillFieldOfViewDeg) / 2.0));

	FImage Still(StillWidth, StillHeight, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	const TArrayView64<FColor> Pixels = Still.AsBGRA8();
	for (int32 Y = 0; Y < StillHeight; ++Y)
	{
		for (int32 X = 0; X < StillWidth; ++X)
		{
			const FVector3d Ray = FVector3d((X + 0.5 - StillWidth / 2.0) / FocalPixels, (Y + 0.5 - StillHeight / 2.0) / FocalPixels, 1.0).GetSafeNormal();

			// The sky, lighter towards the horizon.
			FLinearColor Colour = FMath::Lerp(FLinearColor(0.75f, 0.82f, 0.9f), FLinearColor(0.25f, 0.45f, 0.8f), static_cast<float>(FMath::Clamp(-Ray.Y * 3.0, 0.0, 1.0)));
			double Nearest = TNumericLimits<double>::Max();

			if (Ray.Y > 1e-6)
			{
				Nearest = FloorY / Ray.Y;
				const FVector3d Point = Ray * Nearest;
				const bool bLight = (FMath::FloorToInt32(Point.X) + FMath::FloorToInt32(Point.Z)) % 2 == 0;
				const FLinearColor Floor = bLight ? FLinearColor(0.62f, 0.58f, 0.52f) : FLinearColor(0.2f, 0.19f, 0.18f);
				// The floor fades into haze far away, as floors do.
				Colour = FMath::Lerp(Floor, FLinearColor(0.7f, 0.76f, 0.84f), static_cast<float>(FMath::Clamp(Nearest / 80.0, 0.0, 1.0)));
			}
			for (const FBall& Ball : Balls)
			{
				const double Along = Ray.Dot(Ball.Centre);
				const double Discriminant = Along * Along - Ball.Centre.SquaredLength() + Ball.Radius * Ball.Radius;
				if (Discriminant > 0.0 && Along - FMath::Sqrt(Discriminant) < Nearest)
				{
					Nearest = Along - FMath::Sqrt(Discriminant);
					const FVector3d Normal = (Ray * Nearest - Ball.Centre) / Ball.Radius;
					Colour = Ball.Colour * static_cast<float>(0.25 + 0.75 * FMath::Max(0.0, Normal.Dot(ToLight)));
				}
			}
			Colour.A = 1.0f;
			Pixels[Y * StillWidth + X] = Colour.ToFColorSRGB();
		}
	}

	const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Rosc") / TEXT("AITests") / TEXT("test-still.png"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	verify(FImageUtils::SaveImageByExtension(*File, Still));
	return File;
}

Rosc::AITests::FSettingsBefore::FSettingsBefore()
{
	const URoscAISettings* Settings = GetDefault<URoscAISettings>();
	Backend = Settings->Backend;
	ModelsFolder = Settings->ModelsFolder.Path;
	ServerUrl = Settings->ServerUrl;
	ServerKey = Settings->ServerKey;
	AssistantKey = Settings->AssistantKey;
	AssistantModel = Settings->AssistantModel;
	AssistantUrl = Settings->AssistantUrl;
	ConsentedHosts = Settings->ConsentedHosts;
	AcceptedModelNotices = Settings->AcceptedModelNotices;
}

void Rosc::AITests::FSettingsBefore::PutBack() const
{
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	Settings->Backend = Backend;
	Settings->ModelsFolder.Path = ModelsFolder;
	Settings->ServerUrl = ServerUrl;
	Settings->ServerKey = ServerKey;
	Settings->AssistantKey = AssistantKey;
	Settings->AssistantModel = AssistantModel;
	Settings->AssistantUrl = AssistantUrl;
	Settings->ConsentedHosts = ConsentedHosts;
	Settings->AcceptedModelNotices = AcceptedModelNotices;
	// Giving and withdrawing consent saves the settings, so what was there is saved again.
	Settings->SaveConfig();
}

Rosc::AITests::FStandInServer::FStandInServer()
{
	Router = FHttpServerModule::Get().GetHttpRouter(StandInServerPort, true);
	check(Router.IsValid());
	UnbindStandInServerRoutes(*Router);
	FHttpServerModule::Get().StartAllListeners();
}

Rosc::AITests::FStandInServer::~FStandInServer()
{
	UnbindStandInServerRoutes(*Router);
}

FString Rosc::AITests::FStandInServer::GetUrl() const
{
	return FString::Printf(TEXT("http://127.0.0.1:%u"), StandInServerPort);
}

void Rosc::AITests::FStandInServer::Answer(EHttpServerRequestVerbs Verb, const FString& Path, FAnswer Answer)
{
	check(!Path.Contains(TEXT(":")));
	const FHttpRouteHandle Route = Router->BindRoute(FHttpPath(Path), Verb, FHttpRequestHandler::CreateLambda(
		[Answer = MoveTemp(Answer)](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			OnComplete(Answer(Request));
			return true;
		}));
	check(Route.IsValid());
	StandInServerRoutes.Add(Route);
}

TUniquePtr<FHttpServerResponse> Rosc::AITests::FStandInServer::Json(const FString& Text, EHttpServerResponseCodes Code)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Text, TEXT("application/json"));
	Response->Code = Code;
	return Response;
}

FString Rosc::AITests::FStandInServer::GetHeader(const FHttpServerRequest& Request, const FString& Name)
{
	for (const TPair<FString, TArray<FString>>& Header : Request.Headers)
	{
		if (Header.Key.Equals(Name, ESearchCase::IgnoreCase) && !Header.Value.IsEmpty())
		{
			return Header.Value[0];
		}
	}
	return FString();
}

void Rosc::AITests::WaitThen(FAutomationTestBase& Test, const TCHAR* What, double Seconds, TFunction<bool()> IsDone, TFunction<void()> Then)
{
	// The wait starts when its turn comes, not when the test lines it up.
	const TSharedRef<double> Until = MakeShared<double>(0.0);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([&Test, What = FString(What), Seconds, Until, IsDone = MoveTemp(IsDone), Then = MoveTemp(Then)]
	{
		if (*Until == 0.0)
		{
			*Until = FPlatformTime::Seconds() + Seconds;
		}
		const bool bDone = IsDone();
		if (!bDone && FPlatformTime::Seconds() < *Until)
		{
			return false;
		}
		if (!bDone)
		{
			Test.AddError(FString::Printf(TEXT("Waited %.0f s for %s, and it did not happen."), Seconds, *What));
		}
		Then();
		return true;
	}));
}

#endif
