// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "SRoscGripTab.h"

#include "CineCameraActor.h"
#include "Components/SplineComponent.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Math/UnitConversion.h"
#include "Styling/AppStyle.h"
#include "RoscCamServer.h"
#include "RoscCameraTarget.h"
#include "RoscEditorSettings.h"
#include "RoscGripEditing.h"
#include "RoscMove.h"
#include "RoscOperatingSubsystem.h"
#include "RoscOperator.h"
#include "RoscRig.h"
#include "RoscStyle.h"
#include "RoscUnits.h"
#include "UnrealEdMisc.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Widgets/Input/SButton.h"
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
	/**
	 * Writes and reads a rig axis the way the crew says it: 3.2 m or 10' 6" for distances,
	 * 45° for angles. The value itself stays in centimetres or degrees.
	 */
	class FAxisTypeInterface : public TDefaultNumericTypeInterface<float>
	{
	public:
		explicit FAxisTypeInterface(ERoscRigAxis InAxis)
			: Axis(InAxis)
		{
		}

		virtual FString ToString(const float& Value) const override
		{
			return Rosc::Rig::FormatAxisValue(Axis, Value, URoscEditorSettings::GetDistanceUnits()).ToString();
		}

		virtual TOptional<float> FromString(const FString& InString, const float& ExistingValue) override
		{
			// Text the user did not change keeps the exact value, not the rounded one on show.
			if (InString == ToString(ExistingValue))
			{
				return ExistingValue;
			}
			const TOptional<double> Parsed = Rosc::Rig::ParseAxisValue(Axis, InString, URoscEditorSettings::GetDistanceUnits());
			return Parsed.IsSet() ? TOptional<float>(static_cast<float>(Parsed.GetValue())) : TOptional<float>();
		}

		virtual bool IsCharacterValid(TCHAR InChar) const override
		{
			// Every character but Tab. A text box types whatever its owner calls valid, Tab included,
			// and Tab has to stay the key that moves on to the next control.
			return InChar != TEXT('\t');
		}

	private:
		ERoscRigAxis Axis;
	};

	/** A number shown with its unit by the engine: "4.0 s", "25 %". */
	TSharedPtr<INumericTypeInterface<float>> MakeUnitInterface(EUnit Unit)
	{
		return MakeShared<TNumericUnitTypeInterface<float>>(Unit);
	}

	void SetAxis(ARoscRig& Rig, ERoscRigAxis Axis, double Value)
	{
		FRoscRigPose Pose = Rig.GetPose();
		Pose.Set(Axis, Value);
		Rig.SetPose(Pose);
	}

}

void SRoscGripTab::Construct(const FArguments& InArgs, const TSharedRef<FRoscCameraTarget>& InTarget)
{
	Target = InTarget;
	CurrentLayoutKey = MakeLayoutKey();
	RebuildLayout();
}

void SRoscGripTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	MarkRowFocus.Tick();
	const FLayoutKey NewKey = MakeLayoutKey();
	if (!(NewKey == CurrentLayoutKey))
	{
		CurrentLayoutKey = NewKey;
		RebuildLayout();
		MeasureRebuiltContent(*this, AllottedGeometry);
	}
	else if (const ARoscRig* Rig = GetRig(); Rig != nullptr && Rig->Marks.Num() != ShownMarkCount)
	{
		RebuildMarks();
		MeasureRebuiltContent(*this, AllottedGeometry);
	}
}

SRoscGripTab::FLayoutKey SRoscGripTab::MakeLayoutKey() const
{
	FLayoutKey Key;
	Key.Camera = Target->GetCamera();
	Key.Units = URoscEditorSettings::GetDistanceUnits();
	if (const ARoscRig* Rig = GetRig())
	{
		Key.Rig = Rig;
		Key.PresetId = Rig->GetSpec().PresetId;
	}
	return Key;
}

void SRoscGripTab::RebuildLayout()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	MarksBox.Reset();
	MarkPositionButton.Reset();

	if (CurrentLayoutKey.Camera.IsValid())
	{
		Add(FMargin(0.0f), MakeSectionHeader(LOCTEXT("SupportSection", "Support")));
		Add(FRoscStyle::RowPadding, BuildSupportRow());

		if (const ARoscRig* Rig = CurrentLayoutKey.Rig.Get())
		{
			const FRoscRigSpec Spec = Rig->GetSpec();
			if (Spec.Type == ERoscRigType::Dolly)
			{
				Add(FRoscStyle::RowPadding, BuildTrackRow());
			}

			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("PositionSection", "Position")));
			for (int32 AxisIndex = 0; AxisIndex < RoscRigAxisCount; ++AxisIndex)
			{
				if (Spec.Axes[AxisIndex].bUsed)
				{
					Add(FRoscStyle::RowPadding, BuildAxisRow(Spec.Type, static_cast<ERoscRigAxis>(AxisIndex)));
				}
			}

			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("MarksSection", "Marks")));
			Add(FMargin(0.0f), SAssignNew(MarksBox, SVerticalBox));
			const TSharedRef<SWidget> MarkPosition =
				MakeButton(LOCTEXT("MarkPositionButton", "Mark Position"),
					LOCTEXT("MarkPositionToolTip", "Store where the support is now as the next mark. A move runs from each mark to the one after it."),
					FOnClicked::CreateSPLambda(this, [this]
					{
						Rosc::GripEditing::AddMark(GetRig());
						return FReply::Handled();
					}));
			MarkPositionButton = MarkPosition;
			Add(FRoscStyle::ButtonRowPadding, MarkPosition);

			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("OperatorSection", "Operator")));
			Add(FRoscStyle::RowPadding, BuildOperatorRows());

			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("SequencerSection", "Sequencer")));
			Add(FRoscStyle::RowPadding, BuildBakeRows());

			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("RoscCamSection", "RoscCam")));
			Add(FRoscStyle::RowPadding, BuildRoscCamRows());

			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("DeviceSection", "Device")));
			Add(FRoscStyle::RowPadding, BuildDeviceRows());
		}
		else
		{
			Add(FRoscStyle::SectionPadding,
				SNew(STextBlock)
				.TextStyle(&TextStyle("Rosc.Body"))
				.AutoWrapText(true)
				.Text(LOCTEXT("NoSupportMessage", "The camera is free: it goes wherever you put it, with nothing to limit it. Put it on a support to move it the way real equipment moves, block a move between marks, and bake the move to Sequencer.")));
		}
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

	RebuildMarks();
}

