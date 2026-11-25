// Copyright 2026 Fiavaion. All rights reserved.

#include "SRoscCameraTab.h"

#include "CineCameraActor.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SRoscStepper.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscCameraTarget.h"
#include "RoscEditorSettings.h"
#include "RoscExposure.h"
#include "RoscOptics.h"
#include "RoscStyle.h"
#include "RoscUnits.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

using namespace Rosc::Widgets;

namespace
{
	using Rosc::Units::FormatNumber;

	FText FormatSensor(const FRoscRecordingFormat& Format)
	{
		const FText Size = FText::Format(LOCTEXT("FormatEntrySensorSize", "{0} × {1} mm"), FormatNumber(Format.SensorWidthMm, 2), FormatNumber(Format.SensorHeightMm, 2));
		if (Format.Resolution.X == 0)
		{
			return Size;
		}
		return FText::Format(LOCTEXT("SensorSizeAndResolution", "{0}  ·  {1} × {2}"), Size, FormatNumber(Format.Resolution.X, 0), FormatNumber(Format.Resolution.Y, 0));
	}

	FText FormatLensButton(const FRoscLens& Lens)
	{
		return Lens.IsZoom()
			? FText::Format(LOCTEXT("ZoomButton", "{0}-{1}"), FormatNumber(Lens.FocalLengthMm, 1), FormatNumber(Lens.MaxFocalLengthMm, 1))
			: FormatNumber(Lens.FocalLengthMm, 1);
	}

	TArray<double> ToArray(TConstArrayView<double> View)
	{
		return TArray<double>(View);
	}
}

void SRoscCameraTab::Construct(const FArguments& InArgs, const TSharedRef<FRoscCameraTarget>& InTarget)
{
	Target = InTarget;
	CurrentLayoutKey = MakeLayoutKey();
	RebuildLayout();
}

void SRoscCameraTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const FLayoutKey NewKey = MakeLayoutKey();
	if (!(NewKey == CurrentLayoutKey))
	{
		CurrentLayoutKey = NewKey;
		RebuildLayout();
		MeasureRebuiltContent(*this, AllottedGeometry);
	}
	RefreshResolvedCamera();
}

SRoscCameraTab::FLayoutKey SRoscCameraTab::MakeLayoutKey() const
{
	FLayoutKey Key;
	Key.Camera = Target->GetCamera();
	if (const URoscCameraPackageComponent* Component = GetComponent())
	{
		Key.bHasPackage = true;
		Key.LensKitId = Component->Package.LensKitId;
	}
	return Key;
}

void SRoscCameraTab::RebuildLayout()
{
	WarningsBox.Reset();
	bResolvedValid = false;
	bResolveCacheValid = false;

	// With no camera at all the panel shows its own invitation to make one.
	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	if (CurrentLayoutKey.Camera.IsValid())
	{
		Content = CurrentLayoutKey.bHasPackage ? BuildPackageEditor() : BuildNoPackageState();
	}

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(FRoscStyle::PanelPadding)
		[
			Content
		]
	];

	RefreshResolvedCamera();
}

void SRoscCameraTab::RefreshResolvedCamera()
{
	const URoscCameraPackageComponent* Component = GetComponent();
	if (Component == nullptr)
	{
		bResolvedValid = false;
		bResolveCacheValid = false;
		return;
	}
	if (bResolveCacheValid && FRoscCameraPackage::StaticStruct()->CompareScriptStruct(&Component->Package, &ResolvedFromPackage, 0))
	{
		return;
	}

	const FRoscCameraDatabase& Database = FRoscCameraDatabase::Get();
	ResolvedFromPackage = Component->Package;
	bResolveCacheValid = true;

	FText Error;
	bResolvedValid = Rosc::ResolveCameraPackage(Database, ResolvedFromPackage, Resolved, Error);

	const FRoscCameraBody* Body = Database.FindBody(ResolvedFromPackage.BodyId);
	ResolvedFormat = Body != nullptr ? Body->FindFormat(ResolvedFromPackage.FormatId) : nullptr;

	if (WarningsBox.IsValid())
	{
		WarningsBox->ClearChildren();
		TArray<FText> Messages = bResolvedValid ? Resolved.Warnings : TArray<FText>{ Error };
		for (const FText& Message : Messages)
		{
			WarningsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, FRoscStyle::SectionSpacing, 0.0f, 0.0f)
			[
				MakeWarning(Message)
			];
		}
	}
}

