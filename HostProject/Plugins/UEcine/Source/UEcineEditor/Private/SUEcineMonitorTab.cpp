// Copyright 2026 Fiavaion. All rights reserved.

#include "SUEcineMonitorTab.h"

#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineCameraTarget.h"
#include "UEcineMonitorSubsystem.h"
#include "UEcineStyle.h"
#include "UEcineUnits.h"
#include "UEcineWidgets.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

using namespace UEcine::Widgets;

namespace
{
	/** The zebra levels offered, in percent of signal. */
	const float ZebraLevels[] = { 70.0f, 75.0f, 80.0f, 85.0f, 90.0f, 95.0f, 100.0f };

	const EUEcineMonitorColour MonitorColours[] =
	{
		EUEcineMonitorColour::White,
		EUEcineMonitorColour::Red,
		EUEcineMonitorColour::Green,
		EUEcineMonitorColour::Blue,
		EUEcineMonitorColour::Yellow,
	};

	/** How readily peaking outlines detail: the step in signal between neighbouring pixels that counts as sharp. */
	struct FPeakingSensitivity
	{
		float ThresholdPercent;
		FText Name;
	};

	TConstArrayView<FPeakingSensitivity> GetPeakingSensitivities()
	{
		static const FPeakingSensitivity Sensitivities[] =
		{
			{ 4.0f, LOCTEXT("PeakingHigh", "High") },
			{ 8.0f, LOCTEXT("PeakingMedium", "Medium") },
			{ 16.0f, LOCTEXT("PeakingLow", "Low") },
		};
		return Sensitivities;
	}

	const FUEcineMonitorSettings& GetMonitorSettings()
	{
		return UUEcineMonitorSubsystem::Get()->GetSettings();
	}

	void EditMonitorSettings(TFunctionRef<void(FUEcineMonitorSettings&)> Edit)
	{
		UUEcineMonitorSubsystem* Monitor = UUEcineMonitorSubsystem::Get();
		FUEcineMonitorSettings Settings = Monitor->GetSettings();
		Edit(Settings);
		Monitor->SetSettings(Settings);
	}

	FText FormatSignalPercent(float Percent)
	{
		return FText::Format(LOCTEXT("SignalPercent", "{0} %"), UEcine::Units::FormatNumber(Percent, 0));
	}

	FText GetPeakingSensitivityName(float ThresholdPercent)
	{
		for (const FPeakingSensitivity& Sensitivity : GetPeakingSensitivities())
		{
			if (FMath::IsNearlyEqual(Sensitivity.ThresholdPercent, ThresholdPercent))
			{
				return Sensitivity.Name;
			}
		}
		return FormatSignalPercent(ThresholdPercent);
	}

	/** A button that switches one monitor tool on and off. */
	TSharedRef<SWidget> MakeToolToggle(const FText& Label, const FText& ToolTip, bool FUEcineMonitorSettings::* Tool)
	{
		return MakeToggleButton(Label, ToolTip,
			TAttribute<bool>::CreateLambda([Tool] { return GetMonitorSettings().*Tool; }),
			[Tool](bool bTurnOn)
			{
				EditMonitorSettings([Tool, bTurnOn](FUEcineMonitorSettings& Settings) { Settings.*Tool = bTurnOn; });
			});
	}