void SRoscGripTab::RebuildMarks()
{
	const ARoscRig* Rig = GetRig();
	ShownMarkCount = Rig != nullptr ? Rig->Marks.Num() : 0;
	if (!MarksBox.IsValid())
	{
		return;
	}

	MarksBox->ClearChildren();
	const auto Add = [this](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		MarksBox->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};

	if (ShownMarkCount == 0)
	{
		Add(FRoscStyle::RowPadding,
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Body"))
			.AutoWrapText(true)
			.Text(LOCTEXT("NoMarksMessage", "No marks yet. Set the support where the shot starts and mark the position, then do the same for where it ends.")));
	}

	// In shooting order: a mark, then the move that leaves it for the next mark.
	TArray<TSharedRef<SWidget>> MarkRows;
	for (int32 MarkIndex = 0; MarkIndex < ShownMarkCount; ++MarkIndex)
	{
		if (MarkIndex > 0)
		{
			Add(FRoscStyle::RowPadding, BuildMoveTimeRow(MarkIndex));
			Add(FRoscStyle::RowPadding, BuildEaseRow(MarkIndex, true));
			Add(FRoscStyle::RowPadding, BuildEaseRow(MarkIndex, false));
			if (Rosc::GripEditing::CanDollyZoom(GetRig()))
			{
				Add(FRoscStyle::ButtonRowPadding, BuildDollyZoomRow(MarkIndex));
			}
			Add(FRoscStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SRoscGripTab::GetMoveWarnings, MarkIndex)));
		}
		MarkRows.Add(BuildMarkRow(MarkIndex));
		Add(FRoscStyle::ButtonRowPadding, MarkRows.Last());
	}
	MarkRowFocus.RowsRebuilt(MarkRows, MarkPositionButton);
}

TSharedRef<SWidget> SRoscGripTab::BuildSupportRow()
{
	const FText Label = LOCTEXT("SupportLabel", "Camera on");
	const FText ToolTip = LOCTEXT("SupportToolTip", "What the camera is mounted on. The support is stood up under the camera, so the camera does not move when you choose one. Once it is on a support the camera moves only as the support moves: it cannot be dragged or flown off it in the viewport.");
	return MakeRow(Label, ToolTip,
		SNew(SBox)
		.MinDesiredHeight(FRoscStyle::MinTargetSize)
		[
			SNew(SComboButton)
			.ToolTipText(ToolTip)
			.AccessibleText(Label)
			.OnGetMenuContent(this, &SRoscGripTab::MakeSupportMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("Rosc.Value"))
				.Text(this, &SRoscGripTab::GetSupportName)
			]
		]);
}

TSharedRef<SWidget> SRoscGripTab::BuildTrackRow()
{
	FSpinBoxArgs Args;
	Args.Name = LOCTEXT("TrackLengthLabel", "Track length");
	Args.ToolTip = LOCTEXT("TrackToolTip", "Length of track laid for the dolly. Changing it lays the track straight; to curve the track, select the dolly and move the track's points in the viewport.");
	Args.TransactionName = LOCTEXT("ChangeTrack", "Lay Track");
	Args.MinValue = TOptional<float>(100.0f);
	Args.MaxValue = TOptional<float>(10000.0f);
	Args.MaxSliderValue = 3000.0f;
	Args.Delta = 10.0f;
	Args.TypeInterface = MakeShared<FAxisTypeInterface>(ERoscRigAxis::Travel);
	return MakeRow(Args.Name, Args.ToolTip, MakeRigSpinBox(Args,
		[](const ARoscRig& Rig) { return Rig.GetTrackLength(); },
		[](ARoscRig& Rig, float Value)
		{
			Rig.GetTrack()->Modify();
			Rig.SetStraightTrack(Value);
		}));
}

TSharedRef<SWidget> SRoscGripTab::BuildAxisRow(ERoscRigType Type, ERoscRigAxis Axis)
{
	const FText Name = Rosc::Rig::GetAxisName(Type, Axis);
	const auto GetLimits = [this, Axis]
	{
		const ARoscRig* Rig = GetRig();
		return Rig != nullptr ? Rig->GetSpec().GetAxis(Axis) : FRoscAxisLimits();
	};

	FSpinBoxArgs Args;
	Args.Name = Name;
	Args.ToolTip = TAttribute<FText>::CreateSPLambda(this, [Axis, Name, GetLimits]
	{
		const FRoscAxisLimits Limits = GetLimits();
		const ERoscDistanceUnits Units = URoscEditorSettings::GetDistanceUnits();
		const FText Range = FText::Format(LOCTEXT("AxisRangeToolTip", "{0}: from {1} to {2}."), Name,
			Rosc::Rig::FormatAxisValue(Axis, Limits.Min, Units), Rosc::Rig::FormatAxisValue(Axis, Limits.Max, Units));
		return Limits.IsFixedDuringShot()
			? FText::Format(LOCTEXT("AxisFixedToolTip", "{0} It is set when the support is built and cannot change during a shot."), Range)
			: Range;
	});
	Args.TransactionName = FText::Format(LOCTEXT("ChangeAxis", "Set {0}"), Name);
	Args.MinValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [GetLimits] { return TOptional<float>(static_cast<float>(GetLimits().Min)); });
	Args.MaxValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [GetLimits] { return TOptional<float>(static_cast<float>(GetLimits().Max)); });
	Args.Delta = Rosc::Rig::IsDistanceAxis(Axis) ? 1.0f : 0.1f;
	Args.TypeInterface = MakeShared<FAxisTypeInterface>(Axis);
	return MakeRow(Name, Args.ToolTip, MakeRigSpinBox(Args,
		[Axis](const ARoscRig& Rig) { return static_cast<float>(Rig.GetPose().Get(Axis)); },
		[Axis](ARoscRig& Rig, float Value) { SetAxis(Rig, Axis, Value); }));
}

TSharedRef<SWidget> SRoscGripTab::BuildMarkRow(int32 MarkIndex)
{
	const FText Number = FText::AsNumber(MarkIndex + 1);
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			MakeButton(FText::Format(LOCTEXT("MarkButton", "Mark {0}"), Number),
				FText::Format(LOCTEXT("MarkButtonToolTip", "Put the support on mark {0}."), Number),
				FOnClicked::CreateSPLambda(this, [this, MarkIndex]
				{
					Rosc::GripEditing::GoToMark(GetRig(), MarkIndex);
					return FReply::Handled();
				}))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeButton(LOCTEXT("RemarkButton", "Re-mark"),
				FText::Format(LOCTEXT("RemarkToolTip", "Move mark {0} to where the support is now."), Number),
				FOnClicked::CreateSPLambda(this, [this, MarkIndex]
				{
					Rosc::GripEditing::UpdateMark(GetRig(), MarkIndex);
					return FReply::Handled();
				}))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeIconButton("Icons.Delete", FText::Format(LOCTEXT("RemoveMarkToolTip", "Remove mark {0}"), Number), FOnClicked::CreateSPLambda(this, [this, MarkIndex]
			{
				MarkRowFocus.RowRemoved(MarkIndex);
				Rosc::GripEditing::RemoveMark(GetRig(), MarkIndex);
				return FReply::Handled();
			}))
		];
}

