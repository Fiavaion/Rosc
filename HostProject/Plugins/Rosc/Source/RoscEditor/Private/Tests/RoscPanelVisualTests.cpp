// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "CineCameraActor.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "ImageUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneToolsProjectSettings.h"
#include "ObjectTools.h"
#include "RenderingThread.h"
#include "SRoscAITab.h"
#include "SRoscPanel.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "Tests/RoscDeliverChoicesBefore.h"
#include "Tests/RoscRenderedFiles.h"
#include "Tests/RoscTestDevice.h"
#include "Tests/RoscWidgetTestUtils.h"
#include "RoscOperatingSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "RoscAISettings.h"
#include "RoscAISubsystem.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscDeliverEditing.h"
#include "RoscEditorSettings.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscMonitorSubsystem.h"
#include "RoscRenderSubsystem.h"
#include "RoscRig.h"
#include "RoscShotEditing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const EAutomationTestFlags VisualTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** One picture of the panel: its width in Slate units and the application scale it is drawn at. */
	struct FPanelCapture
	{
		const TCHAR* Suffix;
		float Width;
		float ApplicationScale;
	};

	const FPanelCapture AllSizes[] =
	{
		{ TEXT("420"), 420.0f, 1.0f },
		{ TEXT("320"), 320.0f, 1.0f },
		{ TEXT("320-at-150pc"), 320.0f, 1.5f },
	};

	/** For a second state of a tab whose layout the full set already covers. */
	const FPanelCapture NarrowOnly[] =
	{
		{ TEXT("320"), 320.0f, 1.0f },
	};

	bool CanDrawPanel(FAutomationTestBase& Test)
	{
		if (!FApp::CanEverRender())
		{
			Test.AddInfo(TEXT("Skipped: drawing the panel needs a renderer (tools/test.ps1 -Rendering)."));
			return false;
		}
		return true;
	}

	/** What a run of panel pictures holds on to between frames. */
	struct FPanelCaptureState
	{
		~FPanelCaptureState()
		{
			BeginCleanup(Renderer);
		}

		/** Draws with the display's gamma into a plain target, so the pixels read back are the colours on screen. */
		FWidgetRenderer* Renderer = new FWidgetRenderer(true);
		TWeakObjectPtr<ACineCameraActor> Camera;
		TSharedPtr<SRoscPanel> Panel;
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
	};

	/**
	 * Queues drawing one department of the panel for a camera and saving pictures of it to
	 * Saved/Rosc/Screenshots: by default at a comfortable width, at the narrowest supported
	 * width, and at 150% application scale. The panel is drawn off screen, tall enough to show the whole tab:
	 * a window would be cut to the height of the display (LESSON-TEST-001). The camera, and any
	 * rig it is on, is removed afterwards.
	 * @param Prepare Called for each picture's panel when it has been made, to give it what a tab
	 *        would be handed while the user works: a plan from the assistant, a job's result.
	 */
	void QueuePanelCaptures(FAutomationTestBase& Test, ACineCameraActor* Camera, const TCHAR* NamePrefix, ERoscDepartment Department, float Height, TConstArrayView<FPanelCapture> Captures = AllSizes, TFunction<void(SRoscPanel&)> Prepare = nullptr)
	{
		const TSharedRef<FPanelCaptureState> State = MakeShared<FPanelCaptureState>();
		State->Camera = Camera;

		for (const FPanelCapture& Capture : Captures)
		{
			const FString Name = FString::Printf(TEXT("%s-%s"), NamePrefix, Capture.Suffix);
			const FVector2D DrawSize = FVector2D(Capture.Width, Height) * Capture.ApplicationScale;
			const auto Draw = [State, DrawSize, Scale = Capture.ApplicationScale]
			{
				State->Renderer->DrawWidget(State->Target.Get(), State->Panel.ToSharedRef(), Scale, DrawSize, 0.1f);
			};

			// A first pass makes the tab, which may change what it shows as soon as it is drawn
			// (preflight is run, a plan is drawn). A second lays that out, learns where its text
			// wraps and asks for its glyphs and icons at this scale. The editor makes those ready
			// between frames, so the picture is the third pass, a moment later.
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State, Department, DrawSize, Draw, Prepare]
			{
				State->Panel = SNew(SRoscPanel);
				State->Panel->ShowDepartment(Department);
				if (Prepare)
				{
					Prepare(*State->Panel);
				}
				State->Target.Reset(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, false));
				Draw();
				return true;
			}));
			ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand(Draw, 0.25f));

			ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([&Test, State, Name, DrawSize, Draw]
			{
				Draw();
				FlushRenderingCommands();

				const UTextureRenderTarget2D* Target = State->Target.Get();
				TArray<FColor> Pixels;
				const bool bRead = State->Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
				const bool bWholePicture = bRead && Target->SizeX == FMath::RoundToInt32(DrawSize.X) && Target->SizeY == FMath::RoundToInt32(DrawSize.Y) && Pixels.Num() == Target->SizeX * Target->SizeY;
				if (Test.TestTrue(FString::Printf(TEXT("%s: the panel was drawn at its full size"), *Name), bWholePicture))
				{
					TSet<uint32> Colours;
					for (FColor& Pixel : Pixels)
					{
						Pixel.A = 255;
						Colours.Add(Pixel.ToPackedARGB());
					}
					Test.TestTrue(FString::Printf(TEXT("%s: the picture is not blank"), *Name), Colours.Num() > 8);

					TArray64<uint8> Png;
					FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
					const FString FilePath = FPaths::ProjectSavedDir() / TEXT("Rosc/Screenshots") / Name + TEXT(".png");
					Test.TestTrue(FString::Printf(TEXT("%s: the picture was saved"), *Name), FFileHelper::SaveArrayToFile(Png, *FilePath));
				}

				State->Panel.Reset();
				State->Target.Reset();
			}, 0.25f));
		}

		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]
		{
			if (ACineCameraActor* CameraToRemove = State->Camera.Get())
			{
				GEditor->SelectNone(false, true, false);
				Rosc::GripEditing::TakeCameraOffRig(CameraToRemove);
				CameraToRemove->GetWorld()->EditorDestroyActor(CameraToRemove, false);
			}
			GEditor->ResetTransaction(FText::FromString(TEXT("Rosc panel visual test")));
			return true;
		}));
	}

	/** The AI settings as a test found them, to be put back, in their file too, when it has changed them. */
	struct FVisualAISettingsBefore
	{
		FVisualAISettingsBefore()
		{
			const URoscAISettings* Settings = GetDefault<URoscAISettings>();
			Backend = Settings->Backend;
			ServerUrl = Settings->ServerUrl;
			AssistantKey = Settings->AssistantKey;
			ModelsFolder = Settings->ModelsFolder.Path;
			ConsentedHosts = Settings->ConsentedHosts;
			AcceptedModelNotices = Settings->AcceptedModelNotices;
		}

		void PutBack() const
		{
			URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
			Settings->Backend = Backend;
			Settings->ServerUrl = ServerUrl;
			Settings->AssistantKey = AssistantKey;
			Settings->ModelsFolder.Path = ModelsFolder;
			Settings->ConsentedHosts = ConsentedHosts;
			Settings->AcceptedModelNotices = AcceptedModelNotices;
			// Should anything have saved the settings while the values of the test were in them.
			Settings->SaveConfig();
		}

		ERoscAIBackend Backend;
		FString ServerUrl;
		FString AssistantKey;
		FString ModelsFolder;
		TArray<FString> ConsentedHosts;
		TArray<FString> AcceptedModelNotices;
	};
}