	/** Tool buttons side by side, folding onto further lines when the panel is narrow. */
	TSharedRef<SWidget> MakeToolGroup(const TArray<TSharedRef<SWidget>>& Toggles)
	{
		const TSharedRef<SWrapBox> Group = SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.0, 4.0));
		for (const TSharedRef<SWidget>& Toggle : Toggles)
		{
			Group->AddSlot()[ Toggle ];
		}
		return Group;
	}

	/** A drop-down list of the colours a tool can be drawn in, by name. */
	TSharedRef<SWidget> MakeColourRow(const FText& Label, const FText& ToolTip, EUEcineMonitorColour FUEcineMonitorSettings::* Colour)
	{
		TArray<FChoice> Choices;
		for (const EUEcineMonitorColour Offered : MonitorColours)
		{
			Choices.Add({ UEcine::Monitor::GetColourName(Offered),
				[Colour, Offered] { return GetMonitorSettings().*Colour == Offered; },
				[Colour, Offered]
				{
					EditMonitorSettings([Colour, Offered](FUEcineMonitorSettings& Settings) { Settings.*Colour = Offered; });
				} });
		}
		return MakeChoiceRow(Label, ToolTip,
			TAttribute<FText>::CreateLambda([Colour] { return UEcine::Monitor::GetColourName(GetMonitorSettings().*Colour); }),
			MoveTemp(Choices));
	}

	/** A line of explanation. Takes no space while it has nothing to say. */
	/**
	 * What each false colour means, in words, brightest first as the legend over the picture lists
	 * them. Shown while false colour is on.
	 */
	TSharedRef<SWidget> MakeFalseColourLegend()
	{
		const TSharedRef<SVerticalBox> Legend = SNew(SVerticalBox)
			.Visibility_Lambda([] { return GetMonitorSettings().bFalseColour ? EVisibility::Visible : EVisibility::Collapsed; });

		const TConstArrayView<FUEcineFalseColourBand> Bands = UEcine::Monitor::GetFalseColourBands();
		for (int32 BandIndex = Bands.Num() - 1; BandIndex >= 0; --BandIndex)
		{
			const FUEcineFalseColourBand& Band = Bands[BandIndex];
			Legend->AddSlot()
			.AutoHeight()
			.Padding(FUEcineStyle::RowPadding)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SColorBlock)
					.Color(FLinearColor(Band.Colour))
					.Size(FVector2D(12.0, 12.0))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&TextStyle("UEcine.Label"))
					.Text(Band.Name)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&TextStyle("UEcine.Readout"))
					.Text(UEcine::Monitor::GetFalseColourRange(Band))
				]
			];
		}

		Legend->AddSlot()
		.AutoHeight()
		.Padding(FUEcineStyle::RowPadding)
		[
			MakeNote(LOCTEXT("FalseColourGreyNote", "Signal levels between the bands stay grey."))
		];
		return Legend;
	}
}

