// Copyright 2026 Fiavaion. All rights reserved.

#include "Editor.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/RoscAITestUtils.h"
#include "RoscAIConsent.h"
#include "RoscAIModelStore.h"
#include "RoscAIServerBackend.h"
#include "RoscAISubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace Rosc::AITests;

	/** A grey picture's value at a pixel, from 0 to 1. */
	double ReadGrey(const FImage& Picture, FIntPoint Pixel)
	{
		const int64 Index = static_cast<int64>(Pixel.Y) * Picture.SizeX + Pixel.X;
		return Picture.Format == ERawImageFormat::G16 ? Picture.AsG16()[Index] / 65535.0 : Picture.AsG8()[Index] / 255.0;
	}

	/** Everything a test of picture jobs keeps between editor frames. */
	struct FAIJobsTestState
	{
		FSettingsBefore SettingsBefore;
		TArray<FRoscAIResult> Results;
		FDelegateHandle FinishedHandle;

		/**
		 * The longest time between two ticks of the editor while each job was worked on. The jobs
		 * run in the order they were given, so with three left it is the depth map, with two the
		 * lens estimate and with one the matte.
		 */
		double LastTick = FPlatformTime::Seconds();
		int32 JobsLeftAtLastTick = 3;
		double LongestPause[4] = {};

		const FRoscAIResult* Find(ERoscAITask Task) const
		{
			return Results.FindByPredicate([Task](const FRoscAIResult& Result) { return Result.Request.Task == Task; });
		}

		/** Called every tick while the jobs run. True when none is left. */
		bool TickUntilDone(const URoscAISubsystem& Jobs)
		{
			// A pause belongs to the job that was being worked on when it began: that job is often over by the tick that ends it.
			const double Now = FPlatformTime::Seconds();
			double& Pause = LongestPause[JobsLeftAtLastTick];
			Pause = FMath::Max(Pause, Now - LastTick);
			LastTick = Now;
			JobsLeftAtLastTick = FMath::Clamp(Jobs.GetJobsLeft(), 0, 3);
			return JobsLeftAtLastTick == 0;
		}

		FString DescribePauses() const
		{
			return FString::Printf(TEXT("The editor's longest pause: %.2f s during the depth map, %.2f s during the matte, %.2f s during the lens estimate"), LongestPause[3], LongestPause[1], LongestPause[2]);
		}
	};

	/** The picture jobs, with whatever a test that ended badly left behind given up. */
	URoscAISubsystem* GetJobsForTest()
	{
		URoscAISubsystem* Jobs = GEditor->GetEditorSubsystem<URoscAISubsystem>();
		Jobs->CancelAll();
		return Jobs;
	}

	/** Asks for depth, a matte and the lens of the test still, on the backend the settings name. */
	bool SubmitTestStillJobs(FAutomationTestBase& Test, const TSharedRef<FAIJobsTestState>& State)
	{
		URoscAISubsystem* Jobs = GEditor->GetEditorSubsystem<URoscAISubsystem>();
		State->FinishedHandle = Jobs->OnJobFinished().AddLambda([State](const FRoscAIResult& Result) { State->Results.Add(Result); });
		const FString Still = WriteTestStill();
		// The matte goes last: its model fills the graphics card while it runs (TD-002), and the two light jobs are measured before it.
		for (const ERoscAITask Task : { ERoscAITask::Depth, ERoscAITask::FocalLength, ERoscAITask::Matte })
		{
			FText Error;
			if (!Test.TestTrue(*FString::Printf(TEXT("The %s job is taken (%s)"), Rosc::AI::GetTaskId(Task), *Error.ToString()), Jobs->Submit({ Task, Still }, Error)))
			{
				return false;
			}
		}
		Test.TestEqual(TEXT("Three jobs are running or waiting"), Jobs->GetJobsLeft(), 3);
		return true;
	}

	/** Checks the three results against what is known of the test still. */
	void CheckTestStillResults(FAutomationTestBase& Test, const FAIJobsTestState& State)
	{
		Test.TestEqual(TEXT("Every job ended"), State.Results.Num(), 3);
		for (const FRoscAIResult& Result : State.Results)
		{
			Test.TestTrue(*FString::Printf(TEXT("The %s job succeeded (%s)"), Rosc::AI::GetTaskId(Result.Request.Task), *Result.Error.ToString()), Result.bSuccess);
		}

		FImage Depth;
		if (const FRoscAIResult* Result = State.Find(ERoscAITask::Depth); Result != nullptr && Result->bSuccess
			&& Test.TestTrue(TEXT("The depth map is a picture on disk"), FImageUtils::LoadImage(*Result->File, Depth)))
		{
			Test.AddInfo(FString::Printf(TEXT("Depth map: %s"), *Result->File));
			Test.TestTrue(TEXT("The depth map is 16-bit grey"), Depth.Format == ERawImageFormat::G16);
			Test.TestTrue(TEXT("The depth map is the size of the still"), Depth.SizeX == StillWidth && Depth.SizeY == StillHeight);
			const double NearBall = ReadGrey(Depth, StillNearBall);
			const double FarBall = ReadGrey(Depth, StillFarBall);
			const double NearFloor = ReadGrey(Depth, StillNearFloor);
			const double FarFloor = ReadGrey(Depth, StillFarFloor);
			const double Sky = ReadGrey(Depth, StillSky);
			Test.AddInfo(FString::Printf(TEXT("Depth: ball at 5 m %.3f, ball at 11 m %.3f, floor at 6 m %.3f, floor at 51 m %.3f, sky %.3f"), NearBall, FarBall, NearFloor, FarFloor, Sky));
			Test.TestTrue(TEXT("The ball at 5 m is brighter than the ball at 11 m"), NearBall > FarBall + 0.05);
			Test.TestTrue(TEXT("The ball at 11 m is brighter than the floor at 51 m"), FarBall > FarFloor + 0.02);
			Test.TestTrue(TEXT("The floor at 6 m is brighter than the floor at 51 m"), NearFloor > FarFloor + 0.2);
			Test.TestTrue(TEXT("The sky is the darkest of them"), Sky < FarBall && Sky <= FarFloor + 0.02);
		}

		FImage Matte;
		if (const FRoscAIResult* Result = State.Find(ERoscAITask::Matte); Result != nullptr && Result->bSuccess
			&& Test.TestTrue(TEXT("The matte is a picture on disk"), FImageUtils::LoadImage(*Result->File, Matte)))
		{
			Test.AddInfo(FString::Printf(TEXT("Matte: %s"), *Result->File));
			Test.TestTrue(TEXT("The matte is 8-bit grey"), Matte.Format == ERawImageFormat::G8);
			Test.TestTrue(TEXT("The matte is the size of the still"), Matte.SizeX == StillWidth && Matte.SizeY == StillHeight);
			Test.AddInfo(FString::Printf(TEXT("Matte: near ball %.2f, sky %.2f, far floor %.2f"), ReadGrey(Matte, StillNearBall), ReadGrey(Matte, StillSky), ReadGrey(Matte, StillFarFloor)));
			Test.TestTrue(TEXT("The near ball is in the matte"), ReadGrey(Matte, StillNearBall) > 0.8);
			Test.TestTrue(TEXT("The sky is not"), ReadGrey(Matte, StillSky) < 0.2);
			Test.TestTrue(TEXT("The far floor is not"), ReadGrey(Matte, StillFarFloor) < 0.2);
		}

		if (const FRoscAIResult* Result = State.Find(ERoscAITask::FocalLength); Result != nullptr && Result->bSuccess)
		{
			Test.AddInfo(FString::Printf(TEXT("Field of view: %.1f degrees found, %.1f drawn"), Result->FieldOfViewDeg, StillFieldOfViewDeg));
			// The model was taught on photographs, and reads this drawn still as wider than it was
			// drawn. What is checked here is that the model's answer comes through unchanged, the same
			// in the editor and in the helper. The sum behind it is checked in Rosc.Core.Vision.
			Test.TestEqual(TEXT("The field of view is the model's reading of this still"), Result->FieldOfViewDeg, StillModelFieldOfViewDeg, 5.0);
		}
	}

	/** Whether every model of the shipped list is in the models folder; says which are not. */
	bool HasEveryTestModel(FAutomationTestBase& Test)
	{
		bool bAll = true;
		for (const FRoscAIModel& Model : FRoscAIModelList::Get().GetModels())
		{
			if (!Rosc::AIModelStore::IsInstalled(Model))
			{
				Test.AddInfo(FString::Printf(TEXT("Skipped: %s is not in %s. Download it in the AI tab."), *Model.Name, *Rosc::AIModelStore::GetFolder()));
				bAll = false;
			}
		}
		return bAll;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIConsentTest, "Rosc.AI.Consent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIConsentTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::AIConsent;
	const FSettingsBefore SettingsBefore;

	TestEqual(TEXT("The host of an address with a port and a path"), *GetHost(TEXT("https://Render01.studio.example:8750/v1")), TEXT("render01.studio.example"));
	TestEqual(TEXT("The host of the Messages API"), *GetHost(TEXT("https://api.anthropic.com/v1/messages")), TEXT("api.anthropic.com"));
	TestEqual(TEXT("The host of the helper"), *GetHost(TEXT("http://127.0.0.1:50211")), TEXT("127.0.0.1"));

	TestTrue(TEXT("127.0.0.1 is this machine"), IsThisMachine(TEXT("127.0.0.1")));
	TestTrue(TEXT("localhost is this machine"), IsThisMachine(TEXT("localhost")));
	TestFalse(TEXT("A machine on the studio's network is not"), IsThisMachine(TEXT("192.168.1.20")));
	TestFalse(TEXT("A name that starts like this machine's is not"), IsThisMachine(TEXT("localhost.example.com")));

	TestTrue(TEXT("Nothing has to be agreed for this machine"), IsGiven(TEXT("127.0.0.1")));
	TestFalse(TEXT("No host means no consent"), IsGiven(FString()));
	const FString Host = TEXT("rosc-consent-test.example");
	Withdraw(Host);
	TestFalse(TEXT("A host elsewhere is not agreed to until the user agrees"), IsGiven(Host));
	Give(Host);
	TestTrue(TEXT("It is once they have"), IsGiven(Host));
	TestTrue(TEXT("And it is listed"), GetHosts().Contains(Host));
	TestFalse(TEXT("Agreeing to one host is not agreeing to another"), IsGiven(TEXT("other.example")));
	Withdraw(Host);
	TestFalse(TEXT("Consent can be withdrawn"), IsGiven(Host));

	SettingsBefore.PutBack();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIEditorPicturesTest, "Rosc.AI.Editor.Pictures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIEditorPicturesTest::RunTest(const FString& Parameters)
{
	if (!HasEveryTestModel(*this))
	{
		return true;
	}
	const TSharedRef<FAIJobsTestState> State = MakeShared<FAIJobsTestState>();
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	Settings->Backend = ERoscAIBackend::Editor;

	URoscAISubsystem* Jobs = GetJobsForTest();
	FText WhyNot;
	if (!Jobs->CanRun(ERoscAITask::Depth, WhyNot))
	{
		AddInfo(FString::Printf(TEXT("Skipped: %s"), *WhyNot.ToString()));
		State->SettingsBefore.PutBack();
		return true;
	}
	TestTrue(TEXT("Nothing has to be agreed to work in the editor"), Jobs->GetHostNeedingConsent().IsEmpty());

	const double StartedAt = FPlatformTime::Seconds();
	if (!SubmitTestStillJobs(*this, State))
	{
		Jobs->CancelAll();
		Jobs->OnJobFinished().Remove(State->FinishedHandle);
		State->SettingsBefore.PutBack();
		return false;
	}
	FText Stage;
	float Fraction = 0.0f;
	Jobs->GetProgress(Stage, Fraction);
	TestFalse(TEXT("A running job says what it is doing"), Stage.IsEmpty());

	// The editor's own home screen checks the connection now and then. A check in flight while the matte holds the graphics card times out, and says so in the log.
	AddExpectedMessage(TEXT("HTTP request timed out"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1, false);
	WaitThen(*this, TEXT("the picture jobs in the editor"), 300.0, [State, Jobs] { return State->TickUntilDone(*Jobs); }, [this, State, Jobs, StartedAt]
	{
		AddInfo(FString::Printf(TEXT("Three jobs in the editor took %.1f s. %s"), FPlatformTime::Seconds() - StartedAt, *State->DescribePauses()));
		// The editor stays live while a model loads and runs (ADR-006, rule 3). The spike measured 14 ms on an idle editor; a second leaves room for a busy test run.
		TestTrue(TEXT("The editor does not stand still while a depth map is made"), State->LongestPause[3] < 1.0);
		TestTrue(TEXT("The editor does not stand still while a lens is estimated"), State->LongestPause[2] < 1.0);
		// The matte's model holds the graphics card for seconds at a time and the viewport waits for it (TD-002): reported, not required.
		Jobs->OnJobFinished().Remove(State->FinishedHandle);
		CheckTestStillResults(*this, *State);
		State->SettingsBefore.PutBack();
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIHelperPicturesTest, "Rosc.AI.Helper.Pictures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIHelperPicturesTest::RunTest(const FString& Parameters)
{
	if (!HasEveryTestModel(*this))
	{
		return true;
	}
	const TSharedRef<FAIJobsTestState> State = MakeShared<FAIJobsTestState>();
	GetMutableDefault<URoscAISettings>()->Backend = ERoscAIBackend::Helper;

	URoscAISubsystem* Jobs = GetJobsForTest();
	TestTrue(TEXT("Nothing has to be agreed for the helper: it is on this machine"), Jobs->GetHostNeedingConsent().IsEmpty());
	const double StartedAt = FPlatformTime::Seconds();
	if (!SubmitTestStillJobs(*this, State))
	{
		Jobs->CancelAll();
		Jobs->OnJobFinished().Remove(State->FinishedHandle);
		State->SettingsBefore.PutBack();
		return false;
	}

	// The editor's pauses are reported, not required: whether the helper's Python works on the processor or on the graphics card the viewport also needs is the user's choice of packages.
	WaitThen(*this, TEXT("the picture jobs in the helper"), 300.0, [State, Jobs] { return State->TickUntilDone(*Jobs); }, [this, State, Jobs, StartedAt]
	{
		AddInfo(State->DescribePauses());
		Jobs->OnJobFinished().Remove(State->FinishedHandle);
		const FText HelperStatus = Jobs->GetHelperStatus();
		AddInfo(FString::Printf(TEXT("Helper: %s"), *HelperStatus.ToString()));
		// A machine whose Python lacks what the helper needs is told so in words, and that is all this test can see there.
		const bool bHelperRan = State->Results.ContainsByPredicate([](const FRoscAIResult& Result) { return Result.bSuccess; });
		if (!bHelperRan && !State->Results.IsEmpty() && State->Results[0].Error.EqualTo(HelperStatus))
		{
			AddInfo(TEXT("Skipped: the helper could not start on this machine."));
			TestTrue(TEXT("The reason says how to put it right"), HelperStatus.ToString().Contains(TEXT("python")) || HelperStatus.ToString().Contains(TEXT("Python")));
		}
		else
		{
			AddInfo(FString::Printf(TEXT("Three jobs in the helper took %.1f s, its start included"), FPlatformTime::Seconds() - StartedAt));
			CheckTestStillResults(*this, *State);
		}
		State->SettingsBefore.PutBack();
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIConsentGateTest, "Rosc.AI.ConsentGate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIConsentGateTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FAIJobsTestState> State = MakeShared<FAIJobsTestState>();
	// A name that can never be a real machine: nothing can reach it, agreed to or not.
	const FString Host = TEXT("rosc-ai-test.invalid");
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	Settings->Backend = ERoscAIBackend::Server;
	URoscAISubsystem* Jobs = GetJobsForTest();
	const FString Still = WriteTestStill();
	FText Error;
	// The engine warns of a request that reaches nobody, in words that depend on how it failed.
	AddExpectedMessagePlain(TEXT("libcurl"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(TEXT("completed with reason"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

	Settings->ServerUrl.Reset();
	TestFalse(TEXT("With no address there is no server to ask"), Jobs->Submit({ ERoscAITask::Depth, Still }, Error));
	TestTrue(TEXT("And the reason says where to give one"), Error.ToString().Contains(TEXT("Editor Preferences")));

	Settings->ServerUrl = FString::Printf(TEXT("https://%s:8750/"), *Host);
	Rosc::AIConsent::Withdraw(Host);
	TestEqual(TEXT("The host is named as needing the user's agreement"), *Jobs->GetHostNeedingConsent(), *Host);
	Error = FText::GetEmpty();
	TestFalse(TEXT("A job for a host the user has not agreed to is not taken"), Jobs->Submit({ ERoscAITask::Depth, Still }, Error));
	TestTrue(TEXT("The reason names the host"), Error.ToString().Contains(Host));
	TestEqual(TEXT("Nothing is running or waiting"), Jobs->GetJobsLeft(), 0);

	Rosc::AIConsent::Give(Host);
	TestTrue(TEXT("Once agreed, nothing stands in the way"), Jobs->GetHostNeedingConsent().IsEmpty());
	State->FinishedHandle = Jobs->OnJobFinished().AddLambda([State](const FRoscAIResult& Result) { State->Results.Add(Result); });
	TestTrue(TEXT("And the job is taken"), Jobs->Submit({ ERoscAITask::Depth, Still }, Error));

	WaitThen(*this, TEXT("the job for a server that is not there"), 60.0, [Jobs] { return Jobs->GetJobsLeft() == 0; }, [this, State, Jobs, Host]
	{
		Jobs->OnJobFinished().Remove(State->FinishedHandle);
		if (TestEqual(TEXT("The job ended"), State->Results.Num(), 1))
		{
			TestFalse(TEXT("A server that is not there cannot do the job"), State->Results[0].bSuccess);
			TestEqual(TEXT("And the user is told so"), *State->Results[0].Error.ToString(), *FString::Printf(TEXT("The server at %s did not answer."), *Host));
		}
		State->SettingsBefore.PutBack();
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIServerProtocolTest, "Rosc.AI.Server.Protocol", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIServerProtocolTest::RunTest(const FString& Parameters)
{
	/** A server that speaks the protocol with a key, and gives back a small depth map of its own making. */
	struct FState
	{
		FSettingsBefore SettingsBefore;
		FStandInServer Server;
		FString PostedTask;
		FString PostedContentType;
		FIntPoint PostedSize = FIntPoint::ZeroValue;
		int32 StatusAsked = 0;
		TArray<FRoscAIResult> Results;
		FDelegateHandle FinishedHandle;
		bool bChecked = false;
		bool bReachable = false;
		FText CheckMessage;
	};
	const TSharedRef<FState> State = MakeShared<FState>();
	const FString Key = TEXT("stand-in-key");
	const auto HasKey = [Key](const FHttpServerRequest& Request) { return FStandInServer::GetHeader(Request, TEXT("Authorization")) == TEXT("Bearer ") + Key; };
	const auto NoKey = [] { return FStandInServer::Json(TEXT(R"({"error": "This server needs its key."})"), EHttpServerResponseCodes::Denied); };

	State->Server.Answer(EHttpServerRequestVerbs::VERB_GET, TEXT("/v1/health"), [HasKey, NoKey](const FHttpServerRequest& Request)
	{
		return HasKey(Request) ? FStandInServer::Json(TEXT(R"({"protocol": "rosc-ai/1", "tasks": ["depth", "focal"]})")) : NoKey();
	});
	State->Server.Answer(EHttpServerRequestVerbs::VERB_POST, TEXT("/v1/jobs"), [State, HasKey, NoKey](const FHttpServerRequest& Request)
	{
		if (!HasKey(Request))
		{
			return NoKey();
		}
		State->PostedTask = Request.QueryParams.FindRef(TEXT("task"));
		State->PostedContentType = FStandInServer::GetHeader(Request, TEXT("Content-Type"));
		FImage Posted;
		if (FImageUtils::DecompressImage(Request.Body.GetData(), Request.Body.Num(), Posted))
		{
			State->PostedSize = FIntPoint(Posted.SizeX, Posted.SizeY);
		}
		return FStandInServer::Json(TEXT(R"({"id": "job-1"})"));
	});
	State->Server.Answer(EHttpServerRequestVerbs::VERB_GET, TEXT("/v1/jobs/job-1"), [State](const FHttpServerRequest& Request)
	{
		// Still at work the first time it is asked, done the second.
		return ++State->StatusAsked == 1
			? FStandInServer::Json(TEXT(R"({"state": "running", "stage": "Working on the picture", "fraction": 0.4})"))
			: FStandInServer::Json(TEXT(R"({"state": "done", "stage": "Done", "fraction": 1.0, "result": {"picture": "/v1/jobs/job-1/picture"}})"));
	});
	State->Server.Answer(EHttpServerRequestVerbs::VERB_GET, TEXT("/v1/jobs/job-1/picture"), [](const FHttpServerRequest& Request)
	{
		// Half the still's size: black on the left, white on the right.
		FImage Depth(StillWidth / 2, StillHeight / 2, ERawImageFormat::G16, EGammaSpace::Linear);
		const TArrayView64<uint16> Values = Depth.AsG16();
		for (int32 Y = 0; Y < Depth.SizeY; ++Y)
		{
			for (int32 X = 0; X < Depth.SizeX; ++X)
			{
				Values[Y * Depth.SizeX + X] = static_cast<uint16>(65535 * X / (Depth.SizeX - 1));
			}
		}
		TArray64<uint8> Png;
		FImageUtils::CompressImage(Png, TEXT("png"), Depth);
		return FHttpServerResponse::Create(TArray<uint8>(Png), TEXT("image/png"));
	});

	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	Settings->Backend = ERoscAIBackend::Server;
	Settings->ServerUrl = State->Server.GetUrl();
	Settings->ServerKey = TEXT("not-the-key");
	URoscAISubsystem* Jobs = GetJobsForTest();
	State->FinishedHandle = Jobs->OnJobFinished().AddLambda([State](const FRoscAIResult& Result) { State->Results.Add(Result); });
	const FString Still = WriteTestStill();

	// With the wrong key: the check and the job both say so.
	Jobs->CheckServer([State](bool bReachable, const FText& Message) { State->bChecked = true; State->bReachable = bReachable; State->CheckMessage = Message; });
	FText Error;
	TestTrue(TEXT("A job for a server on this machine is taken without asking"), Jobs->Submit({ ERoscAITask::Depth, Still }, Error));

	WaitThen(*this, TEXT("the server's answer to the wrong key"), 60.0, [State, Jobs] { return State->bChecked && Jobs->GetJobsLeft() == 0; }, [this, State, Jobs, Key, Still]
	{
		TestFalse(TEXT("The check fails with the wrong key"), State->bReachable);
		TestEqual(TEXT("And says the key was not accepted"), *State->CheckMessage.ToString(), TEXT("The server at 127.0.0.1 did not accept the key."));
		if (TestEqual(TEXT("The job ended"), State->Results.Num(), 1))
		{
			TestFalse(TEXT("The job fails with the wrong key"), State->Results[0].bSuccess);
			TestEqual(TEXT("And says the same"), *State->Results[0].Error.ToString(), TEXT("The server at 127.0.0.1 did not accept the key."));
		}

		// With the right key.
		State->Results.Reset();
		State->bChecked = false;
		GetMutableDefault<URoscAISettings>()->ServerKey = Key;
		Jobs->CheckServer([State](bool bReachable, const FText& Message) { State->bChecked = true; State->bReachable = bReachable; State->CheckMessage = Message; });
		FText Error;
		TestTrue(TEXT("The job is taken"), Jobs->Submit({ ERoscAITask::Depth, Still }, Error));
	});

	WaitThen(*this, TEXT("the job on the stand-in server"), 60.0, [State, Jobs] { return State->bChecked && Jobs->GetJobsLeft() == 0; }, [this, State, Jobs]
	{
		Jobs->OnJobFinished().Remove(State->FinishedHandle);
		TestTrue(TEXT("The check passes with the right key"), State->bReachable);
		TestEqual(TEXT("And says what the server can do"), *State->CheckMessage.ToString(), TEXT("The server at 127.0.0.1 answers and can do: depth, focal length."));

		TestEqual(TEXT("The task was named to the server"), *State->PostedTask, TEXT("depth"));
		TestEqual(TEXT("The picture went as a PNG"), *State->PostedContentType, TEXT("image/png"));
		TestTrue(TEXT("At the still's size, which is under the limit"), State->PostedSize == FIntPoint(StillWidth, StillHeight));
		TestEqual(TEXT("The server was asked how the job stood until it was done"), State->StatusAsked, 2);

		FImage Depth;
		if (TestEqual(TEXT("The job ended"), State->Results.Num(), 1) && TestTrue(TEXT("The job succeeded"), State->Results[0].bSuccess)
			&& TestTrue(TEXT("Its result is a picture on disk"), FImageUtils::LoadImage(*State->Results[0].File, Depth)))
		{
			TestTrue(TEXT("The result was brought to the still's size"), Depth.SizeX == StillWidth && Depth.SizeY == StillHeight);
			TestTrue(TEXT("And is still 16-bit grey"), Depth.Format == ERawImageFormat::G16);
			TestEqual(TEXT("Black on the left"), ReadGrey(Depth, FIntPoint(0, 100)), 0.0, 0.01);
			TestEqual(TEXT("Mid grey in the middle"), ReadGrey(Depth, FIntPoint(StillWidth / 2, 100)), 0.5, 0.01);
			TestEqual(TEXT("White on the right"), ReadGrey(Depth, FIntPoint(StillWidth - 1, 100)), 1.0, 0.01);
		}
		State->SettingsBefore.PutBack();
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIServerFailuresTest, "Rosc.AI.Server.Failures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIServerFailuresTest::RunTest(const FString& Parameters)
{
	/** A server whose jobs fail, and one job that is given up while it runs. */
	struct FState
	{
		FSettingsBefore SettingsBefore;
		FStandInServer Server;
		bool bJobFails = true;
		bool bDeleted = false;
		TArray<FRoscAIResult> Results;
		FDelegateHandle FinishedHandle;
	};
	const TSharedRef<FState> State = MakeShared<FState>();
	State->Server.Answer(EHttpServerRequestVerbs::VERB_POST, TEXT("/v1/jobs"), [](const FHttpServerRequest& Request)
	{
		return Request.QueryParams.FindRef(TEXT("task")) == TEXT("matte")
			? FStandInServer::Json(TEXT(R"({"error": "This server has no model for 'matte'."})"), EHttpServerResponseCodes::BadRequest)
			: FStandInServer::Json(TEXT(R"({"id": "job-2"})"));
	});
	State->Server.Answer(EHttpServerRequestVerbs::VERB_GET, TEXT("/v1/jobs/job-2"), [State](const FHttpServerRequest& Request)
	{
		return State->bJobFails
			? FStandInServer::Json(TEXT(R"({"state": "failed", "error": "The picture has too little in it to judge the lens from."})"))
			: FStandInServer::Json(TEXT(R"({"state": "running", "stage": "Working on the picture", "fraction": 0.5})"));
	});
	State->Server.Answer(EHttpServerRequestVerbs::VERB_DELETE, TEXT("/v1/jobs/job-2"), [State](const FHttpServerRequest& Request)
	{
		State->bDeleted = true;
		return FStandInServer::Json(TEXT("{}"));
	});

	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	Settings->Backend = ERoscAIBackend::Server;
	Settings->ServerUrl = State->Server.GetUrl();
	Settings->ServerKey.Reset();
	URoscAISubsystem* Jobs = GetJobsForTest();
	State->FinishedHandle = Jobs->OnJobFinished().AddLambda([State](const FRoscAIResult& Result) { State->Results.Add(Result); });
	const FString Still = WriteTestStill();
	FText Error;
	TestTrue(TEXT("The matte job is taken"), Jobs->Submit({ ERoscAITask::Matte, Still }, Error));
	TestTrue(TEXT("The focal length job is taken"), Jobs->Submit({ ERoscAITask::FocalLength, Still }, Error));

	WaitThen(*this, TEXT("the jobs that fail"), 60.0, [Jobs] { return Jobs->GetJobsLeft() == 0; }, [this, State, Jobs, Still]
	{
		if (TestEqual(TEXT("Both jobs ended"), State->Results.Num(), 2))
		{
			TestFalse(TEXT("A job the server has no model for fails"), State->Results[0].bSuccess);
			TestEqual(TEXT("In the server's words"), *State->Results[0].Error.ToString(), TEXT("This server has no model for 'matte'."));
			TestFalse(TEXT("A job the server could not do fails"), State->Results[1].bSuccess);
			TestEqual(TEXT("In the server's words too"), *State->Results[1].Error.ToString(), TEXT("The picture has too little in it to judge the lens from."));
		}

		// A job that is given up while the server works on it, with another waiting behind it.
		State->Results.Reset();
		State->bJobFails = false;
		FText Error;
		TestTrue(TEXT("A job is taken"), Jobs->Submit({ ERoscAITask::Depth, Still }, Error));
		TestTrue(TEXT("And another behind it"), Jobs->Submit({ ERoscAITask::FocalLength, Still }, Error));
	});

	WaitThen(*this, TEXT("the server to be at work on the job"), 60.0, [Jobs]
	{
		FText Stage;
		float Fraction = 0.0f;
		Jobs->GetProgress(Stage, Fraction);
		return Fraction == 0.5f;
	}, [this, Jobs]
	{
		FText Stage;
		float Fraction = 0.0f;
		Jobs->GetProgress(Stage, Fraction);
		TestEqual(TEXT("The stage is the server's"), *Stage.ToString(), TEXT("Working on the picture"));
		TestEqual(TEXT("Two jobs are running or waiting"), Jobs->GetJobsLeft(), 2);
		Jobs->CancelAll();
	});

	WaitThen(*this, TEXT("the server to be told to stop"), 60.0, [State, Jobs] { return State->bDeleted && Jobs->GetJobsLeft() == 0; }, [this, State, Jobs]
	{
		Jobs->OnJobFinished().Remove(State->FinishedHandle);
		TestTrue(TEXT("The server was told to stop the job"), State->bDeleted);
		if (TestEqual(TEXT("Both jobs ended"), State->Results.Num(), 2))
		{
			for (const FRoscAIResult& Result : State->Results)
			{
				TestFalse(TEXT("A job given up did not succeed"), Result.bSuccess);
				TestTrue(TEXT("And is not reported as a fault"), Result.Error.IsEmpty());
			}
		}
		State->SettingsBefore.PutBack();
	});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAIModelDownloadTest, "Rosc.AI.Models.Download", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscAIModelDownloadTest::RunTest(const FString& Parameters)
{
	struct FState
	{
		FSettingsBefore SettingsBefore;
		FStandInServer Server;
		FString Folder;
		FRoscAIModel Model;
	};
	const TSharedRef<FState> State = MakeShared<FState>();

	// A models folder of the test's own, and a "model" of 200 000 bytes served from this machine.
	State->Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Rosc") / TEXT("AITests") / TEXT("Models"));
	IFileManager::Get().DeleteDirectory(*State->Folder, false, true);
	GetMutableDefault<URoscAISettings>()->ModelsFolder.Path = State->Folder;
	TestEqual(TEXT("The models folder is the one in the settings"), *Rosc::AIModelStore::GetFolder(), *State->Folder);

	TArray<uint8> Content;
	Content.SetNumUninitialized(200000);
	for (int32 Index = 0; Index < Content.Num(); ++Index)
	{
		Content[Index] = static_cast<uint8>((Index * 31 + Index / 251) & 0xFF);
	}
	const FString Reference = State->Folder / TEXT("reference.bin");
	FFileHelper::SaveArrayToFile(Content, *Reference);
	State->Server.Answer(EHttpServerRequestVerbs::VERB_GET, TEXT("/models/good.onnx"), [Content](const FHttpServerRequest& Request)
	{
		return FHttpServerResponse::Create(TArray<uint8>(Content), TEXT("application/octet-stream"));
	});
	State->Server.Answer(EHttpServerRequestVerbs::VERB_GET, TEXT("/models/tampered.onnx"), [Content](const FHttpServerRequest& Request)
	{
		TArray<uint8> Tampered(Content);
		Tampered[1000] ^= 0x01;
		return FHttpServerResponse::Create(MoveTemp(Tampered), TEXT("application/octet-stream"));
	});

	TestEqual(TEXT("A file that is not there has no hash"), *Rosc::AIModelStore::HashFile(State->Folder / TEXT("nothing.bin")), TEXT(""));
	FFileHelper::SaveStringToFile(TEXT("abc"), *(State->Folder / TEXT("abc.txt")), FFileHelper::EEncodingOptions::ForceAnsi);
	TestEqual(TEXT("The SHA-256 of \"abc\" is the published one"), *Rosc::AIModelStore::HashFile(State->Folder / TEXT("abc.txt")),
		TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

	State->Model.Id = TEXT("test-model");
	State->Model.Name = TEXT("Test Model");
	State->Model.FileName = TEXT("test-model.onnx");
	State->Model.SizeBytes = Content.Num();
	State->Model.Sha256 = Rosc::AIModelStore::HashFile(Reference);
	State->Model.Url = State->Server.GetUrl() / TEXT("models/tampered.onnx");

	URoscAISubsystem* Jobs = GEditor->GetEditorSubsystem<URoscAISubsystem>();
	TestFalse(TEXT("The model is not there to begin with"), Rosc::AIModelStore::IsInstalled(State->Model));
	TestTrue(TEXT("A download starts"), Jobs->DownloadModel(State->Model));
	TestFalse(TEXT("A second does not start while it runs"), Jobs->DownloadModel(State->Model));
	TestTrue(TEXT("The model being downloaded is named"), Jobs->GetDownloadingModel() != nullptr && Jobs->GetDownloadingModel()->Id == State->Model.Id);

	WaitThen(*this, TEXT("the download of a file that is not the listed one"), 60.0, [Jobs] { return Jobs->GetDownloadingModel() == nullptr; }, [this, State, Jobs]
	{
		TestEqual(TEXT("A file that is not the listed one is refused in words"), *Jobs->GetDownloadError().ToString(),
			TEXT("What was downloaded is not the file Rosc lists for Test Model, so it was not kept. Try again."));
		TestFalse(TEXT("And is not kept"), Rosc::AIModelStore::IsInstalled(State->Model));
		TestFalse(TEXT("Nor is any part of it"), FPaths::FileExists(Rosc::AIModelStore::GetFile(State->Model) + TEXT(".part")));

		State->Model.Url = State->Server.GetUrl() / TEXT("models/missing.onnx");
		TestTrue(TEXT("A download from an address with nothing at it starts"), Jobs->DownloadModel(State->Model));
	});

	WaitThen(*this, TEXT("the download of a file that is not there"), 60.0, [Jobs] { return Jobs->GetDownloadingModel() == nullptr; }, [this, State, Jobs]
	{
		TestEqual(TEXT("A file that is not there is reported in words"), *Jobs->GetDownloadError().ToString(),
			TEXT("Test Model could not be downloaded (404). Check the connection and try again."));
		TestFalse(TEXT("And nothing is kept"), Rosc::AIModelStore::IsInstalled(State->Model));

		State->Model.Url = State->Server.GetUrl() / TEXT("models/good.onnx");
		TestTrue(TEXT("The download of the listed file starts"), Jobs->DownloadModel(State->Model));
	});

	WaitThen(*this, TEXT("the download of the listed file"), 60.0, [Jobs] { return Jobs->GetDownloadingModel() == nullptr; }, [this, State, Jobs]
	{
		TestTrue(TEXT("The listed file downloads without a fault"), Jobs->GetDownloadError().IsEmpty());
		TestTrue(TEXT("And is in the models folder under its own name"), Rosc::AIModelStore::IsInstalled(State->Model));
		FText Error;
		TestTrue(TEXT("It passes the check made before a model is used"), Rosc::AIModelStore::Verify(State->Model, Error));

		// A file changed on disk afterwards, at the same size, no longer passes.
		TArray<uint8> Changed;
		FFileHelper::LoadFileToArray(Changed, *Rosc::AIModelStore::GetFile(State->Model));
		Changed[5] ^= 0x80;
		FFileHelper::SaveArrayToFile(Changed, *Rosc::AIModelStore::GetFile(State->Model));
		TestFalse(TEXT("A file changed afterwards does not"), Rosc::AIModelStore::Verify(State->Model, Error));
		TestTrue(TEXT("And the reason says to download it again"), Error.ToString().Contains(TEXT("download it again")));

		IFileManager::Get().DeleteDirectory(*State->Folder, false, true);
		State->SettingsBefore.PutBack();
	});
	return true;
}

#endif
