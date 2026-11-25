// Copyright 2026 Fiavaion. All rights reserved.

#include "SRoscPanel.h"

#include "CineCameraActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "SLevelViewport.h"
#include "SRoscAITab.h"
#include "SRoscCameraTab.h"
#include "SRoscDeliverTab.h"
#include "SRoscFocusTab.h"
#include "SRoscGripTab.h"
#include "SRoscMonitorTab.h"
#include "SRoscShotsTab.h"
#include "Styling/AppStyle.h"
#include "RoscCameraEditing.h"
#include "RoscCameraTarget.h"
#include "RoscEditorModule.h"
#include "RoscStyle.h"
#include "RoscWidgets.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

using namespace Rosc::Widgets;

void SRoscPanel::Construct(const FArguments& InArgs)
{
	Target = MakeShared<FRoscCameraTarget>();

	const FText CameraToolTip = LOCTEXT("CameraPickerToolTip", "The camera this panel is working on. Selecting a camera in the level also switches to it.");
	const FText LookThroughToolTip = LOCTEXT("LookThroughToolTip", "Look through this camera in the viewport. Moving the viewport then moves the camera, unless it is on a support: a support holds the camera, and the Grip tab moves it.");
	const FText UserGuideToolTip = LOCTEXT("UserGuideToolTip", "User guide: open the Rosc guide in your web browser.");
	const FText NewCameraToolTip = LOCTEXT("AddCameraHereToolTip", "Add a camera where the viewport is looking from. Move through the level until you like the view, then press this: the new camera stands there, with the lens of its kit that frames nearest to what you saw.");

	const TSharedRef<SWidget> LookThrough = MakeToggleButton(LOCTEXT("LookThroughLabel", "Look Through"), LookThroughToolTip,
		TAttribute<bool>::CreateSP(this, &SRoscPanel::IsLookingThrough),
		[this](bool bLookThrough) { OnLookThroughChanged(bLookThrough); });
	LookThrough->SetEnabled(TAttribute<bool>::CreateSP(this, &SRoscPanel::HasCamera));

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
			.Padding(FMargin(FRoscStyle::PanelPadding, 6.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredHeight(FRoscStyle::MinTargetSize)
					[
						SNew(SComboButton)
						.ToolTipText(CameraToolTip)
						.AccessibleText(LOCTEXT("CameraPickerName", "Camera"))
						.OnGetMenuContent(this, &SRoscPanel::MakeCameraMenu)
						.ButtonContent()
						[
							SNew(STextBlock)
							.TextStyle(&FRoscStyle::Get().GetWidgetStyle<FTextBlockStyle>("Rosc.Value"))
							.Text(this, &SRoscPanel::GetCameraName)
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeIconButton("Icons.Help", UserGuideToolTip, FOnClicked::CreateSP(this, &SRoscPanel::OnUserGuideClicked))
				]
				]
				// The two things done with the viewport, in words: neither is found behind an icon.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(SWrapBox)
					.UseAllottedSize(true)
					.InnerSlotPadding(FVector2D(4.0f, 4.0f))
					+ SWrapBox::Slot()
					[
						LookThrough
					]
					+ SWrapBox::Slot()
					[
						MakeButton(LOCTEXT("AddCameraHereLabel", "Add Camera Here"), NewCameraToolTip, FOnClicked::CreateSP(this, &SRoscPanel::OnNewCameraClicked))
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
			.Padding(FMargin(FRoscStyle::PanelPadding, 0.0f, FRoscStyle::PanelPadding, 6.0f))
			[
				// A wrap box, so the departments fold onto a second line when the panel is narrow.
				SNew(SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(4.0, 4.0))
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(ERoscDepartment::Camera, LOCTEXT("CameraDepartment", "Camera"),
						LOCTEXT("CameraDepartmentToolTip", "Camera: body, recording format, lens and exposure."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(ERoscDepartment::Grip, LOCTEXT("GripDepartment", "Grip"),
						LOCTEXT("GripDepartmentToolTip", "Grip: put the camera on a tripod, slider, dolly, jib, crane, Steadicam or drone, and block its move."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(ERoscDepartment::Focus, LOCTEXT("FocusDepartment", "Focus"),
						LOCTEXT("FocusDepartmentToolTip", "Focus: set focus, follow a subject, mark focus distances and rack between them."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(ERoscDepartment::Monitor, LOCTEXT("MonitorDepartment", "Monitor"),
						LOCTEXT("MonitorDepartmentToolTip", "Monitor: false colour, zebras, waveform, histogram, focus peaking, frame lines and camera data over the viewport picture."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(ERoscDepartment::Shots, LOCTEXT("ShotsDepartment", "Shots"),
						LOCTEXT("ShotsDepartmentToolTip", "Shots: the shot list of the sequence in Sequencer, storyboard frames, overhead plans, and the shot list and camera report to hand out."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(ERoscDepartment::Deliver, LOCTEXT("DeliverDepartment", "Deliver"),
						LOCTEXT("DeliverDepartmentToolTip", "Deliver: render the sequence in Sequencer for review, for visual effects or as a master, after a preflight check."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(ERoscDepartment::AI, LOCTEXT("AIDepartment", "AI"),
						LOCTEXT("AIDepartmentToolTip", "AI, all of it optional: describe a shot in words and get a plan to apply; a depth map, a matte or a lens estimate from a still or plate. Nothing leaves this machine until you agree to it."))
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			.Padding(0.0f)
			[
				// Slot 0 invites the user to make a camera; the departments follow in tab order.
				SNew(SWidgetSwitcher)
				.WidgetIndex(this, &SRoscPanel::GetShownSlot)
				+ SWidgetSwitcher::Slot()
				.Padding(FRoscStyle::PanelPadding)
				[
					MakeEmptyState(
						LOCTEXT("NoCameraMessage", "There is no camera to work on yet. Move through the level until you like the view and add a camera there, or select a Cine Camera Actor in the level."),
						LOCTEXT("NoCameraAddButton", "Add Camera Here"),
						NewCameraToolTip,
						FOnClicked::CreateSP(this, &SRoscPanel::OnNewCameraClicked))
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SRoscCameraTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SRoscGripTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SRoscFocusTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SRoscMonitorTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SRoscShotsTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SRoscDeliverTab)
				]
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(AITab, SRoscAITab, Target.ToSharedRef())
				]
			]
		]
	];
}

TSharedRef<SWidget> SRoscPanel::MakeDepartmentTab(ERoscDepartment Department, const FText& Label, const FText& ToolTip)
{
	return MakeToggleButton(Label, ToolTip,
		TAttribute<bool>::CreateSPLambda(this, [this, Department] { return ActiveDepartment == Department; }),
		[this, Department](bool) { ShowDepartment(Department); });
}

TSharedRef<SWidget> SRoscPanel::MakeCameraMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("CamerasSection", "Cameras in this level"));
	if (const UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		for (TActorIterator<ACineCameraActor> It(World); It; ++It)
		{
			const TWeakObjectPtr<ACineCameraActor> WeakCamera = *It;
			MenuBuilder.AddMenuEntry(
				FText::FromString(It->GetActorLabel()),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSPLambda(this, [this, WeakCamera]
					{
						if (ACineCameraActor* Camera = WeakCamera.Get())
						{
							Target->SetCamera(Camera);
						}
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateSPLambda(this, [this, WeakCamera] { return Target->GetCamera() == WeakCamera.Get(); })),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(NAME_None);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddCameraHereEntry", "Add Camera Here"),
		LOCTEXT("AddCameraHereEntryToolTip", "Add a camera where the viewport is looking from, with the lens of its kit that frames nearest to the view."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
		FUIAction(FExecuteAction::CreateSPLambda(this, [this] { OnNewCameraClicked(); })));
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FText SRoscPanel::GetCameraName() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	return Camera != nullptr ? FText::FromString(Camera->GetActorLabel()) : LOCTEXT("NoCameraName", "No camera");
}

bool SRoscPanel::HasCamera() const
{
	return Target->GetCamera() != nullptr;
}

int32 SRoscPanel::GetShownSlot() const
{
	// The monitor's tools work on any view, the shot list and deliveries are of the sequence in Sequencer, and picture jobs are of a file, so none of them waits for a camera.
	const bool bWorksWithoutCamera = ActiveDepartment == ERoscDepartment::Monitor || ActiveDepartment == ERoscDepartment::Shots || ActiveDepartment == ERoscDepartment::Deliver || ActiveDepartment == ERoscDepartment::AI;
	return HasCamera() || bWorksWithoutCamera ? 1 + static_cast<int32>(ActiveDepartment) : 0;
}

bool SRoscPanel::IsLookingThrough() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	return Camera != nullptr && LevelEditor != nullptr && LevelEditor->GetPilotLevelActor() == Camera;
}

void SRoscPanel::OnLookThroughChanged(bool bLookThrough)
{
	ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (LevelEditor == nullptr)
	{
		return;
	}
	// The subsystem looks through a camera in the active viewport.
	const TSharedPtr<ILevelEditor> Editor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstLevelEditor();
	const TSharedPtr<SLevelViewport> Viewport = Editor.IsValid() ? Editor->GetActiveViewportInterface() : nullptr;
	if (bLookThrough)
	{
		// Where the view was, to go back to when the user stops looking through the camera.
		ViewBeforeLookThrough.Reset();
		if (Viewport.IsValid() && !Viewport->GetLevelViewportClient().GetActiveActorLock().IsValid())
		{
			const FLevelEditorViewportClient& View = Viewport->GetLevelViewportClient();
			ViewBeforeLookThrough = FViewBeforeLookThrough{ Viewport, View.GetViewLocation(), View.GetViewRotation() };
		}
		LevelEditor->PilotLevelActor(Target->GetCamera());
		return;
	}

	LevelEditor->EjectPilotLevelActor();
	if (ViewBeforeLookThrough.IsSet() && ViewBeforeLookThrough->Viewport.Pin() == Viewport && Viewport.IsValid())
	{
		FLevelEditorViewportClient& View = Viewport->GetLevelViewportClient();
		View.SetViewLocation(ViewBeforeLookThrough->Location);
		View.SetViewRotation(ViewBeforeLookThrough->Rotation);
		View.Invalidate();
	}
	ViewBeforeLookThrough.Reset();
}

FReply SRoscPanel::OnNewCameraClicked()
{
	if (ACineCameraActor* NewCamera = Rosc::CameraEditing::SpawnCameraAtViewport())
	{
		Target->SetCamera(NewCamera);
	}
	return FReply::Handled();
}

FReply SRoscPanel::OnUserGuideClicked()
{
	FPlatformProcess::LaunchFileInDefaultExternalApplication(*FRoscEditorModule::GetUserGuideFile());
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
