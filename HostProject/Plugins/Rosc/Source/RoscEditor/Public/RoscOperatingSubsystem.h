// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Misc/FrameNumber.h"
#include "RoscMove.h"
#include "RoscTake.h"
#include "RoscOperatingSubsystem.generated.h"

class ACineCameraActor;
class ARoscRig;
class ULevelSequence;

/**
 * The camera operator's hand on a support (ADR-013). Turning the view while looking through a
 * camera on a support turns the support's head. A device such as a phone running the Unreal VCam
 * app, or any Live Link source that tracks a transform, can drive the head and, on a free-moving
 * support, its position. Its motion can be recorded as a take, processed, and baked into Sequencer.
 */
class ILiveLinkClient;

UCLASS()
class ROSCEDITOR_API URoscOperatingSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	static URoscOperatingSubsystem* Get();

	/** Live Link, where devices come in; null when its module is not loaded. */
	static ILiveLinkClient* GetLiveLinkClient();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The Live Link subjects that track a transform and can drive a support, by name. */
	static TArray<FName> GetDevices();

	/**
	 * Starts driving the rig from the device. The way the device is held when its first frame
	 * arrives is taken as the rig's present pose.
	 * @return false with the reason in OutError.
	 */
	bool StartOperating(ARoscRig* Rig, FName Device, FText& OutError);

	/** Stops driving the rig. Where the device left it is kept, as one undo step. */
	void StopOperating();

	/** Takes the way the device is held now as the rig's present pose. */
	void Zero();

	bool IsOperating() const { return OperatedRig.IsValid(); }
	ARoscRig* GetOperatedRig() const { return OperatedRig.Get(); }
	FName GetDevice() const { return Device; }

	/** True when the device's frames are arriving. */
	bool IsDeviceLive() const { return bDeviceLive; }

	/**
	 * Starts recording a take of the device's drive of the rig, once per frame of the sequence open
	 * in Sequencer from its playhead, and plays the sequence. With none open, one is made and opened.
	 * @return false with the reason in OutError.
	 */
	bool StartRecording(FText& OutError);

	/** Stops recording and Sequencer's playback. The take is kept until it is baked or replaced. */
	void StopRecording();

	bool IsRecording() const { return bRecording; }
	bool HasTake() const { return Take.Poses.Num() > 1 && TakeRig.IsValid(); }
	ARoscRig* GetTakeRig() const { return TakeRig.Get(); }
	ULevelSequence* GetTakeSequence() const { return TakeSequence.Get(); }
	FFrameNumber GetTakeStartFrame() const { return TakeStartFrame; }
	const FRoscTake& GetTake() const { return Take; }
	double GetTakeSeconds() const;

	const FRoscTakeProcessing& GetProcessing() const { return Processing; }
	void SetProcessing(const FRoscTakeProcessing& NewProcessing) { Processing = NewProcessing; }

	/** The take as it would be baked, with what the support could not do. */
	TArray<FRoscRigPose> GetProcessedTake() const;
	TArray<FRoscMoveFinding> GetTakeFindings() const;

	/**
	 * Bakes the processed take into the sequence it was recorded against, from the frame where
	 * recording began, as one undo step.
	 * @return the sequence baked into, or null with the reason in OutError.
	 */
	ULevelSequence* BakeTake(FText& OutError);

	void DiscardTake();

	/** True when the engine's Virtual Camera plugin is on, which shows the shot on a device. */
	static bool IsVirtualCameraOn();

	/**
	 * Turns the engine's Virtual Camera plugin on for the project. It loads when the editor restarts.
	 * @return false with the reason in OutError.
	 */
	static bool TurnOnVirtualCamera(FText& OutError);

	/** True when the camera's picture is streamed to the Unreal VCam app. */
	static bool IsShownOnDevice(const ACineCameraActor* Camera);

	/**
	 * Streams the camera's picture to the Unreal VCam app through the engine's Virtual Camera
	 * plugin, as one undo step. The camera is not moved by the plugin: Rosc drives its support.
	 * @return false with the reason in OutError.
	 */
	static bool ShowOnDevice(ACineCameraActor* Camera, FText& OutError);

	/** Stops streaming the camera's picture, as one undo step. */
	static void StopShowingOnDevice(ACineCameraActor* Camera);

	/** Reads the device, records the take and ends a look in the viewport that has stopped. Done every editor tick. */
	void Update();

private:
	bool Tick(float DeltaSeconds);
	void SampleDevice();
	void RecordFrames();

	void OnViewportCameraMoved(const FVector& ViewLocation, const FRotator& ViewRotation, ELevelViewportType ViewportType, int32 ViewIndex);
	void OnViewportCameraStopped(UObject& Object);
	void FinishLook();

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle CameraMovedHandle;
	FDelegateHandle CameraStoppedHandle;

	/** Driving a rig from a device. */
	TWeakObjectPtr<ARoscRig> OperatedRig;
	FName Device;
	FRoscRigPose PoseBeforeOperating;
	FRoscRigPose PoseAtZero;
	FTransform DeviceAtZero;
	bool bZeroPending = false;
	bool bDeviceLive = false;

	/** Recording a take. */
	bool bRecording = false;
	double RecordStartSeconds = 0.0;
	FRoscTake Take;
	TWeakObjectPtr<ARoscRig> TakeRig;
	TWeakObjectPtr<ULevelSequence> TakeSequence;
	FFrameNumber TakeStartFrame = 0;
	FRoscTakeProcessing Processing;

	/** Turning the head from the viewport: the rig and its pose when the look began. */
	TWeakObjectPtr<ARoscRig> LookRig;
	FRoscRigPose PoseBeforeLook;
	double LastLookSeconds = 0.0;
};