void SUEcineMonitorTab::Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget)
{
	Target = InTarget;

	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};

	Add(FMargin(0.0f), MakeSectionHeader(LOCTEXT("MonitorExposureSection", "Exposure")));
	Add(FUEcineStyle::ButtonRowPadding, MakeToolGroup({
		MakeToolToggle(LOCTEXT("FalseColourButton", "False Colour"),
			LOCTEXT("FalseColourToolTip", "False colour: show the picture in exposure bands, so crushed blacks, middle grey, a stop over grey and clipping stand out. The bands are listed here while it is on."),
			&FUEcineMonitorSettings::bFalseColour),
		MakeToolToggle(LOCTEXT("ZebrasButton", "Zebras"),
			LOCTEXT("ZebrasToolTip", "Zebras: stripe every part of the picture at or above the zebra level."),
			&FUEcineMonitorSettings::bZebras),
		MakeToolToggle(LOCTEXT("WaveformButton", "Waveform"),
			LOCTEXT("WaveformToolTip", "Waveform: the signal level of every column of the picture, drawn at the bottom left. Black sits at the bottom and clipping at the top."),
			&FUEcineMonitorSettings::bWaveform),
		MakeToolToggle(LOCTEXT("HistogramButton", "Histogram"),
			LOCTEXT("HistogramToolTip", "Histogram: how much of the picture sits at each signal level, drawn at the bottom right. Black is on the left and clipping on the right."),
			&FUEcineMonitorSettings::bHistogram) }));

	TArray<FChoice> ZebraChoices;
	for (const float Level : ZebraLevels)
	{
		ZebraChoices.Add({ FormatSignalPercent(Level),
			[Level] { return FMath::IsNearlyEqual(GetMonitorSettings().ZebraLevelPercent, Level); },
			[Level]
			{
				EditMonitorSettings([Level](FUEcineMonitorSettings& Settings) { Settings.ZebraLevelPercent = Level; });
			} });
	}
	Add(FUEcineStyle::RowPadding, MakeChoiceRow(LOCTEXT("ZebraLevelLabel", "Zebra level"),
		LOCTEXT("ZebraLevelToolTip", "The signal level at which zebras start. 95 % and above warns of clipping; 70 % is a common level for checking skin."),
		TAttribute<FText>::CreateLambda([] { return FormatSignalPercent(GetMonitorSettings().ZebraLevelPercent); }),
		MoveTemp(ZebraChoices)));
	Add(FUEcineStyle::RowPadding, MakeNote(TAttribute<FText>::CreateLambda([]
	{
		const FUEcineMonitorSettings& Settings = GetMonitorSettings();
		return Settings.bZebras
			? FText::Format(LOCTEXT("ZebraLegend", "Stripes mark everything at {0} signal or above."), FormatSignalPercent(Settings.ZebraLevelPercent))
			: FText::GetEmpty();
	})));
	Add(FMargin(0.0f), MakeFalseColourLegend());

	Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("MonitorFocusSection", "Focus")));
	Add(FUEcineStyle::ButtonRowPadding, MakeToolGroup({
		MakeToolToggle(LOCTEXT("PeakingButton", "Peaking"),
			LOCTEXT("PeakingToolTip", "Focus peaking: outline the fine detail that is in focus, in the colour chosen here."),
			&FUEcineMonitorSettings::bPeaking) }));
	Add(FUEcineStyle::RowPadding, MakeColourRow(LOCTEXT("PeakingColourLabel", "Colour"),
		LOCTEXT("PeakingColourToolTip", "The colour sharp detail is outlined in. Pick one that stands out from the scene."),
		&FUEcineMonitorSettings::PeakingColour));

	TArray<FChoice> SensitivityChoices;
	for (const FPeakingSensitivity& Sensitivity : GetPeakingSensitivities())
	{
		const float Threshold = Sensitivity.ThresholdPercent;
		SensitivityChoices.Add({ Sensitivity.Name,
			[Threshold] { return FMath::IsNearlyEqual(GetMonitorSettings().PeakingThresholdPercent, Threshold); },
			[Threshold]
			{
				EditMonitorSettings([Threshold](FUEcineMonitorSettings& Settings) { Settings.PeakingThresholdPercent = Threshold; });
			} });
	}
	Add(FUEcineStyle::RowPadding, MakeChoiceRow(LOCTEXT("PeakingSensitivityLabel", "Sensitivity"),
		LOCTEXT("PeakingSensitivityToolTip", "How readily detail is outlined. High marks softer detail too; Low marks only the sharpest edges."),
		TAttribute<FText>::CreateLambda([] { return GetPeakingSensitivityName(GetMonitorSettings().PeakingThresholdPercent); }),
		MoveTemp(SensitivityChoices)));

	Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("MonitorFramingSection", "Framing")));
	Add(FUEcineStyle::ButtonRowPadding, MakeToolGroup({
		MakeToolToggle(LOCTEXT("FrameLinesButton", "Frame Lines"),
			LOCTEXT("FrameLinesToolTip", "Frame lines: outline the frame the shot is delivered in, when that is not the whole recording format."),
			&FUEcineMonitorSettings::bFrameLines),
		MakeToolToggle(LOCTEXT("SafeAreasButton", "Safe Areas"),
			LOCTEXT("SafeAreasToolTip", "Safe areas: outline the action safe area (93 % of the delivery frame) and the title safe area (90 %)."),
			&FUEcineMonitorSettings::bSafeAreas),
		MakeToolToggle(LOCTEXT("CentreMarkButton", "Centre Mark"),
			LOCTEXT("CentreMarkToolTip", "Centre mark: a cross at the centre of the frame."),
			&FUEcineMonitorSettings::bCentreMark) }));
	Add(FUEcineStyle::RowPadding, BuildDeliveryRow());
	Add(FUEcineStyle::RowPadding, MakeColourRow(LOCTEXT("LineColourLabel", "Line colour"),
		LOCTEXT("LineColourToolTip", "The colour of the frame lines, safe areas and centre mark. Each line has a black edge, so it shows on any picture."),
		&FUEcineMonitorSettings::LineColour));
	Add(FUEcineStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SUEcineMonitorTab::GetFramingWarning)));

	Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("MonitorDataSection", "Camera Data")));
	Add(FUEcineStyle::ButtonRowPadding, MakeToolGroup({
		MakeToolToggle(LOCTEXT("DataReadoutButton", "Data Readout"),
			LOCTEXT("DataReadoutToolTip", "Data readout: a line over the picture naming the camera, focal length, T-stop, ISO, shutter angle, frame rate, ND and focus distance."),
			&FUEcineMonitorSettings::bDataReadout) }));
	Add(FUEcineStyle::SectionPadding, MakeNote(LOCTEXT("MonitorWhereNote",
		"The tools show in the level viewport's perspective view and never in a render. Frame lines and the data readout show while the viewport looks through a camera.")));

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(FUEcineStyle::PanelPadding)
		[
			Content
		]
	];
}

