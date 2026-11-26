// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Features/IModularFeatures.h"
#include "HAL/PlatformProcess.h"
#include "ILiveLinkClient.h"
#include "ILiveLinkSource.h"
#include "LevelEditorViewport.h"
#include "LiveLinkSourceSettings.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/AutomationTest.h"
#include "MovieScene.h"
#include "MovieSceneSequencePlayer.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "RoscCameraEditing.h"
#include "RoscGripEditing.h"
#include "RoscOperatingSubsystem.h"
#include "RoscRig.h"
#include "RoscSequenceUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const FName TestDevice(TEXT("RoscTestDevice"));

	/** A Live Link source of the test's own: a device whose every move the test makes. */
	class FTestDeviceSource : public ILiveLinkSource
	{
	public:
		virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override
		{
			Client = InClient;
			Guid = InSourceGuid;
		}
		virtual bool IsSourceStillValid() const override { return true; }
		virtual bool RequestSourceShutdown() override { return true; }
		virtual FText GetSourceType() const override { return FText::FromString(TEXT("Rosc test device")); }
		virtual FText GetSourceMachineName() const override { return FText::FromString(TEXT("Test")); }
		virtual FText GetSourceStatus() const override { return FText::FromString(TEXT("Moving")); }

		void Start()
		{
			FLiveLinkStaticDataStruct StaticData(FLiveLinkTransformStaticData::StaticStruct());
			Client->PushSubjectStaticData_AnyThread(FLiveLinkSubjectKey(Guid, TestDevice), ULiveLinkTransformRole::StaticClass(), MoveTemp(StaticData));
			Client->ForceTick();
		}

		/** Holds the device so, and has Live Link take it in at once. */
		void Hold(const FRotator& Rotation, const FVector& Location = FVector::ZeroVector)
		{
			FLiveLinkFrameDataStruct Frame(FLiveLinkTransformFrameData::StaticStruct());
			Frame.Cast<FLiveLinkTransformFrameData>()->Transform = FTransform(Rotation, Location);
			Frame.GetBaseData()->WorldTime = FLiveLinkWorldTime(FPlatformTime::Seconds());
			Client->PushSubjectFrameData_AnyThread(FLiveLinkSubjectKey(Guid, TestDevice), MoveTemp(Frame));
			Client->ForceTick();
		}

		ILiveLinkClient* Client = nullptr;
		FGuid Guid;
	};

	ILiveLinkClient* GetLiveLinkClient()
	{
		IModularFeatures& Features = IModularFeatures::Get();
		return Features.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)
			? &Features.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName)
			: nullptr;
	}

	/** A camera on a tripod, locked off, looking along +X. */
	ARoscRig* MakeTripod(UWorld* World, ACineCameraActor*& OutCamera)
	{
		OutCamera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 150.0)));
		ARoscRig* Rig = OutCamera != nullptr ? Rosc::GripEditing::PutCameraOnRig(OutCamera, Rosc::Rig::GetDefaultPreset(ERoscRigType::Tripod)) : nullptr;
		if (Rig != nullptr)
		{
			Rosc::GripEditing::EditRig(Rig, FText::FromString(TEXT("Lock Off")), [](ARoscRig& Edited) { Edited.Operator.Style = ERoscOperatorStyle::LockedOff; });
		}
		return Rig;
	}

	void RemoveTripod(UWorld* World, ACineCameraActor* Camera)
	{
		if (Camera != nullptr)
		{
			GEditor->SelectNone(false, true, false);
			Rosc::GripEditing::TakeCameraOffRig(Camera);
			World->EditorDestroyActor(Camera, false);
		}
	}

	const FMovieSceneFloatChannel* FindPanChannel(ULevelSequence* Sequence, ARoscRig* Rig)
	{
		const FMovieSceneBinding* Binding = Sequence->GetMovieScene()->FindBinding(Rosc::SequenceUtils::FindBinding(Sequence, Rig));
		for (const UMovieSceneTrack* Track : Binding != nullptr ? Binding->GetTracks() : TArray<UMovieSceneTrack*>())
		{
			const UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track);
			if (FloatTrack != nullptr && FloatTrack->GetPropertyName() == ARoscRig::GetAxisPropertyName(ERoscRigAxis::Pan) && !FloatTrack->GetAllSections().IsEmpty())
			{
				return &CastChecked<UMovieSceneFloatSection>(FloatTrack->GetAllSections()[0])->GetChannel();
			}
		}
		return nullptr;
	}

	/** How much a pan shakes: the sum of its squared changes of speed from frame to frame. */
	double Shake(TConstArrayView<FRoscRigPose> Poses)
	{
		double Sum = 0.0;
		for (int32 Frame = 1; Frame + 1 < Poses.Num(); ++Frame)
		{
			Sum += FMath::Square(Poses[Frame + 1].Pan - 2.0 * Poses[Frame].Pan + Poses[Frame - 1].Pan);
		}
		return Sum;
	}
}