TSharedRef<SWidget> SRoscCameraTab::BuildNoPackageState()
{
	return MakeEmptyState(
		LOCTEXT("NoPackageMessage", "This camera has no camera package yet. Building one lets you choose a real camera body, recording format and lens for it. Its current focal length and aperture are kept as closely as the lens kit allows."),
		LOCTEXT("BuildPackageButton", "Build Camera Package"),
		LOCTEXT("BuildPackageToolTip", "Add a camera package to this camera. This can be undone."),
		FOnClicked::CreateSP(this, &SRoscCameraTab::OnBuildPackageClicked));
}

TSharedRef<SWidget> SRoscCameraTab::BuildPackageEditor()
{
	const auto MakeComboRow = [this](const FText& Label, const FText& LabelToolTip, TAttribute<FText> ValueText, TSharedRef<SWidget> (SRoscCameraTab::*MakeMenu)())
	{
		return MakeRow(Label, LabelToolTip, MakeComboButton(Label, LabelToolTip, MoveTemp(ValueText), FOnGetContent::CreateSP(this, MakeMenu)));
	};

	// The exposure controls all step along a ladder of standard values.
	const TSharedRef<FLadderControl> TStopControl = MakeShared<FLadderControl>();
	TStopControl->Label = LOCTEXT("TStopLabel", "T-stop");
	TStopControl->LabelToolTip = LOCTEXT("TStopToolTip", "Iris setting. A lower T-stop lets in more light and gives a shallower depth of field. The range is what the lens on the camera can do.");
	TStopControl->StepDownToolTip = LOCTEXT("TStopDown", "Open the iris a third of a stop");
	TStopControl->StepUpToolTip = LOCTEXT("TStopUp", "Close the iris a third of a stop");
	TStopControl->TransactionName = LOCTEXT("ChangeTStop", "Change T-stop");
	TStopControl->GetLadder = [this] { return bResolvedValid ? Rosc::Exposure::TStopLadder(Resolved.WidestTStop, Resolved.MinApertureTStop) : TArray<double>(); };
	TStopControl->GetValue = [](const FRoscCameraPackage& Package) { return static_cast<double>(Package.TStop); };
	TStopControl->SetValue = [](FRoscCameraPackage& Package, double Value) { Package.TStop = static_cast<float>(Value); };
	TStopControl->FormatValue = [](double Value) { return FText::Format(LOCTEXT("TStopValue", "T{0}"), FormatNumber(Value, 1)); };

	const TSharedRef<FLadderControl> IsoControl = MakeShared<FLadderControl>();
	IsoControl->Label = LOCTEXT("IsoLabel", "ISO");
	IsoControl->LabelToolTip = LOCTEXT("IsoToolTip", "Sensor sensitivity rating. It changes image brightness only when physical exposure is on.");
	IsoControl->StepDownToolTip = LOCTEXT("IsoDown", "Lower the ISO a third of a stop");
	IsoControl->StepUpToolTip = LOCTEXT("IsoUp", "Raise the ISO a third of a stop");
	IsoControl->TransactionName = LOCTEXT("ChangeIso", "Change ISO");
	IsoControl->GetLadder = [] { return ToArray(Rosc::Exposure::StandardIsos()); };
	IsoControl->GetValue = [](const FRoscCameraPackage& Package) { return static_cast<double>(Package.Iso); };
	IsoControl->SetValue = [](FRoscCameraPackage& Package, double Value) { Package.Iso = FMath::RoundToInt32(Value); };
	IsoControl->FormatValue = [](double Value) { return FormatNumber(Value, 0); };

	const TSharedRef<FLadderControl> ShutterControl = MakeShared<FLadderControl>();
	ShutterControl->Label = LOCTEXT("ShutterLabel", "Shutter");
	ShutterControl->LabelToolTip = LOCTEXT("ShutterToolTip", "Shutter angle. 180 degrees is the standard film look; a smaller angle gives crisper, more staccato motion, a larger one more motion blur.");
	ShutterControl->StepDownToolTip = LOCTEXT("ShutterDown", "Narrow the shutter angle: less motion blur");
	ShutterControl->StepUpToolTip = LOCTEXT("ShutterUp", "Widen the shutter angle: more motion blur");
	ShutterControl->TransactionName = LOCTEXT("ChangeShutter", "Change Shutter Angle");
	ShutterControl->GetLadder = [] { return ToArray(Rosc::Exposure::StandardShutterAngles()); };
	ShutterControl->GetValue = [](const FRoscCameraPackage& Package) { return static_cast<double>(Package.ShutterAngleDeg); };
	ShutterControl->SetValue = [](FRoscCameraPackage& Package, double Value) { Package.ShutterAngleDeg = static_cast<float>(Value); };
	ShutterControl->FormatValue = [](double Value) { return FText::Format(LOCTEXT("ShutterValue", "{0}°"), FormatNumber(Value, 2)); };

	const TSharedRef<FLadderControl> NdControl = MakeShared<FLadderControl>();
	NdControl->Label = LOCTEXT("NdLabel", "ND");
	NdControl->LabelToolTip = LOCTEXT("NdToolTip", "Neutral density filter in front of the lens. Each 0.3 cuts the light by one stop. It changes image brightness only when physical exposure is on.");
	NdControl->StepDownToolTip = LOCTEXT("NdDown", "One stop less ND");
	NdControl->StepUpToolTip = LOCTEXT("NdUp", "One stop more ND");
	NdControl->TransactionName = LOCTEXT("ChangeNd", "Change ND Filter");
	NdControl->GetLadder = [] { return ToArray(Rosc::Exposure::StandardNdDensities()); };
	NdControl->GetValue = [](const FRoscCameraPackage& Package) { return static_cast<double>(Package.NdDensity); };
	NdControl->SetValue = [](FRoscCameraPackage& Package, double Value) { Package.NdDensity = static_cast<float>(Value); };
	NdControl->FormatValue = [](double Value)
	{
		return Value < 0.01 ? LOCTEXT("NdClear", "Clear") : FText::Format(LOCTEXT("NdValue", "ND {0}"), FormatNumber(Value, 1));
	};

	const TSharedRef<FLadderControl> FrameRateControl = MakeShared<FLadderControl>();
	FrameRateControl->Label = LOCTEXT("FrameRateLabel", "Frame rate");
	FrameRateControl->LabelToolTip = LOCTEXT("FrameRateToolTip", "Frames per second the camera records. With the shutter angle it sets the exposure time.");
	FrameRateControl->StepDownToolTip = LOCTEXT("FrameRateDown", "Lower frame rate");
	FrameRateControl->StepUpToolTip = LOCTEXT("FrameRateUp", "Higher frame rate");
	FrameRateControl->TransactionName = LOCTEXT("ChangeFrameRate", "Change Frame Rate");
	FrameRateControl->GetLadder = [] { return ToArray(Rosc::Exposure::StandardFrameRates()); };
	FrameRateControl->GetValue = [](const FRoscCameraPackage& Package) { return static_cast<double>(Package.FramesPerSecond); };
	FrameRateControl->SetValue = [](FRoscCameraPackage& Package, double Value) { Package.FramesPerSecond = static_cast<float>(Value); };
	FrameRateControl->FormatValue = [](double Value) { return FText::Format(LOCTEXT("FrameRateValue", "{0} fps"), FormatNumber(Value, 3)); };

	FSpinBoxArgs Zoom;
	Zoom.Name = LOCTEXT("ZoomLabel", "Zoom");
	Zoom.ToolTip = LOCTEXT("ZoomToolTip", "Focal length of the zoom lens, in millimetres.");
	Zoom.TransactionName = LOCTEXT("ZoomTransaction", "Zoom");
	Zoom.MinValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [this] { return TOptional<float>(static_cast<float>(Resolved.MinFocalLengthMm)); });
	Zoom.MaxValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [this] { return TOptional<float>(static_cast<float>(Resolved.MaxFocalLengthMm)); });

	FSpinBoxArgs Compensation;
	Compensation.Name = LOCTEXT("CompensationLabel", "Offset");
	Compensation.ToolTip = LOCTEXT("CompensationToolTip", "Brightens (positive) or darkens the image in stops, on top of the physical exposure. Use it when the scene is not lit at real-world levels.");
	Compensation.TransactionName = LOCTEXT("CompensationTransaction", "Change Exposure Offset");
	Compensation.MinValue = TOptional<float>(-15.0f);
	Compensation.MaxValue = TOptional<float>(15.0f);
	Compensation.MinSliderValue = -6.0f;
	Compensation.MaxSliderValue = 6.0f;
	Compensation.Delta = 0.1f;

	return SNew(SVerticalBox)

		// Camera body and recording format
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSectionHeader(LOCTEXT("CameraSection", "Camera"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeComboRow(
				LOCTEXT("BodyLabel", "Body"),
				LOCTEXT("BodyToolTip", "The camera body this camera represents. It sets which recording formats are available."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					const FRoscCameraBody* Body = FRoscCameraDatabase::Get().FindBody(GetPackage().BodyId);
					return Body != nullptr ? FText::FromString(Body->GetDisplayName()) : LOCTEXT("UnknownBodyValue", "Unknown body");
				}),
				&SRoscCameraTab::MakeBodyMenu)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeComboRow(
				LOCTEXT("FormatLabel", "Format"),
				LOCTEXT("FormatToolTip", "The recording format: the part of the sensor the camera uses. A smaller area gives a tighter angle of view with the same lens."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return ResolvedFormat != nullptr ? FText::FromString(ResolvedFormat->Name) : LOCTEXT("UnknownFormatValue", "Unknown format");
				}),
				&SRoscCameraTab::MakeFormatMenu)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("SensorAreaLabel", "Sensor area"),
				LOCTEXT("SensorAreaToolTip", "Active sensor area of the recording format."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return ResolvedFormat != nullptr
						? FText::Format(LOCTEXT("SensorSize", "{0} × {1} mm"), FormatNumber(ResolvedFormat->SensorWidthMm, 2), FormatNumber(ResolvedFormat->SensorHeightMm, 2))
						: FText::GetEmpty();
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("ResolutionLabel", "Resolution"),
				LOCTEXT("ResolutionToolTip", "Photosites the recording format captures, across and down."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return ResolvedFormat != nullptr && ResolvedFormat->Resolution.X > 0
						? FText::Format(LOCTEXT("ResolutionValue", "{0} × {1}"), FormatNumber(ResolvedFormat->Resolution.X, 0), FormatNumber(ResolvedFormat->Resolution.Y, 0))
						: LOCTEXT("ResolutionFilm", "Film");
				}))
		]

		// Lens
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::SectionPadding)
		[
			MakeSectionHeader(LOCTEXT("LensSection", "Lens"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeComboRow(
				LOCTEXT("KitLabel", "Kit"),
				LOCTEXT("KitToolTip", "The lens kit on the truck. The lenses you can put on the camera are the ones in this kit."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					const FRoscLensKit* Kit = FRoscCameraDatabase::Get().FindLensKit(GetPackage().LensKitId);
					return Kit != nullptr ? FText::FromString(Kit->GetDisplayName()) : LOCTEXT("UnknownKitValue", "Unknown lens kit");
				}),
				&SRoscCameraTab::MakeLensKitMenu)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, FRoscStyle::RowSpacing + 2.0f, 0.0f, 0.0f)
		[
			BuildLensButtons()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			SNew(SBox)
			.Visibility_Lambda([this] { return bResolvedValid && Resolved.MaxFocalLengthMm > Resolved.MinFocalLengthMm ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeRow(Zoom.Name, Zoom.ToolTip, MakePackageSpinBox(Zoom,
					[](const FRoscCameraPackage& Package) { return Package.FocalLengthMm; },
					[](FRoscCameraPackage& Package, float Value) { Package.FocalLengthMm = Value; }))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("AngleOfViewLabel", "Angle of view"),
				LOCTEXT("AngleOfViewToolTip", "Horizontal and vertical angle of view of this lens on this recording format."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					if (!bResolvedValid)
					{
						return FText::GetEmpty();
					}
					return FText::Format(LOCTEXT("AngleOfViewValue", "{0}° × {1}°"),
						FormatNumber(Rosc::Optics::HorizontalFieldOfViewDeg(Resolved.SensorWidthMm, Resolved.FocalLengthMm, Resolved.Squeeze), 1),
						FormatNumber(Rosc::Optics::VerticalFieldOfViewDeg(Resolved.SensorHeightMm, Resolved.FocalLengthMm), 1));
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("EquivalentLabel", "Full-frame equivalent"),
				LOCTEXT("EquivalentToolTip", "The focal length that frames the same width on a full-frame (36 mm wide) sensor."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					if (!bResolvedValid)
					{
						return FText::GetEmpty();
					}
					return FText::Format(LOCTEXT("MillimetresValue", "{0} mm"),
						FormatNumber(Rosc::Optics::EquivalentFocalLengthMm(Resolved.FocalLengthMm, Resolved.SensorWidthMm, Resolved.Squeeze), 0));
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("ImageAspectLabel", "Image aspect"),
				LOCTEXT("ImageAspectToolTip", "Shape of the image after any anamorphic squeeze is removed."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return bResolvedValid
						? FText::Format(LOCTEXT("AspectValue", "{0}:1"), FormatNumber(Resolved.SensorWidthMm * Resolved.Squeeze / Resolved.SensorHeightMm, 2))
						: FText::GetEmpty();
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("CloseFocusLabel", "Close focus"),
				LOCTEXT("CloseFocusToolTip", "The nearest distance this lens can focus at."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return bResolvedValid ? Rosc::Units::FormatDistance(Resolved.CloseFocusMm, URoscEditorSettings::GetDistanceUnits()) : FText::GetEmpty();
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("HyperfocalLabel", "Hyperfocal"),
				LOCTEXT("HyperfocalToolTip", "Focus here and everything from half this distance to infinity is acceptably sharp at the current T-stop."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					if (!bResolvedValid)
					{
						return FText::GetEmpty();
					}
					const double CircleOfConfusion = Rosc::Optics::DefaultCircleOfConfusionMm(Resolved.SensorWidthMm, Resolved.SensorHeightMm);
					return Rosc::Units::FormatDistance(Rosc::Optics::HyperfocalDistanceMm(Resolved.FocalLengthMm, Resolved.TStop, CircleOfConfusion), URoscEditorSettings::GetDistanceUnits());
				}))
		]

		// Exposure
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::SectionPadding)
		[
			MakeSectionHeader(LOCTEXT("ExposureSection", "Exposure"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)[ BuildLadderRow(TStopControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)[ BuildLadderRow(IsoControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)[ BuildLadderRow(ShutterControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)[ BuildLadderRow(NdControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)[ BuildLadderRow(FrameRateControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("ShutterTimeLabel", "Exposure time"),
				LOCTEXT("ShutterTimeToolTip", "How long each frame is exposed, from the shutter angle and frame rate."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return bResolvedValid ? FText::Format(LOCTEXT("ShutterTimeValue", "1/{0} s"), FormatNumber(Resolved.ShutterSpeedReciprocal, 1)) : FText::GetEmpty();
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("ExposesForLabel", "Exposes for"),
				LOCTEXT("ExposesForToolTip", "The scene brightness these settings suit, as an exposure value at ISO 100, including the ND filter. A sunny exterior is about EV 15, a lit interior about EV 7."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					if (!bResolvedValid)
					{
						return FText::GetEmpty();
					}
					const double ExposureValue = Rosc::Exposure::EV100(Resolved.TStop, 1.0 / Resolved.ShutterSpeedReciprocal, Resolved.Iso)
						+ Rosc::Exposure::NdStops(ResolvedFromPackage.NdDensity);
					return FText::Format(LOCTEXT("ExposureValueValue", "EV {0}"), FormatNumber(ExposureValue, 1));
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, FRoscStyle::RowSpacing + 4.0f, 0.0f, 0.0f)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("PhysicalExposureToolTip", "When on, ISO, shutter, T-stop and ND set how bright the image is, as on a real camera, and the scene must be lit at real-world light levels. When off, they affect only depth of field and motion blur, and the scene keeps its own exposure."))
			.IsChecked_Lambda([this] { return GetPackage().bPhysicalExposure ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
			{
				const bool bEnable = NewState == ECheckBoxState::Checked;
				Edit(LOCTEXT("TogglePhysicalExposure", "Toggle Physical Exposure"), [bEnable](FRoscCameraPackage& Package) { Package.bPhysicalExposure = bEnable; });
			})
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("Rosc.Label"))
				.Text(LOCTEXT("PhysicalExposureLabel", "Physical exposure"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			SNew(SBox)
			.IsEnabled_Lambda([this] { return GetPackage().bPhysicalExposure; })
			[
				MakeRow(Compensation.Name, Compensation.ToolTip, MakePackageSpinBox(Compensation,
					[](const FRoscCameraPackage& Package) { return Package.ExposureCompensationStops; },
					[](FRoscCameraPackage& Package, float Value) { Package.ExposureCompensationStops = Value; }))
			]
		]

		// Warnings from the resolved package
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(WarningsBox, SVerticalBox)
		];
}

TSharedRef<SWidget> SRoscCameraTab::BuildLensButtons()
{
	const TSharedRef<SWrapBox> Buttons = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(4.0, 4.0));

	const FRoscLensKit* Kit = FRoscCameraDatabase::Get().FindLensKit(GetPackage().LensKitId);
	if (Kit == nullptr)
	{
		return Buttons;
	}

	for (int32 LensIndex = 0; LensIndex < Kit->Lenses.Num(); ++LensIndex)
	{
		const FRoscLens& Lens = Kit->Lenses[LensIndex];
		const FText ToolTip = FText::Format(LOCTEXT("LensButtonToolTip", "{0}. Close focus {1}."),
			FText::FromString(Lens.GetDisplayName()),
			Rosc::Units::FormatDistance(Lens.CloseFocusM * 1000.0, URoscEditorSettings::GetDistanceUnits()));

		Buttons->AddSlot()
		[
			MakeToggleButton(FormatLensButton(Lens), ToolTip,
				TAttribute<bool>::CreateSPLambda(this, [this, LensIndex] { return GetPackage().LensIndex == LensIndex; }),
				[this, Kit, LensIndex](bool)
				{
					Edit(LOCTEXT("ChangeLens", "Change Lens"), [Kit, LensIndex](FRoscCameraPackage& Package) { Package.SelectLens(*Kit, LensIndex); });
				})
		];
	}
	return Buttons;
}

TSharedRef<SWidget> SRoscCameraTab::BuildLadderRow(const TSharedRef<FLadderControl>& Control)
{
	return MakeRow(Control->Label, Control->LabelToolTip,
		SNew(SRoscStepper)
		.Label(Control->Label)
		.StepDownToolTip(Control->StepDownToolTip)
		.StepUpToolTip(Control->StepUpToolTip)
		.ValueText_Lambda([this, Control] { return Control->FormatValue(Control->GetValue(GetPackage())); })
		.CanStep_Lambda([this, Control](int32 Direction)
		{
			return Rosc::Exposure::StepOnLadder(Control->GetLadder(), Control->GetValue(GetPackage()), Direction).IsSet();
		})
		.OnStep_Lambda([this, Control](int32 Direction)
		{
			const TOptional<double> NewValue = Rosc::Exposure::StepOnLadder(Control->GetLadder(), Control->GetValue(GetPackage()), Direction);
			if (NewValue.IsSet())
			{
				Edit(Control->TransactionName, [&Control, &NewValue](FRoscCameraPackage& Package) { Control->SetValue(Package, NewValue.GetValue()); });
			}
		})
		.OnGetMenuContent(this, &SRoscCameraTab::MakeLadderMenu, Control));
}

TSharedRef<SWidget> SRoscCameraTab::MakeBodyMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	FString OpenManufacturer;
	bool bSectionOpen = false;

	for (const FRoscCameraBody& Body : FRoscCameraDatabase::Get().GetBodies())
	{
		if (!bSectionOpen || Body.Manufacturer != OpenManufacturer)
		{
			if (bSectionOpen)
			{
				MenuBuilder.EndSection();
			}
			MenuBuilder.BeginSection(NAME_None, FText::FromString(Body.Manufacturer));
			OpenManufacturer = Body.Manufacturer;
			bSectionOpen = true;
		}

		const FRoscCameraBody* BodyPtr = &Body;
		MenuBuilder.AddMenuEntry(
			FText::FromString(Body.Model),
			FText::Format(LOCTEXT("BodyEntryToolTip", "{0}, {1} recording formats"), FText::FromString(Body.GetDisplayName()), FText::AsNumber(Body.Formats.Num())),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSPLambda(this, [this, BodyPtr]
				{
					Edit(LOCTEXT("ChangeBody", "Change Camera Body"), [BodyPtr](FRoscCameraPackage& Package) { Package.SelectBody(*BodyPtr); });
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateSPLambda(this, [this, BodyPtr] { return GetPackage().BodyId == BodyPtr->Id; })),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	if (bSectionOpen)
	{
		MenuBuilder.EndSection();
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SRoscCameraTab::MakeFormatMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	if (const FRoscCameraBody* Body = FRoscCameraDatabase::Get().FindBody(GetPackage().BodyId))
	{
		for (const FRoscRecordingFormat& Format : Body->Formats)
		{
			const FString FormatId = Format.Id;
			MenuBuilder.AddMenuEntry(
				FText::FromString(Format.Name),
				FormatSensor(Format),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSPLambda(this, [this, FormatId]
					{
						Edit(LOCTEXT("ChangeFormat", "Change Recording Format"), [&FormatId](FRoscCameraPackage& Package) { Package.FormatId = FormatId; });
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateSPLambda(this, [this, FormatId] { return GetPackage().FormatId == FormatId; })),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SRoscCameraTab::MakeLensKitMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	const auto AddKits = [this, &MenuBuilder](bool bAnamorphic, const FText& SectionTitle)
	{
		MenuBuilder.BeginSection(NAME_None, SectionTitle);
		for (const FRoscLensKit& Kit : FRoscCameraDatabase::Get().GetLensKits())
		{
			if (Kit.IsAnamorphic() != bAnamorphic)
			{
				continue;
			}
			const FRoscLensKit* KitPtr = &Kit;
			const FText Coverage = ResolvedFormat != nullptr && !Kit.CoversFormat(*ResolvedFormat)
				? LOCTEXT("KitDoesNotCover", " Does not cover the current recording format.")
				: FText::GetEmpty();
			MenuBuilder.AddMenuEntry(
				FText::FromString(Kit.GetDisplayName()),
				FText::Format(LOCTEXT("KitEntryToolTip", "{0} lenses, {1} mm image circle.{2}"), FText::AsNumber(Kit.Lenses.Num()), FormatNumber(Kit.ImageCircleMm, 1), Coverage),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSPLambda(this, [this, KitPtr]
					{
						Edit(LOCTEXT("ChangeKit", "Change Lens Kit"), [KitPtr](FRoscCameraPackage& Package) { Package.SelectLensKit(*KitPtr); });
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateSPLambda(this, [this, KitPtr] { return GetPackage().LensKitId == KitPtr->Id; })),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
		MenuBuilder.EndSection();
	};

	AddKits(false, LOCTEXT("SphericalSection", "Spherical"));
	AddKits(true, LOCTEXT("AnamorphicSection", "Anamorphic"));
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SRoscCameraTab::MakeLadderMenu(TSharedRef<FLadderControl> Control)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	for (const double Value : Control->GetLadder())
	{
		MenuBuilder.AddMenuEntry(
			Control->FormatValue(Value),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSPLambda(this, [this, Control, Value]
				{
					Edit(Control->TransactionName, [&Control, Value](FRoscCameraPackage& Package) { Control->SetValue(Package, Value); });
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateSPLambda(this, [this, Control, Value]
				{
					return Rosc::Exposure::IsSameSetting(Control->GetValue(GetPackage()), Value);
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	return MenuBuilder.MakeWidget();
}

URoscCameraPackageComponent* SRoscCameraTab::GetComponent() const
{
	return Target->GetPackageComponent();
}

const FRoscCameraPackage& SRoscCameraTab::GetPackage() const
{
	static const FRoscCameraPackage EmptyPackage;
	const URoscCameraPackageComponent* Component = GetComponent();
	return Component != nullptr ? Component->Package : EmptyPackage;
}

void SRoscCameraTab::Edit(const FText& Description, TFunctionRef<void(FRoscCameraPackage&)> EditFunction)
{
	FText Error;
	FText Notice;
	if (!Rosc::CameraEditing::EditPackage(GetComponent(), Description, EditFunction, Error, &Notice))
	{
		Notify(Error, false);
	}
	else if (!Notice.IsEmpty())
	{
		Notify(Notice, false);
	}
}

void SRoscCameraTab::DragEdit(TFunctionRef<void(FRoscCameraPackage&)> EditFunction)
{
	if (URoscCameraPackageComponent* Component = GetComponent())
	{
		EditFunction(Component->Package);
		FText Error;
		Component->ApplyToCamera(Error);
		GEditor->RedrawLevelEditingViewports();
	}
}

TSharedRef<SWidget> SRoscCameraTab::MakePackageSpinBox(FSpinBoxArgs Args, TFunction<float(const FRoscCameraPackage&)> GetValue, TFunction<void(FRoscCameraPackage&, float)> SetValue)
{
	Args.Value = TAttribute<float>::CreateSPLambda(this, [this, GetValue = MoveTemp(GetValue)] { return GetValue(GetPackage()); });
	Args.SaveForUndo = [this]
	{
		if (URoscCameraPackageComponent* Component = GetComponent())
		{
			Component->Modify();
		}
	};
	Args.OnDrag = [this, SetValue](float NewValue)
	{
		DragEdit([&SetValue, NewValue](FRoscCameraPackage& Package) { SetValue(Package, NewValue); });
	};
	Args.OnCommit = [this, SetValue, Description = Args.TransactionName](float NewValue)
	{
		Edit(Description, [&SetValue, NewValue](FRoscCameraPackage& Package) { SetValue(Package, NewValue); });
	};
	return MakeSpinBox(MoveTemp(Args));
}

FReply SRoscCameraTab::OnBuildPackageClicked()
{
	Rosc::CameraEditing::AddCameraPackage(Target->GetCamera());
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
