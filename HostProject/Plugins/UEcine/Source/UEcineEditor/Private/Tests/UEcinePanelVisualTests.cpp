#include "CineCameraActor.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MovieSceneSequencePlayer.h"
#include "ObjectTools.h"
#include "RenderingThread.h"
#include "SUEcinePanel.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineMonitorSubsystem.h"
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
	 */
	void QueuePanelCaptures(FAutomationTestBase& Test, ACineCameraActor* Camera, const TCHAR* NamePrefix, EUEcineDepartment Department, float Height, TConstArrayView<FPanelCapture> Captures = AllSizes)
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

			// A first pass lays the tab out and asks for its glyphs and icons at this scale. The
			// editor makes those ready between frames, so the picture is taken a moment later.
			ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State, Department, DrawSize, Draw]
			{
				State->Panel = SNew(SUEcinePanel);
				State->Panel->ShowDepartment(Department);
				State->Target.Reset(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, false));
				Draw();
				return true;
			}));

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
			}, 0.5f));
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
	SetFocusDistance(CameraActor, 320.0f);
	AddFocusMark(CameraActor);
	AStaticMeshActor* Subject = World->SpawnActor<AStaticMeshActor>(FVector(20.0, 0.0, 0.0), FRotator::ZeroRotator);
	Subject->SetActorLabel(TEXT("Hero"));
	FollowActor(CameraActor, Subject);
	TestTrue(TEXT("The subject is nearer than the lens can focus"), GetFocusDistance(CameraActor) < GetCloseFocus(CameraActor));

	QueuePanelCaptures(*this, CameraActor, TEXT("focus-tab"), EUEcineDepartment::Focus, 760.0f);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([WeakSubject = TWeakObjectPtr<AStaticMeshActor>(Subject)]
	{
		if (AStaticMeshActor* SubjectToRemove = WeakSubject.Get())
		{
			SubjectToRemove->GetWorld()->EditorDestroyActor(SubjectToRemove, false);
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

#endif