TSharedRef<SWidget> SRoscGripTab::BuildDollyZoomRow(int32 MarkIndex)
{
	const TAttribute<FText> Zoom = TAttribute<FText>::CreateSPLambda(this, [this, MarkIndex]
	{
		Rosc::GripEditing::FDollyZoom Plan;
		// A prime does not zoom: the warning under the move says so, and there is nothing to read out.
		if (!Rosc::GripEditing::PlanDollyZoom(GetRig(), MarkIndex, Plan) || !Plan.IsZoomLens())
		{
			return FText::GetEmpty();
		}
		FNumberFormattingOptions Whole;
		Whole.SetMaximumFractionalDigits(0);
		return FText::Format(LOCTEXT("DollyZoomReadout", "The lens zooms from {0} to {1} mm as the camera travels."),
			FText::AsNumber(FMath::RoundToInt32(Plan.StartFocalLengthMm), &Whole),
			FText::AsNumber(FMath::RoundToInt32(FMath::Clamp(Plan.EndFocalLengthMm, Plan.LensWidestMm, Plan.LensLongestMm)), &Whole));
	});

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeToggleButton(LOCTEXT("DollyZoomButton", "Dolly Zoom"),
				FText::Format(LOCTEXT("DollyZoomToolTip", "Make the move from mark {0} to mark {1} a dolly zoom: the lens zooms as the camera travels, so what it is focused on stays the same size while the background grows or shrinks behind it. It needs a zoom lens, and the focus held on the subject."),
					FText::AsNumber(MarkIndex), FText::AsNumber(MarkIndex + 1)),
				TAttribute<bool>::CreateSPLambda(this, [this, MarkIndex]
				{
					const ARoscRig* Rig = GetRig();
					return Rig != nullptr && Rig->Marks.IsValidIndex(MarkIndex) && Rig->Marks[MarkIndex].bDollyZoom;
				}),
				[this, MarkIndex](bool bOn)
				{
					Rosc::GripEditing::EditRig(GetRig(), LOCTEXT("ChangeDollyZoom", "Change Dolly Zoom"), [MarkIndex, bOn](ARoscRig& Rig)
					{
						if (Rig.Marks.IsValidIndex(MarkIndex))
						{
							Rig.Marks[MarkIndex].bDollyZoom = bOn;
						}
					});
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Body"))
			.AutoWrapText(true)
			.Text(Zoom)
			.Visibility_Lambda([Zoom] { return Zoom.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		];
}

TSharedRef<SWidget> SRoscGripTab::BuildMoveTimeRow(int32 MarkIndex)
{
	FSpinBoxArgs Args;
	Args.Name = LOCTEXT("MoveTimeLabel", "Move time");
	Args.ToolTip = FText::Format(LOCTEXT("MoveTimeToolTip", "How long the move from mark {0} to mark {1} takes."), FText::AsNumber(MarkIndex), FText::AsNumber(MarkIndex + 1));
	Args.TransactionName = LOCTEXT("ChangeMoveTime", "Change Move Time");
	Args.MinValue = TOptional<float>(0.1f);
	Args.MaxValue = TOptional<float>(600.0f);
	Args.MaxSliderValue = 30.0f;
	Args.Delta = 0.1f;
	Args.TypeInterface = MakeUnitInterface(EUnit::Seconds);
	return MakeRow(Args.Name, Args.ToolTip, MakeRigSpinBox(Args,
		[MarkIndex](const ARoscRig& Rig) { return Rig.Marks.IsValidIndex(MarkIndex) ? static_cast<float>(Rig.Marks[MarkIndex].Arrival.DurationSeconds) : 0.0f; },
		[MarkIndex](ARoscRig& Rig, float Value)
		{
			if (Rig.Marks.IsValidIndex(MarkIndex))
			{
				Rig.Marks[MarkIndex].Arrival.DurationSeconds = Value;
			}
		}));
}

TSharedRef<SWidget> SRoscGripTab::BuildEaseRow(int32 MarkIndex, bool bEaseIn)
{
	// The two eases share the move, so each can take only what the other leaves.
	const auto GetFraction = [](const ARoscRig& Rig, int32 Index, bool bIn)
	{
		if (!Rig.Marks.IsValidIndex(Index))
		{
			return 0.0;
		}
		const FRoscMoveTiming& Timing = Rig.Marks[Index].Arrival;
		return bIn ? Timing.EaseInFraction : Timing.EaseOutFraction;
	};

	FSpinBoxArgs Args;
	Args.Name = bEaseIn ? LOCTEXT("EaseInLabel", "Ease in") : LOCTEXT("EaseOutLabel", "Ease out");
	Args.ToolTip = bEaseIn
		? LOCTEXT("EaseInToolTip", "How much of the move is spent getting up to speed. At 0 % the support is at full speed the instant it leaves the mark.")
		: LOCTEXT("EaseOutToolTip", "How much of the move is spent coming to rest. At 0 % the support stops dead on the mark.");
	Args.TransactionName = bEaseIn ? LOCTEXT("ChangeEaseIn", "Change Ease In") : LOCTEXT("ChangeEaseOut", "Change Ease Out");
	Args.MinValue = TOptional<float>(0.0f);
	Args.MaxValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [this, MarkIndex, bEaseIn, GetFraction]
	{
		const ARoscRig* Rig = GetRig();
		const double Other = Rig != nullptr ? GetFraction(*Rig, MarkIndex, !bEaseIn) : 0.0;
		return TOptional<float>(static_cast<float>(100.0 - Other * 100.0));
	});
	Args.MinSliderValue = 0.0f;
	Args.MaxSliderValue = 100.0f;
	Args.Delta = 1.0f;
	Args.MaxFractionalDigits = 0;
	Args.TypeInterface = MakeUnitInterface(EUnit::Percentage);
	return MakeRow(Args.Name, Args.ToolTip, MakeRigSpinBox(Args,
		[MarkIndex, bEaseIn, GetFraction](const ARoscRig& Rig) { return static_cast<float>(GetFraction(Rig, MarkIndex, bEaseIn) * 100.0); },
		[MarkIndex, bEaseIn](ARoscRig& Rig, float Value)
		{
			if (Rig.Marks.IsValidIndex(MarkIndex))
			{
				FRoscMoveTiming& Timing = Rig.Marks[MarkIndex].Arrival;
				(bEaseIn ? Timing.EaseInFraction : Timing.EaseOutFraction) = Value / 100.0;
			}
		}));
}

TSharedRef<SWidget> SRoscGripTab::BuildOperatorRows()
{
	const FText StyleLabel = LOCTEXT("OperatorStyleLabel", "Style");
	const FText StyleToolTip = LOCTEXT("OperatorToolTip", "Whose hands the camera is in. Each adds its own small, human movement on top of the move when it is baked.");

	FSpinBoxArgs Intensity;
	Intensity.Name = LOCTEXT("IntensityLabel", "Intensity");
	Intensity.ToolTip = LOCTEXT("IntensityToolTip", "How much the operator moves. 100 % is a competent operator on a normal day, 0 % is perfectly still, 200 % is a rough day.");
	Intensity.TransactionName = LOCTEXT("ChangeIntensity", "Change Operator Intensity");
	Intensity.MinValue = TOptional<float>(0.0f);
	Intensity.MaxValue = TOptional<float>(200.0f);
	Intensity.Delta = 5.0f;
	Intensity.MaxFractionalDigits = 0;
	Intensity.TypeInterface = MakeUnitInterface(EUnit::Percentage);

	FSpinBoxArgs Take;
	Take.Name = LOCTEXT("TakeLabel", "Take");
	Take.ToolTip = LOCTEXT("TakeToolTip", "Every take moves a little differently. The same take number always gives exactly the same movement, so a bake can be repeated.");
	Take.TransactionName = LOCTEXT("ChangeTake", "Change Take");
	Take.MinValue = TOptional<float>(1.0f);
	Take.MaxValue = TOptional<float>(999.0f);
	Take.MaxSliderValue = 50.0f;
	Take.Delta = 1.0f;
	Take.MaxFractionalDigits = 0;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeRow(StyleLabel, StyleToolTip,
				SNew(SBox)
				.MinDesiredHeight(FRoscStyle::MinTargetSize)
				[
					SNew(SComboButton)
					.ToolTipText(StyleToolTip)
					.AccessibleText(StyleLabel)
					.OnGetMenuContent(this, &SRoscGripTab::MakeOperatorMenu)
					.ButtonContent()
					[
						SNew(STextBlock)
						.TextStyle(&TextStyle("Rosc.Value"))
						.Text_Lambda([this]
						{
							const ARoscRig* Rig = GetRig();
							return Rig != nullptr ? Rosc::Operator::GetStyleName(Rig->Operator.Style) : FText::GetEmpty();
						})
					]
				])
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			// A locked-off camera has no operator to be more or less steady.
			SNew(SVerticalBox)
			.IsEnabled_Lambda([this]
			{
				const ARoscRig* Rig = GetRig();
				return Rig != nullptr && Rig->Operator.Style != ERoscOperatorStyle::LockedOff;
			})
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FRoscStyle::RowPadding)
			[
				MakeRow(Intensity.Name, Intensity.ToolTip, MakeRigSpinBox(Intensity,
					[](const ARoscRig& Rig) { return Rig.Operator.Intensity * 100.0f; },
					[](ARoscRig& Rig, float Value) { Rig.Operator.Intensity = Value / 100.0f; }))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FRoscStyle::RowPadding)
			[
				MakeRow(Take.Name, Take.ToolTip, MakeRigSpinBox(Take,
					[](const ARoscRig& Rig) { return static_cast<float>(Rig.Operator.Seed); },
					[](ARoscRig& Rig, float Value) { Rig.Operator.Seed = FMath::RoundToInt32(Value); }))
			]
		];
}

