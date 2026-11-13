// Copyright 2026 Fiavaion. All rights reserved.

#include "CineCameraActor.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
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
#include "SUEcineAITab.h"
#include "SUEcinePanel.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "Tests/UEcineDeliverChoicesBefore.h"
#include "Tests/UEcineRenderedFiles.h"
#include "UObject/StrongObjectPtr.h"
#include "UEcineAISettings.h"
#include "UEcineAISubsystem.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineDeliverEditing.h"
#include "UEcineEditorSettings.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineMonitorSubsystem.h"
#include "UEcineRenderSubsystem.h"
#include "UEcineRig.h"
#include "UEcineShotEditing.h"

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
		TSharedPtr<SUEcinePanel> Panel;
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
	};

	/**
	 * Queues drawing one department of the panel for a camera and saving pictures of it to
	 * Saved/UEcine/Screenshots: by default at a comfortable width, at the narrowest supported
	 * width, and at 150% application scale. The panel is drawn off screen, tall enough to show the whole tab:
	 * a window would be cut to the height of the display (LESSON-TEST-001). The camera, and any
	 * rig it is on, is removed afterwards.
	 * @param Prepare Called for each picture's panel when it has been made, to give it what a tab
	 *        would be handed while the user works: a plan from the assistant, a job's result.
	 */
	void QueuePanelCaptures(FAutomationTestBase& Test, ACineCameraActor* Camera, const TCHAR* NamePrefix, EUEcineDepartment Department, float Height, TConstArrayView<FPanelCapture> Captures = AllSizes, TFunction<void(SUEcinePanel&)> Prepare = nullptr)
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
				State->Panel = SNew(SUEcinePanel);
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
					const FString FilePath = FPaths::ProjectSavedDir() / TEXT("UEcine/Screenshots") / Name + TEXT(".png");
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
				UEcine::GripEditing::TakeCameraOffRig(CameraToRemove);
				CameraToRemove->GetWorld()->EditorDestroyActor(CameraToRemove, false);
			}
			GEditor->ResetTransaction(FText::FromString(TEXT("UEcine panel visual test")));
			return true;
		}));
	}

	/** The AI settings as a test found them, to be put back, in their file too, when it has changed them. */
	struct FVisualAISettingsBefore
	{
		FVisualAISettingsBefore()
		{
			const UUEcineAISettings* Settings = GetDefault<UUEcineAISettings>();
			Backend = Settings->Backend;
			ServerUrl = Settings->ServerUrl;
			AssistantKey = Settings->AssistantKey;
			ConsentedHosts = Settings->ConsentedHosts;
		}

		void PutBack() const
		{
			UUEcineAISettings* Settings = GetMutableDefault<UUEcineAISettings>();
			Settings->Backend = Backend;
			Settings->ServerUrl = ServerUrl;
			Settings->AssistantKey = AssistantKey;
			Settings->ConsentedHosts = ConsentedHosts;
			// Should anything have saved the settings while the values of the test were in them.
			Settings->SaveConfig();
		}

		EUEcineAIBackend Backend;
		FString ServerUrl;
		FString AssistantKey;
		TArray<FString> ConsentedHosts;
	};
}

