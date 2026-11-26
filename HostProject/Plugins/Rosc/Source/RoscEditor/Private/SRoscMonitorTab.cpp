// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "SRoscMonitorTab.h"

#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscCameraTarget.h"
#include "RoscMonitorSubsystem.h"
#include "RoscStyle.h"
#include "RoscUnits.h"
#include "RoscWidgets.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

using namespace Rosc::Widgets;

namespace
{
	/** The zebra levels offered, in percent of signal. */
	const float ZebraLevels[] = { 70.0f, 75.0f, 80.0f, 85.0f, 90.0f, 95.0f, 100.0f };

	const ERoscMonitorColour MonitorColours[] =
	{
		ERoscMonitorColour::White,
		ERoscMonitorColour::Red,
		ERoscMonitorColour::Green,
		ERoscMonitorColour::Blue,
		ERoscMonitorColour::Yellow,
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

	const FRoscMonitorSettings& GetMonitorSettings()
	{
		return URoscMonitorSubsystem::Get()->GetSettings();
	}

	void EditMonitorSettings(TFunctionRef<void(FRoscMonitorSettings&)> Edit)
	{
		URoscMonitorSubsystem* Monitor = URoscMonitorSubsystem::Get();
		FRoscMonitorSettings Settings = Monitor->GetSettings();
		Edit(Settings);
		Monitor->SetSettings(Settings);
	}

	FText FormatSignalPercent(float Percent)
	{
		return FText::Format(LOCTEXT("SignalPercent", "{0} %"), Rosc::Units::FormatNumber(Percent, 0));
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
	TSharedRef<SWidget> MakeToolToggle(const FText& Label, const FText& ToolTip, bool FRoscMonitorSettings::* Tool)
	{
		return MakeToggleButton(Label, ToolTip,
			TAttribute<bool>::CreateLambda([Tool] { return GetMonitorSettings().*Tool; }),
			[Tool](bool bTurnOn)
			{
				EditMonitorSettings([Tool, bTurnOn](FRoscMonitorSettings& Settings) { Settings.*Tool = bTurnOn; });
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
	TSharedRef<SWidget> MakeColourRow(const FText& Label, const FText& ToolTip, ERoscMonitorColour FRoscMonitorSettings::* Colour)
	{
		TArray<FChoice> Choices;
		for (const ERoscMonitorColour Offered : MonitorColours)
		{
			Choices.Add({ Rosc::Monitor::GetColourName(Offered),
				[Colour, Offered] { return GetMonitorSettings().*Colour == Offered; },
				[Colour, Offered]
				{
					EditMonitorSettings([Colour, Offered](FRoscMonitorSettings& Settings) { Settings.*Colour = Offered; });
				} });
		}
		return MakeChoiceRow(Label, ToolTip,
			TAttribute<FText>::CreateLambda([Colour] { return Rosc::Monitor::GetColourName(GetMonitorSettings().*Colour); }),
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

		const TConstArrayView<FRoscFalseColourBand> Bands = Rosc::Monitor::GetFalseColourBands();
		for (int32 BandIndex = Bands.Num() - 1; BandIndex >= 0; --BandIndex)
		{
			const FRoscFalseColourBand& Band = Bands[BandIndex];
			Legend->AddSlot()
			.AutoHeight()
			.Padding(FRoscStyle::RowPadding)
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
					.TextStyle(&TextStyle("Rosc.Label"))
					.Text(Band.Name)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&TextStyle("Rosc.Readout"))
					.Text(Rosc::Monitor::GetFalseColourRange(Band))
				]
			];
		}

		Legend->AddSlot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeNote(LOCTEXT("FalseColourGreyNote", "Signal levels between the bands stay grey."))
		];
		return Legend;
	}
}

void SRoscMonitorTab::Construct(const FArguments& InArgs, const TSharedRef<FRoscCameraTarget>& InTarget)
{
	Target = InTarget;

	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};

	Add(FMargin(0.0f), MakeSectionHeader(LOCTEXT("MonitorExposureSection", "Exposure")));
	Add(FRoscStyle::ButtonRowPadding, MakeToolGroup({
		MakeToolToggle(LOCTEXT("FalseColourButton", "False Colour"),
			LOCTEXT("FalseColourToolTip", "False colour: show the picture in exposure bands, so crushed blacks, middle grey, a stop over grey and clipping stand out. The bands are listed here while it is on."),
			&FRoscMonitorSettings::bFalseColour),
		MakeToolToggle(LOCTEXT("ZebrasButton", "Zebras"),
			LOCTEXT("ZebrasToolTip", "Zebras: stripe every part of the picture at or above the zebra level."),
			&FRoscMonitorSettings::bZebras),
		MakeToolToggle(LOCTEXT("WaveformButton", "Waveform"),
			LOCTEXT("WaveformToolTip", "Waveform: the signal level of every column of the picture, drawn at the bottom left. Black sits at the bottom and clipping at the top."),
			&FRoscMonitorSettings::bWaveform),
		MakeToolToggle(LOCTEXT("HistogramButton", "Histogram"),
			LOCTEXT("HistogramToolTip", "Histogram: how much of the picture sits at each signal level, drawn at the bottom right. Black is on the left and clipping on the right."),
			&FRoscMonitorSettings::bHistogram) }));

	TArray<FChoice> ZebraChoices;
	for (const float Level : ZebraLevels)
	{
		ZebraChoices.Add({ FormatSignalPercent(Level),
			[Level] { return FMath::IsNearlyEqual(GetMonitorSettings().ZebraLevelPercent, Level); },
			[Level]
			{
				EditMonitorSettings([Level](FRoscMonitorSettings& Settings) { Settings.ZebraLevelPercent = Level; });
			} });
	}
	Add(FRoscStyle::RowPadding, MakeChoiceRow(LOCTEXT("ZebraLevelLabel", "Zebra level"),
		LOCTEXT("ZebraLevelToolTip", "The signal level at which zebras start. 95 % and above warns of clipping; 70 % is a common level for checking skin."),
		TAttribute<FText>::CreateLambda([] { return FormatSignalPercent(GetMonitorSettings().ZebraLevelPercent); }),
		MoveTemp(ZebraChoices)));
	Add(FRoscStyle::RowPadding, MakeNote(TAttribute<FText>::CreateLambda([]
	{
		const FRoscMonitorSettings& Settings = GetMonitorSettings();
		return Settings.bZebras
			? FText::Format(LOCTEXT("ZebraLegend", "Stripes mark everything at {0} signal or above."), FormatSignalPercent(Settings.ZebraLevelPercent))
			: FText::GetEmpty();
	})));
	Add(FMargin(0.0f), MakeFalseColourLegend());

	Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("MonitorFocusSection", "Focus")));
	Add(FRoscStyle::ButtonRowPadding, MakeToolGroup({
		MakeToolToggle(LOCTEXT("PeakingButton", "Peaking"),
			LOCTEXT("PeakingToolTip", "Focus peaking: outline the fine detail that is in focus, in the colour chosen here."),
			&FRoscMonitorSettings::bPeaking) }));
	Add(FRoscStyle::RowPadding, MakeColourRow(LOCTEXT("PeakingColourLabel", "Colour"),
		LOCTEXT("PeakingColourToolTip", "The colour sharp detail is outlined in. Pick one that stands out from the scene."),
		&FRoscMonitorSettings::PeakingColour));

	TArray<FChoice> SensitivityChoices;
	for (const FPeakingSensitivity& Sensitivity : GetPeakingSensitivities())
	{
		const float Threshold = Sensitivity.ThresholdPercent;
		SensitivityChoices.Add({ Sensitivity.Name,
			[Threshold] { return FMath::IsNearlyEqual(GetMonitorSettings().PeakingThresholdPercent, Threshold); },
			[Threshold]
			{
				EditMonitorSettings([Threshold](FRoscMonitorSettings& Settings) { Settings.PeakingThresholdPercent = Threshold; });
			} });
	}
	Add(FRoscStyle::RowPadding, MakeChoiceRow(LOCTEXT("PeakingSensitivityLabel", "Sensitivity"),
		LOCTEXT("PeakingSensitivityToolTip", "How readily detail is outlined. High marks softer detail too; Low marks only the sharpest edges."),
		TAttribute<FText>::CreateLambda([] { return GetPeakingSensitivityName(GetMonitorSettings().PeakingThresholdPercent); }),
		MoveTemp(SensitivityChoices)));

	Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("MonitorFramingSection", "Framing")));
	Add(FRoscStyle::ButtonRowPadding, MakeToolGroup({
		MakeToolToggle(LOCTEXT("FrameLinesButton", "Frame Lines"),
			LOCTEXT("FrameLinesToolTip", "Frame lines: outline the frame the shot is delivered in, when that is not the whole recording format."),
			&FRoscMonitorSettings::bFrameLines),
		MakeToolToggle(LOCTEXT("SafeAreasButton", "Safe Areas"),
			LOCTEXT("SafeAreasToolTip", "Safe areas: outline the action safe area (93 % of the delivery frame) and the title safe area (90 %)."),
			&FRoscMonitorSettings::bSafeAreas),
		MakeToolToggle(LOCTEXT("CentreMarkButton", "Centre Mark"),
			LOCTEXT("CentreMarkToolTip", "Centre mark: a cross at the centre of the frame."),
			&FRoscMonitorSettings::bCentreMark) }));
	Add(FRoscStyle::RowPadding, BuildDeliveryRow());
	Add(FRoscStyle::RowPadding, MakeColourRow(LOCTEXT("LineColourLabel", "Line colour"),
		LOCTEXT("LineColourToolTip", "The colour of the frame lines, safe areas and centre mark. Each line has a black edge, so it shows on any picture."),
		&FRoscMonitorSettings::LineColour));
	Add(FRoscStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SRoscMonitorTab::GetFramingWarning)));

	Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("MonitorDataSection", "Camera Data")));
	Add(FRoscStyle::ButtonRowPadding, MakeToolGroup({
		MakeToolToggle(LOCTEXT("DataReadoutButton", "Data Readout"),
			LOCTEXT("DataReadoutToolTip", "Data readout: a line over the picture naming the camera, focal length, T-stop, ISO, shutter angle, frame rate, ND and focus distance."),
			&FRoscMonitorSettings::bDataReadout) }));
	Add(FRoscStyle::SectionPadding, MakeNote(LOCTEXT("MonitorWhereNote",
		"The tools show in the level viewport's perspective view and never in a render. Frame lines and the data readout show while the viewport looks through a camera.")));

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(FRoscStyle::PanelPadding)
		[
			Content
		]
	];
}