TSharedRef<SWidget> SRoscGripTab::BuildBakeRows()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Body"))
			.AutoWrapText(true)
			.Text(this, &SRoscGripTab::GetBakeStatus)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeWarning(TAttribute<FText>::CreateSPLambda(this, [this]
			{
				const ARoscRig* Rig = GetRig();
				return Rig != nullptr && Rig->HasChangedSinceBake()
					? LOCTEXT("BakeOutOfDate", "The marks or the operator have changed since the move was baked. Sequencer still plays the move as it was: bake again to replace it.")
					: FText::GetEmpty();
			}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0f, FRoscStyle::RowSpacing + 4.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.MinDesiredHeight(FRoscStyle::MinTargetSize)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("BakeButton", "Bake to Sequencer"))
				.ToolTipText(LOCTEXT("BakeToolTip", "Write the move into Sequencer as keys on the support's axes, with the operator's movement on the camera, so it plays back and renders. Baking again replaces the move."))
				.IsEnabled_Lambda([this]
				{
					const ARoscRig* Rig = GetRig();
					return Rig != nullptr && Rig->Marks.Num() >= 2;
				})
				.OnClicked(this, &SRoscGripTab::OnBakeClicked)
			]
		];
}

FName SRoscGripTab::GetChosenDevice() const
{
	const TArray<FName> Devices = URoscOperatingSubsystem::GetDevices();
	if (Devices.Contains(ChosenDevice))
	{
		return ChosenDevice;
	}
	// A device paired through RoscCam is the one meant, unless another was chosen.
	const URoscCamServer* Server = URoscCamServer::Get();
	if (Server != nullptr && Devices.Contains(Server->GetSubjectName()))
	{
		return Server->GetSubjectName();
	}
	return Devices.IsEmpty() ? NAME_None : Devices[0];
}

bool SRoscGripTab::IsRoscCamServingCamera() const
{
	const URoscCamServer* Server = URoscCamServer::Get();
	return Server != nullptr && Server->IsRunning() && Server->GetCamera() == Target->GetCamera();
}

FText SRoscGripTab::GetRoscCamStatus() const
{
	const URoscCamServer* Server = URoscCamServer::Get();
	if (Server == nullptr || !Server->IsRunning())
	{
		return LOCTEXT("RoscCamOff", "Off. Start it, then open RoscCam on the phone or tablet; both need to be on the same network.");
	}
	if (!IsRoscCamServingCamera())
	{
		return FText::Format(LOCTEXT("RoscCamOtherCamera", "Running for {0}. Start it here to hand RoscCam this camera instead."),
			Server->GetCamera() != nullptr ? FText::FromString(Server->GetCamera()->GetActorLabel()) : FText::GetEmpty());
	}
	if (Server->IsPaired())
	{
		return Server->IsMotionArriving()
			? FText::Format(LOCTEXT("RoscCamPaired", "Paired with {0}, which gets this camera's picture. Its motion is arriving: it is listed as a device below."), FText::FromString(Server->GetDeviceName()))
			: FText::Format(LOCTEXT("RoscCamPairedQuiet", "Paired with {0}, which gets this camera's picture."), FText::FromString(Server->GetDeviceName()));
	}
	const FString Code = Server->GetPairingCode();
	if (!Code.IsEmpty())
	{
		return FText::Format(LOCTEXT("RoscCamPairing", "{0} asks to pair. Type {1} on the device. Nothing is sent to it until it is paired."),
			FText::FromString(Server->GetDeviceName()), FText::FromString(Code));
	}
	const FString Addresses = URoscCamServer::GetAddresses();
	return Addresses.IsEmpty()
		? LOCTEXT("RoscCamNoNetwork", "Waiting for RoscCam, but this computer is on no network a device can reach.")
		: FText::Format(LOCTEXT("RoscCamWaiting", "Waiting for RoscCam. The app finds this computer on the network, or give it the address {0}."), FText::FromString(Addresses));
}

