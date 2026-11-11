#include "SUEcineCameraTab.h"

#include "CineCameraActor.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SUEcineStepper.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineCameraTarget.h"
#include "UEcineEditorSettings.h"
#include "UEcineExposure.h"
#include "UEcineOptics.h"
#include "UEcineStyle.h"
#include "UEcineUnits.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

using namespace UEcine::Widgets;

namespace
{
	using UEcine::Units::FormatNumber;

	FText FormatSensor(const FUEcineRecordingFormat& Format)
	{
		const FText Size = FText::Format(LOCTEXT("FormatEntrySensorSize", "{0} × {1} mm"), FormatNumber(Format.SensorWidthMm, 2), FormatNumber(Format.SensorHeightMm, 2));
		if (Format.Resolution.X == 0)
		{
			return Size;
		}
		return FText::Format(LOCTEXT("SensorSizeAndResolution", "{0}  ·  {1} × {2}"), Size, FormatNumber(Format.Resolution.X, 0), FormatNumber(Format.Resolution.Y, 0));
	}

	FText FormatLensButton(const FUEcineLens& Lens)
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

void SUEcineCameraTab::Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget)
{
	Target = InTarget;
	CurrentLayoutKey = MakeLayoutKey();
	RebuildLayout();
}

void SUEcineCameraTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const FLayoutKey NewKey = MakeLayoutKey();
	if (!(NewKey == CurrentLayoutKey))
	{
		CurrentLayoutKey = NewKey;
		RebuildLayout();
	}
	RefreshResolvedCamera();
}

SUEcineCameraTab::FLayoutKey SUEcineCameraTab::MakeLayoutKey() const
{
	FLayoutKey Key;
	Key.Camera = Target->GetCamera();
	if (const UUEcineCameraPackageComponent* Component = GetComponent())
	{
		Key.bHasPackage = true;
		Key.LensKitId = Component->Package.LensKitId;
	}
	return Key;
}

void SUEcineCameraTab::RebuildLayout()
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
		.Padding(FUEcineStyle::PanelPadding)
		[
			Content
		]
	];

	RefreshResolvedCamera();
}

void SUEcineCameraTab::RefreshResolvedCamera()
{
	const UUEcineCameraPackageComponent* Component = GetComponent();
	if (Component == nullptr)
	{
		bResolvedValid = false;
		bResolveCacheValid = false;
		return;
	}
	if (bResolveCacheValid && FUEcineCameraPackage::StaticStruct()->CompareScriptStruct(&Component->Package, &ResolvedFromPackage, 0))
	{
		return;
	}

	const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();
	ResolvedFromPackage = Component->Package;
	bResolveCacheValid = true;

	FText Error;
	bResolvedValid = UEcine::ResolveCameraPackage(Database, ResolvedFromPackage, Resolved, Error);

	const FUEcineCameraBody* Body = Database.FindBody(ResolvedFromPackage.BodyId);
	ResolvedFormat = Body != nullptr ? Body->FindFormat(ResolvedFromPackage.FormatId) : nullptr;

	if (WarningsBox.IsValid())
	{
		WarningsBox->ClearChildren();
		TArray<FText> Messages = bResolvedValid ? Resolved.Warnings : TArray<FText>{ Error };
		for (const FText& Message : Messages)
		{
			WarningsBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, FUEcineStyle::SectionSpacing, 0.0f, 0.0f)
			[
				MakeWarning(FText::Format(LOCTEXT("WarningPrefix", "Warning: {0}"), Message))
			];
		}
	}
}

TSharedRef<SWidget> SUEcineCameraTab::BuildNoPackageState()
{
	return MakeEmptyState(
		LOCTEXT("NoPackageMessage", "This camera has no camera package yet. Building one lets you choose a real camera body, recording format and lens for it. Its current focal length and aperture are kept as closely as the lens kit allows."),
		LOCTEXT("BuildPackageButton", "Build Camera Package"),
		LOCTEXT("BuildPackageToolTip", "Add a camera package to this camera. This can be undone."),
		FOnClicked::CreateSP(this, &SUEcineCameraTab::OnBuildPackageClicked));
}

