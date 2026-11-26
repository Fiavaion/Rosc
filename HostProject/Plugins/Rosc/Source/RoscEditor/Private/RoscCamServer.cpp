// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscCamServer.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Common/TcpSocketBuilder.h"
#include "Common/UdpSocketBuilder.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/PlatformTime.h"
#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "LevelSequence.h"
#include "LiveLinkSourceSettings.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "RoscCam.h"
#include "RoscCamPicture.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscEditorSettings.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscOperatingSubsystem.h"
#include "RoscRig.h"
#include "RoscRigTypes.h"
#include "RoscTake.h"
#include "RoscUnits.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

#define LOCTEXT_NAMESPACE "RoscCamServer"

namespace
{
	/** The smallest picture sent (ADR-014); a frame of another shape keeps this much on both sides. */
	constexpr int32 PictureWidth = 960;
	constexpr int32 PictureHeight = 540;
	constexpr double PictureFramesPerSecond = 24.0;

	constexpr double AnnounceInterval = 1.0;
	constexpr double StateInterval = 0.5;
	/** The app sends its motion many times a second; a device silent this long has gone. */
	constexpr double SilenceSeconds = 5.0;
	constexpr double MotionArrivingSeconds = 0.5;
	/** Wrong codes a device may type before it is let go and has to connect again for a new one. */
	constexpr int32 PairingTries = 3;
	constexpr int32 DeviceNameLength = 40;

	ISocketSubsystem* GetSockets()
	{
		return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	}

	void DestroySocket(FSocket*& Socket)
	{
		if (Socket != nullptr)
		{
			Socket->Close();
			GetSockets()->DestroySocket(Socket);
			Socket = nullptr;
		}
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	FString GetString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		if (Object.IsValid())
		{
			Object->TryGetStringField(Field, Value);
		}
		return Value;
	}

	int32 GetInteger(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		int32 Value = 0;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(Field, Value);
		}
		return Value;
	}

	FString WriteJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Object, Writer);
		return Json;
	}

	TArray<TSharedPtr<FJsonValue>> ToJsonStrings(const TArray<FString>& Lines)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Line : Lines)
		{
			Values.Add(MakeShared<FJsonValueString>(Line));
		}
		return Values;
	}

	/** What the device calls itself, kept to letters a list and a Live Link subject can show. */
	FString CleanDeviceName(const FString& Name)
	{
		FString Clean;
		for (const TCHAR Character : Name.Left(DeviceNameLength))
		{
			Clean.AppendChar(FChar::IsPrint(Character) ? Character : TEXT(' '));
		}
		Clean.TrimStartAndEndInline();
		return Clean.IsEmpty() ? FString(TEXT("Device")) : Clean;
	}

	FString GetEditorName()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Rosc"));
		return Plugin.IsValid() ? FString::Printf(TEXT("Rosc %s"), *Plugin->GetDescriptor().VersionName) : FString(TEXT("Rosc"));
	}

	/** The picture for a camera whose view is this wide for its height: at least the smallest picture both ways. */
	FIntPoint GetPictureSize(double Aspect)
	{
		if (Aspect <= UE_SMALL_NUMBER)
		{
			return FIntPoint(PictureWidth, PictureHeight);
		}
		const double Smallest = double(PictureWidth) / PictureHeight;
		return Aspect >= Smallest
			? FIntPoint(FMath::RoundToInt32(PictureHeight * Aspect / 2.0) * 2, PictureHeight)
			: FIntPoint(PictureWidth, FMath::RoundToInt32(PictureWidth / Aspect / 2.0) * 2);
	}
}

/** The Live Link source a paired device's motion comes in through. */
class FRoscCamLiveLinkSource : public ILiveLinkSource
{
public:
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override
	{
		Client = InClient;
		Guid = InSourceGuid;
	}
	virtual bool IsSourceStillValid() const override { return true; }
	virtual bool RequestSourceShutdown() override
	{
		Client = nullptr;
		return true;
	}
	virtual FText GetSourceType() const override { return LOCTEXT("SourceType", "RoscCam"); }
	virtual FText GetSourceMachineName() const override { return FText::FromString(FPlatformProcess::ComputerName()); }
	virtual FText GetSourceStatus() const override { return Subject.IsNone() ? LOCTEXT("SourceWaiting", "Waiting for a device") : LOCTEXT("SourcePaired", "Paired"); }