FText SRoscGripTab::GetRoscCamWarning() const
{
	const URoscCamServer* Server = URoscCamServer::Get();
	if (IsRoscCamServingCamera() && Server->IsPaired() && !Server->IsMotionArriving())
	{
		return FText::Format(LOCTEXT("RoscCamSilent", "No motion is arriving from {0}. Check that RoscCam is open on it and the device is awake."), FText::FromString(Server->GetDeviceName()));
	}
	return FText::GetEmpty();
}

bool SRoscGripTab::IsOperatingRig() const
{
	const URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	return Operating != nullptr && Operating->IsOperating() && Operating->GetOperatedRig() == GetRig();
}

bool SRoscGripTab::HasTakeOfRig() const
{
	const URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	return Operating != nullptr && Operating->HasTake() && Operating->GetTakeRig() == GetRig();
}

FText SRoscGripTab::GetDeviceStatus() const
{
	const URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	if (!IsOperatingRig() || !Operating->IsDeviceLive())
	{
		return FText::GetEmpty();
	}
	if (Operating->IsRecording())
	{
		return FText::Format(LOCTEXT("DeviceRecording", "Recording a take from {0}: {1} s. Sequencer plays the shot while you operate."),
			FText::FromName(Operating->GetDevice()), Rosc::Units::FormatNumber(Operating->GetTakeSeconds(), 1));
	}
	return FText::Format(LOCTEXT("DeviceLive", "{0} has the head: turn the device to pan, tilt and roll. Zero takes the way you hold it now as the start."),
		FText::FromName(Operating->GetDevice()));
}

FText SRoscGripTab::GetDeviceWarning() const
{
	const URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	if (IsOperatingRig() && !Operating->IsDeviceLive())
	{
		return FText::Format(LOCTEXT("DeviceSilent", "No motion is arriving from {0}. Check that the app is running and connected to this computer."),
			FText::FromName(Operating->GetDevice()));
	}
	return FText::GetEmpty();
}

FText SRoscGripTab::GetTakeSummary() const
{
	if (!HasTakeOfRig())
	{
		return FText::GetEmpty();
	}
	const URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
	const ULevelSequence* Sequence = Operating->GetTakeSequence();
	return FText::Format(LOCTEXT("TakeSummary", "Take of {0} s ({1} frames) in {2}, from frame {3}. It is kept until you discard it or record another, so you can change the settings below and bake it again."),
		Rosc::Units::FormatNumber(Operating->GetTakeSeconds(), 1), FText::AsNumber(Operating->GetTake().Poses.Num()),
		Sequence != nullptr ? FText::FromString(Sequence->GetName()) : FText::GetEmpty(), FText::AsNumber(Operating->GetTakeStartFrame().Value));
}

FText SRoscGripTab::GetTakeWarnings() const
{
	const ARoscRig* Rig = GetRig();
	if (!HasTakeOfRig() || Rig == nullptr)
	{
		return FText::GetEmpty();
	}
	TArray<FString> Lines;
	for (const FRoscMoveFinding& Finding : URoscOperatingSubsystem::Get()->GetTakeFindings())
	{
		Lines.Add(Rosc::Take::Describe(Finding, Rig->GetSpec().Type, URoscEditorSettings::GetDistanceUnits()).ToString());
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

TSharedRef<SWidget> SRoscGripTab::MakeDeviceMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	const TArray<FName> Devices = URoscOperatingSubsystem::GetDevices();
	for (const FName Device : Devices)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromName(Device),
			FText::Format(LOCTEXT("DeviceEntryToolTip", "Operate the support with {0}."), FText::FromName(Device)),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSPLambda(this, [this, Device] { ChosenDevice = Device; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateSPLambda(this, [this, Device] { return GetChosenDevice() == Device; })),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	if (Devices.IsEmpty())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NoDeviceEntry", "No device found"),
			LOCTEXT("NoDeviceEntryToolTip", "Open the Unreal VCam app and connect it to this computer, or start another Live Link source, and it is listed here."),
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SRoscGripTab::BuildRoscCamRows()
{
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Rows->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};

	Add(FMargin(0.0f), MakeNote(LOCTEXT("RoscCamIntro", "RoscCam, Rosc's own app, makes a phone or tablet the camera: it shows this camera's picture, operates the support as you turn it, and carries the controls for a take.")));

	const TSharedRef<SWrapBox> Controls = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, FRoscStyle::RowSpacing));
	Controls->AddSlot()
	[
		MakeToggleButton(LOCTEXT("RoscCamButton", "RoscCam Server"),
			LOCTEXT("RoscCamToolTip", "Let RoscCam on a phone or tablet connect to this computer, for this camera. It is off until you start it, and stops when the editor closes. A device gets nothing until you pair it with the code shown here."),
			TAttribute<bool>::CreateSP(this, &SRoscGripTab::IsRoscCamServingCamera),
			[this](bool bTurnOn)
			{
				URoscCamServer* Server = URoscCamServer::Get();
				FText Error;
				if (!bTurnOn)
				{
					Server->Stop();
				}
				else if (!Server->Start(Target->GetCamera(), Error))
				{
					Notify(Error, false);
				}
			})
	];
	Controls->AddSlot()
	[
		MakeButton(LOCTEXT("UnpairButton", "Unpair"),
			LOCTEXT("UnpairToolTip", "Let the device go. It has to be paired again, with a new code, to get the picture or operate the support."),
			FOnClicked::CreateLambda([] { URoscCamServer::Get()->Unpair(); return FReply::Handled(); }),
			TAttribute<bool>::CreateSPLambda(this, [this] { return IsRoscCamServingCamera() && URoscCamServer::Get()->HasDevice(); }))
	];
	Add(FRoscStyle::ButtonRowPadding, Controls);
	Add(FRoscStyle::RowPadding, MakeStatusLine("Icons.InfoWithColor", TAttribute<FText>::CreateSP(this, &SRoscGripTab::GetRoscCamStatus)));
	Add(FRoscStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SRoscGripTab::GetRoscCamWarning)));
	return Rows;
}

