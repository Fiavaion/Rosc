// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Misc/AutomationTest.h"
#include "RoscAISettings.h"

#if WITH_DEV_AUTOMATION_TESTS

class IHttpRouter;

/** What Rosc's AI tests share: a test still, a stand-in server, and the user's settings put back afterwards. */
namespace Rosc::AITests
{
	/** The test still's size, and the field of view it is drawn with. */
	inline constexpr int32 StillWidth = 960;
	inline constexpr int32 StillHeight = 540;
	inline constexpr double StillFieldOfViewDeg = 50.0;
	/** What the lens model makes of the still: recorded 2026-11-11 with MoGe-2 Small, 72.3 in the editor and 72.8 in the helper. */
	inline constexpr double StillModelFieldOfViewDeg = 72.5;

	/** Where things are in the test still, in pixels. */
	inline const FIntPoint StillNearBall(542, 440);
	inline const FIntPoint StillFarBall(246, 330);
	inline const FIntPoint StillNearFloor(100, 530);
	inline const FIntPoint StillFarFloor(100, 300);
	inline const FIntPoint StillSky(480, 40);

	/**
	 * Draws a still of a known scene and writes it as a PNG: a chequered floor running to the
	 * horizon under a sky, a ball 5 m from the camera and another 11 m away. Returns its file.
	 */
	FString WriteTestStill();

	/** The user's AI settings as they were before a test, to put back when it ends. */
	struct FSettingsBefore
	{
		FSettingsBefore();
		void PutBack() const;

		ERoscAIBackend Backend;
		FString ModelsFolder;
		FString ServerUrl;
		FString ServerKey;
		FString AssistantKey;
		FString AssistantModel;
		FString AssistantUrl;
		TArray<FString> ConsentedHosts;
	};

	/**
	 * A server inside the editor, on this machine only, that answers as a test tells it to: it
	 * stands in for the helper, a server elsewhere, a model download and the Messages API. One at
	 * a time: a new one takes over from the one before it.
	 */
	class FStandInServer
	{
	public:
		using FAnswer = TFunction<TUniquePtr<FHttpServerResponse>(const FHttpServerRequest&)>;

		FStandInServer();
		~FStandInServer();

		/** The server's address, such as http://127.0.0.1:48741. */
		FString GetUrl() const;

		/**
		 * Answers requests to a path, which is given in full: the engine's server never lets go of a
		 * path with a part that stands for anything, so the test before would go on answering for it.
		 */
		void Answer(EHttpServerRequestVerbs Verb, const FString& Path, FAnswer Answer);

		static TUniquePtr<FHttpServerResponse> Json(const FString& Text, EHttpServerResponseCodes Code = EHttpServerResponseCodes::Ok);
		/** The value of a header of a request, whatever its case. Empty when it is not there. */
		static FString GetHeader(const FHttpServerRequest& Request, const FString& Name);

	private:
		TSharedPtr<IHttpRouter> Router;
	};

	/**
	 * Waits, over editor frames, until something is done, then goes on; says so when it has not
	 * happened in the time allowed.
	 */
	void WaitThen(FAutomationTestBase& Test, const TCHAR* What, double Seconds, TFunction<bool()> IsDone, TFunction<void()> Then);
}

#endif