	void AddSubject(FName Name)
	{
		RemoveSubject();
		if (Client == nullptr)
		{
			return;
		}
		Subject = Name;
		FLiveLinkStaticDataStruct StaticData(FLiveLinkTransformStaticData::StaticStruct());
		Client->PushSubjectStaticData_AnyThread(FLiveLinkSubjectKey(Guid, Subject), ULiveLinkTransformRole::StaticClass(), MoveTemp(StaticData));
	}

	void RemoveSubject()
	{
		if (Client != nullptr && !Subject.IsNone())
		{
			Client->RemoveSubject_AnyThread(FLiveLinkSubjectKey(Guid, Subject));
		}
		Subject = NAME_None;
	}

	void Push(const FTransform& Transform)
	{
		if (Client == nullptr || Subject.IsNone())
		{
			return;
		}
		FLiveLinkFrameDataStruct Frame(FLiveLinkTransformFrameData::StaticStruct());
		Frame.Cast<FLiveLinkTransformFrameData>()->Transform = Transform;
		Frame.GetBaseData()->WorldTime = FLiveLinkWorldTime(FPlatformTime::Seconds());
		Client->PushSubjectFrameData_AnyThread(FLiveLinkSubjectKey(Guid, Subject), MoveTemp(Frame));
	}

	FName GetSubject() const { return Subject; }

private:
	ILiveLinkClient* Client = nullptr;
	FGuid Guid;
	FName Subject;
};

struct URoscCamServer::FConnection
{
	FSocket* Socket = nullptr;
	Rosc::Cam::FMessageReader Reader;
	TArray<uint8> Out;
	FString DeviceName;
	FString PairingCode;
	int32 WrongCodes = 0;
	bool bSaidHello = false;
	bool bPaired = false;
	double LastHeardSeconds = 0.0;
	double LastMotionSeconds = -1.0;
};

URoscCamServer* URoscCamServer::Get()
{
	return GEditor != nullptr ? GEditor->GetEditorSubsystem<URoscCamServer>() : nullptr;
}

void URoscCamServer::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

bool URoscCamServer::Start(ACineCameraActor* InCamera, FText& OutError)
{
	if (InCamera == nullptr)
	{
		OutError = LOCTEXT("NoCamera", "Choose a camera for RoscCam first.");
		return false;
	}
	if (IsRunning())
	{
		Camera = InCamera;
		LastStateJson.Reset();
		return true;
	}

	ListenSocket = FTcpSocketBuilder(TEXT("RoscCam server"))
		.AsNonBlocking()
		// Not shared, so a second editor is told the port is taken instead of taking it over.
		.AsReusable(false)
		.BoundToEndpoint(FIPv4Endpoint(FIPv4Address::Any, Rosc::Cam::ServerPort))
		.Listening(2)
		.WithSendBufferSize(1024 * 1024);
	if (ListenSocket == nullptr)
	{
		OutError = FText::Format(LOCTEXT("PortTaken", "RoscCam could not listen on port {0}: another program, or another editor running RoscCam, has it. Stop that one and start again."),
			FText::AsNumber(Rosc::Cam::ServerPort, &FNumberFormattingOptions::DefaultNoGrouping()));
		return false;
	}
	// Without the announcement the app can still be given this computer's address.
	AnnounceSocket = FUdpSocketBuilder(TEXT("RoscCam announcement")).AsNonBlocking().WithBroadcast();

	if (ILiveLinkClient* LiveLink = URoscOperatingSubsystem::GetLiveLinkClient())
	{
		LiveLinkSource = MakeShared<FRoscCamLiveLinkSource>();
		LiveLinkSourceGuid = LiveLink->AddSource(LiveLinkSource);
		if (ULiveLinkSourceSettings* Settings = LiveLink->GetSourceSettings(LiveLinkSourceGuid))
		{
			// The newest motion at once: an operator feels any delay added for smoothness.
			Settings->Mode = ELiveLinkSourceMode::Latest;
		}
	}

	Camera = InCamera;
	Random.GenerateNewSeed();
	NextAnnounceSeconds = 0.0;
	LastStateJson.Reset();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &URoscCamServer::Tick));
	return true;
}

