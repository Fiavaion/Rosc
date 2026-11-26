// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "RoscCamServer.generated.h"

class ACineCameraActor;
class FRoscCamLiveLinkSource;
class FRoscCamPicture;
class FSocket;
namespace Rosc::Cam { enum class EMessage : uint8; class FMessageReader; }

/**
 * The editor's side of RoscCam (ADR-014): a server the RoscCam app on a phone or tablet connects
 * to. It is off until started, for one camera. A device that connects is paired with a code the
 * Grip tab shows; only then does it get the camera's picture and state, and do its motion and
 * controls count. Its motion comes in as a Live Link subject, so a support is operated from it as
 * from any other device.
 */
UCLASS()
class ROSCEDITOR_API URoscCamServer : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	static URoscCamServer* Get();

	virtual void Deinitialize() override;

	/**
	 * Starts listening for the app, for this camera, and announces the editor on the network.
	 * @return false with the reason in OutError.
	 */
	bool Start(ACineCameraActor* Camera, FText& OutError);

	/** Stops listening and lets go of any device, paired or not. */
	void Stop();

	bool IsRunning() const { return ListenSocket != nullptr; }
	ACineCameraActor* GetCamera() const { return Camera.Get(); }

	/** True while a device is connected, paired or not. */
	bool HasDevice() const { return Connection.IsValid(); }
	bool IsPaired() const;
	FString GetDeviceName() const;

	/** The code to type on the device that asks to be paired; empty when none is asking. */
	FString GetPairingCode() const;

	/** The Live Link subject the paired device's motion comes in as. */
	FName GetSubjectName() const;

	/** True when the paired device's motion has arrived in the last half second. */
	bool IsMotionArriving() const;

	/** Lets go of the device, paired or not. */
	void Unpair();

	/** The addresses of this computer that a device on the network can reach, as text. */
	static FString GetAddresses();

	/** Reads and answers the device, and sends it the picture and state. Done every editor tick. */
	void Update();

private:
	struct FConnection;

	bool Tick(float DeltaSeconds);
	void Accept();
	void Receive();
	void Handle(Rosc::Cam::EMessage Type, const TArray<uint8>& Body);
	void HandleHello(const FString& Json);
	void HandlePair(const FString& Json);
	void HandleControl(const FString& Json);
	void SendState();
	void SendPicture();
	void Announce();
	void Send(Rosc::Cam::EMessage Type, const FString& Json);
	void Flush();
	void CloseConnection();
	FString MakeStateJson() const;

	FTSTicker::FDelegateHandle TickerHandle;
	FSocket* ListenSocket = nullptr;
	FSocket* AnnounceSocket = nullptr;
	double NextAnnounceSeconds = 0.0;
	double NextStateSeconds = 0.0;
	FString LastStateJson;

	TSharedPtr<FConnection> Connection;

	TWeakObjectPtr<ACineCameraActor> Camera;
	TSharedPtr<FRoscCamPicture> Picture;
	TSharedPtr<FRoscCamLiveLinkSource> LiveLinkSource;
	FGuid LiveLinkSourceGuid;
	FRandomStream Random;
};