TSharedRef<SWidget> SRoscGripTab::BuildDeviceRows()
{
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Rows->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};

	Add(FMargin(0.0f), MakeNote(LOCTEXT("DeviceIntro", "Operate the support with a phone or tablet held like a camera: RoscCam once it is paired above, the Unreal VCam app on an iPad or iPhone, or a tracker that Live Link brings in.")));

	// The shot on the device.
	if (URoscOperatingSubsystem::IsVirtualCameraOn())
	{
		Add(FRoscStyle::ButtonRowPadding, MakeToggleButton(LOCTEXT("ShowOnDevice", "Show on Device"),
			LOCTEXT("ShowOnDeviceToolTip", "Stream this camera's picture to the Unreal VCam app. On the device, open the app and connect it to this computer; the device is then listed below."),
			TAttribute<bool>::CreateSPLambda(this, [this] { return URoscOperatingSubsystem::IsShownOnDevice(Target->GetCamera()); }),
			[this](bool bTurnOn)
			{
				ACineCameraActor* Camera = Target->GetCamera();
				FText Error;
				if (!bTurnOn)
				{
					URoscOperatingSubsystem::StopShowingOnDevice(Camera);
				}
				else if (!URoscOperatingSubsystem::ShowOnDevice(Camera, Error))
				{
					Notify(Error, false);
				}
			}));
	}
	else
	{
		Add(FRoscStyle::RowPadding, MakeNote(LOCTEXT("VirtualCameraOff", "To show the shot on the device, Rosc uses the engine's Virtual Camera plugin, which is off in this project.")));
		Add(FRoscStyle::ButtonRowPadding, MakeButton(LOCTEXT("TurnOnVirtualCamera", "Turn On Virtual Camera and Restart"),
			LOCTEXT("TurnOnVirtualCameraToolTip", "Turn on the engine's Virtual Camera plugin for this project and restart the editor so that it loads. The editor offers to save your work before it closes."),
			FOnClicked::CreateLambda([]
			{
				FText Error;
				if (URoscOperatingSubsystem::TurnOnVirtualCamera(Error))
				{
					FUnrealEdMisc::Get().RestartEditor(true);
				}
				else
				{
					Notify(Error, false);
				}
				return FReply::Handled();
			})));
	}

	// The device and what it does.
	const FText DeviceLabel = LOCTEXT("DeviceLabel", "Device");
	const FText DeviceToolTip = LOCTEXT("DeviceToolTip", "The phone, tablet or tracker that operates the support. Every Live Link source that tracks a position and a direction is listed.");
	Add(FRoscStyle::RowPadding, MakeRow(DeviceLabel, DeviceToolTip,
		MakeComboButton(DeviceLabel, DeviceToolTip,
			TAttribute<FText>::CreateSPLambda(this, [this]
			{
				const FName Device = GetChosenDevice();
				return Device.IsNone() ? LOCTEXT("NoDeviceValue", "None found") : FText::FromName(Device);
			}),
			FOnGetContent::CreateSP(this, &SRoscGripTab::MakeDeviceMenu))));
	Add(FRoscStyle::RowPadding, MakeNote(TAttribute<FText>::CreateLambda([]
	{
		return URoscOperatingSubsystem::GetDevices().IsEmpty()
			? LOCTEXT("NoDevices", "No device found yet. Open the Unreal VCam app and connect it to this computer, or start another Live Link source, and it is listed here.")
			: FText::GetEmpty();
	})));

	const TSharedRef<SWrapBox> Controls = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, FRoscStyle::RowSpacing));
	Controls->AddSlot()
	[
		SNew(SBox)
		.IsEnabled_Lambda([this] { return IsOperatingRig() || !GetChosenDevice().IsNone(); })
		[
			MakeToggleButton(LOCTEXT("OperateButton", "Operate"),
				LOCTEXT("OperateToolTip", "Hand the support's head to the device: as you turn it, the head pans, tilts and rolls, and a Steadicam, handheld rig or drone also goes where you carry it. Turn it off to keep where the device left the support, as one undo step."),
				TAttribute<bool>::CreateSP(this, &SRoscGripTab::IsOperatingRig),
				[this](bool bTurnOn)
				{
					URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
					FText Error;
					if (!bTurnOn)
					{
						Operating->StopOperating();
					}
					else if (!Operating->StartOperating(GetRig(), GetChosenDevice(), Error))
					{
						Notify(Error, false);
					}
				})
		]
	];
	Controls->AddSlot()
	[
		MakeButton(LOCTEXT("ZeroButton", "Zero"),
			LOCTEXT("ZeroToolTip", "Take the way you hold the device now as the support's present direction, to rest your arms or turn round without moving the shot."),
			FOnClicked::CreateLambda([] { URoscOperatingSubsystem::Get()->Zero(); return FReply::Handled(); }),
			TAttribute<bool>::CreateSP(this, &SRoscGripTab::IsOperatingRig))
	];
	Controls->AddSlot()
	[
		SNew(SBox)
		.IsEnabled_Lambda([this] { return IsOperatingRig() && URoscOperatingSubsystem::Get()->IsDeviceLive(); })
		[
			MakeToggleButton(
				TAttribute<FText>::CreateLambda([] { return URoscOperatingSubsystem::Get()->IsRecording() ? LOCTEXT("StopRecordingButton", "Stop") : LOCTEXT("RecordButton", "Record"); }),
				LOCTEXT("RecordToolTip", "Record a take: Sequencer plays the open shot from the playhead while you operate, and the device's motion is kept one frame at a time. With no sequence open, one is made."),
				TAttribute<bool>::CreateLambda([] { return URoscOperatingSubsystem::Get()->IsRecording(); }),
				[](bool bTurnOn)
				{
					URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
					FText Error;
					if (!bTurnOn)
					{
						Operating->StopRecording();
					}
					else if (!Operating->StartRecording(Error))
					{
						Notify(Error, false);
					}
				})
		]
	];
	Add(FRoscStyle::ButtonRowPadding, Controls);
	Add(FRoscStyle::RowPadding, MakeStatusLine("Icons.InfoWithColor", TAttribute<FText>::CreateSP(this, &SRoscGripTab::GetDeviceStatus)));
	Add(FRoscStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SRoscGripTab::GetDeviceWarning)));

	Rows->AddSlot()
	.AutoHeight()
	[
		SNew(SBox)
		.Visibility_Lambda([this] { return HasTakeOfRig() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			BuildTakeRows()
		]
	];
	return Rows;
}