void URoscCamServer::Stop()
{
	CloseConnection();
	if (TickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	DestroySocket(ListenSocket);
	DestroySocket(AnnounceSocket);
	if (LiveLinkSource.IsValid())
	{
		if (ILiveLinkClient* LiveLink = URoscOperatingSubsystem::GetLiveLinkClient())
		{
			LiveLink->RemoveSource(LiveLinkSourceGuid);
		}
		LiveLinkSource.Reset();
		LiveLinkSourceGuid.Invalidate();
	}
	Picture.Reset();
	Camera.Reset();
}

bool URoscCamServer::IsPaired() const
{
	return Connection.IsValid() && Connection->bPaired;
}

FString URoscCamServer::GetDeviceName() const
{
	return Connection.IsValid() ? Connection->DeviceName : FString();
}

FString URoscCamServer::GetPairingCode() const
{
	return Connection.IsValid() && !Connection->bPaired ? Connection->PairingCode : FString();
}

FName URoscCamServer::GetSubjectName() const
{
	return LiveLinkSource.IsValid() ? LiveLinkSource->GetSubject() : NAME_None;
}

bool URoscCamServer::IsMotionArriving() const
{
	return IsPaired() && Connection->LastMotionSeconds >= 0.0
		&& FPlatformTime::Seconds() - Connection->LastMotionSeconds < MotionArrivingSeconds;
}

void URoscCamServer::Unpair()
{
	CloseConnection();
}

FString URoscCamServer::GetAddresses()
{
	TArray<TSharedPtr<FInternetAddr>> Addresses;
	GetSockets()->GetLocalAdapterAddresses(Addresses);
	TArray<FString> Texts;
	for (const TSharedPtr<FInternetAddr>& Address : Addresses)
	{
		if (!Address.IsValid() || Address->GetProtocolType() != FNetworkProtocolTypes::IPv4)
		{
			continue;
		}
		const FString Text = Address->ToString(false);
		if (!Text.StartsWith(TEXT("127.")) && !Text.StartsWith(TEXT("169.254.")))
		{
			Texts.AddUnique(Text);
		}
	}
	return FString::Join(Texts, TEXT(", "));
}

bool URoscCamServer::Tick(float DeltaSeconds)
{
	Update();
	return true;
}

void URoscCamServer::Update()
{
	if (!IsRunning())
	{
		return;
	}
	if (!Camera.IsValid())
	{
		// The camera was deleted or its level closed: there is nothing left to operate or show.
		Stop();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextAnnounceSeconds)
	{
		Announce();
		NextAnnounceSeconds = Now + AnnounceInterval;
	}
	Accept();
	Receive();
	if (Connection.IsValid() && Now - Connection->LastHeardSeconds > SilenceSeconds)
	{
		CloseConnection();
	}
	if (IsPaired())
	{
		const FIntPoint Size = GetPictureSize(Camera->GetCineCameraComponent()->AspectRatio);
		if (!Picture.IsValid() || Picture->GetWidth() != Size.X || Picture->GetHeight() != Size.Y)
		{
			Picture = MakeShared<FRoscCamPicture>(Size.X, Size.Y, PictureFramesPerSecond);
		}
		Picture->Update(Camera.Get(), Now);
		SendPicture();
		SendState();
	}
	Flush();
}

void URoscCamServer::Accept()
{
	bool bPending = false;
	while (ListenSocket->HasPendingConnection(bPending) && bPending)
	{
		const TSharedRef<FInternetAddr> From = GetSockets()->CreateInternetAddr();
		FSocket* Socket = ListenSocket->Accept(*From, TEXT("RoscCam device"));
		if (Socket == nullptr)
		{
			return;
		}
		Socket->SetNonBlocking(true);
		Socket->SetNoDelay(true);
		if (IsPaired())
		{
			// One device operates at a time; the one paired keeps the camera until it is let go.
			const TSharedRef<FJsonObject> Answer = MakeShared<FJsonObject>();
			Answer->SetBoolField(TEXT("paired"), false);
			Answer->SetStringField(TEXT("reason"), FText::Format(LOCTEXT("AnotherDevice", "{0} is paired with this editor. Unpair it in the Grip tab first."), FText::FromString(Connection->DeviceName)).ToString());
			TArray<uint8> Refusal;
			Rosc::Cam::AppendMessage(Refusal, Rosc::Cam::EMessage::Paired, WriteJson(Answer));
			int32 Sent = 0;
			Socket->Send(Refusal.GetData(), Refusal.Num(), Sent);
			DestroySocket(Socket);
			continue;
		}
		// A device that has not paired yet gives way to the one connecting now.
		CloseConnection();
		Connection = MakeShared<FConnection>();
		Connection->Socket = Socket;
		Connection->LastHeardSeconds = FPlatformTime::Seconds();
	}
}

void URoscCamServer::Receive()
{
	if (!Connection.IsValid())
	{
		return;
	}
	uint8 Buffer[16 * 1024];
	for (;;)
	{
		int32 Read = 0;
		if (!Connection->Socket->Recv(Buffer, sizeof(Buffer), Read))
		{
			// The device closed the connection, or it broke.
			CloseConnection();
			return;
		}
		if (Read == 0)
		{
			break;
		}
		Connection->LastHeardSeconds = FPlatformTime::Seconds();
		Connection->Reader.Add(TConstArrayView<uint8>(Buffer, Read));
	}
	Rosc::Cam::EMessage Type;
	TArray<uint8> Body;
	while (Connection.IsValid() && Connection->Reader.Next(Type, Body))
	{
		Handle(Type, Body);
	}
	if (Connection.IsValid() && Connection->Reader.IsBroken())
	{
		CloseConnection();
	}
}

void URoscCamServer::Handle(Rosc::Cam::EMessage Type, const TArray<uint8>& Body)
{
	using Rosc::Cam::EMessage;
	switch (Type)
	{
	case EMessage::Hello:
		HandleHello(Rosc::Cam::ToText(Body));
		break;
	case EMessage::Pair:
		HandlePair(Rosc::Cam::ToText(Body));
		break;
	case EMessage::Motion:
	{
		Rosc::Cam::FMotion Motion;
		if (Connection->bPaired && Rosc::Cam::ReadMotion(Body, Motion))
		{
			LiveLinkSource->Push(Rosc::Cam::ToCameraTransform(Motion));
			Connection->LastMotionSeconds = FPlatformTime::Seconds();
		}
		break;
	}
	case EMessage::Control:
		if (Connection->bPaired)
		{
			HandleControl(Rosc::Cam::ToText(Body));
		}
		break;
	default:
		// Only the editor sends the other messages; a newer app's extras are let pass.
		break;
	}
}

void URoscCamServer::HandleHello(const FString& Json)
{
	const TSharedPtr<FJsonObject> Hello = ParseJson(Json);
	const TSharedRef<FJsonObject> Welcome = MakeShared<FJsonObject>();
	Welcome->SetNumberField(TEXT("protocol"), Rosc::Cam::ProtocolVersion);
	Welcome->SetStringField(TEXT("editor"), GetEditorName());
	Send(Rosc::Cam::EMessage::Welcome, WriteJson(Welcome));

	int32 Protocol = 0;
	if (!Hello.IsValid() || !Hello->TryGetNumberField(TEXT("protocol"), Protocol) || Protocol != Rosc::Cam::ProtocolVersion)
	{
		Flush();
		CloseConnection();
		return;
	}
	if (Connection->bSaidHello)
	{
		return;
	}
	Connection->bSaidHello = true;
	Connection->DeviceName = CleanDeviceName(GetString(Hello, TEXT("device")));
	Connection->PairingCode = Rosc::Cam::MakePairingCode(Random);
}

void URoscCamServer::HandlePair(const FString& Json)
{
	if (!Connection->bSaidHello || Connection->bPaired)
	{
		return;
	}
	const TSharedPtr<FJsonObject> Pair = ParseJson(Json);
	const TSharedRef<FJsonObject> Answer = MakeShared<FJsonObject>();
	if (GetString(Pair, TEXT("code")) == Connection->PairingCode)
	{
		Connection->bPaired = true;
		Connection->PairingCode.Reset();
		LiveLinkSource->AddSubject(FName(*FString::Printf(TEXT("RoscCam: %s"), *Connection->DeviceName)));
		LastStateJson.Reset();
		Answer->SetBoolField(TEXT("paired"), true);
		Send(Rosc::Cam::EMessage::Paired, WriteJson(Answer));
		return;
	}
	++Connection->WrongCodes;
	const bool bLastTry = Connection->WrongCodes >= PairingTries;
	Answer->SetBoolField(TEXT("paired"), false);
	Answer->SetStringField(TEXT("reason"), bLastTry
		? FText::Format(LOCTEXT("TooManyCodes", "{0} wrong codes. Connect again for a new code."), FText::AsNumber(PairingTries)).ToString()
		: LOCTEXT("WrongCode", "That is not the code the editor shows. Look in the Grip tab's RoscCam section.").ToString());
	Send(Rosc::Cam::EMessage::Paired, WriteJson(Answer));
	if (bLastTry)
	{
		Flush();
		CloseConnection();
	}
}

void URoscCamServer::HandleControl(const FString& Json)
{
	const TSharedPtr<FJsonObject> Control = ParseJson(Json);
	const FString Action = GetString(Control, TEXT("action"));
	URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	ACineCameraActor* const TheCamera = Camera.Get();
	const bool bOperatingThis = Operating->IsOperating() && Operating->GetDevice() == GetSubjectName();

	bool bOk = false;
	FText Message;
	if (Action == TEXT("operate"))
	{
		ARoscRig* Rig = Rosc::GripEditing::FindRig(TheCamera);
		if (Rig == nullptr)
		{
			Message = LOCTEXT("NoRig", "The camera is not on a support. Put it on one in the Grip tab.");
		}
		else
		{
			bOk = Operating->StartOperating(Rig, GetSubjectName(), Message);
		}
	}
	else if (Action == TEXT("release"))
	{
		if (bOperatingThis)
		{
			Operating->StopOperating();
		}
		bOk = true;
	}
	else if (Action == TEXT("zero"))
	{
		bOk = bOperatingThis;
		if (bOk)
		{
			Operating->Zero();
		}
		else
		{
			Message = LOCTEXT("NotOperating", "Turn Operate on first.");
		}
	}
	else if (Action == TEXT("record"))
	{
		if (!bOperatingThis)
		{
			Message = LOCTEXT("NotOperating", "Turn Operate on first.");
		}
		else
		{
			bOk = Operating->IsRecording() || Operating->StartRecording(Message);
		}
	}
	else if (Action == TEXT("stop"))
	{
		if (Operating->IsRecording())
		{
			Operating->StopRecording();
		}
		bOk = true;
	}
	else if (Action == TEXT("bake"))
	{
		if (Operating->IsRecording())
		{
			Message = LOCTEXT("StillRecording", "Stop recording first.");
		}
		else if (const ULevelSequence* Sequence = Operating->BakeTake(Message))
		{
			bOk = true;
			Message = FText::Format(LOCTEXT("TakeBaked", "Take baked into {0}."), FText::FromString(Sequence->GetName()));
		}
	}
	else if (Action == TEXT("lens"))
	{
		URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(TheCamera);
		const FRoscLensKit* Kit = Component != nullptr ? FRoscCameraDatabase::Get().FindLensKit(Component->Package.LensKitId) : nullptr;
		const int32 Step = GetInteger(Control, TEXT("step")) < 0 ? -1 : 1;
		if (Kit == nullptr)
		{
			Message = LOCTEXT("NoKit", "The camera has no lens kit. Choose one in the Camera tab.");
		}
		else if (!Kit->Lenses.IsValidIndex(Component->Package.LensIndex + Step))
		{
			Message = Step < 0 ? LOCTEXT("FirstLens", "This is the first lens of the kit.") : LOCTEXT("LastLens", "This is the last lens of the kit.");
		}
		else
		{
			const int32 LensIndex = Component->Package.LensIndex + Step;
			FText Notice;
			bOk = Rosc::CameraEditing::EditPackage(Component, LOCTEXT("ChangeLens", "Change Lens"),
				[Kit, LensIndex](FRoscCameraPackage& Package) { Package.SelectLens(*Kit, LensIndex); }, Message, &Notice);
			if (bOk)
			{
				Message = Notice;
			}
		}
	}
	else if (Action == TEXT("rack"))
	{
		const int32 Mark = GetInteger(Control, TEXT("mark"));
		if (const ULevelSequence* Sequence = Rosc::FocusEditing::RackToFocusMark(TheCamera, Mark, Message))
		{
			bOk = true;
			Message = FText::Format(LOCTEXT("RackBaked", "Rack to mark {0} baked into {1}."), FText::AsNumber(Mark + 1), FText::FromString(Sequence->GetName()));
		}
	}
	else
	{
		Message = FText::Format(LOCTEXT("UnknownControl", "This editor does not know the control \"{0}\". Update Rosc."), FText::FromString(Action));
	}

	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("action"), Action);
	Result->SetBoolField(TEXT("ok"), bOk);
	Result->SetStringField(TEXT("message"), Message.ToString());
	Send(Rosc::Cam::EMessage::Result, WriteJson(Result));
	// What the control changed reaches the device with its answer.
	LastStateJson.Reset();
}