TSharedRef<SWidget> SUEcineMonitorTab::BuildDeliveryRow()
{
	TArray<FChoice> Choices;
	for (const FUEcineDeliveryAspect& Delivery : UEcine::GetDeliveryAspects())
	{
		const float Aspect = Delivery.Aspect;
		Choices.Add({ Delivery.Name,
			[Target = Target, Aspect]
			{
				const UUEcineCameraPackageComponent* Component = Target->GetPackageComponent();
				return Component != nullptr && FMath::IsNearlyEqual(Component->Package.DeliveryAspect, Aspect);
			},
			[Target = Target, Aspect]
			{
				FText Error;
				if (!UEcine::CameraEditing::EditPackage(Target->GetPackageComponent(), LOCTEXT("ChangeDeliveryFrame", "Change Delivery Frame"),
					[Aspect](FUEcineCameraPackage& Package) { Package.DeliveryAspect = Aspect; }, Error))
				{
					Notify(Error, false);
				}
			} });
	}
	return MakeChoiceRow(LOCTEXT("DeliverInLabel", "Deliver in"),
		LOCTEXT("DeliverInToolTip", "The shape of the frame the shot is delivered in. Frame lines and safe areas show it inside the recording format. It is kept with the camera package, and changing it can be undone."),
		TAttribute<FText>::CreateSP(this, &SUEcineMonitorTab::GetDeliveryName),
		MoveTemp(Choices),
		TAttribute<bool>::CreateSP(this, &SUEcineMonitorTab::HasPackage));
}

bool SUEcineMonitorTab::HasPackage() const
{
	return Target->GetPackageComponent() != nullptr;
}

FText SUEcineMonitorTab::GetDeliveryName() const
{
	const UUEcineCameraPackageComponent* Component = Target->GetPackageComponent();
	return Component != nullptr ? UEcine::GetDeliveryAspectName(Component->Package.DeliveryAspect) : LOCTEXT("NoDeliveryFrame", "No camera package");
}

FText SUEcineMonitorTab::GetFramingWarning() const
{
	if (!GetMonitorSettings().bFrameLines)
	{
		return FText::GetEmpty();
	}
	const UUEcineCameraPackageComponent* Component = Target->GetPackageComponent();
	if (Component == nullptr)
	{
		return LOCTEXT("FrameLinesNeedPackage", "Frame lines need a delivery frame, which is kept with a camera package. Build one in the Camera tab, then choose a frame in Deliver in.");
	}
	if (Component->Package.DeliveryAspect <= 0.0f)
	{
		return LOCTEXT("FrameLinesNeedFrame", "This camera delivers the whole recording format, so there is no frame line to draw. Choose a frame in Deliver in.");
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