TSharedRef<SWidget> SRoscGripTab::BuildTakeRows()
{
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Rows->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	// The processing only changes how the take will be baked, so it is not an undo step.
	const auto EditProcessing = [](TFunction<void(FRoscTakeProcessing&)> Edit)
	{
		URoscOperatingSubsystem* Operating = URoscOperatingSubsystem::Get();
		FRoscTakeProcessing Processing = Operating->GetProcessing();
		Edit(Processing);
		Operating->SetProcessing(Processing);
	};

	Add(FRoscStyle::RowPadding, MakeNote(TAttribute<FText>::CreateSP(this, &SRoscGripTab::GetTakeSummary)));

	TArray<FChoice> Smoothings;
	for (const ERoscTakeSmoothing Smoothing : { ERoscTakeSmoothing::Off, ERoscTakeSmoothing::Light, ERoscTakeSmoothing::Medium, ERoscTakeSmoothing::Heavy })
	{
		Smoothings.Add({ Rosc::Take::GetSmoothingName(Smoothing),
			[Smoothing] { return URoscOperatingSubsystem::Get()->GetProcessing().Smoothing == Smoothing; },
			[EditProcessing, Smoothing] { EditProcessing([Smoothing](FRoscTakeProcessing& Processing) { Processing.Smoothing = Smoothing; }); } });
	}
	Add(FRoscStyle::RowPadding, MakeChoiceRow(LOCTEXT("SmoothingLabel", "Smoothing"),
		LOCTEXT("SmoothingToolTip", "Takes the jitter out of the take: the tremor of a hand and the noise of the tracking. It adds no lag: a move starts and stops on the frames it did."),
		TAttribute<FText>::CreateLambda([] { return Rosc::Take::GetSmoothingName(URoscOperatingSubsystem::Get()->GetProcessing().Smoothing); }),
		MoveTemp(Smoothings)));

	FSpinBoxArgs Stabilise;
	Stabilise.Name = LOCTEXT("StabiliseLabel", "Stabilise");
	Stabilise.ToolTip = LOCTEXT("StabiliseToolTip", "Takes the operator's shake out and keeps the move, like a stabilised head. 0 % leaves the shake in; more stabilising also softens quick moves, so a whip pan lands a little more gently.");
	Stabilise.TransactionName = LOCTEXT("StabiliseTransaction", "Stabilise Take");
	Stabilise.Value = TAttribute<float>::CreateLambda([] { return static_cast<float>(URoscOperatingSubsystem::Get()->GetProcessing().Stabilise * 100.0); });
	Stabilise.MinValue = TOptional<float>(0.0f);
	Stabilise.MaxValue = TOptional<float>(100.0f);
	Stabilise.Delta = 5.0f;
	Stabilise.MaxFractionalDigits = 0;
	Stabilise.TypeInterface = MakeUnitInterface(EUnit::Percentage);
	Stabilise.OnDrag = [EditProcessing](float Percent) { EditProcessing([Percent](FRoscTakeProcessing& Processing) { Processing.Stabilise = Percent / 100.0; }); };
	Stabilise.OnCommit = Stabilise.OnDrag;
	const FText StabiliseName = Stabilise.Name;
	const TAttribute<FText> StabiliseToolTip = Stabilise.ToolTip;
	Add(FRoscStyle::RowPadding, MakeRow(StabiliseName, StabiliseToolTip, MakeSpinBox(MoveTemp(Stabilise))));

	const TSharedRef<SWrapBox> Locks = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, FRoscStyle::RowSpacing));
	const auto AddLock = [&Locks, EditProcessing](const FText& Label, const FText& ToolTip, bool FRoscTakeProcessing::* Lock)
	{
		Locks->AddSlot()
		[
			MakeToggleButton(Label, ToolTip,
				TAttribute<bool>::CreateLambda([Lock] { return URoscOperatingSubsystem::Get()->GetProcessing().*Lock; }),
				[EditProcessing, Lock](bool bTurnOn) { EditProcessing([Lock, bTurnOn](FRoscTakeProcessing& Processing) { Processing.*Lock = bTurnOn; }); })
		];
	};
	AddLock(LOCTEXT("LockPan", "Lock Pan"), LOCTEXT("LockPanToolTip", "Hold the pan where the take began."), &FRoscTakeProcessing::bLockPan);
	AddLock(LOCTEXT("LockTilt", "Lock Tilt"), LOCTEXT("LockTiltToolTip", "Hold the tilt where the take began."), &FRoscTakeProcessing::bLockTilt);
	AddLock(LOCTEXT("LockRoll", "Lock Roll"), LOCTEXT("LockRollToolTip", "Hold the roll where the take began, so the horizon stays as it was."), &FRoscTakeProcessing::bLockRoll);
	if (const ARoscRig* Rig = GetRig(); Rig != nullptr && Rosc::Take::IsFreeMoving(Rig->GetSpec().Type))
	{
		AddLock(LOCTEXT("LockPosition", "Lock Position"), LOCTEXT("LockPositionToolTip", "Hold the support where the take began, so only the head moves."), &FRoscTakeProcessing::bLockPosition);
	}
	Add(FRoscStyle::ButtonRowPadding, Locks);
	Add(FRoscStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SRoscGripTab::GetTakeWarnings)));

	const TSharedRef<SWrapBox> Buttons = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, FRoscStyle::RowSpacing));
	Buttons->AddSlot()
	[
		SNew(SBox)
		.MinDesiredHeight(FRoscStyle::MinTargetSize)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
			.VAlign(VAlign_Center)
			.Text(LOCTEXT("BakeTakeButton", "Bake Take"))
			.ToolTipText(LOCTEXT("BakeTakeToolTip", "Write the take, smoothed, stabilised and locked as set above, into Sequencer as a key on every frame of the support's axes, from the frame where recording began. Keys already there in that time are replaced."))
			.IsEnabled_Lambda([] { return !URoscOperatingSubsystem::Get()->IsRecording(); })
			.OnClicked_Lambda([]
			{
				FText Error;
				const ULevelSequence* Sequence = URoscOperatingSubsystem::Get()->BakeTake(Error);
				Notify(Sequence != nullptr ? FText::Format(LOCTEXT("TakeBaked", "Take baked into {0}."), FText::FromString(Sequence->GetName())) : Error, Sequence != nullptr);
				return FReply::Handled();
			})
		]
	];
	Buttons->AddSlot()
	[
		MakeButton(LOCTEXT("DiscardTakeButton", "Discard Take"),
			LOCTEXT("DiscardTakeToolTip", "Throw the take away. What was baked from it stays in Sequencer."),
			FOnClicked::CreateLambda([] { URoscOperatingSubsystem::Get()->DiscardTake(); return FReply::Handled(); }))
	];
	Add(FRoscStyle::ButtonRowPadding, Buttons);
	return Rows;
}