FString URoscCamServer::MakeStateJson() const
{
	const ACineCameraActor* TheCamera = Camera.Get();
	const UCineCameraComponent* Lens = TheCamera->GetCineCameraComponent();
	const URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	const ERoscDistanceUnits Units = URoscEditorSettings::GetDistanceUnits();
	const ARoscRig* Rig = Rosc::GripEditing::FindRig(TheCamera);
	const bool bOperatingThis = Operating->IsOperating() && Operating->GetDevice() == GetSubjectName();

	const TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetStringField(TEXT("camera"), TheCamera->GetActorLabel());
	FText Support = LOCTEXT("NoSupport", "No support");
	if (Rig != nullptr)
	{
		const FRoscRigSpec Spec = Rig->GetSpec();
		const FRoscRigPreset* Preset = Rosc::Rig::FindPreset(Spec.PresetId);
		Support = Preset != nullptr ? Preset->Name : Rosc::Rig::GetTypeName(Spec.Type);
	}
	State->SetStringField(TEXT("support"), Support.ToString());
	State->SetBoolField(TEXT("operating"), bOperatingThis);
	State->SetBoolField(TEXT("live"), bOperatingThis && Operating->IsDeviceLive());
	State->SetBoolField(TEXT("recording"), bOperatingThis && Operating->IsRecording());
	// Whole tenths, so the state changes ten times a second while recording and no more.
	State->SetNumberField(TEXT("takeSeconds"), FMath::RoundToDouble(Operating->GetTakeSeconds() * 10.0) / 10.0);
	const bool bTakeOfThis = Operating->HasTake() && Operating->GetTakeRig() == Rig;
	State->SetBoolField(TEXT("hasTake"), bTakeOfThis);

	const URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(TheCamera);
	const FRoscLensKit* Kit = Component != nullptr ? FRoscCameraDatabase::Get().FindLensKit(Component->Package.LensKitId) : nullptr;
	const FString LensName = Kit != nullptr && Kit->Lenses.IsValidIndex(Component->Package.LensIndex)
		? Kit->Lenses[Component->Package.LensIndex].GetDisplayName()
		: FText::Format(LOCTEXT("FocalLength", "{0} mm"), Rosc::Units::FormatNumber(Lens->CurrentFocalLength, 0)).ToString();
	State->SetStringField(TEXT("lens"), LensName);
	State->SetStringField(TEXT("tStop"), FText::Format(LOCTEXT("TStop", "T{0}"), Rosc::Units::FormatNumber(Lens->CurrentAperture, 1)).ToString());
	State->SetStringField(TEXT("focus"), Rosc::Units::FormatDistance(Lens->CurrentFocusDistance * 10.0, Units).ToString());
	State->SetNumberField(TEXT("aspect"), Component != nullptr ? Component->Package.DeliveryAspect : 0.0);

	TArray<FString> Marks;
	if (Component != nullptr)
	{
		for (int32 Index = 0; Index < Component->FocusMarks.Num(); ++Index)
		{
			Marks.Add(FText::Format(LOCTEXT("Mark", "Mark {0}: {1}"), FText::AsNumber(Index + 1),
				Rosc::Units::FormatDistance(Rosc::FocusEditing::GetFocusMarkDistance(TheCamera, Index) * 10.0, Units)).ToString());
		}
	}
	State->SetArrayField(TEXT("marks"), ToJsonStrings(Marks));

	TArray<FString> Warnings;
	if (bTakeOfThis)
	{
		for (const FRoscMoveFinding& Finding : Operating->GetTakeFindings())
		{
			Warnings.Add(Rosc::Take::Describe(Finding, Rig->GetSpec().Type, Units).ToString());
		}
	}
	State->SetArrayField(TEXT("warnings"), ToJsonStrings(Warnings));
	return WriteJson(State);
}

