// Copyright 2026 Fiavaion. All rights reserved.

#include "SUEcinePanel.h"

#include "CineCameraActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditorSubsystem.h"
#include "SUEcineAITab.h"
#include "SUEcineCameraTab.h"
#include "SUEcineDeliverTab.h"
#include "SUEcineFocusTab.h"
#include "SUEcineGripTab.h"
#include "SUEcineMonitorTab.h"
#include "SUEcineShotsTab.h"
#include "Styling/AppStyle.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraTarget.h"
#include "UEcineEditorModule.h"
#include "UEcineStyle.h"
#include "UEcineWidgets.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

using namespace UEcine::Widgets;

void SUEcinePanel::Construct(const FArguments& InArgs)
{
	Target = MakeShared<FUEcineCameraTarget>();

	const FText CameraToolTip = LOCTEXT("CameraPickerToolTip", "The camera this panel is working on. Selecting a camera in the level also switches to it.");
	const FText LookThroughToolTip = LOCTEXT("LookThroughToolTip", "Look through this camera in the viewport. Moving the viewport then moves the camera, unless it is on a support: a support holds the camera, and the Grip tab moves it.");
	const FText UserGuideToolTip = LOCTEXT("UserGuideToolTip", "User guide: open the UEcine guide in your web browser.");
	const FText NewCameraToolTip = LOCTEXT("PanelNewCameraToolTip", "New camera: create a Cine Camera Actor where the viewport is looking from, with a camera package ready to edit.");

	const TSharedRef<SWidget> LookThrough = MakeToggleButton(LOCTEXT("LookThroughLabel", "Look Through"), LookThroughToolTip,
		TAttribute<bool>::CreateSP(this, &SUEcinePanel::IsLookingThrough),
		[this](bool bLookThrough) { OnLookThroughChanged(bLookThrough); });
	LookThrough->SetEnabled(TAttribute<bool>::CreateSP(this, &SUEcinePanel::HasCamera));

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
			.Padding(FMargin(FUEcineStyle::PanelPadding, 6.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredHeight(FUEcineStyle::MinTargetSize)
					[
						SNew(SComboButton)
						.ToolTipText(CameraToolTip)
						.AccessibleText(LOCTEXT("CameraPickerName", "Camera"))
						.OnGetMenuContent(this, &SUEcinePanel::MakeCameraMenu)
						.ButtonContent()
						[
							SNew(STextBlock)
							.TextStyle(&FUEcineStyle::Get().GetWidgetStyle<FTextBlockStyle>("UEcine.Value"))
							.Text(this, &SUEcinePanel::GetCameraName)
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					LookThrough
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeIconButton("Icons.Plus", NewCameraToolTip, FOnClicked::CreateSP(this, &SUEcinePanel::OnNewCameraClicked))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					MakeIconButton("Icons.Help", UserGuideToolTip, FOnClicked::CreateSP(this, &SUEcinePanel::OnUserGuideClicked))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
			.Padding(FMargin(FUEcineStyle::PanelPadding, 0.0f, FUEcineStyle::PanelPadding, 6.0f))
			[
				// A wrap box, so the departments fold onto a second line when the panel is narrow.
				SNew(SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(4.0, 4.0))
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(EUEcineDepartment::Camera, LOCTEXT("CameraDepartment", "Camera"),
						LOCTEXT("CameraDepartmentToolTip", "Camera: body, recording format, lens and exposure."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(EUEcineDepartment::Grip, LOCTEXT("GripDepartment", "Grip"),
						LOCTEXT("GripDepartmentToolTip", "Grip: put the camera on a tripod, slider, dolly, jib, crane, Steadicam or drone, and block its move."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(EUEcineDepartment::Focus, LOCTEXT("FocusDepartment", "Focus"),
						LOCTEXT("FocusDepartmentToolTip", "Focus: set focus, follow a subject, mark focus distances and rack between them."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(EUEcineDepartment::Monitor, LOCTEXT("MonitorDepartment", "Monitor"),
						LOCTEXT("MonitorDepartmentToolTip", "Monitor: false colour, zebras, waveform, histogram, focus peaking, frame lines and camera data over the viewport picture."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(EUEcineDepartment::Shots, LOCTEXT("ShotsDepartment", "Shots"),
						LOCTEXT("ShotsDepartmentToolTip", "Shots: the shot list of the sequence in Sequencer, storyboard frames, overhead plans, and the shot list and camera report to hand out."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(EUEcineDepartment::Deliver, LOCTEXT("DeliverDepartment", "Deliver"),
						LOCTEXT("DeliverDepartmentToolTip", "Deliver: render the sequence in Sequencer for review, for visual effects or as a master, after a preflight check."))
				]
				+ SWrapBox::Slot()
				[
					MakeDepartmentTab(EUEcineDepartment::AI, LOCTEXT("AIDepartment", "AI"),
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
				.WidgetIndex(this, &SUEcinePanel::GetShownSlot)
				+ SWidgetSwitcher::Slot()
				.Padding(FUEcineStyle::PanelPadding)
				[
					MakeEmptyState(
						LOCTEXT("NoCameraMessage", "There is no camera to work on yet. Create one at the current viewport position, or select a Cine Camera Actor in the level."),
						LOCTEXT("NewCameraButton", "New Camera"),
						LOCTEXT("NewCameraToolTip", "Create a Cine Camera Actor where the viewport is looking from, with a camera package ready to edit."),
						FOnClicked::CreateSP(this, &SUEcinePanel::OnNewCameraClicked))
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SUEcineCameraTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SUEcineGripTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SUEcineFocusTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SUEcineMonitorTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SUEcineShotsTab, Target.ToSharedRef())
				]
				+ SWidgetSwitcher::Slot()
				[
					SNew(SUEcineDeliverTab)
				]
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(AITab, SUEcineAITab, Target.ToSharedRef())
				]
			]
		]
	];
}

TSharedRef<SWidget> SUEcinePanel::MakeDepartmentTab(EUEcineDepartment Department, const FText& Label, const FText& ToolTip)
{
	return MakeToggleButton(Label, ToolTip,
		TAttribute<bool>::CreateSPLambda(this, [this, Department] { return ActiveDepartment == Department; }),
		[this, Department](bool) { ShowDepartment(Department); });
}

TSharedRef<SWidget> SUEcinePanel::MakeCameraMenu()
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
		LOCTEXT("NewCameraEntry", "New Camera"),
		LOCTEXT("NewCameraEntryToolTip", "Create a Cine Camera Actor where the viewport is looking from, with a camera package ready to edit."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
		FUIAction(FExecuteAction::CreateSPLambda(this, [this] { OnNewCameraClicked(); })));
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FText SUEcinePanel::GetCameraName() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	return Camera != nullptr ? FText::FromString(Camera->GetActorLabel()) : LOCTEXT("NoCameraName", "No camera");
}

bool SUEcinePanel::HasCamera() const
{
	return Target->GetCamera() != nullptr;
}

int32 SUEcinePanel::GetShownSlot() const
{
	// The monitor's tools work on any view, the shot list and deliveries are of the sequence in Sequencer, and picture jobs are of a file, so none of them waits for a camera.
	const bool bWorksWithoutCamera = ActiveDepartment == EUEcineDepartment::Monitor || ActiveDepartment == EUEcineDepartment::Shots || ActiveDepartment == EUEcineDepartment::Deliver || ActiveDepartment == EUEcineDepartment::AI;
	return HasCamera() || bWorksWithoutCamera ? 1 + static_cast<int32>(ActiveDepartment) : 0;
}

bool SUEcinePanel::IsLookingThrough() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	return Camera != nullptr && LevelEditor != nullptr && LevelEditor->GetPilotLevelActor() == Camera;
}

void SUEcinePanel::OnLookThroughChanged(bool bLookThrough)
{
	ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (LevelEditor == nullptr)
	{
		return;
	}
	if (bLookThrough)
	{
		LevelEditor->PilotLevelActor(Target->GetCamera());
	}
	else
	{
		LevelEditor->EjectPilotLevelActor();
	}
}

FReply SUEcinePanel::OnNewCameraClicked()
{
	if (ACineCameraActor* NewCamera = UEcine::CameraEditing::SpawnCameraAtViewport())
	{
		Target->SetCamera(NewCamera);
	}
	return FReply::Handled();
}

FReply SUEcinePanel::OnUserGuideClicked()
{
	FPlatformProcess::LaunchFileInDefaultExternalApplication(*FUEcineEditorModule::GetUserGuideFile());
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
