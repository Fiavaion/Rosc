// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Algo/Count.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Common/TcpSocketBuilder.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "HAL/PlatformProcess.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieSceneSequencePlayer.h"
#include "RoscCam.h"
#include "RoscCamServer.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraTarget.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscOperator.h"
#include "RoscOperatingSubsystem.h"
#include "RoscRig.h"
#include "RoscShotEditing.h"
#include "RoscTestDevice.h"
#include "RoscWidgetTestUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "SRoscGripTab.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using Rosc::Cam::EMessage;

	/** The RoscCam app as far as the editor can tell: the protocol over a real connection. */
	class FTestClient
	{
	public:
		~FTestClient()
		{
			if (Socket != nullptr)
			{
				Socket->Close();
				ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
			}
		}

		bool Connect()
		{
			Socket = FTcpSocketBuilder(TEXT("RoscCam test client")).AsBlocking().Build();
			const TSharedRef<FInternetAddr> Editor = FIPv4Endpoint(FIPv4Address(127, 0, 0, 1), Rosc::Cam::ServerPort).ToInternetAddr();
			if (Socket == nullptr || !Socket->Connect(*Editor))
			{
				return false;
			}
			Socket->SetNonBlocking(true);
			return true;
		}

		void Send(EMessage Type, const FString& Json)
		{
			TArray<uint8> Bytes;
			Rosc::Cam::AppendMessage(Bytes, Type, Json);
			SendBytes(Bytes);
		}

		void SendMotion(const FQuat& Rotation)
		{
			Rosc::Cam::FMotion Motion;
			Motion.Seconds = FPlatformTime::Seconds();
			Motion.Rotation = Rotation;
			TArray<uint8> Bytes;
			Rosc::Cam::AppendMessage(Bytes, EMessage::Motion, Rosc::Cam::WriteMotion(Motion));
			SendBytes(Bytes);
		}

		/** Takes in what the editor has sent; false once the editor has closed the connection. */
		bool Read()
		{
			uint8 Buffer[64 * 1024];
			int32 Count = 0;
			while (!bClosed)
			{
				if (!Socket->Recv(Buffer, sizeof(Buffer), Count))
				{
					bClosed = true;
				}
				else if (Count == 0)
				{
					break;
				}
				else
				{
					Reader.Add(TConstArrayView<uint8>(Buffer, Count));
				}
			}
			EMessage Type;
			TArray<uint8> Body;
			while (Reader.Next(Type, Body))
			{
				Received.Emplace(Type, MoveTemp(Body));
			}
			return !bClosed;
		}

		/** The first message of a type the editor has sent, taken off the list. */
		bool Take(EMessage Type, TArray<uint8>& OutBody)
		{
			const int32 Index = Received.IndexOfByPredicate([Type](const TPair<EMessage, TArray<uint8>>& Message) { return Message.Key == Type; });
			if (Index == INDEX_NONE)
			{
				return false;
			}
			OutBody = MoveTemp(Received[Index].Value);
			Received.RemoveAt(Index);
			return true;
		}

		/** The newest message of a type, the others of that type dropped. */
		bool TakeLatest(EMessage Type, TArray<uint8>& OutBody)
		{
			bool bFound = false;
			while (Take(Type, OutBody))
			{
				bFound = true;
			}
			return bFound;
		}

		int32 Count(EMessage Type) const
		{
			return Algo::CountIf(Received, [Type](const TPair<EMessage, TArray<uint8>>& Message) { return Message.Key == Type; });
		}

		bool IsClosed() const { return bClosed; }

	private:
		void SendBytes(const TArray<uint8>& Bytes)
		{
			int32 Sent = 0;
			Socket->Send(Bytes.GetData(), Bytes.Num(), Sent);
		}

		FSocket* Socket = nullptr;
		Rosc::Cam::FMessageReader Reader;
		TArray<TPair<EMessage, TArray<uint8>>> Received;
		bool bClosed = false;
	};

	TSharedPtr<FJsonObject> ToJson(const TArray<uint8>& Body)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Rosc::Cam::ToText(Body)), Object);
		return Object;
	}

	/** Lets the editor and Live Link take a turn, as an editor frame would, and the client read what came. */
	void Pump(URoscCamServer& Server, FTestClient& Client)
	{
		Server.Update();
		if (ILiveLinkClient* LiveLink = Rosc::DeviceTests::GetLiveLinkClient())
		{
			LiveLink->ForceTick();
		}
		URoscOperatingSubsystem::Get()->Update();
		FPlatformProcess::Sleep(0.002f);
		Client.Read();
	}

	/** Pumps until a message of the type comes, for up to a second. */
	bool WaitFor(URoscCamServer& Server, FTestClient& Client, EMessage Type, TArray<uint8>& OutBody)
	{
		const double Until = FPlatformTime::Seconds() + 1.0;
		while (FPlatformTime::Seconds() < Until)
		{
			Pump(Server, Client);
			if (Client.Take(Type, OutBody))
			{
				return true;
			}
		}
		return false;
	}

	/** Sends a control and waits for its answer. */
	TSharedPtr<FJsonObject> Control(URoscCamServer& Server, FTestClient& Client, const FString& Json)
	{
		Client.Send(EMessage::Control, Json);
		TArray<uint8> Body;
		return WaitFor(Server, Client, EMessage::Result, Body) ? ToJson(Body) : nullptr;
	}

	bool IsOk(const TSharedPtr<FJsonObject>& Result)
	{
		return Result.IsValid() && Result->GetBoolField(TEXT("ok"));
	}

	FString MessageOf(const TSharedPtr<FJsonObject>& Result)
	{
		FString Message;
		if (Result.IsValid())
		{
			Result->TryGetStringField(TEXT("message"), Message);
		}
		return Message;
	}

	/** The device held upright in landscape, top to the left, turned right by this much from facing north. */
	FQuat Held(double RightDegrees, double UpDegrees = 0.0)
	{
		// Columns: where the device's x (up), y (left, so west) and z (towards the operator, so south) point.
		const FQuat North(FMatrix(FPlane(0.0, 0.0, 1.0, 0.0), FPlane(-1.0, 0.0, 0.0, 0.0), FPlane(0.0, -1.0, 0.0, 0.0), FPlane(0.0, 0.0, 0.0, 1.0)));
		// Tipped up about east (x of the device's world), then turned about up (z), right-handed.
		const FQuat Tipped = FQuat(FVector(1.0, 0.0, 0.0), FMath::DegreesToRadians(UpDegrees)) * North;
		return FQuat(FVector(0.0, 0.0, 1.0), FMath::DegreesToRadians(-RightDegrees)) * Tipped;
	}

	ACineCameraActor* MakeCameraOnTripod(UWorld* World, ARoscRig*& OutRig)
	{
		ACineCameraActor* Camera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 150.0)));
		OutRig = Camera != nullptr ? Rosc::GripEditing::PutCameraOnRig(Camera, Rosc::Rig::GetDefaultPreset(ERoscRigType::Tripod)) : nullptr;
		if (OutRig != nullptr)
		{
			Rosc::GripEditing::EditRig(OutRig, FText::FromString(TEXT("Lock Off")), [](ARoscRig& Edited) { Edited.Operator.Style = ERoscOperatorStyle::LockedOff; });
		}
		return Camera;
	}

	void RemoveCamera(UWorld* World, ACineCameraActor* Camera)
	{
		if (Camera != nullptr)
		{
			GEditor->SelectNone(false, true, false);
			Rosc::GripEditing::TakeCameraOffRig(Camera);
			World->EditorDestroyActor(Camera, false);
		}
	}

	/** The mean brightness of each cell of a grid laid over part of an image, 0 to 255. */
	TArray<double> GridBrightness(const FImage& Decoded, const FIntRect& Area, int32 Columns, int32 Rows)
	{
		FImage Image;
		Decoded.CopyTo(Image, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const TArrayView64<FColor> Pixels = Image.AsBGRA8();
		TArray<double> Cells;
		Cells.SetNumZeroed(Columns * Rows);
		TArray<int32> Counts;
		Counts.SetNumZeroed(Columns * Rows);
		for (int32 Y = Area.Min.Y; Y < Area.Max.Y; ++Y)
		{
			for (int32 X = Area.Min.X; X < Area.Max.X; ++X)
			{
				const FColor& Pixel = Pixels[int64(Y) * Image.SizeX + X];
				const int32 Cell = ((Y - Area.Min.Y) * Rows / Area.Height()) * Columns + (X - Area.Min.X) * Columns / Area.Width();
				Cells[Cell] += (Pixel.R + Pixel.G + Pixel.B) / 3.0;
				++Counts[Cell];
			}
		}
		for (int32 Cell = 0; Cell < Cells.Num(); ++Cell)
		{
			Cells[Cell] /= FMath::Max(Counts[Cell], 1);
		}
		return Cells;
	}

	TArray<double> GridBrightness(const TArray<uint8>& Jpeg, int32 Columns, int32 Rows)
	{
		FImage Decoded;
		if (!FImageUtils::DecompressImage(Jpeg.GetData(), Jpeg.Num(), Decoded))
		{
			return TArray<double>();
		}
		return GridBrightness(Decoded, FIntRect(0, 0, Decoded.SizeX, Decoded.SizeY), Columns, Rows);
	}

	double LargestDifference(const TArray<double>& A, const TArray<double>& B)
	{
		double Largest = 0.0;
		for (int32 Cell = 0; Cell < FMath::Min(A.Num(), B.Num()); ++Cell)
		{
			Largest = FMath::Max(Largest, FMath::Abs(A[Cell] - B[Cell]));
		}
		return Largest;
	}

	FIntPoint JpegSize(const TArray<uint8>& Jpeg)
	{
		FImage Decoded;
		return FImageUtils::DecompressImage(Jpeg.GetData(), Jpeg.Num(), Decoded) ? FIntPoint(Decoded.SizeX, Decoded.SizeY) : FIntPoint::ZeroValue;
	}
}