/** Pictures of the Camera tab. Needs a renderer: run with tools/test.ps1 -Rendering; otherwise it reports itself skipped. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineCameraTabVisualTest, "UEcine.Editor.Visual.CameraTab", VisualTestFlags)

bool FUEcineCameraTabVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(GEditor->GetEditorWorldContext().World(), FTransform::Identity);
	UUEcineCameraPackageComponent* Component = UUEcineCameraPackageComponent::Find(CameraActor);
	if (!TestNotNull(TEXT("A camera with a package to show"), Component))
	{
		return false;
	}

	// Show the busiest state: a zoom (extra row), physical exposure on, and a lens kit that
	// does not cover the recording format (warning row).
	const FUEcineLensKit* Zoom = FUEcineCameraDatabase::Get().FindLensKit(TEXT("angenieux-optimo-24-290"));
	if (!TestNotNull(TEXT("The zoom kit used for the picture is in the database"), Zoom))
	{
		return false;
	}
	FText Error;
	UEcine::CameraEditing::EditPackage(Component, FText::FromString(TEXT("Visual test setup")), [Zoom](FUEcineCameraPackage& Package)
	{
		Package.SelectLensKit(*Zoom);
		Package.bPhysicalExposure = true;
		Package.NdDensity = 0.6f;
	}, Error);

	QueuePanelCaptures(*this, CameraActor, TEXT("camera-tab"), EUEcineDepartment::Camera, 900.0f);
	return true;
}

/** Pictures of the Grip tab. Needs a renderer, like the Camera tab pictures. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineGripTabVisualTest, "UEcine.Editor.Visual.GripTab", VisualTestFlags)

bool FUEcineGripTabVisualTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::GripEditing;

	if (!CanDrawPanel(*this))
	{
		return true;
	}

	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(GEditor->GetEditorWorldContext().World(), FTransform(FVector(0.0, 0.0, 300.0)));
	AUEcineRig* Rig = PutCameraOnRig(CameraActor, *UEcine::Rig::FindPreset(TEXT("crane-30ft")));
	if (!TestNotNull(TEXT("A camera on a crane to show"), Rig))
	{
		return false;
	}

	// Show the busiest state: every crane axis, three marks, a second move too quick for the
	// crane (warning rows) and an operator with the intensity and take controls live.
	AddMark(Rig);
	EditRig(Rig, FText::FromString(TEXT("Visual test setup")), [](AUEcineRig& EditedRig)
	{
		FUEcineRigPose Pose = EditedRig.GetPose();
		Pose.Swing = 30.0;
		Pose.Boom = 20.0;
		Pose.Extension += 150.0;
		EditedRig.SetPose(Pose);
	});
	AddMark(Rig);
	EditRig(Rig, FText::FromString(TEXT("Visual test setup")), [](AUEcineRig& EditedRig)
	{
		FUEcineRigPose Pose = EditedRig.GetPose();
		Pose.Swing = -40.0;
		Pose.Extension += 200.0;
		Pose.Pan = 25.0;
		EditedRig.SetPose(Pose);
	});
	AddMark(Rig);
	EditRig(Rig, FText::FromString(TEXT("Visual test setup")), [](AUEcineRig& EditedRig)
	{
		EditedRig.Marks[1].Arrival.DurationSeconds = 6.0;
		EditedRig.Marks[2].Arrival.DurationSeconds = 1.5;
		EditedRig.Operator.Style = EUEcineOperatorStyle::Handheld;
	});

	QueuePanelCaptures(*this, CameraActor, TEXT("grip-tab"), EUEcineDepartment::Grip, 1000.0f);
	return true;
}

/** Pictures of the Grip tab's quieter states: a camera on nothing, and a dolly with no marks yet. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineGripTabStatesVisualTest, "UEcine.Editor.Visual.GripTabStates", VisualTestFlags)

bool FUEcineGripTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();

	ACineCameraActor* FreeCamera = UEcine::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 150.0)));
	QueuePanelCaptures(*this, FreeCamera, TEXT("grip-tab-no-support"), EUEcineDepartment::Grip, 400.0f, NarrowOnly);

	// The panel follows the last camera made, so the dolly's camera is made once the first picture is taken.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World]
	{
		ACineCameraActor* DollyCamera = UEcine::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 150.0)));
		if (TestNotNull(TEXT("A camera on a dolly to show"), UEcine::GripEditing::PutCameraOnRig(DollyCamera, UEcine::Rig::GetDefaultPreset(EUEcineRigType::Dolly))))
		{
			QueuePanelCaptures(*this, DollyCamera, TEXT("grip-tab-dolly"), EUEcineDepartment::Grip, 700.0f, NarrowOnly);
		}
		return true;
	}));
	return true;
}

/** Pictures of the Focus tab. Needs a renderer, like the Camera tab pictures. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineFocusTabVisualTest, "UEcine.Editor.Visual.FocusTab", VisualTestFlags)

bool FUEcineFocusTabVisualTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::FocusEditing;

	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(World, FTransform::Identity);
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

	QueuePanelCaptures(*this, CameraActor, TEXT("focus-tab"), EUEcineDepartment::Focus, 760.0f);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineFocusTabStatesVisualTest, "UEcine.Editor.Visual.FocusTabStates", VisualTestFlags)

bool FUEcineFocusTabStatesVisualTest::RunTest(const FString& Parameters)
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
	QueuePanelCaptures(*this, BareCamera, TEXT("focus-tab-no-package"), EUEcineDepartment::Focus, 620.0f, NarrowOnly);
	return true;
}

/** Pictures of the Monitor tab. Needs a renderer, like the Camera tab pictures. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMonitorTabVisualTest, "UEcine.Editor.Visual.MonitorTab", VisualTestFlags)

bool FUEcineMonitorTabVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UUEcineMonitorSubsystem* Monitor = UUEcineMonitorSubsystem::Get();
	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(GEditor->GetEditorWorldContext().World(), FTransform::Identity);
	if (!TestNotNull(TEXT("The monitor exists in the editor"), Monitor) || !TestNotNull(TEXT("A camera to show"), CameraActor))
	{
		return false;
	}

	// Show the busiest state: every tool on, so the zebra and false colour legends are there, and
	// frame lines for a camera that delivers its whole recording format (warning row).
	const FUEcineMonitorSettings SettingsBefore = Monitor->GetSettings();
	FUEcineMonitorSettings Everything;
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

	QueuePanelCaptures(*this, CameraActor, TEXT("monitor-tab"), EUEcineDepartment::Monitor, 1000.0f);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([SettingsBefore]
	{
		UUEcineMonitorSubsystem::Get()->SetSettings(SettingsBefore);
		return true;
	}));
	return true;
}

/** A picture of the Monitor tab with every tool off and no camera to work on: the tab is there all the same. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMonitorTabStatesVisualTest, "UEcine.Editor.Visual.MonitorTabStates", VisualTestFlags)

bool FUEcineMonitorTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UUEcineMonitorSubsystem* Monitor = UUEcineMonitorSubsystem::Get();
	if (!TestNotNull(TEXT("The monitor exists in the editor"), Monitor))
	{
		return false;
	}

	const FUEcineMonitorSettings SettingsBefore = Monitor->GetSettings();
	Monitor->SetSettings(FUEcineMonitorSettings());
	GEditor->SelectNone(false, true, false);

	QueuePanelCaptures(*this, nullptr, TEXT("monitor-tab-no-camera"), EUEcineDepartment::Monitor, 720.0f, NarrowOnly);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([SettingsBefore]
	{
		UUEcineMonitorSubsystem::Get()->SetSettings(SettingsBefore);
		return true;
	}));
	return true;
}

/**
 * Pictures of the Shots tab with a three-shot sequence open: the list, the first shot's fields,
 * two storyboard frames, the overhead plan of a dolly move, and the paperwork buttons.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineShotsTabVisualTest, "UEcine.Editor.Visual.ShotsTab", VisualTestFlags)

bool FUEcineShotsTabVisualTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::ShotEditing;

	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(World, FTransform(FRotator::ZeroRotator, FVector(0.0, -200.0, 150.0)));
	AStaticMeshActor* Hero = World->SpawnActor<AStaticMeshActor>(FVector(700.0, -100.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("A camera to show"), CameraActor) || !TestNotNull(TEXT("A subject to show"), Hero))
	{
		return false;
	}
	Hero->SetActorLabel(TEXT("Hero"));
	UEcine::FocusEditing::FollowActor(CameraActor, Hero);

	AUEcineRig* Dolly = UEcine::GripEditing::PutCameraOnRig(CameraActor, *UEcine::Rig::FindPreset(TEXT("dolly")));
	if (!TestNotNull(TEXT("The camera is put on a dolly"), Dolly))
	{
		return false;
	}
	UEcine::GripEditing::AddMark(Dolly);
	UEcine::GripEditing::EditRig(Dolly, FText::FromString(TEXT("Move Rig")), [](AUEcineRig& Rig)
	{
		FUEcineRigPose Pose = Rig.GetPose();
		Pose.Travel += 350.0;
		Pose.Pan = 20.0;
		Rig.SetPose(Pose);
	});
	UEcine::GripEditing::AddMark(Dolly);
	UEcine::GripEditing::EditRig(Dolly, FText::FromString(TEXT("Time Move")), [](AUEcineRig& Rig) { Rig.Marks[1].Arrival.DurationSeconds = 4.0; });
	UEcine::GripEditing::GoToMark(Dolly, 0);

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
		SetSize(Shots[0].Sequence, EUEcineShotSize::Wide);
		SetMove(Shots[0].Sequence, TEXT("Track in on Hero, pan right"));
		SetNotes(Shots[0].Sequence, TEXT("Dawn light, haze. Hold two beats before the move."));
		SetScene(Shots[1].Sequence, TEXT("12"));
		SetSize(Shots[1].Sequence, EUEcineShotSize::CloseUp);
		SetScene(Shots[2].Sequence, TEXT("12A"));
		SetSize(Shots[2].Sequence, EUEcineShotSize::OverTheShoulder);

		GoToShot(Shots[0]);
		TestTrue(TEXT("The dolly move is baked into the first shot"), UEcine::GripEditing::BakeMoveToSequencer(Dolly, Error) == Shots[0].Sequence);
		for (const int32 Frame : { 80, 0 })
		{
			ULevelSequenceEditorBlueprintLibrary::SetGlobalPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Scrub));
			TestTrue(TEXT("A storyboard frame is grabbed"), GrabStoryboardFrame(Shots[0], Error));
		}
		QueuePanelCaptures(*this, CameraActor, TEXT("shots-tab"), EUEcineDepartment::Shots, 1500.0f);
	}

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, CreatedSequences, WeakHero = TWeakObjectPtr<AStaticMeshActor>(Hero)]
	{
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine shots tab visual test")));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineShotsTabStatesVisualTest, "UEcine.Editor.Visual.ShotsTabStates", VisualTestFlags)

bool FUEcineShotsTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	GEditor->SelectNone(false, true, false);
	QueuePanelCaptures(*this, nullptr, TEXT("shots-tab-no-sequence"), EUEcineDepartment::Shots, 420.0f, NarrowOnly);
	return true;
}

/**
 * Pictures of the Deliver tab with a four-shot sequence open. Set to deliver for review, with a
 * shot that has lost its camera cut (a finding that stops the render, with its fix) and a slate
 * that falls before the sequence (a finding worth knowing). Then with the camera cut put back by
 * the fix and set to deliver a master, ready to render. Then while that master renders.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverTabVisualTest, "UEcine.Editor.Visual.DeliverTab", VisualTestFlags)

bool FUEcineDeliverTabVisualTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::DeliverEditing;
	using namespace UEcine::ShotEditing;

	if (!CanDrawPanel(*this))
	{
		return true;
	}
	UEcine::RenderedFiles::ExpectRenderLogNoise(*this);

	const FUEcineDeliverChoicesBefore ChoicesBefore;
	const FString OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEcine/Renders/DeliverTabPictures"));
	IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
	EditChoices([&OutputDirectory](UUEcineEditorSettings& Choices)
	{
		Choices.DeliverIntent = EUEcineDeliverIntent::Review;
		Choices.DeliverFolder.Path = OutputDirectory;
		Choices.DeliverNamePattern = UEcine::Deliver::DefaultNamePattern;
		Choices.bDeliverSlate = true;
		Choices.bDeliverBurnIn = true;
	});

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(World, FTransform(FRotator::ZeroRotator, FVector(0.0, -200.0, 150.0)));
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
		QueuePanelCaptures(*this, nullptr, TEXT("deliver-tab"), EUEcineDepartment::Deliver, 1400.0f);

		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]
		{
			const FCheck Check = CheckDelivery(EUEcineDeliverIntent::Review);
			const FUEcinePreflightFinding* NoCameraCut = Check.Findings.FindByPredicate([](const FUEcinePreflightFinding& Finding) { return Finding.Check == EUEcinePreflightCheck::ShotWithoutCameraCut; });
			FText FixError;
			TestTrue(TEXT("The shot without a camera cut is found and put right"), NoCameraCut != nullptr && ApplyFix(*NoCameraCut, FixError));
			EditChoices([](UUEcineEditorSettings& Choices) { Choices.DeliverIntent = EUEcineDeliverIntent::Master; });
			return true;
		}));
		QueuePanelCaptures(*this, nullptr, TEXT("deliver-tab-ready"), EUEcineDepartment::Deliver, 900.0f, NarrowOnly);

		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]
		{
			FDelivery Delivery;
			FText StartError;
			if (!StartDelivery(EUEcineDeliverIntent::Master, Delivery, StartError))
			{
				AddError(FString::Printf(TEXT("The delivery to show rendering did not start: %s"), *StartError.ToString()));
			}
			return true;
		}));
		// The picture is of a render under way, so it waits for the first frame to be made.
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]
		{
			const UUEcineRenderSubsystem* Render = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>();
			return !Render->IsRendering() || Render->GetProgress() > 0.0f;
		}));
		QueuePanelCaptures(*this, nullptr, TEXT("deliver-tab-rendering"), EUEcineDepartment::Deliver, 900.0f, NarrowOnly);
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]
		{
			// The picture only shows a render when the render outlasts the taking of it.
			AddInfo(GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->IsRendering()
				? TEXT("deliver-tab-rendering was taken while the render ran")
				: TEXT("deliver-tab-rendering was taken after the render had ended: it shows the tab at rest"));
			return true;
		}));
	}

	const double CleanUpStartSeconds = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, ChoicesBefore, CreatedSequences, OutputDirectory, CleanUpStartSeconds, WeakCamera = TWeakObjectPtr<ACineCameraActor>(CameraActor)]
	{
		UUEcineRenderSubsystem* Render = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>();
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
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine deliver tab visual test")));
		for (ULevelSequence* Sequence : CreatedSequences)
		{
			TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
		}
		IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
		return true;
	}));
	return true;
}

/** A picture of the Deliver tab with no sequence open, set to deliver a VFX handoff: preflight says what is missing. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverTabStatesVisualTest, "UEcine.Editor.Visual.DeliverTabStates", VisualTestFlags)

bool FUEcineDeliverTabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	const FUEcineDeliverChoicesBefore ChoicesBefore;
	UEcine::DeliverEditing::EditChoices([](UUEcineEditorSettings& Choices) { Choices.DeliverIntent = EUEcineDeliverIntent::VfxHandoff; });
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	GEditor->SelectNone(false, true, false);
	QueuePanelCaptures(*this, nullptr, TEXT("deliver-tab-no-sequence"), EUEcineDepartment::Deliver, 800.0f, NarrowOnly);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineAITabVisualTest, "UEcine.Editor.Visual.AITab", VisualTestFlags)

bool FUEcineAITabVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	AStaticMeshActor* Anna = World->SpawnActor<AStaticMeshActor>(FVector(600.0, 80.0, 90.0), FRotator::ZeroRotator);
	Anna->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Anna->SetActorLabel(TEXT("Anna"));
	ACineCameraActor* CameraActor = UEcine::CameraEditing::SpawnCamera(World, FTransform(FVector(0.0, 0.0, 160.0)));

	// The assistant can be asked, as far as the tab can tell: a key that is no key, and an agreement to its host.
	const FVisualAISettingsBefore SettingsBefore;
	UUEcineAISettings* Settings = GetMutableDefault<UUEcineAISettings>();
	Settings->Backend = EUEcineAIBackend::Editor;
	Settings->AssistantKey = TEXT("not-a-key");
	Settings->ConsentedHosts = { TEXT("api.anthropic.com") };

	FUEcineShotPlanResult Plan;
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
	Plan.bSuccess = UEcine::ShotPlan::Parse(Plan.PlanJson, Plan.Plan, Fault);
	TestTrue(*FString::Printf(TEXT("The plan to show is a plan (%s)"), *Fault), Plan.bSuccess);

	FUEcineAIResult Lens;
	Lens.Request.Task = EUEcineAITask::FocalLength;
	Lens.bSuccess = true;
	Lens.FieldOfViewDeg = 26.0;

	QueuePanelCaptures(*this, CameraActor, TEXT("ai-tab"), EUEcineDepartment::AI, 1750.0f, AllSizes, [Plan, Lens](SUEcinePanel& Panel)
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineAITabStatesVisualTest, "UEcine.Editor.Visual.AITabStates", VisualTestFlags)

bool FUEcineAITabStatesVisualTest::RunTest(const FString& Parameters)
{
	if (!CanDrawPanel(*this))
	{
		return true;
	}

	const FVisualAISettingsBefore SettingsBefore;
	UUEcineAISettings* Settings = GetMutableDefault<UUEcineAISettings>();
	Settings->Backend = EUEcineAIBackend::Server;
	Settings->ServerUrl = TEXT("https://render01.studio.example:8750");
	Settings->AssistantKey = TEXT("not-a-key");
	Settings->ConsentedHosts.Reset();

	FUEcineAIResult NotTaken;
	NotTaken.Request.Task = EUEcineAITask::Depth;
	NotTaken.Error = FText::FromString(TEXT("Choose the picture to work on first."));

	UWorld* World = GEditor->GetEditorWorldContext().World();
	QueuePanelCaptures(*this, UEcine::CameraEditing::SpawnCamera(World, FTransform::Identity), TEXT("ai-tab-agreement"), EUEcineDepartment::AI, 1300.0f, NarrowOnly, [NotTaken](SUEcinePanel& Panel)
	{
		Panel.GetAITab()->ShowJobResult(NotTaken);
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, SettingsBefore]
	{
		UUEcineAISettings* NoKeySettings = GetMutableDefault<UUEcineAISettings>();
		NoKeySettings->Backend = EUEcineAIBackend::Helper;
		NoKeySettings->AssistantKey.Reset();
		QueuePanelCaptures(*this, UEcine::CameraEditing::SpawnCamera(World, FTransform::Identity), TEXT("ai-tab-no-key"), EUEcineDepartment::AI, 1100.0f, NarrowOnly);
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
