#include "SUEcinePanel.h"

#include "CineCameraActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditorSubsystem.h"
#include "SUEcineCameraTab.h"
#include "SUEcineFocusTab.h"
#include "SUEcineGripTab.h"
#include "SUEcineMonitorTab.h"
#include "Styling/AppStyle.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraTarget.h"
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
	const FText LookThroughToolTip = LOCTEXT("LookThroughToolTip", "Look through this camera in the viewport. Moving the viewport then moves the camera.");
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
				// The monitor's tools work on any view, so its tab does not wait for a camera.
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]
				{
					return HasCamera() || ActiveDepartment == EUEcineDepartment::Monitor ? 1 + static_cast<int32>(ActiveDepartment) : 0;
				})
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

#undef LOCTEXT_NAMESPACE