void URoscCamServer::SendState()
{
	const double Now = FPlatformTime::Seconds();
	const FString Json = MakeStateJson();
	if (Json == LastStateJson && Now < NextStateSeconds)
	{
		return;
	}
	Send(Rosc::Cam::EMessage::State, Json);
	LastStateJson = Json;
	NextStateSeconds = Now + StateInterval;
}

void URoscCamServer::SendPicture()
{
	// A frame waits while the last is still going out: on a slow network the device gets fewer
	// frames, not older ones.
	TArray<uint8> Jpeg;
	if (Connection->Out.IsEmpty() && Picture->TakeFrame(Jpeg))
	{
		Rosc::Cam::AppendMessage(Connection->Out, Rosc::Cam::EMessage::Picture, Jpeg);
	}
}

void URoscCamServer::Announce()
{
	if (AnnounceSocket == nullptr)
	{
		return;
	}
	const TSharedRef<FJsonObject> Announcement = MakeShared<FJsonObject>();
	Announcement->SetNumberField(TEXT("rosccam"), Rosc::Cam::ProtocolVersion);
	Announcement->SetStringField(TEXT("name"), FPlatformProcess::ComputerName());
	Announcement->SetNumberField(TEXT("port"), Rosc::Cam::ServerPort);
	const FTCHARToUTF8 Utf8(*WriteJson(Announcement));
	const TSharedRef<FInternetAddr> Everyone = GetSockets()->CreateInternetAddr();
	Everyone->SetBroadcastAddress();
	Everyone->SetPort(Rosc::Cam::DiscoveryPort);
	int32 Sent = 0;
	AnnounceSocket->SendTo(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), Sent, *Everyone);
}