/**
 * The RoscCam server driven by a test client over a real connection, as the app drives it:
 * nothing counts until the device is paired with the code the editor shows; the device's motion
 * pans and tilts the support through Live Link; each control does what the panel's does; a second
 * device is refused while one is paired; unpairing lets the device go.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCamServerTest, "Rosc.Editor.RoscCam.Server", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscCamServerTest::RunTest(const FString& Parameters)
{
	URoscCamServer* Server = URoscCamServer::Get();
	URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	if (!TestNotNull(TEXT("The RoscCam server exists"), Server) || !TestNotNull(TEXT("Live Link is on"), Rosc::DeviceTests::GetLiveLinkClient()))
	{
		return false;
	}
	TestFalse(TEXT("The server is off until started"), Server->IsRunning());

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ARoscRig* Rig = nullptr;
	ACineCameraActor* Camera = MakeCameraOnTripod(World, Rig);
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional);
	Sequence->Initialize();
	Sequence->GetMovieScene()->SetDisplayRate(FFrameRate(24, 1));
	Sequence->GetMovieScene()->SetPlaybackRange(TRange<FFrameNumber>(0, Sequence->GetMovieScene()->GetTickResolution().AsFrameNumber(10.0).Value));
	ON_SCOPE_EXIT
	{
		Server->Stop();
		Operating->DiscardTake();
		Operating->StopOperating();
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		RemoveCamera(World, Camera);
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc RoscCam test")));
	};
	if (!TestNotNull(TEXT("A camera on a tripod"), Rig))
	{
		return false;
	}

	FText Error;
	TestFalse(TEXT("The server needs a camera"), Server->Start(nullptr, Error));
	TestFalse(TEXT("and says so"), Error.IsEmpty());

	// Another program on the port: the server says so instead of starting.
	{
		FSocket* Squatter = FTcpSocketBuilder(TEXT("RoscCam test squatter")).BoundToEndpoint(FIPv4Endpoint(FIPv4Address::Any, Rosc::Cam::ServerPort)).Listening(1).Build();
		if (TestNotNull(TEXT("The test can take the port"), Squatter))
		{
			Error = FText::GetEmpty();
			TestFalse(TEXT("The server does not start with its port taken"), Server->Start(Camera, Error));
			TestTrue(*FString::Printf(TEXT("and says the port is taken (%s)"), *Error.ToString()), Error.ToString().Contains(TEXT("47821")));
			Squatter->Close();
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Squatter);
		}
	}
	// From here the server is worked from the Grip tab, as the operator would.
	const TSharedRef<FRoscCameraTarget> Target = MakeShared<FRoscCameraTarget>();
	Target->SetCamera(Camera);
	const TSharedRef<SRoscGripTab> Tab = SNew(SRoscGripTab, Target);
	const auto Look = [&Tab]
	{
		Tab->Tick(FGeometry(), FPlatformTime::Seconds(), 0.0f);
		Tab->SlatePrepass(1.0f);
	};
	Look();
	TestTrue(TEXT("The tab says RoscCam is off"), Rosc::WidgetTests::ShowsText(Tab, TEXT("Off. Start it")));
	const TSharedPtr<SCheckBox> ServerToggle = Rosc::WidgetTests::FindToggleShowing(Tab, TEXT("RoscCam Server"));
	if (!TestNotNull(TEXT("The tab has the RoscCam Server toggle"), ServerToggle.Get()))
	{
		return false;
	}
	ServerToggle->ToggleCheckedState();
	if (!TestTrue(TEXT("The toggle starts the server for the camera"), Server->IsRunning()))
	{
		return false;
	}
	Look();
	TestTrue(TEXT("The tab says it waits for the app"), Rosc::WidgetTests::ShowsText(Tab, TEXT("Waiting for RoscCam")));
	TestTrue(TEXT("and runs"), Server->IsRunning() && Server->GetCamera() == Camera);
	AddInfo(FString::Printf(TEXT("This computer's addresses: %s"), *URoscCamServer::GetAddresses()));

	FTestClient Phone;
	if (!TestTrue(TEXT("The app connects"), Phone.Connect()))
	{
		return false;
	}
	Pump(*Server, Phone);
	TestTrue(TEXT("The editor sees the device"), Server->HasDevice());
	TestFalse(TEXT("but it is not paired"), Server->IsPaired());
	TestTrue(TEXT("No code is shown before the device says hello"), Server->GetPairingCode().IsEmpty());

	// Before pairing nothing the device sends counts and nothing is sent to it.
	Phone.SendMotion(Held(0.0));
	Phone.Send(EMessage::Control, TEXT("{\"action\":\"operate\"}"));
	for (int32 Turn = 0; Turn < 10; ++Turn)
	{
		Pump(*Server, Phone);
	}
	TestFalse(TEXT("A control from a device not paired does nothing"), Operating->IsOperating());
	TestEqual(TEXT("and is not answered"), Phone.Count(EMessage::Result), 0);
	TestTrue(TEXT("No Live Link subject is made for a device not paired"), Server->GetSubjectName().IsNone());

	Phone.Send(EMessage::Hello, TEXT("{\"protocol\":1,\"device\":\"Test phone\",\"app\":\"0.1.0\"}"));
	TArray<uint8> Body;
	if (!TestTrue(TEXT("The editor welcomes the device"), WaitFor(*Server, Phone, EMessage::Welcome, Body)))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Welcome = ToJson(Body);
	TestTrue(TEXT("in the same protocol"), Welcome.IsValid() && Welcome->GetIntegerField(TEXT("protocol")) == Rosc::Cam::ProtocolVersion);
	const FString Code = Server->GetPairingCode();
	TestTrue(*FString::Printf(TEXT("The editor shows a four-digit code (%s)"), *Code), Code.Len() == 4 && Code.IsNumeric());
	TestEqual(TEXT("and the device's name"), Server->GetDeviceName(), FString(TEXT("Test phone")));
	Look();
	TestTrue(TEXT("The tab shows the code to type on the device"), Rosc::WidgetTests::ShowsText(Tab, *FString::Printf(TEXT("Type %s on the device"), *Code)));
	for (int32 Turn = 0; Turn < 10; ++Turn)
	{
		Pump(*Server, Phone);
	}
	TestEqual(TEXT("No picture goes to a device not paired"), Phone.Count(EMessage::Picture), 0);
	TestEqual(TEXT("nor any state"), Phone.Count(EMessage::State), 0);

	// Reading the code off the tab and typing it takes a while, with nothing sent meanwhile.
	const double Typing = FPlatformTime::Seconds() + 5.5;
	while (FPlatformTime::Seconds() < Typing)
	{
		Pump(*Server, Phone);
		FPlatformProcess::Sleep(0.05f);
	}
	TestTrue(TEXT("A device waiting to be paired is kept while its code is typed"), Server->HasDevice() && !Phone.IsClosed());

	const FString Wrong = Code == TEXT("0000") ? TEXT("0001") : TEXT("0000");
	Phone.Send(EMessage::Pair, FString::Printf(TEXT("{\"code\":\"%s\"}"), *Wrong));
	if (TestTrue(TEXT("A wrong code is answered"), WaitFor(*Server, Phone, EMessage::Paired, Body)))
	{
		const TSharedPtr<FJsonObject> Refused = ToJson(Body);
		TestTrue(TEXT("and refused, with the reason"), Refused.IsValid() && !Refused->GetBoolField(TEXT("paired")) && !Refused->GetStringField(TEXT("reason")).IsEmpty());
	}
	TestFalse(TEXT("A wrong code does not pair"), Server->IsPaired());

	Phone.Send(EMessage::Pair, FString::Printf(TEXT("{\"code\":\"%s\"}"), *Code));
	if (!TestTrue(TEXT("The right code is answered"), WaitFor(*Server, Phone, EMessage::Paired, Body)))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> PairedAnswer = ToJson(Body);
	TestTrue(TEXT("and pairs"), PairedAnswer.IsValid() && PairedAnswer->GetBoolField(TEXT("paired")) && Server->IsPaired());
	TestTrue(TEXT("The code is no longer shown"), Server->GetPairingCode().IsEmpty());
	const FName Subject = Server->GetSubjectName();
	TestEqual(TEXT("The device's motion comes in as a Live Link subject"), Subject, FName(TEXT("RoscCam: Test phone")));
	TestTrue(TEXT("which the Grip tab lists among the devices"), URoscOperatingSubsystem::GetDevices().Contains(Subject));
	Phone.SendMotion(Held(0.0));
	Pump(*Server, Phone);
	Look();
	TestTrue(TEXT("The tab says the device is paired and its motion arriving"), Rosc::WidgetTests::ShowsText(Tab, TEXT("Paired with Test phone")) && Rosc::WidgetTests::ShowsText(Tab, TEXT("Its motion is arriving")));
	TestTrue(TEXT("and chooses it as the device"), Rosc::WidgetTests::ShowsText(Tab, TEXT("RoscCam: Test phone")));

	if (TestTrue(TEXT("The paired device is sent the state"), WaitFor(*Server, Phone, EMessage::State, Body)))
	{
		const TSharedPtr<FJsonObject> State = ToJson(Body);
		AddInfo(Rosc::Cam::ToText(Body));
		TestTrue(TEXT("naming the camera"), State.IsValid() && State->GetStringField(TEXT("camera")) == Camera->GetActorLabel());
		TestTrue(TEXT("and not yet operating"), State.IsValid() && !State->GetBoolField(TEXT("operating")));
		TestTrue(TEXT("with the lens and T-stop"), State.IsValid() && !State->GetStringField(TEXT("lens")).IsEmpty() && State->GetStringField(TEXT("tStop")).StartsWith(TEXT("T")));
	}

	// Operate from the device: its turn pans the head, its tip tilts it.
	Phone.SendMotion(Held(0.0));
	Pump(*Server, Phone);
	TestTrue(TEXT("The device's motion is arriving"), Server->IsMotionArriving());
	const TSharedPtr<FJsonObject> Operate = Control(*Server, Phone, TEXT("{\"action\":\"operate\"}"));
	TestTrue(*FString::Printf(TEXT("Operate from the device is done (%s)"), *MessageOf(Operate)), IsOk(Operate));
	TestTrue(TEXT("and operates the tripod from the device"), Operating->IsOperating() && Operating->GetOperatedRig() == Rig && Operating->GetDevice() == Subject);
	Pump(*Server, Phone);
	const FRoscRigPose Before = Rig->GetPose();
	Phone.SendMotion(Held(30.0, 10.0));
	Pump(*Server, Phone);
	Pump(*Server, Phone);
	TestEqual(TEXT("Turning the device 30° right pans the head 30° right"), Rig->GetPose().Pan, Before.Pan + 30.0, 0.01);
	TestEqual(TEXT("Tipping it 10° up tilts the head 10° up"), Rig->GetPose().Tilt, Before.Tilt + 10.0, 0.01);
	if (TestTrue(TEXT("The state follows"), WaitFor(*Server, Phone, EMessage::State, Body)))
	{
		Phone.TakeLatest(EMessage::State, Body);
		const TSharedPtr<FJsonObject> State = ToJson(Body);
		TestTrue(TEXT("and says the device operates the support, live"), State.IsValid() && State->GetBoolField(TEXT("operating")) && State->GetBoolField(TEXT("live")));
	}

	// The operator turns round and presses Zero: the head stays, and turns on from the new hold.
	Phone.SendMotion(Held(120.0, 10.0));
	TestTrue(TEXT("Zero from the device is done"), IsOk(Control(*Server, Phone, TEXT("{\"action\":\"zero\"}"))));
	Pump(*Server, Phone);
	TestEqual(TEXT("After Zero, the new hold does not move the head"), Rig->GetPose().Pan, Before.Pan + 30.0, 0.01);
	Phone.SendMotion(Held(130.0, 10.0));
	Pump(*Server, Phone);
	Pump(*Server, Phone);
	TestEqual(TEXT("and the head turns on from it"), Rig->GetPose().Pan, Before.Pan + 40.0, 0.01);

	// A take, recorded, stopped and baked from the device.
	if (!TestTrue(TEXT("The sequence opens in Sequencer"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence)))
	{
		return false;
	}
	ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(0), EUpdatePositionMethod::Scrub));
	TestTrue(TEXT("Record from the device is done"), IsOk(Control(*Server, Phone, TEXT("{\"action\":\"record\"}"))));
	TestTrue(TEXT("and records"), Operating->IsRecording());
	const double Start = FPlatformTime::Seconds();
	double Elapsed = 0.0;
	while ((Elapsed = FPlatformTime::Seconds() - Start) < 0.75)
	{
		Phone.SendMotion(Held(130.0 + 20.0 * Elapsed, 10.0));
		Pump(*Server, Phone);
		FPlatformProcess::Sleep(1.0f / 96.0f);
	}
	TestTrue(TEXT("Stop from the device is done"), IsOk(Control(*Server, Phone, TEXT("{\"action\":\"stop\"}"))));
	TestFalse(TEXT("and stops recording"), Operating->IsRecording());
	TestTrue(TEXT("There is a take"), Operating->HasTake());
	const TSharedPtr<FJsonObject> Bake = Control(*Server, Phone, TEXT("{\"action\":\"bake\"}"));
	TestTrue(*FString::Printf(TEXT("Bake Take from the device is done (%s)"), *MessageOf(Bake)), IsOk(Bake));
	TestTrue(TEXT("and says where it went"), MessageOf(Bake).Contains(Sequence->GetName()));
	TestFalse(TEXT("The bake leaves no undo step open"), GEditor->IsTransactionActive());
	TestTrue(TEXT("Release from the device is done"), IsOk(Control(*Server, Phone, TEXT("{\"action\":\"release\"}"))));
	TestFalse(TEXT("and hands the head back"), Operating->IsOperating());

	// The lens: next and previous of the kit, each one undo step.
	URoscCameraPackageComponent* Package = URoscCameraPackageComponent::Find(Camera);
	const FRoscLensKit* Kit = Package != nullptr ? FRoscCameraDatabase::Get().FindLensKit(Package->Package.LensKitId) : nullptr;
	if (TestTrue(TEXT("The camera has a lens kit of more than one lens"), Kit != nullptr && Kit->Lenses.Num() > 1))
	{
		Rosc::CameraEditing::EditPackage(Package, FText::FromString(TEXT("First Lens")), [Kit](FRoscCameraPackage& Edited) { Edited.SelectLens(*Kit, 0); }, Error);
		const TSharedPtr<FJsonObject> Previous = Control(*Server, Phone, TEXT("{\"action\":\"lens\",\"step\":-1}"));
		TestFalse(TEXT("There is no lens before the first"), IsOk(Previous));
		TestFalse(TEXT("and the device is told why"), MessageOf(Previous).IsEmpty());
		TestTrue(TEXT("The next lens from the device is done"), IsOk(Control(*Server, Phone, TEXT("{\"action\":\"lens\",\"step\":1}"))));
		TestEqual(TEXT("and puts the kit's second lens on"), Package->Package.LensIndex, 1);
		TestEqual(TEXT("at its focal length"), Camera->GetCineCameraComponent()->CurrentFocalLength, static_cast<float>(Kit->Lenses[1].FocalLengthMm), 0.01f);
		TestTrue(TEXT("The lens change can be undone"), GEditor->UndoTransaction());
		TestEqual(TEXT("in one step"), Package->Package.LensIndex, 0);
	}

	// A rack to a focus mark, baked at the playhead.
	Rosc::FocusEditing::SetFocusDistance(Camera, 450.0f);
	Rosc::FocusEditing::AddFocusMark(Camera);
	Rosc::FocusEditing::SetFocusDistance(Camera, 150.0f);
	Pump(*Server, Phone);
	Phone.TakeLatest(EMessage::State, Body);
	const TSharedPtr<FJsonObject> MarkState = ToJson(Body);
	const TArray<TSharedPtr<FJsonValue>>* Marks = nullptr;
	TestTrue(TEXT("The state lists the focus mark"), MarkState.IsValid() && MarkState->TryGetArrayField(TEXT("marks"), Marks) && Marks->Num() == 1);
	const TSharedPtr<FJsonObject> Rack = Control(*Server, Phone, TEXT("{\"action\":\"rack\",\"mark\":0}"));
	TestTrue(*FString::Printf(TEXT("A rack to the mark from the device is done (%s)"), *MessageOf(Rack)), IsOk(Rack));
	TestTrue(TEXT("into the open sequence"), MessageOf(Rack).Contains(Sequence->GetName()));
	TestFalse(TEXT("A rack to a mark that is not there is refused"), IsOk(Control(*Server, Phone, TEXT("{\"action\":\"rack\",\"mark\":5}"))));
	TestFalse(TEXT("A control the editor does not know is refused"), IsOk(Control(*Server, Phone, TEXT("{\"action\":\"teleport\"}"))));

	// One device at a time.
	FTestClient Other;
	if (TestTrue(TEXT("A second device connects"), Other.Connect()))
	{
		const double Until = FPlatformTime::Seconds() + 1.0;
		while (!Other.IsClosed() && FPlatformTime::Seconds() < Until)
		{
			Pump(*Server, Phone);
			Other.Read();
		}
		TestTrue(TEXT("and is let go while one is paired"), Other.IsClosed());
		TestTrue(TEXT("having been told why"), Other.Take(EMessage::Paired, Body) && ToJson(Body).IsValid() && !ToJson(Body)->GetBoolField(TEXT("paired")));
		TestTrue(TEXT("The paired device stays paired"), Server->IsPaired());
	}

	Look();
	const TSharedPtr<SButton> Unpair = Rosc::WidgetTests::FindButtonShowing(Tab, TEXT("Unpair"));
	if (TestNotNull(TEXT("The tab has Unpair"), Unpair.Get()))
	{
		Unpair->SimulateClick();
	}
	TestFalse(TEXT("Unpair lets the device go"), Server->HasDevice());
	Pump(*Server, Phone);
	TestTrue(TEXT("and closes its connection"), Phone.IsClosed());
	TestFalse(TEXT("Its subject leaves Live Link"), URoscOperatingSubsystem::GetDevices().Contains(Subject));

	Look();
	ServerToggle->ToggleCheckedState();
	TestFalse(TEXT("The toggle stops the server"), Server->IsRunning());
	FTestClient Late;
	TestFalse(TEXT("and nothing more can connect"), Late.Connect());
	return true;
}

/**
 * The picture a paired device is sent, over the engine's own frames: the camera's view (as the
 * storyboard frames take it) at 960 x 540 or more, 20 frames a second or more. Needs a renderer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCamPictureTest, "Rosc.Editor.RoscCam.Picture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscCamPictureTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: the picture needs a renderer (tools/test.ps1 -Rendering)."));
		return true;
	}
	URoscCamServer* Server = URoscCamServer::Get();
	UWorld* World = GEditor->GetEditorWorldContext().World();

	struct FRun
	{
		ARoscRig* Rig = nullptr;
		ACineCameraActor* Camera = nullptr;
		AStaticMeshActor* Wall = nullptr;
		FTestClient Phone;
		double StartSeconds = 0.0;
		double FirstPictureSeconds = 0.0;
		int32 Pictures = 0;
		TArray<uint8> Latest;
	};
	const TSharedRef<FRun> Run = MakeShared<FRun>();
	Run->Camera = MakeCameraOnTripod(World, Run->Rig);
	// A wall across the left half of the view, so the picture has something to be matched by.
	Run->Wall = World->SpawnActor<AStaticMeshActor>(FVector(700.0, -350.0, 150.0), FRotator::ZeroRotator);
	Run->Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Run->Wall->SetActorScale3D(FVector(0.2, 7.0, 10.0));

	const auto CleanUp = [Server, World, Run]
	{
		Server->Stop();
		if (Run->Wall != nullptr)
		{
			World->EditorDestroyActor(Run->Wall, false);
		}
		RemoveCamera(World, Run->Camera);
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc RoscCam picture test")));
	};
	FText Error;
	TArray<uint8> Body;
	if (!TestTrue(TEXT("The server starts"), Server->Start(Run->Camera, Error)) || !TestTrue(TEXT("The app connects"), Run->Phone.Connect()))
	{
		CleanUp();
		return false;
	}
	Run->Phone.Send(EMessage::Hello, TEXT("{\"protocol\":1,\"device\":\"Test tablet\"}"));
	if (!WaitFor(*Server, Run->Phone, EMessage::Welcome, Body))
	{
		AddError(TEXT("The editor did not welcome the device"));
		CleanUp();
		return false;
	}
	Run->Phone.Send(EMessage::Pair, FString::Printf(TEXT("{\"code\":\"%s\"}"), *Server->GetPairingCode()));
	if (!TestTrue(TEXT("The device pairs"), WaitFor(*Server, Run->Phone, EMessage::Paired, Body) && Server->IsPaired()))
	{
		CleanUp();
		return false;
	}
	Run->StartSeconds = FPlatformTime::Seconds();

	// The engine runs its own frames from here, and the server sends from its ticker as in use.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, CleanUp]
	{
		const double Now = FPlatformTime::Seconds();
		// The app sends its motion all the time; without it the editor lets the device go.
		Run->Phone.SendMotion(Held(0.0));
		Run->Phone.Read();
		TArray<uint8> Jpeg;
		while (Run->Phone.Take(EMessage::Picture, Jpeg))
		{
			if (Run->Pictures++ == 0)
			{
				Run->FirstPictureSeconds = Now;
			}
			Run->Latest = MoveTemp(Jpeg);
		}
		const bool bTimeUp = Now - Run->StartSeconds > 5.0;
		const bool bEnough = Run->Pictures > 1 && Now - Run->FirstPictureSeconds >= 3.0;
		if (!bTimeUp && !bEnough)
		{
			return false;
		}

		const double Seconds = Now - Run->FirstPictureSeconds;
		const double Rate = Run->Pictures > 1 ? (Run->Pictures - 1) / Seconds : 0.0;
		AddInfo(FString::Printf(TEXT("%d pictures in %.2f s: %.1f a second, the last %d bytes"), Run->Pictures, Seconds, Rate, Run->Latest.Num()));
		TestTrue(TEXT("The device is sent pictures"), Run->Pictures > 1);
		TestTrue(*FString::Printf(TEXT("at 20 a second or more (%.1f)"), Rate), Rate >= 20.0);
		const FIntPoint Size = JpegSize(Run->Latest);
		TestTrue(*FString::Printf(TEXT("of 960 x 540 or more (%d x %d)"), Size.X, Size.Y), Size.X >= 960 && Size.Y >= 540);

		// The camera's view as the storyboard frames take it, compared on a coarse grid.
		FRoscStoryboardFrame Storyboard;
		if (TestTrue(TEXT("The storyboard picture of the camera is taken"), Rosc::ShotEditing::TakeStoryboardPicture(*Run->Camera, Storyboard)))
		{
			const TArray<double> Sent = GridBrightness(Run->Latest, 8, 4);
			const TArray<double> Seen = GridBrightness(Storyboard.Jpeg, 8, 4);
			if (TestTrue(TEXT("Both pictures are read"), Sent.Num() == 32 && Seen.Num() == 32))
			{
				const double Largest = LargestDifference(Sent, Seen);
				const double WallSide = (Sent[8] + Sent[9] + Sent[16] + Sent[17]) / 4.0;
				const double OpenSide = (Sent[14] + Sent[15] + Sent[22] + Sent[23]) / 4.0;
				AddInfo(FString::Printf(TEXT("Largest difference from the storyboard picture %.1f of 255; wall side %.1f, open side %.1f"), Largest, WallSide, OpenSide));
				TestTrue(TEXT("The picture is the camera's view"), Largest < 20.0);
				TestTrue(TEXT("with the wall in it"), FMath::Abs(WallSide - OpenSide) > 10.0);
			}
		}
		CleanUp();
		return true;
	}));
	return true;
}

/**
 * The RoscCam app itself, on the Android emulator, against this editor: tools/test-rosccam.ps1
 * starts the app and this test with -RoscCamDevice=<folder>, and the two take turns through files
 * in that folder. The app connects and pairs with the code this test writes; the script turns the
 * device 30° right, and the support pans by the same amount; then the script grabs the device's
 * screen, which has to show the camera's picture. Without the folder there is nothing to do.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCamDeviceTest, "Rosc.Editor.RoscCam.Device", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscCamDeviceTest::RunTest(const FString& Parameters)
{
	FString Folder;
	if (!FParse::Value(FCommandLine::Get(), TEXT("RoscCamDevice="), Folder))
	{
		AddInfo(TEXT("Skipped: run by tools/test-rosccam.ps1, with the app on the Android emulator."));
		return true;
	}
	if (!FApp::CanEverRender())
	{
		AddError(TEXT("The device test needs a renderer (-Rendering)."));
		return false;
	}
	URoscCamServer* Server = URoscCamServer::Get();
	URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	UWorld* World = GEditor->GetEditorWorldContext().World();

	struct FRun
	{
		FString Folder;
		ARoscRig* Rig = nullptr;
		ACineCameraActor* Camera = nullptr;
		int32 Stage = 0;
		double StageSeconds = 0.0;
		double StartPan = 0.0;
		double LastPan = 0.0;
		double SteadySince = 0.0;
	};
	const TSharedRef<FRun> Run = MakeShared<FRun>();
	Run->Folder = Folder;
	Run->Camera = MakeCameraOnTripod(World, Run->Rig);
	const auto Write = [Run](const TCHAR* Name, const FString& Text)
	{
		FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Run->Folder, Name));
	};
	const auto CleanUp = [Server, Operating, World, Run]
	{
		Operating->StopOperating();
		Server->Stop();
		RemoveCamera(World, Run->Camera);
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc RoscCam device test")));
	};
	FText Error;
	if (Run->Rig == nullptr || !Server->Start(Run->Camera, Error))
	{
		AddError(FString::Printf(TEXT("The server did not start for a camera on a tripod: %s"), *Error.ToString()));
		CleanUp();
		return false;
	}
	Write(TEXT("running.txt"), TEXT("The RoscCam server is running."));
	Run->StageSeconds = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, Server, Operating, Write, CleanUp]
	{
		const double Now = FPlatformTime::Seconds();
		const double Pan = Run->Rig->GetPose().Pan;
		const auto Next = [&Run, Now] { ++Run->Stage; Run->StageSeconds = Now; };
		const auto Fail = [this, &CleanUp](const FString& Why) { AddError(Why); CleanUp(); return true; };
		const TCHAR* const Waits[] = { TEXT("the app to connect"), TEXT("the app to pair and send its motion"), TEXT("the support to pan"), TEXT("the screen grab") };
		if (Now - Run->StageSeconds > (Run->Stage == 0 ? 120.0 : 60.0))
		{
			return Fail(FString::Printf(TEXT("Waited too long for %s."), Waits[Run->Stage]));
		}
		switch (Run->Stage)
		{
		case 0:
			if (!Server->GetPairingCode().IsEmpty())
			{
				AddInfo(FString::Printf(TEXT("%s asks to pair."), *Server->GetDeviceName()));
				Write(TEXT("code.txt"), Server->GetPairingCode());
				Next();
			}
			return false;
		case 1:
			if (Server->IsPaired() && Server->IsMotionArriving())
			{
				// As Operate on the Grip tab does, with the device the tab chooses.
				FText OperateError;
				if (!Operating->StartOperating(Run->Rig, Server->GetSubjectName(), OperateError))
				{
					return Fail(FString::Printf(TEXT("Operate failed: %s"), *OperateError.ToString()));
				}
				Run->StartPan = Pan;
				Run->LastPan = Pan;
				Run->SteadySince = Now;
				Write(TEXT("operating.txt"), FString::Printf(TEXT("%.3f"), Pan));
				Next();
			}
			return false;
		case 2:
			if (!FMath::IsNearlyEqual(Pan, Run->LastPan, 0.01))
			{
				Run->LastPan = Pan;
				Run->SteadySince = Now;
			}
			// The turn has come when the head has moved, and then held still for half a second.
			if (FMath::Abs(Pan - Run->StartPan) > 1.0 && Now - Run->SteadySince > 0.5)
			{
				const double Turned = Pan - Run->StartPan;
				AddInfo(FString::Printf(TEXT("The device turned 30° right; the tripod panned %.2f°."), Turned));
				TestEqual(TEXT("Turning the device 30° right pans the support 30° right, within 1°"), Turned, 30.0, 1.0);
				Write(TEXT("panned.txt"), FString::Printf(TEXT("%.3f"), Turned));
				Next();
			}
			return false;
		default:
			break;
		}

		const FString Screen = FPaths::Combine(Run->Folder, TEXT("screen.png"));
		FString Density;
		if (!FPaths::FileExists(FPaths::Combine(Run->Folder, TEXT("grabbed.txt")))
			|| !FFileHelper::LoadFileToString(Density, *FPaths::Combine(Run->Folder, TEXT("density.txt"))))
		{
			return false;
		}
		FImage Grab;
		if (!FImageUtils::LoadImage(*Screen, Grab))
		{
			return Fail(TEXT("The screen grab could not be read."));
		}
		// The app draws the picture as large as it fits left of its controls (railWidth, 248 dp).
		const double PixelsPerDp = FCString::Atod(*Density) / 160.0;
		const double AreaWidth = Grab.SizeX - 248.0 * PixelsPerDp;
		// The picture has the camera's shape (a 3:2 sensor sends 960 x 640).
		const double Aspect = Run->Camera->GetCineCameraComponent()->AspectRatio;
		const double Width = FMath::Min(AreaWidth, Grab.SizeY * Aspect);
		const double Height = Width / Aspect;
		const FIntRect Picture(FMath::RoundToInt32((AreaWidth - Width) / 2.0), FMath::RoundToInt32((Grab.SizeY - Height) / 2.0),
			FMath::RoundToInt32((AreaWidth + Width) / 2.0), FMath::RoundToInt32((Grab.SizeY + Height) / 2.0));
		AddInfo(FString::Printf(TEXT("Screen %d x %d at %s dpi; the picture is at %s"), Grab.SizeX, Grab.SizeY, *Density.TrimStartAndEnd(), *Picture.ToString()));

		FRoscStoryboardFrame Storyboard;
		if (TestTrue(TEXT("Grid sizes fit"), Picture.Width() > 64 && Picture.Height() > 32 && Picture.Max.X <= Grab.SizeX && Picture.Max.Y <= Grab.SizeY)
			&& TestTrue(TEXT("The storyboard picture of the camera is taken"), Rosc::ShotEditing::TakeStoryboardPicture(*Run->Camera, Storyboard)))
		{
			const TArray<double> OnDevice = GridBrightness(Grab, Picture, 8, 4);
			const TArray<double> Seen = GridBrightness(Storyboard.Jpeg, 8, 4);
			const double Largest = LargestDifference(OnDevice, Seen);
			const double Spread = FMath::Max(Seen) - FMath::Min(Seen);
			AddInfo(FString::Printf(TEXT("On the device the picture differs from the camera's view by %.1f of 255 at most; the view's own cells spread over %.1f."), Largest, Spread));
			TestTrue(TEXT("The camera's view has something in it to match"), Spread > 20.0);
			TestTrue(TEXT("The device's screen shows the camera's picture"), Largest < 25.0);
		}
		CleanUp();
		return true;
	}));
	return true;
}

#endif
