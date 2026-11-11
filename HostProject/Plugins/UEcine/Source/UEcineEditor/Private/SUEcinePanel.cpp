#include "SUEcinePanel.h"

#include "CineCameraActor.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditorSubsystem.h"
#include "SUEcineCameraTab.h"
#include "Styling/AppStyle.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraTarget.h"
#include "UEcineStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

void SUEcinePanel::Construct(const FArguments& InArgs)
{
	Target = MakeShared<FUEcineCameraTarget>();

	const FText CameraToolTip = LOCTEXT("CameraPickerToolTip", "The camera this panel is working on. Selecting a camera in the level also switches to it.");
	const FText LookThroughToolTip = LOCTEXT("LookThroughToolTip", "Look through this camera in the viewport. Moving the viewport then moves the camera.");
	const FText NewCameraToolTip = LOCTEXT("PanelNewCameraToolTip", "New camera: create a Cine Camera Actor where the viewport is looking from, with a camera package ready to edit.");

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
					SNew(SBox)
					.MinDesiredHeight(FUEcineStyle::MinTargetSize)
					[
						SNew(SCheckBox)
						.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("DetailsView.SectionButton"))
						.HAlign(HAlign_Center)
						.ToolTipText(LookThroughToolTip)
						.IsEnabled(this, &SUEcinePanel::HasCamera)
						.IsChecked(this, &SUEcinePanel::IsLookingThrough)
						.OnCheckStateChanged(this, &SUEcinePanel::OnLookThroughChanged)
						[
							SNew(STextBlock)
							.TextStyle(&FUEcineStyle::Get().GetWidgetStyle<FTextBlockStyle>("UEcine.Label"))
							.ColorAndOpacity(FSlateColor::UseForeground())
							.Text(LOCTEXT("LookThroughLabel", "Look Through"))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(FUEcineStyle::MinTargetSize + 4.0f)
					.HeightOverride(FUEcineStyle::MinTargetSize + 4.0f)
					[
						SNew(SButton)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
						.ContentPadding(0.0f)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.ToolTipText(NewCameraToolTip)
						.AccessibleText(NewCameraToolTip)
						.OnClicked(this, &SUEcinePanel::OnNewCameraClicked)
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.Plus"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
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
				SNew(SUEcineCameraTab, Target.ToSharedRef())
			]
		]
	];
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

ECheckBoxState SUEcinePanel::IsLookingThrough() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	return Camera != nullptr && LevelEditor != nullptr && LevelEditor->GetPilotLevelActor() == Camera
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SUEcinePanel::OnLookThroughChanged(ECheckBoxState NewState)
{
	ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (LevelEditor == nullptr)
	{
		return;
	}
	if (NewState == ECheckBoxState::Checked)
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