void URoscCamServer::Send(Rosc::Cam::EMessage Type, const FString& Json)
{
	if (Connection.IsValid())
	{
		Rosc::Cam::AppendMessage(Connection->Out, Type, Json);
	}
}

void URoscCamServer::Flush()
{
	while (Connection.IsValid() && !Connection->Out.IsEmpty())
	{
		int32 Sent = 0;
		if (!Connection->Socket->Send(Connection->Out.GetData(), Connection->Out.Num(), Sent))
		{
			if (GetSockets()->GetLastErrorCode() != SE_EWOULDBLOCK)
			{
				CloseConnection();
			}
			return;
		}
		Connection->Out.RemoveAt(0, Sent, EAllowShrinking::No);
		if (Sent == 0)
		{
			return;
		}
	}
}

void URoscCamServer::CloseConnection()
{
	if (!Connection.IsValid())
	{
		return;
	}
	if (Connection->bPaired && LiveLinkSource.IsValid())
	{
		// A device that pairs again under the same name comes back as the same subject, so Operate
		// picks it up where it was.
		LiveLinkSource->RemoveSubject();
	}
	DestroySocket(Connection->Socket);
	Connection.Reset();
	Picture.Reset();
	LastStateJson.Reset();
}

#undef LOCTEXT_NAMESPACE
