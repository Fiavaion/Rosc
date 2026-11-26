// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscOperatingSubsystem.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Features/IModularFeatures.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IProjectManager.h"
#include "ILiveLinkClient.h"
#include "LevelEditorViewport.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieScene.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "RoscGripEditing.h"
#include "RoscRig.h"
#include "RoscSequenceUtils.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "RoscOperating"

namespace
{
	/**
	 * The engine's Virtual Camera plugin, used by class path so that Rosc does not load it for users
	 * who never show the shot on a device (ADR-013). A VCam component with no modifiers does not move
	 * its camera; its Pixel Streaming output sends the picture to the Unreal VCam app and the app's
	 * motion back through Live Link.
	 */
	const TCHAR* const VirtualCameraPlugin = TEXT("VirtualCamera");
	const TCHAR* const VCamComponentClassPath = TEXT("/Script/VCamCore.VCamComponent");
	const TCHAR* const PixelStreamingOutputClassPath = TEXT("/Script/DecoupledOutputProvider.VCamPixelStreamingSession");

	UClass* FindClassByPath(const TCHAR* Path)
	{
		return FindObject<UClass>(FTopLevelAssetPath(Path));
	}

	/** Calls a function of the object found by name, with the arguments set and read by the caller; false when there is no such function. */
	bool CallByName(UObject* Object, FName FunctionName, TFunctionRef<void(const UFunction&, uint8*)> SetArguments, TFunctionRef<void(const UFunction&, const uint8*)> ReadResults)
	{
		UFunction* Function = Object != nullptr ? Object->FindFunction(FunctionName) : nullptr;
		if (Function == nullptr)
		{
			return false;
		}
		uint8* Frame = static_cast<uint8*>(FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment()));
		FMemory::Memzero(Frame, Function->ParmsSize);
		Function->InitializeStruct(Frame);
		SetArguments(*Function, Frame);
		Object->ProcessEvent(Function, Frame);
		ReadResults(*Function, Frame);
		Function->DestroyStruct(Frame);
		return true;
	}

	/** Calls a function that takes one true-or-false argument. */
	bool CallWithBool(UObject* Object, FName FunctionName, bool bValue)
	{
		return CallByName(Object, FunctionName,
			[bValue](const UFunction& Function, uint8* Frame)
			{
				if (FBoolProperty* Argument = CastField<FBoolProperty>(Function.ChildProperties))
				{
					Argument->SetPropertyValue_InContainer(Frame, bValue);
				}
			},
			[](const UFunction&, const uint8*) {});
	}

	UActorComponent* FindDeviceView(const ACineCameraActor* Camera)
	{
		UClass* VCamClass = FindClassByPath(VCamComponentClassPath);
		return Camera != nullptr && VCamClass != nullptr ? Camera->FindComponentByClass(VCamClass) : nullptr;
	}

	/** A look in the viewport that has not turned the head for this long is over, also when no end of it was heard. */
	constexpr double LookEndsAfterSeconds = 0.5;

	ILiveLinkClient* GetLiveLinkClient()
	{
		IModularFeatures& Features = IModularFeatures::Get();
		return Features.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)
			? &Features.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName)
			: nullptr;
	}

	bool ReadDevice(FName Device, FTransform& OutTransform)
	{
		ILiveLinkClient* Client = GetLiveLinkClient();
		FLiveLinkSubjectFrameData Frame;
		if (Client == nullptr || Device.IsNone() || !Client->EvaluateFrame_AnyThread(Device, ULiveLinkTransformRole::StaticClass(), Frame))
		{
			return false;
		}
		const FLiveLinkTransformFrameData* Data = Frame.FrameData.Cast<FLiveLinkTransformFrameData>();
		if (Data == nullptr)
		{
			return false;
		}
		OutTransform = Data->Transform;
		return true;
	}

	bool IsSamePose(const FRoscRigPose& A, const FRoscRigPose& B)
	{
		for (int32 AxisIndex = 0; AxisIndex < RoscRigAxisCount; ++AxisIndex)
		{
			const ERoscRigAxis Axis = static_cast<ERoscRigAxis>(AxisIndex);
			if (!FMath::IsNearlyEqual(A.Get(Axis), B.Get(Axis), 1.e-4))
			{
				return false;
			}
		}
		return true;
	}

	/** Shows a pose between the first and the last, which are kept as one undo step by KeepPose. */
	void ShowPose(ARoscRig& Rig, const FRoscRigPose& Pose)
	{
		Rig.SetPose(Pose);
		GEditor->RedrawLevelEditingViewports();
	}

	/** Keeps the rig's present pose as one undo step from the pose it had before. */
	void KeepPose(ARoscRig& Rig, const FRoscRigPose& Before, const FText& Description)
	{
		const FRoscRigPose After = Rig.GetPose();
		if (IsSamePose(Before, After))
		{
			return;
		}
		Rig.SetPose(Before);
		const FScopedTransaction Transaction(Description);
		Rig.Modify();
		Rig.SetPose(After);
		GEditor->RedrawLevelEditingViewports();
	}
}