TSharedRef<SWidget> SRoscGripTab::MakeSupportMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection(NAME_None);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("NoSupport", "No support"),
		LOCTEXT("NoSupportToolTip", "Take the camera off its support. The camera stays where it is; the support and its marks are removed."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSPLambda(this, [this] { Rosc::GripEditing::TakeCameraOffRig(Target->GetCamera()); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateSPLambda(this, [this] { return GetRig() == nullptr; })),
		NAME_None,
		EUserInterfaceActionType::RadioButton);
	MenuBuilder.EndSection();

	// Presets come grouped by type: one menu section per kind of support.
	TOptional<ERoscRigType> OpenType;
	for (const FRoscRigPreset& Preset : Rosc::Rig::GetPresets())
	{
		if (OpenType != Preset.Spec.Type)
		{
			if (OpenType.IsSet())
			{
				MenuBuilder.EndSection();
			}
			MenuBuilder.BeginSection(NAME_None, Rosc::Rig::GetTypeName(Preset.Spec.Type));
			OpenType = Preset.Spec.Type;
		}

		const FRoscRigPreset* PresetPtr = &Preset;
		MenuBuilder.AddMenuEntry(
			Preset.Name,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSPLambda(this, [this, PresetPtr] { Rosc::GripEditing::PutCameraOnRig(Target->GetCamera(), *PresetPtr); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateSPLambda(this, [this, PresetPtr]
				{
					const ARoscRig* Rig = GetRig();
					return Rig != nullptr && Rig->GetSpec().PresetId == PresetPtr->Id;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	if (OpenType.IsSet())
	{
		MenuBuilder.EndSection();
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SRoscGripTab::MakeOperatorMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	const UEnum* Styles = StaticEnum<ERoscOperatorStyle>();
	// The last entry of a reflected enum is its hidden maximum.
	for (int32 Index = 0; Index < Styles->NumEnums() - 1; ++Index)
	{
		const ERoscOperatorStyle Style = static_cast<ERoscOperatorStyle>(Styles->GetValueByIndex(Index));
		MenuBuilder.AddMenuEntry(
			Rosc::Operator::GetStyleName(Style),
			Styles->GetToolTipTextByIndex(Index),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSPLambda(this, [this, Style]
				{
					Rosc::GripEditing::EditRig(GetRig(), LOCTEXT("ChangeOperator", "Change Operator"), [Style](ARoscRig& Rig) { Rig.Operator.Style = Style; });
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateSPLambda(this, [this, Style]
				{
					const ARoscRig* Rig = GetRig();
					return Rig != nullptr && Rig->Operator.Style == Style;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SRoscGripTab::MakeRigSpinBox(FSpinBoxArgs Args, TFunction<float(const ARoscRig&)> GetValue, TFunction<void(ARoscRig&, float)> SetValue)
{
	Args.Value = TAttribute<float>::CreateSPLambda(this, [this, GetValue = MoveTemp(GetValue)]
	{
		const ARoscRig* Rig = GetRig();
		return Rig != nullptr ? GetValue(*Rig) : 0.0f;
	});
	Args.SaveForUndo = [this]
	{
		if (ARoscRig* Rig = GetRig())
		{
			Rig->Modify();
		}
	};
	Args.OnDrag = [this, SetValue](float NewValue)
	{
		if (ARoscRig* Rig = GetRig())
		{
			SetValue(*Rig, NewValue);
			GEditor->RedrawLevelEditingViewports();
		}
	};
	Args.OnCommit = [this, SetValue, Description = Args.TransactionName](float NewValue)
	{
		Rosc::GripEditing::EditRig(GetRig(), Description, [&SetValue, NewValue](ARoscRig& Rig) { SetValue(Rig, NewValue); });
	};
	return MakeSpinBox(MoveTemp(Args));
}

ARoscRig* SRoscGripTab::GetRig() const
{
	return Rosc::GripEditing::FindRig(Target->GetCamera());
}

FText SRoscGripTab::GetSupportName() const
{
	const ARoscRig* Rig = GetRig();
	if (Rig == nullptr)
	{
		return LOCTEXT("NoSupport", "No support");
	}
	const FRoscRigSpec Spec = Rig->GetSpec();
	const FRoscRigPreset* Preset = Rosc::Rig::FindPreset(Spec.PresetId);
	return Preset != nullptr ? Preset->Name : Rosc::Rig::GetTypeName(Spec.Type);
}

FText SRoscGripTab::GetMoveWarnings(int32 MarkIndex) const
{
	const ARoscRig* Rig = GetRig();
	if (Rig == nullptr || MarkIndex < 1 || !Rig->Marks.IsValidIndex(MarkIndex))
	{
		return FText::GetEmpty();
	}

	const FRoscRigSpec Spec = Rig->GetSpec();
	const FRoscRigMark& From = Rig->Marks[MarkIndex - 1];
	const FRoscRigMark& To = Rig->Marks[MarkIndex];
	TArray<FText> Lines;
	for (const FRoscMoveFinding& Finding : Rosc::Move::MostLimiting(Rosc::Move::Check(Spec, From.Pose, To.Pose, To.Arrival)))
	{
		Lines.Add(Rosc::Move::Describe(Finding, Spec.Type, URoscEditorSettings::GetDistanceUnits()));
	}

	Rosc::GripEditing::FDollyZoom Zoom;
	if (Rosc::GripEditing::PlanDollyZoom(Rig, MarkIndex, Zoom))
	{
		FNumberFormattingOptions Whole;
		Whole.SetMaximumFractionalDigits(0);
		const auto Millimetres = [&Whole](double Value) { return FText::AsNumber(FMath::RoundToInt32(Value), &Whole); };
		if (!Zoom.IsZoomLens())
		{
			Lines.Add(FText::Format(LOCTEXT("DollyZoomPrimeWarning", "A dolly zoom needs a zoom lens, and the lens on this camera is a {0} mm prime, so it will not zoom. Choose a zoom in the Camera tab."), Millimetres(Zoom.LensWidestMm)));
		}
		else if (!Zoom.IsWithinLens())
		{
			Lines.Add(FText::Format(LOCTEXT("DollyZoomRangeWarning", "This dolly zoom asks the lens for {0} to {1} mm, and it zooms from {2} to {3} mm, so it stops at the end of its range. Shorten the travel, or start on a focal length nearer the middle of the zoom."),
				Millimetres(Zoom.WidestMm), Millimetres(Zoom.LongestMm), Millimetres(Zoom.LensWidestMm), Millimetres(Zoom.LensLongestMm)));
		}
		if (!Zoom.bFocusHeld)
		{
			Lines.Add(LOCTEXT("DollyZoomFocusWarning", "The focus is set by hand, so the subject goes soft as the camera travels. Hold the focus on the subject in the Focus tab."));
		}
	}
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

FText SRoscGripTab::GetBakeStatus() const
{
	const ARoscRig* Rig = GetRig();
	if (Rig == nullptr || Rig->Marks.Num() < 2)
	{
		return LOCTEXT("BakeStatusNeedsMarks", "Mark at least two positions, then bake the move between them to Sequencer.");
	}

	const FText Move = FText::Format(LOCTEXT("BakeStatusMove", "The move runs through {0} marks in {1} s."),
		FText::AsNumber(Rig->Marks.Num()), Rosc::Units::FormatNumber(Rosc::Move::GetTotalDuration(Rig->Marks), 1));
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	return OpenSequence != nullptr
		? FText::Format(LOCTEXT("BakeStatusOpenSequence", "{0} It will be baked into {1}, starting at the playhead."), Move, OpenSequence->GetDisplayName())
		: FText::Format(LOCTEXT("BakeStatusNewSequence", "{0} No sequence is open in Sequencer, so a new one will be made for it."), Move);
}

FReply SRoscGripTab::OnBakeClicked()
{
	FText Error;
	const ULevelSequence* Sequence = Rosc::GripEditing::BakeMoveToSequencer(GetRig(), Error);
	Notify(Sequence != nullptr
		? FText::Format(LOCTEXT("BakeDone", "The move is baked into {0}. Play it in Sequencer."), Sequence->GetDisplayName())
		: Error, Sequence != nullptr);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