/**
 * Operating a tripod from a device, through Live Link as the Unreal VCam app would: the head
 * follows the device, Zero takes a new hold, stopping keeps the result as one undo step, and a
 * shaky take is recorded, smoothed and baked, then scrubbed in the real Sequencer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscOperateDeviceTest, "Rosc.Editor.Operate.Device", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscOperateDeviceTest::RunTest(const FString& Parameters)
{
	URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!TestNotNull(TEXT("The operating subsystem exists"), Operating) || !TestNotNull(TEXT("Live Link is on"), Client))
	{
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = nullptr;
	ARoscRig* Rig = MakeTripod(World, Camera);
	TSharedPtr<FTestDeviceSource> Source = MakeShared<FTestDeviceSource>();
	const FGuid SourceGuid = Client->AddSource(Source);
	// The engine's clock does not move inside a test, so the test's device is read at its latest frame.
	if (ULiveLinkSourceSettings* SourceSettings = Client->GetSourceSettings(SourceGuid))
	{
		SourceSettings->Mode = ELiveLinkSourceMode::Latest;
	}
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional);
	Sequence->Initialize();
	Sequence->GetMovieScene()->SetDisplayRate(FFrameRate(24, 1));
	Sequence->GetMovieScene()->SetPlaybackRange(TRange<FFrameNumber>(0, Sequence->GetMovieScene()->GetTickResolution().AsFrameNumber(10.0).Value));
	ON_SCOPE_EXIT
	{
		Operating->DiscardTake();
		Operating->StopOperating();
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		Client->RemoveSource(SourceGuid);
		Client->ForceTick();
		RemoveTripod(World, Camera);
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc operating test")));
	};
	if (!TestNotNull(TEXT("A camera on a tripod"), Rig))
	{
		return false;
	}
	Source->Start();
	Source->Hold(FRotator(0.0, 30.0, 0.0));
	TestTrue(TEXT("The device is listed"), URoscOperatingSubsystem::GetDevices().Contains(TestDevice));

	FText Error;
	TestFalse(TEXT("Operating needs a device chosen"), Operating->StartOperating(Rig, NAME_None, Error));
	TestFalse(TEXT("and says so"), Error.IsEmpty());
	TestFalse(TEXT("A take needs the support operated first"), Operating->StartRecording(Error));

	// The head follows the device from the way it was held when operating began.
	const FRoscRigPose Before = Rig->GetPose();
	TestTrue(TEXT("The tripod is operated from the device"), Operating->StartOperating(Rig, TestDevice, Error));
	Operating->Update();
	TestTrue(TEXT("The device's motion is arriving"), Operating->IsDeviceLive());
	TestEqual(TEXT("Taking up the device does not move the head"), Rig->GetPose().Pan, Before.Pan, 1.e-3);
	Source->Hold(FRotator(10.0, 55.0, 0.0));
	Operating->Update();
	TestEqual(TEXT("Turning the device 25° pans the head 25°"), Rig->GetPose().Pan, Before.Pan + 25.0, 1.e-3);
	TestEqual(TEXT("Tilting it 10° tilts the head 10°"), Rig->GetPose().Tilt, Before.Tilt + 10.0, 1.e-3);
	TestEqual(TEXT("The camera looks where the head points"), FRotator::NormalizeAxis(Camera->GetActorRotation().Yaw - (Before.Pan + 25.0)), 0.0, 0.01);
	TestEqual(TEXT("The tripod's legs stay where they are"), Rig->GetPose().Lift, Before.Lift, 1.e-3);

	// Zero: the operator turns away to rest and takes up the shot from there.
	Operating->Zero();
	Source->Hold(FRotator(10.0, 150.0, 0.0));
	Operating->Update();
	TestEqual(TEXT("After Zero, a new hold does not move the head"), Rig->GetPose().Pan, Before.Pan + 25.0, 1.e-3);
	Source->Hold(FRotator(10.0, 160.0, 0.0));
	Operating->Update();
	TestEqual(TEXT("and the head turns on from it"), Rig->GetPose().Pan, Before.Pan + 35.0, 1.e-3);

	// Stopping keeps where the head is as one undo step.
	Operating->StopOperating();
	TestFalse(TEXT("The support is no longer operated"), Operating->IsOperating());
	TestTrue(TEXT("Operating can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("One undo puts the head back where it was before operating"), Rig->GetPose().Pan, Before.Pan, 1.e-3);
	TestTrue(TEXT("and redone"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo turns it again"), Rig->GetPose().Pan, Before.Pan + 35.0, 1.e-3);

	// A take: a slow pan with a shaky hand, recorded against the open sequence from frame 12.
	if (!TestTrue(TEXT("The sequence opens in Sequencer"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence)))
	{
		return false;
	}
	ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(12), EUpdatePositionMethod::Scrub));
	const FRoscRigPose TakeStart = Rig->GetPose();
	Source->Hold(FRotator::ZeroRotator);
	TestTrue(TEXT("Operating again"), Operating->StartOperating(Rig, TestDevice, Error));
	Operating->Update();
	TestTrue(TEXT("Recording starts"), Operating->StartRecording(Error));
	const double Start = FPlatformTime::Seconds();
	double Elapsed = 0.0;
	while ((Elapsed = FPlatformTime::Seconds() - Start) < 1.5)
	{
		const double Yaw = 30.0 * Elapsed + 1.5 * FMath::Sin(2.0 * PI * 9.0 * Elapsed);
		Source->Hold(FRotator(0.0, Yaw, 0.0));
		Operating->Update();
		FPlatformProcess::Sleep(1.0f / 96.0f);
	}
	Operating->StopRecording();
	TestFalse(TEXT("Recording stops"), Operating->IsRecording());
	Operating->StopOperating();
	TestTrue(TEXT("The take is kept when operating stops"), Operating->HasTake());
	if (!TestTrue(TEXT("A take is kept"), Operating->HasTake()))
	{
		return false;
	}
	const FRoscTake& Take = Operating->GetTake();
	AddInfo(FString::Printf(TEXT("Take: %d frames at %.0f fps"), Take.Poses.Num(), Take.FramesPerSecond));
	TestEqual(TEXT("The take is at the sequence's frame rate"), Take.FramesPerSecond, 24.0, 1.e-9);
	TestTrue(TEXT("About a frame for every 1/24 s recorded"), FMath::Abs(Take.Poses.Num() - 1 - 1.5 * 24.0) <= 3.0);
	TestEqual(TEXT("The take starts where the head was"), Take.Poses[0].Pan, TakeStart.Pan, 0.5);

	const TArray<FRoscRigPose> Processed = Operating->GetProcessedTake();
	AddInfo(FString::Printf(TEXT("Shake before %.3f, after %.3f"), Shake(Take.Poses), Shake(Processed)));
	TestTrue(TEXT("The processed take shakes less than the hand did"), Shake(Processed) < 0.25 * Shake(Take.Poses));
	for (const FRoscMoveFinding& Finding : Operating->GetTakeFindings())
	{
		AddError(FString::Printf(TEXT("A slow pan on a tripod is within its limits, but axis %d has problem %d: %.2f against %.2f"),
			static_cast<int32>(Finding.Axis), static_cast<int32>(Finding.Problem), Finding.Value, Finding.Limit));
	}

	// Bake, then scrub the real Sequencer and see where the head is.
	TestTrue(TEXT("The take bakes into the sequence it was recorded against"), Operating->BakeTake(Error) == Sequence);
	TestFalse(TEXT("The bake leaves no undo step open"), GEditor->IsTransactionActive());
	const FMovieSceneFloatChannel* Pan = FindPanChannel(Sequence, Rig);
	if (!TestNotNull(TEXT("The pan has a track"), Pan))
	{
		return false;
	}
	TestEqual(TEXT("A key on every frame of the take"), Pan->GetNumKeys(), Processed.Num());
	for (int32 Frame = 0; Frame < Processed.Num(); Frame += 4)
	{
		ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(12 + Frame), EUpdatePositionMethod::Scrub));
		TestEqual(*FString::Printf(TEXT("Scrubbed to frame %d: the head pans as the processed take"), 12 + Frame), Rig->GetPose().Pan, Processed[Frame].Pan, 0.01);
	}

	TestTrue(TEXT("The bake can be undone"), GEditor->UndoTransaction());
	const FMovieSceneFloatChannel* PanAfterUndo = FindPanChannel(Sequence, Rig);
	TestTrue(TEXT("After undo the pan has no keys"), PanAfterUndo == nullptr || PanAfterUndo->GetNumKeys() == 0);
	return true;
}

/**
 * Turning the view while looking through a camera on a support turns the support's head; the
 * camera stays on its mount, the head stops at its limits, and the look is one undo step.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscOperateViewportTest, "Rosc.Editor.Operate.Viewport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscOperateViewportTest::RunTest(const FString& Parameters)
{
	FLevelEditorViewportClient* View = nullptr;
	for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
	{
		if (Candidate != nullptr && Candidate->IsPerspective())
		{
			View = Candidate;
			break;
		}
	}
	if (!TestNotNull(TEXT("There is a perspective level viewport"), View))
	{
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = nullptr;
	ARoscRig* Rig = MakeTripod(World, Camera);
	const FVector ViewLocation = View->GetViewLocation();
	const FRotator ViewRotation = View->GetViewRotation();
	ON_SCOPE_EXIT
	{
		View->SetActorLock(nullptr);
		View->UpdateViewForLockedActor();
		View->SetViewLocation(ViewLocation);
		View->SetViewRotation(ViewRotation);
		RemoveTripod(World, Camera);
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc operating viewport test")));
	};
	if (!TestNotNull(TEXT("A camera on a tripod"), Rig))
	{
		return false;
	}

	// Look through the camera, as Pilot does.
	View->SetActorLock(Camera);
	View->MoveCameraToLockedActor();
	View->UpdateViewForLockedActor();
	const FRoscRigPose Before = Rig->GetPose();
	const FVector CameraAt = Camera->GetActorLocation();

	// The operator turns the view: 20° to the left, 8° up, in two drags of the mouse.
	View->MoveViewportCamera(FVector::ZeroVector, FRotator(5.0, 12.0, 0.0));
	View->MoveViewportCamera(FVector::ZeroVector, FRotator(3.0, 8.0, 0.0));
	TestEqual(TEXT("The head pans with the view"), Rig->GetPose().Pan, Before.Pan + 20.0, 0.01);
	TestEqual(TEXT("and tilts with it"), Rig->GetPose().Tilt, Before.Tilt + 8.0, 0.01);
	TestTrue(TEXT("The camera stays on its mount"), Camera->GetActorLocation().Equals(CameraAt, 0.01));
	TestEqual(TEXT("and looks where the view does"), FRotator::NormalizeAxis(Camera->GetActorRotation().Yaw - View->GetViewRotation().Yaw), 0.0, 0.01);

	// The look ends when the mouse lets go: one undo step for the whole look.
	GEditor->BroadcastEndCameraMovement(*Camera);
	TestTrue(TEXT("The look can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("One undo puts the head back"), Rig->GetPose().Pan, Before.Pan, 0.01);
	TestTrue(TEXT("and redone"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo turns it again"), Rig->GetPose().Pan, Before.Pan + 20.0, 0.01);

	// Tilting past the head's stop leaves the head on its stop.
	View->MoveCameraToLockedActor();
	const double TiltMax = Rig->GetSpec().GetAxis(ERoscRigAxis::Tilt).Max;
	View->MoveViewportCamera(FVector::ZeroVector, FRotator(TiltMax + 10.0, 0.0, 0.0));
	GEditor->BroadcastEndCameraMovement(*Camera);
	TestTrue(TEXT("The head stops at its tilt limit or short of it"), Rig->GetPose().Tilt <= TiltMax + 1.e-3);
	return true;
}

/**
 * Show on Device (ADR-013, Spike #4): the camera's picture goes to the Unreal VCam app through the
 * engine's Virtual Camera plugin, which does not move the camera. With the plugin off, Rosc says
 * what to turn on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscOperateShowOnDeviceTest, "Rosc.Editor.Operate.ShowOnDevice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoscOperateShowOnDeviceTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* Camera = nullptr;
	ARoscRig* Rig = MakeTripod(World, Camera);
	ON_SCOPE_EXIT
	{
		URoscOperatingSubsystem::StopShowingOnDevice(Camera);
		RemoveTripod(World, Camera);
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc show on device test")));
	};
	if (!TestNotNull(TEXT("A camera on a tripod"), Rig))
	{
		return false;
	}

	FText Error;
	if (!URoscOperatingSubsystem::IsVirtualCameraOn())
	{
		TestFalse(TEXT("With the Virtual Camera plugin off, the shot is not shown on a device"), URoscOperatingSubsystem::ShowOnDevice(Camera, Error));
		TestTrue(TEXT("and Rosc says which plugin to turn on"), Error.ToString().Contains(TEXT("Virtual Camera")));
		TestFalse(TEXT("and nothing is added to the camera"), URoscOperatingSubsystem::IsShownOnDevice(Camera));
		AddInfo(TEXT("The Virtual Camera plugin is off in this project: the refusal was checked."));
		return true;
	}

	// The engine's VCam warns that its default input profile names keys for its own lens and
	// filmback controls, which a view with no modifiers does not have. The warnings are the
	// plugin's, for any such view, and change nothing.
	AddExpectedMessagePlain(TEXT("Failed to map"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	// Undoing the view, the plugin tells its output of the change before the view is set up again.
	AddExpectedMessagePlain(TEXT("Owning VCamComponent is not enabled or initialized"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);

	const FTransform Before = Camera->GetActorTransform();
	TestTrue(TEXT("The shot is shown on a device"), URoscOperatingSubsystem::ShowOnDevice(Camera, Error));
	TestTrue(TEXT("The camera has the device view"), URoscOperatingSubsystem::IsShownOnDevice(Camera));
	TestTrue(TEXT("Showing it twice adds nothing"), URoscOperatingSubsystem::ShowOnDevice(Camera, Error));

	TArray<UActorComponent*> Views;
	Camera->GetComponents(FindObject<UClass>(FTopLevelAssetPath(TEXT("/Script/VCamCore.VCamComponent"))), Views);
	if (!TestEqual(TEXT("One device view on the camera"), Views.Num(), 1))
	{
		return false;
	}
	USceneComponent* View = Cast<USceneComponent>(Views[0]);
	TestTrue(TEXT("It looks through the camera's lens"), View != nullptr && View->GetAttachParent() == Camera->GetCineCameraComponent());

	// What the plugin says of itself: its target camera, and a Pixel Streaming output that is on.
	UFunction* GetTarget = View->FindFunction(TEXT("GetTargetCamera"));
	UObject* Target = nullptr;
	if (TestNotNull(TEXT("The view can name its camera"), GetTarget))
	{
		uint8* Frame = static_cast<uint8*>(FMemory_Alloca_Aligned(GetTarget->ParmsSize, GetTarget->GetMinAlignment()));
		FMemory::Memzero(Frame, GetTarget->ParmsSize);
		View->ProcessEvent(GetTarget, Frame);
		Target = CastField<FObjectPropertyBase>(GetTarget->GetReturnProperty())->GetObjectPropertyValue_InContainer(Frame);
	}
	TestTrue(TEXT("The view's target is the Rosc camera"), Target == Camera->GetCineCameraComponent());
	TArray<UObject*> Outputs;
	GetObjectsWithOuter(View, Outputs, EGetObjectsFlags::None);
	const UObject* const* Output = Outputs.FindByPredicate([](const UObject* Object) { return Object->GetClass()->GetPathName() == TEXT("/Script/DecoupledOutputProvider.VCamPixelStreamingSession"); });
	TestNotNull(TEXT("The view streams with Pixel Streaming"), Output);

	// The view does not move the camera: the support does.
	URoscOperatingSubsystem::Get()->Update();
	TestTrue(TEXT("The camera stays where its support holds it"), Camera->GetActorTransform().Equals(Before, 0.01));

	TestTrue(TEXT("Showing on a device can be undone"), GEditor->UndoTransaction());
	TestFalse(TEXT("After undo the camera has no device view"), URoscOperatingSubsystem::IsShownOnDevice(Camera));
	TestTrue(TEXT("and redone"), GEditor->RedoTransaction());
	TestTrue(TEXT("After redo it is shown again"), URoscOperatingSubsystem::IsShownOnDevice(Camera));
	URoscOperatingSubsystem::StopShowingOnDevice(Camera);
	TestFalse(TEXT("Stop Showing takes the view off"), URoscOperatingSubsystem::IsShownOnDevice(Camera));
	return true;
}

#endif