URoscOperatingSubsystem* URoscOperatingSubsystem::Get()
{
	return GEditor != nullptr ? GEditor->GetEditorSubsystem<URoscOperatingSubsystem>() : nullptr;
}

void URoscOperatingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &URoscOperatingSubsystem::Tick));
	CameraMovedHandle = FEditorDelegates::OnEditorCameraMoved.AddUObject(this, &URoscOperatingSubsystem::OnViewportCameraMoved);
	CameraStoppedHandle = GEditor->OnEndCameraMovement().AddUObject(this, &URoscOperatingSubsystem::OnViewportCameraStopped);
}

void URoscOperatingSubsystem::Deinitialize()
{
	FinishLook();
	StopOperating();
	FTSTicker::RemoveTicker(TickerHandle);
	FEditorDelegates::OnEditorCameraMoved.Remove(CameraMovedHandle);
	if (GEditor != nullptr)
	{
		GEditor->OnEndCameraMovement().Remove(CameraStoppedHandle);
	}
	Super::Deinitialize();
}

TArray<FName> URoscOperatingSubsystem::GetDevices()
{
	TArray<FName> Devices;
	if (ILiveLinkClient* Client = GetLiveLinkClient())
	{
		const bool bIncludeDisabled = false;
		const bool bIncludeVirtual = true;
		for (const FLiveLinkSubjectKey& Key : Client->GetSubjectsSupportingRole(ULiveLinkTransformRole::StaticClass(), bIncludeDisabled, bIncludeVirtual))
		{
			Devices.AddUnique(Key.SubjectName.Name);
		}
	}
	Devices.Sort(FNameLexicalLess());
	return Devices;
}

bool URoscOperatingSubsystem::StartOperating(ARoscRig* Rig, FName NewDevice, FText& OutError)
{
	if (Rig == nullptr || Rig->GetMountedCamera() == nullptr)
	{
		OutError = LOCTEXT("OperateNoRig", "Put the camera on a support first: the device drives the support's head.");
		return false;
	}
	if (NewDevice.IsNone())
	{
		OutError = LOCTEXT("OperateNoDevice", "Choose a device. Start the Unreal VCam app, or another Live Link source, and it is listed here.");
		return false;
	}

	StopOperating();
	FinishLook();
	OperatedRig = Rig;
	Device = NewDevice;
	PoseBeforeOperating = Rig->GetPose();
	bZeroPending = true;
	bDeviceLive = false;
	return true;
}

void URoscOperatingSubsystem::StopOperating()
{
	if (bRecording)
	{
		StopRecording();
	}
	if (ARoscRig* Rig = OperatedRig.Get())
	{
		KeepPose(*Rig, PoseBeforeOperating, LOCTEXT("OperateFromDevice", "Operate Support from Device"));
	}
	OperatedRig.Reset();
	Device = NAME_None;
	bDeviceLive = false;
}

void URoscOperatingSubsystem::Zero()
{
	bZeroPending = true;
}

bool URoscOperatingSubsystem::StartRecording(FText& OutError)
{
	ARoscRig* Rig = OperatedRig.Get();
	if (Rig == nullptr)
	{
		OutError = LOCTEXT("RecordNotOperating", "Operate the support from a device first: a take records the device.");
		return false;
	}
	if (!bDeviceLive)
	{
		OutError = FText::Format(LOCTEXT("RecordNoMotion", "No motion is arriving from {0}. Check that the app is running and connected."), FText::FromName(Device));
		return false;
	}

	ACineCameraActor* Camera = Rig->GetMountedCamera();
	const Rosc::SequenceUtils::FBakeTarget Target = Rosc::SequenceUtils::FindOrCreateBakeTarget(Camera, TEXT("_Take"), Camera);
	if (Target.Sequence == nullptr || Target.Sequence->GetMovieScene() == nullptr)
	{
		OutError = LOCTEXT("RecordNoSequence", "A new sequence could not be made for the take.");
		return false;
	}

	Take = FRoscTake();
	Take.FramesPerSecond = Target.Sequence->GetMovieScene()->GetDisplayRate().AsDecimal();
	Take.Poses.Add(Rig->GetPose());
	TakeRig = Rig;
	TakeSequence = Target.Sequence;
	TakeStartFrame = Target.StartFrame;
	RecordStartSeconds = FPlatformTime::Seconds();
	bRecording = true;
	ULevelSequenceEditorBlueprintLibrary::Play();
	return true;
}