TSharedRef<SWidget> SRoscMonitorTab::BuildDeliveryRow()
{
	TArray<FChoice> Choices;
	for (const FRoscDeliveryAspect& Delivery : Rosc::GetDeliveryAspects())
	{
		const float Aspect = Delivery.Aspect;
		Choices.Add({ Delivery.Name,
			[Target = Target, Aspect]
			{
				const URoscCameraPackageComponent* Component = Target->GetPackageComponent();
				return Component != nullptr && FMath::IsNearlyEqual(Component->Package.DeliveryAspect, Aspect);
			},
			[Target = Target, Aspect]
			{
				FText Error;
				if (!Rosc::CameraEditing::EditPackage(Target->GetPackageComponent(), LOCTEXT("ChangeDeliveryFrame", "Change Delivery Frame"),
					[Aspect](FRoscCameraPackage& Package) { Package.DeliveryAspect = Aspect; }, Error))
				{
					Notify(Error, false);
				}
			} });
	}
	return MakeChoiceRow(LOCTEXT("DeliverInLabel", "Deliver in"),
		LOCTEXT("DeliverInToolTip", "The shape of the frame the shot is delivered in. Frame lines and safe areas show it inside the recording format. It is kept with the camera package, and changing it can be undone."),
		TAttribute<FText>::CreateSP(this, &SRoscMonitorTab::GetDeliveryName),
		MoveTemp(Choices),
		TAttribute<bool>::CreateSP(this, &SRoscMonitorTab::HasPackage));
}

bool SRoscMonitorTab::HasPackage() const
{
	return Target->GetPackageComponent() != nullptr;
}

FText SRoscMonitorTab::GetDeliveryName() const
{
	const URoscCameraPackageComponent* Component = Target->GetPackageComponent();
	return Component != nullptr ? Rosc::GetDeliveryAspectName(Component->Package.DeliveryAspect) : LOCTEXT("NoDeliveryFrame", "No camera package");
}

FText SRoscMonitorTab::GetFramingWarning() const
{
	if (!GetMonitorSettings().bFrameLines)
	{
		return FText::GetEmpty();
	}
	const URoscCameraPackageComponent* Component = Target->GetPackageComponent();
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