TSharedRef<SWidget> SUEcineCameraTab::BuildPackageEditor()
{
	const auto MakeComboRow = [this](const FText& Label, const FText& LabelToolTip, TAttribute<FText> ValueText, TSharedRef<SWidget> (SUEcineCameraTab::*MakeMenu)())
	{
		return MakeRow(Label, LabelToolTip,
			SNew(SBox)
			.MinDesiredHeight(FUEcineStyle::MinTargetSize)
			[
				SNew(SComboButton)
				.ToolTipText(LabelToolTip)
				.AccessibleText(Label)
				.OnGetMenuContent(this, MakeMenu)
				.ButtonContent()
				[
					SNew(STextBlock)
					.TextStyle(&TextStyle("UEcine.Value"))
					.Text(MoveTemp(ValueText))
				]
			]);
	};

	// The exposure controls all step along a ladder of standard values.
	const TSharedRef<FLadderControl> TStopControl = MakeShared<FLadderControl>();
	TStopControl->Label = LOCTEXT("TStopLabel", "T-stop");
	TStopControl->LabelToolTip = LOCTEXT("TStopToolTip", "Iris setting. A lower T-stop lets in more light and gives a shallower depth of field. The range is what the lens on the camera can do.");
	TStopControl->StepDownToolTip = LOCTEXT("TStopDown", "Open the iris a third of a stop");
	TStopControl->StepUpToolTip = LOCTEXT("TStopUp", "Close the iris a third of a stop");
	TStopControl->TransactionName = LOCTEXT("ChangeTStop", "Change T-stop");
	TStopControl->GetLadder = [this] { return bResolvedValid ? UEcine::Exposure::TStopLadder(Resolved.WidestTStop, Resolved.MinApertureTStop) : TArray<double>(); };
	TStopControl->GetValue = [](const FUEcineCameraPackage& Package) { return static_cast<double>(Package.TStop); };
	TStopControl->SetValue = [](FUEcineCameraPackage& Package, double Value) { Package.TStop = static_cast<float>(Value); };
	TStopControl->FormatValue = [](double Value) { return FText::Format(LOCTEXT("TStopValue", "T{0}"), FormatNumber(Value, 1)); };

	const TSharedRef<FLadderControl> IsoControl = MakeShared<FLadderControl>();
	IsoControl->Label = LOCTEXT("IsoLabel", "ISO");
	IsoControl->LabelToolTip = LOCTEXT("IsoToolTip", "Sensor sensitivity rating. It changes image brightness only when physical exposure is on.");
	IsoControl->StepDownToolTip = LOCTEXT("IsoDown", "Lower the ISO a third of a stop");
	IsoControl->StepUpToolTip = LOCTEXT("IsoUp", "Raise the ISO a third of a stop");
	IsoControl->TransactionName = LOCTEXT("ChangeIso", "Change ISO");
	IsoControl->GetLadder = [] { return ToArray(UEcine::Exposure::StandardIsos()); };
	IsoControl->GetValue = [](const FUEcineCameraPackage& Package) { return static_cast<double>(Package.Iso); };
	IsoControl->SetValue = [](FUEcineCameraPackage& Package, double Value) { Package.Iso = FMath::RoundToInt32(Value); };
	IsoControl->FormatValue = [](double Value) { return FormatNumber(Value, 0); };

	const TSharedRef<FLadderControl> ShutterControl = MakeShared<FLadderControl>();
	ShutterControl->Label = LOCTEXT("ShutterLabel", "Shutter");
	ShutterControl->LabelToolTip = LOCTEXT("ShutterToolTip", "Shutter angle. 180 degrees is the standard film look; a smaller angle gives crisper, more staccato motion, a larger one more motion blur.");
	ShutterControl->StepDownToolTip = LOCTEXT("ShutterDown", "Narrow the shutter angle: less motion blur");
	ShutterControl->StepUpToolTip = LOCTEXT("ShutterUp", "Widen the shutter angle: more motion blur");
	ShutterControl->TransactionName = LOCTEXT("ChangeShutter", "Change Shutter Angle");
	ShutterControl->GetLadder = [] { return ToArray(UEcine::Exposure::StandardShutterAngles()); };
	ShutterControl->GetValue = [](const FUEcineCameraPackage& Package) { return static_cast<double>(Package.ShutterAngleDeg); };
	ShutterControl->SetValue = [](FUEcineCameraPackage& Package, double Value) { Package.ShutterAngleDeg = static_cast<float>(Value); };
	ShutterControl->FormatValue = [](double Value) { return FText::Format(LOCTEXT("ShutterValue", "{0}°"), FormatNumber(Value, 2)); };

	const TSharedRef<FLadderControl> NdControl = MakeShared<FLadderControl>();
	NdControl->Label = LOCTEXT("NdLabel", "ND");
	NdControl->LabelToolTip = LOCTEXT("NdToolTip", "Neutral density filter in front of the lens. Each 0.3 cuts the light by one stop. It changes image brightness only when physical exposure is on.");
	NdControl->StepDownToolTip = LOCTEXT("NdDown", "One stop less ND");
	NdControl->StepUpToolTip = LOCTEXT("NdUp", "One stop more ND");
	NdControl->TransactionName = LOCTEXT("ChangeNd", "Change ND Filter");
	NdControl->GetLadder = [] { return ToArray(UEcine::Exposure::StandardNdDensities()); };
	NdControl->GetValue = [](const FUEcineCameraPackage& Package) { return static_cast<double>(Package.NdDensity); };
	NdControl->SetValue = [](FUEcineCameraPackage& Package, double Value) { Package.NdDensity = static_cast<float>(Value); };
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
	FrameRateControl->GetLadder = [] { return ToArray(UEcine::Exposure::StandardFrameRates()); };
	FrameRateControl->GetValue = [](const FUEcineCameraPackage& Package) { return static_cast<double>(Package.FramesPerSecond); };
	FrameRateControl->SetValue = [](FUEcineCameraPackage& Package, double Value) { Package.FramesPerSecond = static_cast<float>(Value); };
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

	const FMargin RowPadding(0.0f, FUEcineStyle::RowSpacing, 0.0f, 0.0f);
	const FMargin SectionPadding(0.0f, FUEcineStyle::SectionSpacing, 0.0f, 0.0f);

	return SNew(SVerticalBox)

		// Camera body and recording format
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSectionHeader(LOCTEXT("CameraSection", "Camera"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
		[
			MakeComboRow(
				LOCTEXT("BodyLabel", "Body"),
				LOCTEXT("BodyToolTip", "The camera body this camera represents. It sets which recording formats are available."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					const FUEcineCameraBody* Body = FUEcineCameraDatabase::Get().FindBody(GetPackage().BodyId);
					return Body != nullptr ? FText::FromString(Body->GetDisplayName()) : LOCTEXT("UnknownBodyValue", "Unknown body");
				}),
				&SUEcineCameraTab::MakeBodyMenu)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
		[
			MakeComboRow(
				LOCTEXT("FormatLabel", "Format"),
				LOCTEXT("FormatToolTip", "The recording format: the part of the sensor the camera uses. A smaller area gives a tighter angle of view with the same lens."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return ResolvedFormat != nullptr ? FText::FromString(ResolvedFormat->Name) : LOCTEXT("UnknownFormatValue", "Unknown format");
				}),
				&SUEcineCameraTab::MakeFormatMenu)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
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
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
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
		+ SVerticalBox::Slot().AutoHeight().Padding(SectionPadding)
		[
			MakeSectionHeader(LOCTEXT("LensSection", "Lens"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
		[
			MakeComboRow(
				LOCTEXT("KitLabel", "Kit"),
				LOCTEXT("KitToolTip", "The lens kit on the truck. The lenses you can put on the camera are the ones in this kit."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					const FUEcineLensKit* Kit = FUEcineCameraDatabase::Get().FindLensKit(GetPackage().LensKitId);
					return Kit != nullptr ? FText::FromString(Kit->GetDisplayName()) : LOCTEXT("UnknownKitValue", "Unknown lens kit");
				}),
				&SUEcineCameraTab::MakeLensKitMenu)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, FUEcineStyle::RowSpacing + 2.0f, 0.0f, 0.0f)
		[
			BuildLensButtons()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
		[
			SNew(SBox)
			.Visibility_Lambda([this] { return bResolvedValid && Resolved.MaxFocalLengthMm > Resolved.MinFocalLengthMm ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeRow(Zoom.Name, Zoom.ToolTip, MakePackageSpinBox(Zoom,
					[](const FUEcineCameraPackage& Package) { return Package.FocalLengthMm; },
					[](FUEcineCameraPackage& Package, float Value) { Package.FocalLengthMm = Value; }))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
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
						FormatNumber(UEcine::Optics::HorizontalFieldOfViewDeg(Resolved.SensorWidthMm, Resolved.FocalLengthMm, Resolved.Squeeze), 1),
						FormatNumber(UEcine::Optics::VerticalFieldOfViewDeg(Resolved.SensorHeightMm, Resolved.FocalLengthMm), 1));
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
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
						FormatNumber(UEcine::Optics::EquivalentFocalLengthMm(Resolved.FocalLengthMm, Resolved.SensorWidthMm, Resolved.Squeeze), 0));
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
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
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("CloseFocusLabel", "Close focus"),
				LOCTEXT("CloseFocusToolTip", "The nearest distance this lens can focus at."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return bResolvedValid ? UEcine::Units::FormatDistance(Resolved.CloseFocusMm, UUEcineEditorSettings::GetDistanceUnits()) : FText::GetEmpty();
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
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
					const double CircleOfConfusion = UEcine::Optics::DefaultCircleOfConfusionMm(Resolved.SensorWidthMm, Resolved.SensorHeightMm);
					return UEcine::Units::FormatDistance(UEcine::Optics::HyperfocalDistanceMm(Resolved.FocalLengthMm, Resolved.TStop, CircleOfConfusion), UUEcineEditorSettings::GetDistanceUnits());
				}))
		]

		// Exposure
		+ SVerticalBox::Slot().AutoHeight().Padding(SectionPadding)
		[
			MakeSectionHeader(LOCTEXT("ExposureSection", "Exposure"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)[ BuildLadderRow(TStopControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)[ BuildLadderRow(IsoControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)[ BuildLadderRow(ShutterControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)[ BuildLadderRow(NdControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)[ BuildLadderRow(FrameRateControl) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
		[
			MakeReadoutRow(
				LOCTEXT("ShutterTimeLabel", "Exposure time"),
				LOCTEXT("ShutterTimeToolTip", "How long each frame is exposed, from the shutter angle and frame rate."),
				TAttribute<FText>::CreateSPLambda(this, [this]
				{
					return bResolvedValid ? FText::Format(LOCTEXT("ShutterTimeValue", "1/{0} s"), FormatNumber(Resolved.ShutterSpeedReciprocal, 1)) : FText::GetEmpty();
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
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
					const double ExposureValue = UEcine::Exposure::EV100(Resolved.TStop, 1.0 / Resolved.ShutterSpeedReciprocal, Resolved.Iso)
						+ UEcine::Exposure::NdStops(ResolvedFromPackage.NdDensity);
					return FText::Format(LOCTEXT("ExposureValueValue", "EV {0}"), FormatNumber(ExposureValue, 1));
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, FUEcineStyle::RowSpacing + 4.0f, 0.0f, 0.0f)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("PhysicalExposureToolTip", "When on, ISO, shutter, T-stop and ND set how bright the image is, as on a real camera, and the scene must be lit at real-world light levels. When off, they affect only depth of field and motion blur, and the scene keeps its own exposure."))
			.IsChecked_Lambda([this] { return GetPackage().bPhysicalExposure ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
			{
				const bool bEnable = NewState == ECheckBoxState::Checked;
				Edit(LOCTEXT("TogglePhysicalExposure", "Toggle Physical Exposure"), [bEnable](FUEcineCameraPackage& Package) { Package.bPhysicalExposure = bEnable; });
			})
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("UEcine.Label"))
				.Text(LOCTEXT("PhysicalExposureLabel", "Physical exposure"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPadding)
		[
			SNew(SBox)
			.IsEnabled_Lambda([this] { return GetPackage().bPhysicalExposure; })
			[
				MakeRow(Compensation.Name, Compensation.ToolTip, MakePackageSpinBox(Compensation,
					[](const FUEcineCameraPackage& Package) { return Package.ExposureCompensationStops; },
					[](FUEcineCameraPackage& Package, float Value) { Package.ExposureCompensationStops = Value; }))
			]
		]

		// Warnings from the resolved package
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(WarningsBox, SVerticalBox)
		];
}

TSharedRef<SWidget> SUEcineCameraTab::BuildLensButtons()
{
	const TSharedRef<SWrapBox> Buttons = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(4.0, 4.0));

	const FUEcineLensKit* Kit = FUEcineCameraDatabase::Get().FindLensKit(GetPackage().LensKitId);
	if (Kit == nullptr)
	{
		return Buttons;
	}

	for (int32 LensIndex = 0; LensIndex < Kit->Lenses.Num(); ++LensIndex)
	{
		const FUEcineLens& Lens = Kit->Lenses[LensIndex];
		const FText ToolTip = FText::Format(LOCTEXT("LensButtonToolTip", "{0}. Close focus {1}."),
			FText::FromString(Lens.GetDisplayName()),
			UEcine::Units::FormatDistance(Lens.CloseFocusM * 1000.0, UUEcineEditorSettings::GetDistanceUnits()));

		Buttons->AddSlot()
		[
			MakeToggleButton(FormatLensButton(Lens), ToolTip,
				TAttribute<bool>::CreateSPLambda(this, [this, LensIndex] { return GetPackage().LensIndex == LensIndex; }),
				[this, Kit, LensIndex](bool)
				{
					Edit(LOCTEXT("ChangeLens", "Change Lens"), [Kit, LensIndex](FUEcineCameraPackage& Package) { Package.SelectLens(*Kit, LensIndex); });
				})
		];
	}
	return Buttons;
}

TSharedRef<SWidget> SUEcineCameraTab::BuildLadderRow(const TSharedRef<FLadderControl>& Control)
{
	return MakeRow(Control->Label, Control->LabelToolTip,
		SNew(SUEcineStepper)
		.Label(Control->Label)
		.StepDownToolTip(Control->StepDownToolTip)
		.StepUpToolTip(Control->StepUpToolTip)
		.ValueText_Lambda([this, Control] { return Control->FormatValue(Control->GetValue(GetPackage())); })
		.CanStep_Lambda([this, Control](int32 Direction)
		{
			return UEcine::Exposure::StepOnLadder(Control->GetLadder(), Control->GetValue(GetPackage()), Direction).IsSet();
		})
		.OnStep_Lambda([this, Control](int32 Direction)
		{
			const TOptional<double> NewValue = UEcine::Exposure::StepOnLadder(Control->GetLadder(), Control->GetValue(GetPackage()), Direction);
			if (NewValue.IsSet())
			{
				Edit(Control->TransactionName, [&Control, &NewValue](FUEcineCameraPackage& Package) { Control->SetValue(Package, NewValue.GetValue()); });
			}
		})
		.OnGetMenuContent(this, &SUEcineCameraTab::MakeLadderMenu, Control));
}

TSharedRef<SWidget> SUEcineCameraTab::MakeBodyMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	FString OpenManufacturer;
	bool bSectionOpen = false;

	for (const FUEcineCameraBody& Body : FUEcineCameraDatabase::Get().GetBodies())
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

		const FUEcineCameraBody* BodyPtr = &Body;
		MenuBuilder.AddMenuEntry(
			FText::FromString(Body.Model),
			FText::Format(LOCTEXT("BodyEntryToolTip", "{0}, {1} recording formats"), FText::FromString(Body.GetDisplayName()), FText::AsNumber(Body.Formats.Num())),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSPLambda(this, [this, BodyPtr]
				{
					Edit(LOCTEXT("ChangeBody", "Change Camera Body"), [BodyPtr](FUEcineCameraPackage& Package) { Package.SelectBody(*BodyPtr); });
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

TSharedRef<SWidget> SUEcineCameraTab::MakeFormatMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	if (const FUEcineCameraBody* Body = FUEcineCameraDatabase::Get().FindBody(GetPackage().BodyId))
	{
		for (const FUEcineRecordingFormat& Format : Body->Formats)
		{
			const FString FormatId = Format.Id;
			MenuBuilder.AddMenuEntry(
				FText::FromString(Format.Name),
				FormatSensor(Format),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSPLambda(this, [this, FormatId]
					{
						Edit(LOCTEXT("ChangeFormat", "Change Recording Format"), [&FormatId](FUEcineCameraPackage& Package) { Package.FormatId = FormatId; });
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateSPLambda(this, [this, FormatId] { return GetPackage().FormatId == FormatId; })),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SUEcineCameraTab::MakeLensKitMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	const auto AddKits = [this, &MenuBuilder](bool bAnamorphic, const FText& SectionTitle)
	{
		MenuBuilder.BeginSection(NAME_None, SectionTitle);
		for (const FUEcineLensKit& Kit : FUEcineCameraDatabase::Get().GetLensKits())
		{
			if (Kit.IsAnamorphic() != bAnamorphic)
			{
				continue;
			}
			const FUEcineLensKit* KitPtr = &Kit;
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
						Edit(LOCTEXT("ChangeKit", "Change Lens Kit"), [KitPtr](FUEcineCameraPackage& Package) { Package.SelectLensKit(*KitPtr); });
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

TSharedRef<SWidget> SUEcineCameraTab::MakeLadderMenu(TSharedRef<FLadderControl> Control)
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
					Edit(Control->TransactionName, [&Control, Value](FUEcineCameraPackage& Package) { Control->SetValue(Package, Value); });
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateSPLambda(this, [this, Control, Value]
				{
					return UEcine::Exposure::IsSameSetting(Control->GetValue(GetPackage()), Value);
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	return MenuBuilder.MakeWidget();
}

UUEcineCameraPackageComponent* SUEcineCameraTab::GetComponent() const
{
	return Target->GetPackageComponent();
}

const FUEcineCameraPackage& SUEcineCameraTab::GetPackage() const
{
	static const FUEcineCameraPackage EmptyPackage;
	const UUEcineCameraPackageComponent* Component = GetComponent();
	return Component != nullptr ? Component->Package : EmptyPackage;
}

void SUEcineCameraTab::Edit(const FText& Description, TFunctionRef<void(FUEcineCameraPackage&)> EditFunction)
{
	FText Error;
	if (!UEcine::CameraEditing::EditPackage(GetComponent(), Description, EditFunction, Error))
	{
		Notify(Error, false);
	}
}

void SUEcineCameraTab::DragEdit(TFunctionRef<void(FUEcineCameraPackage&)> EditFunction)
{
	if (UUEcineCameraPackageComponent* Component = GetComponent())
	{
		EditFunction(Component->Package);
		FText Error;
		Component->ApplyToCamera(Error);
		GEditor->RedrawLevelEditingViewports();
	}
}

TSharedRef<SWidget> SUEcineCameraTab::MakePackageSpinBox(FSpinBoxArgs Args, TFunction<float(const FUEcineCameraPackage&)> GetValue, TFunction<void(FUEcineCameraPackage&, float)> SetValue)
{
	Args.Value = TAttribute<float>::CreateSPLambda(this, [this, GetValue = MoveTemp(GetValue)] { return GetValue(GetPackage()); });
	Args.SaveForUndo = [this]
	{
		if (UUEcineCameraPackageComponent* Component = GetComponent())
		{
			Component->Modify();
		}
	};
	Args.OnDrag = [this, SetValue](float NewValue)
	{
		DragEdit([&SetValue, NewValue](FUEcineCameraPackage& Package) { SetValue(Package, NewValue); });
	};
	Args.OnCommit = [this, SetValue, Description = Args.TransactionName](float NewValue)
	{
		Edit(Description, [&SetValue, NewValue](FUEcineCameraPackage& Package) { SetValue(Package, NewValue); });
	};
	return MakeSpinBox(MoveTemp(Args));
}

FReply SUEcineCameraTab::OnBuildPackageClicked()
{
	UEcine::CameraEditing::AddCameraPackage(Target->GetCamera());
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