void URoscOperatingSubsystem::StopRecording()
{
	if (!bRecording)
	{
		return;
	}
	RecordFrames();
	bRecording = false;
	ULevelSequenceEditorBlueprintLibrary::Pause();
}

double URoscOperatingSubsystem::GetTakeSeconds() const
{
	return Take.Poses.Num() > 1 && Take.FramesPerSecond > 0.0 ? (Take.Poses.Num() - 1) / Take.FramesPerSecond : 0.0;
}

TArray<FRoscRigPose> URoscOperatingSubsystem::GetProcessedTake() const
{
	return Rosc::Take::Process(Take, Processing);
}

TArray<FRoscMoveFinding> URoscOperatingSubsystem::GetTakeFindings() const
{
	const ARoscRig* Rig = TakeRig.Get();
	return Rig != nullptr ? Rosc::Take::Check(Rig->GetSpec(), GetProcessedTake(), Take.FramesPerSecond) : TArray<FRoscMoveFinding>();
}

ULevelSequence* URoscOperatingSubsystem::BakeTake(FText& OutError)
{
	if (bRecording)
	{
		OutError = LOCTEXT("BakeWhileRecording", "Stop recording before baking the take.");
		return nullptr;
	}
	ULevelSequence* Sequence = TakeSequence.Get();
	return Rosc::GripEditing::BakeTake(TakeRig.Get(), Sequence, TakeStartFrame, GetProcessedTake(), OutError) ? Sequence : nullptr;
}

void URoscOperatingSubsystem::DiscardTake()
{
	if (bRecording)
	{
		StopRecording();
	}
	Take = FRoscTake();
	TakeRig.Reset();
	TakeSequence.Reset();
}

bool URoscOperatingSubsystem::IsVirtualCameraOn()
{
	return FindClassByPath(VCamComponentClassPath) != nullptr && FindClassByPath(PixelStreamingOutputClassPath) != nullptr;
}

bool URoscOperatingSubsystem::TurnOnVirtualCamera(FText& OutError)
{
	IProjectManager& Projects = IProjectManager::Get();
	return Projects.SetPluginEnabled(VirtualCameraPlugin, true, OutError) && Projects.SaveCurrentProjectToDisk(OutError);
}

bool URoscOperatingSubsystem::IsShownOnDevice(const ACineCameraActor* Camera)
{
	return FindDeviceView(Camera) != nullptr;
}

bool URoscOperatingSubsystem::ShowOnDevice(ACineCameraActor* Camera, FText& OutError)
{
	if (Camera == nullptr)
	{
		OutError = LOCTEXT("ShowNoCamera", "There is no camera to show on a device.");
		return false;
	}
	UClass* VCamClass = FindClassByPath(VCamComponentClassPath);
	UClass* OutputClass = FindClassByPath(PixelStreamingOutputClassPath);
	if (VCamClass == nullptr || OutputClass == nullptr)
	{
		OutError = LOCTEXT("ShowNeedsVirtualCamera", "Showing the shot on a device needs the engine's Virtual Camera plugin. Turn it on, and the editor restarts.");
		return false;
	}
	if (IsShownOnDevice(Camera))
	{
		return true;
	}

	FScopedTransaction Transaction(LOCTEXT("ShowOnDevice", "Show on Device"));
	Camera->Modify();
	USceneComponent* View = NewObject<USceneComponent>(Camera, VCamClass, MakeUniqueObjectName(Camera, VCamClass, TEXT("RoscDeviceView")), RF_Transactional);
	View->SetupAttachment(Camera->GetCineCameraComponent());
	Camera->AddInstanceComponent(View);
	View->RegisterComponent();

	UObject* Output = nullptr;
	CallByName(View, TEXT("AddOutputProvider"),
		[OutputClass](const UFunction& Function, uint8* Frame)
		{
			if (FObjectPropertyBase* ProviderClass = CastField<FObjectPropertyBase>(Function.FindPropertyByName(TEXT("ProviderClass"))))
			{
				ProviderClass->SetObjectPropertyValue_InContainer(Frame, OutputClass);
			}
		},
		[&Output](const UFunction& Function, const uint8* Frame)
		{
			if (const FObjectPropertyBase* Created = CastField<FObjectPropertyBase>(Function.FindPropertyByName(TEXT("CreatedProvider"))))
			{
				Output = Created->GetObjectPropertyValue_InContainer(Frame);
			}
		});
	if (Output == nullptr)
	{
		Transaction.Cancel();
		Camera->RemoveInstanceComponent(View);
		View->DestroyComponent();
		OutError = LOCTEXT("ShowNoOutput", "The Virtual Camera plugin did not make its Pixel Streaming output. Check that the plugin is on and up to date.");
		return false;
	}
	CallWithBool(View, TEXT("SetEnabled"), true);
	CallWithBool(Output, TEXT("SetActive"), true);
	return true;
}