/** Pictures of the Camera tab. Needs a renderer: run with tools/test.ps1 -Rendering; otherwise it reports itself skipped. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscCameraTabVisualTest, "Rosc.Editor.Visual.CameraTab", VisualTestFlags)

bool FRoscCameraTabVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(GEditor->GetEditorWorldContext().World(), FTransform::Identity);
	URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(CameraActor);
	if (!TestNotNull(TEXT("A camera with a package to show"), Component))
	{
		return false;
	}

	// Show the busiest state: a zoom (extra row), physical exposure on, and a lens kit that
	// does not cover the recording format (warning row).
	const FRoscLensKit* Zoom = FRoscCameraDatabase::Get().FindLensKit(TEXT("angenieux-optimo-24-290"));
	if (!TestNotNull(TEXT("The zoom kit used for the picture is in the database"), Zoom))
	{
		return false;
	}
	FText Error;
	Rosc::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Visual test setup")), [Zoom](FRoscCameraPackage& Package)
	{
		Package.SelectLensKit(*Zoom);
		Package.bPhysicalExposure = true;
		Package.NdDensity = 0.6f;
	}, Error);

	QueuePanelCaptures(*this, CameraActor, TEXT("camera-tab"), ERoscDepartment::Camera, 900.0f);
	return true;
}

/** Pictures of the Grip tab. Needs a renderer, like the Camera tab pictures. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscGripTabVisualTest, "Rosc.Editor.Visual.GripTab", VisualTestFlags)

bool FRoscGripTabVisualTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::GripEditing;

	if (!CanDrawPanel(*this))
	{
		return true;
	}

	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(GEditor->GetEditorWorldContext().World(), FTransform(FVector(0.0, 0.0, 300.0)));
	ARoscRig* Rig = PutCameraOnRig(CameraActor, *Rosc::Rig::FindPreset(TEXT("crane-30ft")));
	if (!TestNotNull(TEXT("A camera on a crane to show"), Rig))
	{
		return false;
	}

	// Show the busiest state: every crane axis, three marks, a second move too quick for the
	// crane (warning rows) and an operator with the intensity and take controls live.
	AddMark(Rig);
	EditRig(Rig, FText::FromString(TEXT("Visual test setup")), [](ARoscRig& EditedRig)
	{
		FRoscRigPose Pose = EditedRig.GetPose();
		Pose.Swing = 30.0;
		Pose.Boom = 20.0;
		Pose.Extension += 150.0;
		EditedRig.SetPose(Pose);
	});
	AddMark(Rig);
	EditRig(Rig, FText::FromString(TEXT("Visual test setup")), [](ARoscRig& EditedRig)
	{
		FRoscRigPose Pose = EditedRig.GetPose();
		Pose.Swing = -40.0;
		Pose.Extension += 200.0;
		Pose.Pan = 25.0;
		EditedRig.SetPose(Pose);
	});
	AddMark(Rig);
	EditRig(Rig, FText::FromString(TEXT("Visual test setup")), [](ARoscRig& EditedRig)
	{
		EditedRig.Marks[1].Arrival.DurationSeconds = 6.0;
		EditedRig.Marks[2].Arrival.DurationSeconds = 1.5;
		EditedRig.Operator.Style = ERoscOperatorStyle::Handheld;
	});

	QueuePanelCaptures(*this, CameraActor, TEXT("grip-tab"), ERoscDepartment::Grip, 1000.0f);
	return true;
}

/**
 * Pictures of the Grip tab's Device section with a take kept: a Steadicam operated from a
 * device that was carried forward with a quick pan at the end, so the take's settings, the
 * position lock and a warning are all shown.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscGripTabDeviceVisualTest, "Rosc.Editor.Visual.GripTabDevice", VisualTestFlags)

bool FRoscGripTabDeviceVisualTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::DeviceTests;

	ILiveLinkClient* LiveLink = GetLiveLinkClient();
	URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	if (!CanDrawPanel(*this) || !TestNotNull(TEXT("Live Link is on"), LiveLink) || !TestNotNull(TEXT("The operating subsystem exists"), Operating))
	{
		return true;
	}

	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(GEditor->GetEditorWorldContext().World(), FTransform(FVector(0.0, 0.0, 150.0)));
	ARoscRig* Rig = Rosc::GripEditing::PutCameraOnRig(CameraActor, Rosc::Rig::GetDefaultPreset(ERoscRigType::Steadicam));
	const TSharedRef<FTestDeviceSource> Source = FTestDeviceSource::Add(*LiveLink);
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transactional);
	Sequence->Initialize();
	Sequence->GetMovieScene()->SetDisplayRate(FFrameRate(24, 1));
	Sequence->GetMovieScene()->SetPlaybackRange(TRange<FFrameNumber>(0, Sequence->GetMovieScene()->GetTickResolution().AsFrameNumber(10.0).Value));
	const TStrongObjectPtr<ULevelSequence> KeepSequence(Sequence);

	FText Error;
	Source->Hold(FRotator::ZeroRotator);
	bool bRecording = TestNotNull(TEXT("A camera on a Steadicam to show"), Rig)
		&& TestTrue(TEXT("The sequence opens in Sequencer"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Sequence))
		&& TestTrue(TEXT("The Steadicam is operated from the device"), Operating->StartOperating(Rig, TestDevice, Error));
	if (bRecording)
	{
		Operating->Update();
		bRecording = TestTrue(TEXT("A take is recorded"), Operating->StartRecording(Error));
	}
	if (bRecording)
	{
		const double Start = FPlatformTime::Seconds();
		for (double Elapsed = 0.0; Elapsed < 1.0; Elapsed = FPlatformTime::Seconds() - Start)
		{
			const double Yaw = Elapsed < 0.7 ? 10.0 * Elapsed : 7.0 + 400.0 * (Elapsed - 0.7);
			Source->Hold(FRotator(0.0, Yaw, 0.0), FVector(120.0 * Elapsed, 0.0, 0.0));
			Operating->Update();
			FPlatformProcess::Sleep(1.0f / 96.0f);
		}
		Operating->StopRecording();
		Operating->StopOperating();
		AddInfo(FString::Printf(TEXT("The take has %d frames and %d findings"), Operating->GetTake().Poses.Num(), Operating->GetTakeFindings().Num()));
		TestTrue(TEXT("The take is kept to show"), Operating->HasTake());
	}

	QueuePanelCaptures(*this, CameraActor, TEXT("grip-tab-device"), ERoscDepartment::Grip, 1700.0f);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Operating, Source, KeepSequence]
	{
		Operating->DiscardTake();
		Source->Remove();
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		return true;
	}));
	return true;
}

/** Pictures of the Grip tab's quieter states: a camera on nothing, and a dolly with no marks yet. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscGripTabStatesVisualTest, "Rosc.Editor.Visual.GripTabStates", VisualTestFlags)

bool FRoscGripTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();

	ACineCameraActor* FreeCamera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 150.0)));
	QueuePanelCaptures(*this, FreeCamera, TEXT("grip-tab-no-support"), ERoscDepartment::Grip, 400.0f, NarrowOnly);

	// The panel follows the last camera made, so the dolly's camera is made once the first picture is taken.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World]
	{
		ACineCameraActor* DollyCamera = Rosc::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 150.0)));
		ARoscRig* DollyRig = Rosc::GripEditing::PutCameraOnRig(DollyCamera, Rosc::Rig::GetDefaultPreset(ERoscRigType::Dolly));
		if (TestNotNull(TEXT("A camera on a dolly to show"), DollyRig))
		{
			// A dolly zoom on a prime with the focus set by hand: the switch, what the lens would do, and both warnings.
			Rosc::GripEditing::AddMark(DollyRig);
			Rosc::GripEditing::EditRig(DollyRig, FText::FromString(TEXT("Move Rig")), [](ARoscRig& Rig)
			{
				FRoscRigPose Pose = Rig.GetPose();
				Pose.Travel += 150.0;
				Rig.SetPose(Pose);
			});
			Rosc::GripEditing::AddMark(DollyRig);
			Rosc::GripEditing::EditRig(DollyRig, FText::FromString(TEXT("Change Dolly Zoom")), [](ARoscRig& Rig) { Rig.Marks[1].bDollyZoom = true; });
			QueuePanelCaptures(*this, DollyCamera, TEXT("grip-tab-dolly"), ERoscDepartment::Grip, 1100.0f, NarrowOnly);
		}
		return true;
	}));
	return true;
}

/** Pictures of the Focus tab. Needs a renderer, like the Camera tab pictures. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscFocusTabVisualTest, "Rosc.Editor.Visual.FocusTab", VisualTestFlags)

bool FRoscFocusTabVisualTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::FocusEditing;

	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(World, FTransform::Identity);
	if (!TestNotNull(TEXT("A camera to show"), CameraActor))
	{
		return false;
	}

	// Show the busiest state: two focus marks, and the focus following a subject that has come
	// nearer than the lens can focus (warning row).
	SetFocusDistance(CameraActor, 150.0f);
	AddFocusMark(CameraActor);
	// The second mark is on a point picked on a subject: the longest label a mark has.
	AStaticMeshActor* Far = World->SpawnActor<AStaticMeshActor>(FVector(320.0, 0.0, 0.0), FRotator::ZeroRotator);
	Far->SetActorLabel(TEXT("Anna head"));
	FText HoldError;
	HoldPoint(CameraActor, Far, FVector(320.0, 0.0, 0.0), HoldError);
	AddFocusMark(CameraActor);
	AStaticMeshActor* Subject = World->SpawnActor<AStaticMeshActor>(FVector(20.0, 0.0, 0.0), FRotator::ZeroRotator);
	Subject->SetActorLabel(TEXT("Hero"));
	FollowActor(CameraActor, Subject);
	TestTrue(TEXT("The subject is nearer than the lens can focus"), GetFocusDistance(CameraActor) < GetCloseFocus(CameraActor));

	QueuePanelCaptures(*this, CameraActor, TEXT("focus-tab"), ERoscDepartment::Focus, 760.0f);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([WeakSubject = TWeakObjectPtr<AStaticMeshActor>(Subject), WeakFar = TWeakObjectPtr<AStaticMeshActor>(Far)]
	{
		for (const TWeakObjectPtr<AStaticMeshActor>& Weak : { WeakSubject, WeakFar })
		{
			if (AStaticMeshActor* ToRemove = Weak.Get())
			{
				ToRemove->GetWorld()->EditorDestroyActor(ToRemove, false);
			}
		}
		return true;
	}));
	return true;
}

/** A picture of the Focus tab for a camera with no camera package: focus works, marks ask for a package. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscFocusTabStatesVisualTest, "Rosc.Editor.Visual.FocusTabStates", VisualTestFlags)

bool FRoscFocusTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* BareCamera = Cast<ACineCameraActor>(GEditor->AddActor(World->GetCurrentLevel(), ACineCameraActor::StaticClass(), FTransform::Identity));
	if (!TestNotNull(TEXT("A camera with no package to show"), BareCamera))
	{
		return false;
	}
	QueuePanelCaptures(*this, BareCamera, TEXT("focus-tab-no-package"), ERoscDepartment::Focus, 620.0f, NarrowOnly);
	return true;
}

/** Pictures of the Monitor tab. Needs a renderer, like the Camera tab pictures. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscMonitorTabVisualTest, "Rosc.Editor.Visual.MonitorTab", VisualTestFlags)

bool FRoscMonitorTabVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	URoscMonitorSubsystem* Monitor = URoscMonitorSubsystem::Get();
	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(GEditor->GetEditorWorldContext().World(), FTransform::Identity);
	if (!TestNotNull(TEXT("The monitor exists in the editor"), Monitor) || !TestNotNull(TEXT("A camera to show"), CameraActor))
	{
		return false;
	}

	// Show the busiest state: every tool on, so the zebra and false colour legends are there, and
	// frame lines for a camera that delivers its whole recording format (warning row).
	const FRoscMonitorSettings SettingsBefore = Monitor->GetSettings();
	FRoscMonitorSettings Everything;
	Everything.bFalseColour = true;
	Everything.bZebras = true;
	Everything.bPeaking = true;
	Everything.bWaveform = true;
	Everything.bHistogram = true;
	Everything.bFrameLines = true;
	Everything.bSafeAreas = true;
	Everything.bCentreMark = true;
	Everything.bDataReadout = true;
	Monitor->SetSettings(Everything);

	QueuePanelCaptures(*this, CameraActor, TEXT("monitor-tab"), ERoscDepartment::Monitor, 1000.0f);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([SettingsBefore]
	{
		URoscMonitorSubsystem::Get()->SetSettings(SettingsBefore);
		return true;
	}));
	return true;
}

/** A picture of the Monitor tab with every tool off and no camera to work on: the tab is there all the same. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscMonitorTabStatesVisualTest, "Rosc.Editor.Visual.MonitorTabStates", VisualTestFlags)

bool FRoscMonitorTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	URoscMonitorSubsystem* Monitor = URoscMonitorSubsystem::Get();
	if (!TestNotNull(TEXT("The monitor exists in the editor"), Monitor))
	{
		return false;
	}

	const FRoscMonitorSettings SettingsBefore = Monitor->GetSettings();
	Monitor->SetSettings(FRoscMonitorSettings());
	GEditor->SelectNone(false, true, false);

	QueuePanelCaptures(*this, nullptr, TEXT("monitor-tab-no-camera"), ERoscDepartment::Monitor, 720.0f, NarrowOnly);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([SettingsBefore]
	{
		URoscMonitorSubsystem::Get()->SetSettings(SettingsBefore);
		return true;
	}));
	return true;
}

/**
 * Pictures of the Shots tab with a three-shot sequence open: the list, the first shot's fields,
 * two storyboard frames, the overhead plan of a dolly move, and the paperwork buttons.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscShotsTabVisualTest, "Rosc.Editor.Visual.ShotsTab", VisualTestFlags)

bool FRoscShotsTabVisualTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::ShotEditing;

	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(World, FTransform(FRotator::ZeroRotator, FVector(0.0, -200.0, 150.0)));
	AStaticMeshActor* Hero = World->SpawnActor<AStaticMeshActor>(FVector(700.0, -100.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("A camera to show"), CameraActor) || !TestNotNull(TEXT("A subject to show"), Hero))
	{
		return false;
	}
	Hero->SetActorLabel(TEXT("Hero"));
	Rosc::FocusEditing::FollowActor(CameraActor, Hero);

	ARoscRig* Dolly = Rosc::GripEditing::PutCameraOnRig(CameraActor, *Rosc::Rig::FindPreset(TEXT("dolly")));
	if (!TestNotNull(TEXT("The camera is put on a dolly"), Dolly))
	{
		return false;
	}
	Rosc::GripEditing::AddMark(Dolly);
	Rosc::GripEditing::EditRig(Dolly, FText::FromString(TEXT("Move Rig")), [](ARoscRig& Rig)
	{
		FRoscRigPose Pose = Rig.GetPose();
		Pose.Travel += 350.0;
		Pose.Pan = 20.0;
		Rig.SetPose(Pose);
	});
	Rosc::GripEditing::AddMark(Dolly);
	Rosc::GripEditing::EditRig(Dolly, FText::FromString(TEXT("Time Move")), [](ARoscRig& Rig) { Rig.Marks[1].Arrival.DurationSeconds = 4.0; });
	Rosc::GripEditing::GoToMark(Dolly, 0);

	FText Error;
	const bool bShotsAdded = AddShot(CameraActor, Error) && AddShot(CameraActor, Error) && AddShot(CameraActor, Error);
	TArray<ULevelSequence*> CreatedSequences;
	if (ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())
	{
		CreatedSequences.Add(OpenSequence);
	}
	const TArray<FShot> Shots = GetShots();
	for (const FShot& Shot : Shots)
	{
		CreatedSequences.AddUnique(Shot.Sequence);
	}

	if (TestTrue(TEXT("Three shots to show"), bShotsAdded && Shots.Num() == 3))
	{
		SetScene(Shots[0].Sequence, TEXT("12"));
		SetSize(Shots[0].Sequence, ERoscShotSize::Wide);
		SetMove(Shots[0].Sequence, TEXT("Track in on Hero, pan right"));
		SetNotes(Shots[0].Sequence, TEXT("Dawn light, haze. Hold two beats before the move."));
		SetScene(Shots[1].Sequence, TEXT("12"));
		SetSize(Shots[1].Sequence, ERoscShotSize::CloseUp);
		SetScene(Shots[2].Sequence, TEXT("12A"));
		SetSize(Shots[2].Sequence, ERoscShotSize::OverTheShoulder);

		GoToShot(Shots[0]);
		TestTrue(TEXT("The dolly move is baked into the first shot"), Rosc::GripEditing::BakeMoveToSequencer(Dolly, Error) == Shots[0].Sequence);
		for (const int32 Frame : { 80, 0 })
		{
			ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
			TestTrue(TEXT("A storyboard frame is grabbed"), GrabStoryboardFrame(Shots[0], Error));
		}
		QueuePanelCaptures(*this, CameraActor, TEXT("shots-tab"), ERoscDepartment::Shots, 1500.0f);
	}

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, CreatedSequences, WeakHero = TWeakObjectPtr<AStaticMeshActor>(Hero)]
	{
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc shots tab visual test")));
		for (ULevelSequence* Sequence : CreatedSequences)
		{
			TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
		}
		if (AStaticMeshActor* HeroToRemove = WeakHero.Get())
		{
			HeroToRemove->GetWorld()->EditorDestroyActor(HeroToRemove, false);
		}
		return true;
	}));
	return true;
}

/** A picture of the Shots tab with no sequence open and no camera: it invites the first shot. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscShotsTabStatesVisualTest, "Rosc.Editor.Visual.ShotsTabStates", VisualTestFlags)

bool FRoscShotsTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	GEditor->SelectNone(false, true, false);
	QueuePanelCaptures(*this, nullptr, TEXT("shots-tab-no-sequence"), ERoscDepartment::Shots, 420.0f, NarrowOnly);
	return true;
}

/**
 * Pictures of the Deliver tab with a four-shot sequence open. Set to deliver for review, with a
 * shot that has lost its camera cut (a finding that stops the render, with its fix) and a slate
 * that falls before the sequence (a finding worth knowing). Then with the camera cut put back by
 * the fix and set to deliver a master, ready to render. Then while that master renders.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscDeliverTabVisualTest, "Rosc.Editor.Visual.DeliverTab", VisualTestFlags)

bool FRoscDeliverTabVisualTest::RunTest(const FString& Parameters)
{
	using namespace Rosc::DeliverEditing;
	using namespace Rosc::ShotEditing;

	if (!CanDrawPanel(*this))
	{
		return true;
	}
	Rosc::RenderedFiles::ExpectRenderLogNoise(*this);

	const FRoscDeliverChoicesBefore ChoicesBefore;
	const FString OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Rosc/Renders/DeliverTabPictures"));
	IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
	EditChoices([&OutputDirectory](URoscEditorSettings& Choices)
	{
		Choices.DeliverIntent = ERoscDeliverIntent::Review;
		Choices.DeliverFolder.Path = OutputDirectory;
		Choices.DeliverNamePattern = Rosc::Deliver::DefaultNamePattern;
		Choices.bDeliverSlate = true;
		Choices.bDeliverBurnIn = true;
	});

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(World, FTransform(FRotator::ZeroRotator, FVector(0.0, -200.0, 150.0)));
	if (!TestNotNull(TEXT("A camera to show"), CameraActor))
	{
		ChoicesBefore.PutBack();
		return false;
	}

	// Six frames a shot: long enough to take a picture of the render, and no longer.
	UMovieSceneToolsProjectSettings* SequencerSettings = GetMutableDefault<UMovieSceneToolsProjectSettings>();
	const float ShotSecondsBefore = SequencerSettings->DefaultDuration;
	SequencerSettings->DefaultDuration = 6.0f / 24.0f;
	FText Error;
	const bool bShotsAdded = AddShot(CameraActor, Error) && AddShot(CameraActor, Error) && AddShot(CameraActor, Error) && AddShot(CameraActor, Error);
	SequencerSettings->DefaultDuration = ShotSecondsBefore;
	TArray<ULevelSequence*> CreatedSequences;
	if (ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())
	{
		CreatedSequences.Add(OpenSequence);
	}
	const TArray<FShot> Shots = GetShots();
	for (const FShot& Shot : Shots)
	{
		CreatedSequences.AddUnique(Shot.Sequence);
	}

	if (TestTrue(TEXT("Four shots to show"), bShotsAdded && Shots.Num() == 4))
	{
		Shots[1].Sequence->GetMovieScene()->RemoveCameraCutTrack();
		QueuePanelCaptures(*this, nullptr, TEXT("deliver-tab"), ERoscDepartment::Deliver, 1400.0f);

		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]
		{
			const FCheck Check = CheckDelivery(ERoscDeliverIntent::Review);
			const FRoscPreflightFinding* NoCameraCut = Check.Findings.FindByPredicate([](const FRoscPreflightFinding& Finding) { return Finding.Check == ERoscPreflightCheck::ShotWithoutCameraCut; });
			FText FixError;
			TestTrue(TEXT("The shot without a camera cut is found and put right"), NoCameraCut != nullptr && ApplyFix(*NoCameraCut, FixError));
			EditChoices([](URoscEditorSettings& Choices) { Choices.DeliverIntent = ERoscDeliverIntent::Master; });
			return true;
		}));
		QueuePanelCaptures(*this, nullptr, TEXT("deliver-tab-ready"), ERoscDepartment::Deliver, 900.0f, NarrowOnly);

		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]
		{
			FDelivery Delivery;
			FText StartError;
			if (!StartDelivery(ERoscDeliverIntent::Master, Delivery, StartError))
			{
				AddError(FString::Printf(TEXT("The delivery to show rendering did not start: %s"), *StartError.ToString()));
			}
			return true;
		}));
		// The picture is of a render under way, so it waits for the first frame to be made.
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]
		{
			const URoscRenderSubsystem* Render = GEditor->GetEditorSubsystem<URoscRenderSubsystem>();
			return !Render->IsRendering() || Render->GetProgress() > 0.0f;
		}));
		QueuePanelCaptures(*this, nullptr, TEXT("deliver-tab-rendering"), ERoscDepartment::Deliver, 900.0f, NarrowOnly);
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]
		{
			// The picture only shows a render when the render outlasts the taking of it.
			AddInfo(GEditor->GetEditorSubsystem<URoscRenderSubsystem>()->IsRendering()
				? TEXT("deliver-tab-rendering was taken while the render ran")
				: TEXT("deliver-tab-rendering was taken after the render had ended: it shows the tab at rest"));
			return true;
		}));
	}

	const double CleanUpStartSeconds = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, ChoicesBefore, CreatedSequences, OutputDirectory, CleanUpStartSeconds, WeakCamera = TWeakObjectPtr<ACineCameraActor>(CameraActor)]
	{
		URoscRenderSubsystem* Render = GEditor->GetEditorSubsystem<URoscRenderSubsystem>();
		if (Render->IsRendering())
		{
			if (FPlatformTime::Seconds() - CleanUpStartSeconds < 300.0)
			{
				return false;
			}
			AddError(TEXT("The render did not finish in 300 seconds"));
			Render->CancelRender();
		}

		ChoicesBefore.PutBack();
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		GEditor->SelectNone(false, true, false);
		if (ACineCameraActor* CameraToRemove = WeakCamera.Get())
		{
			CameraToRemove->GetWorld()->EditorDestroyActor(CameraToRemove, false);
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("Rosc deliver tab visual test")));
		for (ULevelSequence* Sequence : CreatedSequences)
		{
			TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
		}
		IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
		return true;
	}));
	return true;
}

/** A picture of the Deliver tab with no sequence open, set to deliver a DCP source at its choice of size: preflight says what is missing. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscDeliverTabStatesVisualTest, "Rosc.Editor.Visual.DeliverTabStates", VisualTestFlags)

bool FRoscDeliverTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	const FRoscDeliverChoicesBefore ChoicesBefore;
	Rosc::DeliverEditing::EditChoices([](URoscEditorSettings& Choices) { Choices.DeliverIntent = ERoscDeliverIntent::DcpSource; });
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	GEditor->SelectNone(false, true, false);
	QueuePanelCaptures(*this, nullptr, TEXT("deliver-tab-no-sequence"), ERoscDepartment::Deliver, 800.0f, NarrowOnly);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([ChoicesBefore]
	{
		ChoicesBefore.PutBack();
		return true;
	}));
	return true;
}

/**
 * Pictures of the AI tab at its busiest: a plan from the assistant being read, with what the
 * gear makes of it; a lens estimate with the lens to put on; and the models.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAITabVisualTest, "Rosc.Editor.Visual.AITab", VisualTestFlags)

bool FRoscAITabVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	AStaticMeshActor* Anna = World->SpawnActor<AStaticMeshActor>(FVector(600.0, 80.0, 90.0), FRotator::ZeroRotator);
	Anna->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Anna->SetActorLabel(TEXT("Anna"));
	ACineCameraActor* CameraActor = Rosc::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 160.0)));

	// The assistant can be asked, as far as the tab can tell: a key that is no key, and an agreement to its host.
	const FVisualAISettingsBefore SettingsBefore;
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	Settings->Backend = ERoscAIBackend::Editor;
	Settings->AssistantKey = TEXT("not-a-key");
	Settings->ConsentedHosts = { TEXT("api.anthropic.com") };

	FRoscShotPlanResult Plan;
	Plan.InputTokens = 6120;
	Plan.OutputTokens = 412;
	Plan.PlanJson = TEXT(R"({ "summary": "On the 30 ft crane with the Venice and the Cooke 32, the camera rises over the table to find Anna and then swings round her, with the focus following her throughout. The crane has no track, so the push forward is left out.", "steps": [
		{ "step": "choose_camera", "bodyId": "sony-venice-2-8k", "formatId": "8.6k-17x9" },
		{ "step": "choose_lens", "lensKitId": "cooke-s4i", "focalLengthMm": 30 },
		{ "step": "set_exposure", "tStop": 1.4, "iso": 1600, "shutterAngleDeg": 172.8, "ndStops": 2 },
		{ "step": "put_on_support", "supportId": "crane-30ft" },
		{ "step": "move_to_mark", "travelM": 2, "sidewaysM": 0, "liftM": 0, "swingDeg": 0, "boomDeg": 20, "extensionM": 1.5,
		  "panDeg": 0, "tiltDeg": -10, "rollDeg": 0, "durationSeconds": 8, "easeIn": 0.25, "easeOut": 0.25 },
		{ "step": "move_to_mark", "travelM": 0, "sidewaysM": 0, "liftM": 0, "swingDeg": 90, "boomDeg": 0, "extensionM": 0,
		  "panDeg": 0, "tiltDeg": 0, "rollDeg": 0, "durationSeconds": 1, "easeIn": 0.25, "easeOut": 0.25 },
		{ "step": "focus_on", "subject": "Anna", "follow": true },
		{ "step": "focus_on", "subject": "Ben", "follow": false } ] })");
	FString Fault;
	Plan.bSuccess = Rosc::ShotPlan::Parse(Plan.PlanJson, Plan.Plan, Fault);
	TestTrue(*FString::Printf(TEXT("The plan to show is a plan (%s)"), *Fault), Plan.bSuccess);

	FRoscAIResult Lens;
	Lens.Request.Task = ERoscAITask::FocalLength;
	Lens.bSuccess = true;
	Lens.FieldOfViewDeg = 26.0;

	QueuePanelCaptures(*this, CameraActor, TEXT("ai-tab"), ERoscDepartment::AI, 1750.0f, AllSizes, [Plan, Lens](SRoscPanel& Panel)
	{
		Panel.GetAITab()->ShowPlan(Plan);
		Panel.GetAITab()->ShowJobResult(Lens);
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([SettingsBefore, WeakAnna = TWeakObjectPtr<AStaticMeshActor>(Anna)]
	{
		SettingsBefore.PutBack();
		if (AStaticMeshActor* AnnaToRemove = WeakAnna.Get())
		{
			AnnaToRemove->GetWorld()->EditorDestroyActor(AnnaToRemove, false);
		}
		return true;
	}));
	return true;
}

/**
 * Pictures of the AI tab before it can be used: asking for agreement before anything is sent to
 * the assistant or to a server, a job that was not taken, and then with no key at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscAITabStatesVisualTest, "Rosc.Editor.Visual.AITabStates", VisualTestFlags)

bool FRoscAITabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	const FVisualAISettingsBefore SettingsBefore;
	URoscAISettings* Settings = GetMutableDefault<URoscAISettings>();
	Settings->Backend = ERoscAIBackend::Server;
	Settings->ServerUrl = TEXT("https://render01.studio.example:8750");
	Settings->AssistantKey = TEXT("not-a-key");
	Settings->ConsentedHosts.Reset();

	FRoscAIResult NotTaken;
	NotTaken.Request.Task = ERoscAITask::Depth;
	NotTaken.Error = FText::FromString(TEXT("Choose the picture to work on first."));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	QueuePanelCaptures(*this, Rosc::CameraEditing::SpawnCamera(World, FTransform::Identity), TEXT("ai-tab-agreement"), ERoscDepartment::AI, 1300.0f, NarrowOnly, [NotTaken](SRoscPanel& Panel)
	{
		Panel.GetAITab()->ShowJobResult(NotTaken);
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, SettingsBefore]
	{
		URoscAISettings* NoKeySettings = GetMutableDefault<URoscAISettings>();
		NoKeySettings->Backend = ERoscAIBackend::Helper;
		NoKeySettings->AssistantKey.Reset();
		QueuePanelCaptures(*this, Rosc::CameraEditing::SpawnCamera(World, FTransform::Identity), TEXT("ai-tab-no-key"), ERoscDepartment::AI, 1100.0f, NarrowOnly);

		// The notice before a model's first download, for the model with the longest one: no model
		// downloaded yet, in a models folder of the test's own, and no notice agreed to.
		URoscAISettings* NoticeSettings = GetMutableDefault<URoscAISettings>();
		NoticeSettings->ModelsFolder.Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Rosc") / TEXT("VisualTests") / TEXT("NoModels"));
		NoticeSettings->AcceptedModelNotices.Reset();
		QueuePanelCaptures(*this, Rosc::CameraEditing::SpawnCamera(World, FTransform::Identity), TEXT("ai-tab-model-notice"), ERoscDepartment::AI, 2000.0f, AllSizes, [this](SRoscPanel& Panel)
		{
			const TSharedRef<SRoscAITab> Tab = Panel.GetAITab();
			Tab->Tick(FGeometry(), 1.0, 0.0f);
			TArray<TSharedRef<SButton>> Downloads;
			Rosc::WidgetTests::FindButtonsShowing(Tab, TEXT("Download"), Downloads);
			if (TestEqual(TEXT("Each model has a Download button"), Downloads.Num(), 3))
			{
				Downloads.Last()->SimulateClick();
			}
		});
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([SettingsBefore]
		{
			SettingsBefore.PutBack();
			return true;
		}));
		return true;
	}));
	return true;
}

#endif