void URoscOperatingSubsystem::StopShowingOnDevice(ACineCameraActor* Camera)
{
	UActorComponent* View = FindDeviceView(Camera);
	if (View == nullptr)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("StopShowingOnDevice", "Stop Showing on Device"));
	Camera->Modify();
	View->Modify();
	// Switched off first, so its outputs close in order before it goes.
	CallWithBool(View, TEXT("SetEnabled"), false);
	Camera->RemoveInstanceComponent(View);
	View->DestroyComponent();
}

bool URoscOperatingSubsystem::Tick(float DeltaSeconds)
{
	Update();
	return true;
}

void URoscOperatingSubsystem::Update()
{
	SampleDevice();
	if (bRecording)
	{
		RecordFrames();
	}
	if (LookRig.IsValid() && FPlatformTime::Seconds() - LastLookSeconds > LookEndsAfterSeconds)
	{
		FinishLook();
	}
}

void URoscOperatingSubsystem::SampleDevice()
{
	ARoscRig* Rig = OperatedRig.Get();
	if (Rig == nullptr)
	{
		if (!Device.IsNone())
		{
			// The rig was deleted while it was being operated.
			bRecording = false;
			Device = NAME_None;
			bDeviceLive = false;
		}
		return;
	}

	FTransform Now;
	bDeviceLive = ReadDevice(Device, Now);
	if (!bDeviceLive)
	{
		return;
	}
	if (bZeroPending)
	{
		DeviceAtZero = Now;
		PoseAtZero = Rig->GetPose();
		bZeroPending = false;
	}
	ShowPose(*Rig, Rosc::Take::DrivePose(Rig->GetSpec(), PoseAtZero, DeviceAtZero, Now, Rig->GetPose()));
}

void URoscOperatingSubsystem::RecordFrames()
{
	const ARoscRig* Rig = TakeRig.Get();
	if (Rig == nullptr || !TakeSequence.IsValid())
	{
		bRecording = false;
		return;
	}
	// One pose a frame of the sequence: frames that went by within one editor tick get the pose of that tick.
	const int32 FramesDue = FMath::FloorToInt32((FPlatformTime::Seconds() - RecordStartSeconds) * Take.FramesPerSecond) + 1;
	while (Take.Poses.Num() < FramesDue)
	{
		Take.Poses.Add(Rig->GetPose());
	}
}

void URoscOperatingSubsystem::OnViewportCameraMoved(const FVector& ViewLocation, const FRotator& ViewRotation, ELevelViewportType ViewportType, int32 ViewIndex)
{
	if (ViewportType != LVT_Perspective || GEditor == nullptr)
	{
		return;
	}

	ACineCameraActor* Camera = nullptr;
	for (const FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
	{
		// Only a camera the user pilots: a view Sequencer has cut to is the sequence's to move.
		if (Client != nullptr && Client->ViewIndex == ViewIndex && Client->GetActiveActorLock() == Client->GetActorLock().LockedActor)
		{
			Camera = Cast<ACineCameraActor>(Client->GetActorLock().GetLockedActor());
			break;
		}
	}
	ARoscRig* Rig = Rosc::GripEditing::FindRig(Camera);
	if (Rig == nullptr || Rig == OperatedRig.Get())
	{
		// A device has the head; the viewport only shows what it does.
		return;
	}

	const FRoscRigPose Pose = Rig->GetPose();
	const FRoscRigPose Turned = Rosc::Take::TurnHead(Rig->GetSpec(), Pose, Camera->GetActorRotation(), ViewRotation);
	if (IsSamePose(Pose, Turned))
	{
		return;
	}
	if (LookRig.Get() != Rig)
	{
		FinishLook();
		LookRig = Rig;
		PoseBeforeLook = Pose;
	}
	LastLookSeconds = FPlatformTime::Seconds();
	ShowPose(*Rig, Turned);
}

void URoscOperatingSubsystem::OnViewportCameraStopped(UObject& Object)
{
	FinishLook();
}

void URoscOperatingSubsystem::FinishLook()
{
	if (ARoscRig* Rig = LookRig.Get())
	{
		KeepPose(*Rig, PoseBeforeLook, LOCTEXT("TurnHead", "Turn Head"));
	}
	LookRig.Reset();
}

#undef LOCTEXT_NAMESPACE
