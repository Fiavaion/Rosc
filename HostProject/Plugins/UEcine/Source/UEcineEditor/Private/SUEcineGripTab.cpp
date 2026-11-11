#include "SUEcineGripTab.h"

#include "CineCameraActor.h"
#include "Components/SplineComponent.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Math/UnitConversion.h"
#include "Styling/AppStyle.h"
#include "UEcineCameraTarget.h"
#include "UEcineEditorSettings.h"
#include "UEcineGripEditing.h"
#include "UEcineMove.h"
#include "UEcineOperator.h"
#include "UEcineRig.h"
#include "UEcineStyle.h"
#include "UEcineUnits.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

using namespace UEcine::Widgets;

namespace
{
	/**
	 * Writes and reads a rig axis the way the crew says it: 3.2 m or 10' 6" for distances,
	 * 45° for angles. The value itself stays in centimetres or degrees.
	 */
	class FAxisTypeInterface : public TDefaultNumericTypeInterface<float>
	{
	public:
		explicit FAxisTypeInterface(EUEcineRigAxis InAxis)
			: Axis(InAxis)
		{
		}

		virtual FString ToString(const float& Value) const override
		{
			return UEcine::Rig::FormatAxisValue(Axis, Value, UUEcineEditorSettings::GetDistanceUnits()).ToString();
		}

		virtual TOptional<float> FromString(const FString& InString, const float& ExistingValue) override
		{
			// Text the user did not change keeps the exact value, not the rounded one on show.
			if (InString == ToString(ExistingValue))
			{
				return ExistingValue;
			}
			const TOptional<double> Parsed = UEcine::Rig::ParseAxisValue(Axis, InString, UUEcineEditorSettings::GetDistanceUnits());
			return Parsed.IsSet() ? TOptional<float>(static_cast<float>(Parsed.GetValue())) : TOptional<float>();
		}

		virtual bool IsCharacterValid(TCHAR InChar) const override
		{
			return true;
		}

	private:
		EUEcineRigAxis Axis;
	};

	/** A number shown with its unit by the engine: "4.0 s", "25 %". */
	TSharedPtr<INumericTypeInterface<float>> MakeUnitInterface(EUnit Unit)
	{
		return MakeShared<TNumericUnitTypeInterface<float>>(Unit);
	}

	void SetAxis(AUEcineRig& Rig, EUEcineRigAxis Axis, double Value)
	{
		FUEcineRigPose Pose = Rig.GetPose();
		Pose.Set(Axis, Value);
		Rig.SetPose(Pose);
	}

}

void SUEcineGripTab::Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget)
{
	Target = InTarget;
	CurrentLayoutKey = MakeLayoutKey();
	RebuildLayout();
}

void SUEcineGripTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const FLayoutKey NewKey = MakeLayoutKey();
	if (!(NewKey == CurrentLayoutKey))
	{
		CurrentLayoutKey = NewKey;
		RebuildLayout();
	}
	else if (const AUEcineRig* Rig = GetRig(); Rig != nullptr && Rig->Marks.Num() != ShownMarkCount)
	{
		RebuildMarks();
	}
}

SUEcineGripTab::FLayoutKey SUEcineGripTab::MakeLayoutKey() const
{
	FLayoutKey Key;
	Key.Camera = Target->GetCamera();
	Key.Units = UUEcineEditorSettings::GetDistanceUnits();
	if (const AUEcineRig* Rig = GetRig())
	{
		Key.Rig = Rig;
		Key.PresetId = Rig->GetSpec().PresetId;
	}
	return Key;
}

void SUEcineGripTab::RebuildLayout()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	MarksBox.Reset();

	if (CurrentLayoutKey.Camera.IsValid())
	{
		Add(FMargin(0.0f), MakeSectionHeader(LOCTEXT("SupportSection", "Support")));
		Add(FUEcineStyle::RowPadding, BuildSupportRow());

		if (const AUEcineRig* Rig = CurrentLayoutKey.Rig.Get())
		{
			const FUEcineRigSpec Spec = Rig->GetSpec();
			if (Spec.Type == EUEcineRigType::Dolly)
			{
				Add(FUEcineStyle::RowPadding, BuildTrackRow());
			}

			Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("PositionSection", "Position")));
			for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
			{
				if (Spec.Axes[AxisIndex].bUsed)
				{
					Add(FUEcineStyle::RowPadding, BuildAxisRow(Spec.Type, static_cast<EUEcineRigAxis>(AxisIndex)));
				}
			}

			Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("MarksSection", "Marks")));
			Add(FMargin(0.0f), SAssignNew(MarksBox, SVerticalBox));
			Add(FUEcineStyle::ButtonRowPadding,
				MakeButton(LOCTEXT("MarkPositionButton", "Mark Position"),
					LOCTEXT("MarkPositionToolTip", "Store where the support is now as the next mark. A move runs from each mark to the one after it."),
					FOnClicked::CreateSPLambda(this, [this]
					{
						UEcine::GripEditing::AddMark(GetRig());
						return FReply::Handled();
					})));

			Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("OperatorSection", "Operator")));
			Add(FUEcineStyle::RowPadding, BuildOperatorRows());

			Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("SequencerSection", "Sequencer")));
			Add(FUEcineStyle::RowPadding, BuildBakeRows());
		}
		else
		{
			Add(FUEcineStyle::SectionPadding,
				SNew(STextBlock)
				.TextStyle(&TextStyle("UEcine.Body"))
				.AutoWrapText(true)
				.Text(LOCTEXT("NoSupportMessage", "The camera is free: it goes wherever you put it, with nothing to limit it. Put it on a support to move it the way real equipment moves, block a move between marks, and bake the move to Sequencer.")));
		}
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

	RebuildMarks();
}

void SUEcineGripTab::RebuildMarks()
{
	const AUEcineRig* Rig = GetRig();
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
		Add(FUEcineStyle::RowPadding,
			SNew(STextBlock)
			.TextStyle(&TextStyle("UEcine.Body"))
			.AutoWrapText(true)
			.Text(LOCTEXT("NoMarksMessage", "No marks yet. Set the support where the shot starts and mark the position, then do the same for where it ends.")));
	}

	// In shooting order: a mark, then the move that leaves it for the next mark.
	for (int32 MarkIndex = 0; MarkIndex < ShownMarkCount; ++MarkIndex)
	{
		if (MarkIndex > 0)
		{
			Add(FUEcineStyle::RowPadding, BuildMoveTimeRow(MarkIndex));
			Add(FUEcineStyle::RowPadding, BuildEaseRow(MarkIndex, true));
			Add(FUEcineStyle::RowPadding, BuildEaseRow(MarkIndex, false));
			Add(FUEcineStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SUEcineGripTab::GetMoveWarnings, MarkIndex)));
		}
		Add(FUEcineStyle::ButtonRowPadding, BuildMarkRow(MarkIndex));
	}
}

TSharedRef<SWidget> SUEcineGripTab::BuildSupportRow()
{
	const FText Label = LOCTEXT("SupportLabel", "Camera on");
	const FText ToolTip = LOCTEXT("SupportToolTip", "What the camera is mounted on. The support is stood up under the camera, so the camera does not move when you choose one.");
	return MakeRow(Label, ToolTip,
		SNew(SBox)
		.MinDesiredHeight(FUEcineStyle::MinTargetSize)
		[
			SNew(SComboButton)
			.ToolTipText(ToolTip)
			.AccessibleText(Label)
			.OnGetMenuContent(this, &SUEcineGripTab::MakeSupportMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.TextStyle(&TextStyle("UEcine.Value"))
				.Text(this, &SUEcineGripTab::GetSupportName)
			]
		]);
}

TSharedRef<SWidget> SUEcineGripTab::BuildTrackRow()
{
	FSpinBoxArgs Args;
	Args.Name = LOCTEXT("TrackLengthLabel", "Track length");
	Args.ToolTip = LOCTEXT("TrackToolTip", "Length of track laid for the dolly. Changing it lays the track straight; to curve the track, select the dolly and move the track's points in the viewport.");
	Args.TransactionName = LOCTEXT("ChangeTrack", "Lay Track");
	Args.MinValue = TOptional<float>(100.0f);
	Args.MaxValue = TOptional<float>(10000.0f);
	Args.MaxSliderValue = 3000.0f;
	Args.Delta = 10.0f;
	Args.TypeInterface = MakeShared<FAxisTypeInterface>(EUEcineRigAxis::Travel);
	return MakeRow(Args.Name, Args.ToolTip, MakeRigSpinBox(Args,
		[](const AUEcineRig& Rig) { return Rig.GetTrackLength(); },
		[](AUEcineRig& Rig, float Value)
		{
			Rig.GetTrack()->Modify();
			Rig.SetStraightTrack(Value);
		}));
}

TSharedRef<SWidget> SUEcineGripTab::BuildAxisRow(EUEcineRigType Type, EUEcineRigAxis Axis)
{
	const FText Name = UEcine::Rig::GetAxisName(Type, Axis);
	const auto GetLimits = [this, Axis]
	{
		const AUEcineRig* Rig = GetRig();
		return Rig != nullptr ? Rig->GetSpec().GetAxis(Axis) : FUEcineAxisLimits();
	};

	FSpinBoxArgs Args;
	Args.Name = Name;
	Args.ToolTip = TAttribute<FText>::CreateSPLambda(this, [Axis, Name, GetLimits]
	{
		const FUEcineAxisLimits Limits = GetLimits();
		const EUEcineDistanceUnits Units = UUEcineEditorSettings::GetDistanceUnits();
		const FText Range = FText::Format(LOCTEXT("AxisRangeToolTip", "{0}: from {1} to {2}."), Name,
			UEcine::Rig::FormatAxisValue(Axis, Limits.Min, Units), UEcine::Rig::FormatAxisValue(Axis, Limits.Max, Units));
		return Limits.IsFixedDuringShot()
			? FText::Format(LOCTEXT("AxisFixedToolTip", "{0} It is set when the support is built and cannot change during a shot."), Range)
			: Range;
	});
	Args.TransactionName = FText::Format(LOCTEXT("ChangeAxis", "Set {0}"), Name);
	Args.MinValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [GetLimits] { return TOptional<float>(static_cast<float>(GetLimits().Min)); });
	Args.MaxValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [GetLimits] { return TOptional<float>(static_cast<float>(GetLimits().Max)); });
	Args.Delta = UEcine::Rig::IsDistanceAxis(Axis) ? 1.0f : 0.1f;
	Args.TypeInterface = MakeShared<FAxisTypeInterface>(Axis);
	return MakeRow(Name, Args.ToolTip, MakeRigSpinBox(Args,
		[Axis](const AUEcineRig& Rig) { return static_cast<float>(Rig.GetPose().Get(Axis)); },
		[Axis](AUEcineRig& Rig, float Value) { SetAxis(Rig, Axis, Value); }));
}

TSharedRef<SWidget> SUEcineGripTab::BuildMarkRow(int32 MarkIndex)
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
					UEcine::GripEditing::GoToMark(GetRig(), MarkIndex);
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
					UEcine::GripEditing::UpdateMark(GetRig(), MarkIndex);
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
				UEcine::GripEditing::RemoveMark(GetRig(), MarkIndex);
				return FReply::Handled();
			}))
		];
}

TSharedRef<SWidget> SUEcineGripTab::BuildMoveTimeRow(int32 MarkIndex)
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
		[MarkIndex](const AUEcineRig& Rig) { return Rig.Marks.IsValidIndex(MarkIndex) ? static_cast<float>(Rig.Marks[MarkIndex].Arrival.DurationSeconds) : 0.0f; },
		[MarkIndex](AUEcineRig& Rig, float Value)
		{
			if (Rig.Marks.IsValidIndex(MarkIndex))
			{
				Rig.Marks[MarkIndex].Arrival.DurationSeconds = Value;
			}
		}));
}

TSharedRef<SWidget> SUEcineGripTab::BuildEaseRow(int32 MarkIndex, bool bEaseIn)
{
	// The two eases share the move, so each can take only what the other leaves.
	const auto GetFraction = [](const AUEcineRig& Rig, int32 Index, bool bIn)
	{
		if (!Rig.Marks.IsValidIndex(Index))
		{
			return 0.0;
		}
		const FUEcineMoveTiming& Timing = Rig.Marks[Index].Arrival;
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
		const AUEcineRig* Rig = GetRig();
		const double Other = Rig != nullptr ? GetFraction(*Rig, MarkIndex, !bEaseIn) : 0.0;
		return TOptional<float>(static_cast<float>(100.0 - Other * 100.0));
	});
	Args.MinSliderValue = 0.0f;
	Args.MaxSliderValue = 100.0f;
	Args.Delta = 1.0f;
	Args.MaxFractionalDigits = 0;
	Args.TypeInterface = MakeUnitInterface(EUnit::Percentage);
	return MakeRow(Args.Name, Args.ToolTip, MakeRigSpinBox(Args,
		[MarkIndex, bEaseIn, GetFraction](const AUEcineRig& Rig) { return static_cast<float>(GetFraction(Rig, MarkIndex, bEaseIn) * 100.0); },
		[MarkIndex, bEaseIn](AUEcineRig& Rig, float Value)
		{
			if (Rig.Marks.IsValidIndex(MarkIndex))
			{
				FUEcineMoveTiming& Timing = Rig.Marks[MarkIndex].Arrival;
				(bEaseIn ? Timing.EaseInFraction : Timing.EaseOutFraction) = Value / 100.0;
			}
		}));
}

TSharedRef<SWidget> SUEcineGripTab::BuildOperatorRows()
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
				.MinDesiredHeight(FUEcineStyle::MinTargetSize)
				[
					SNew(SComboButton)
					.ToolTipText(StyleToolTip)
					.AccessibleText(StyleLabel)
					.OnGetMenuContent(this, &SUEcineGripTab::MakeOperatorMenu)
					.ButtonContent()
					[
						SNew(STextBlock)
						.TextStyle(&TextStyle("UEcine.Value"))
						.Text_Lambda([this]
						{
							const AUEcineRig* Rig = GetRig();
							return Rig != nullptr ? UEcine::Operator::GetStyleName(Rig->Operator.Style) : FText::GetEmpty();
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
				const AUEcineRig* Rig = GetRig();
				return Rig != nullptr && Rig->Operator.Style != EUEcineOperatorStyle::LockedOff;
			})
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FUEcineStyle::RowPadding)
			[
				MakeRow(Intensity.Name, Intensity.ToolTip, MakeRigSpinBox(Intensity,
					[](const AUEcineRig& Rig) { return Rig.Operator.Intensity * 100.0f; },
					[](AUEcineRig& Rig, float Value) { Rig.Operator.Intensity = Value / 100.0f; }))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FUEcineStyle::RowPadding)
			[
				MakeRow(Take.Name, Take.ToolTip, MakeRigSpinBox(Take,
					[](const AUEcineRig& Rig) { return static_cast<float>(Rig.Operator.Seed); },
					[](AUEcineRig& Rig, float Value) { Rig.Operator.Seed = FMath::RoundToInt32(Value); }))
			]
		];
}

TSharedRef<SWidget> SUEcineGripTab::BuildBakeRows()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("UEcine.Body"))
			.AutoWrapText(true)
			.Text(this, &SUEcineGripTab::GetBakeStatus)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.Padding(0.0f, FUEcineStyle::RowSpacing + 4.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.MinDesiredHeight(FUEcineStyle::MinTargetSize)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("BakeButton", "Bake to Sequencer"))
				.ToolTipText(LOCTEXT("BakeToolTip", "Write the move into Sequencer as keys on the support's axes, with the operator's movement on the camera, so it plays back and renders. Baking again replaces the move."))
				.IsEnabled_Lambda([this]
				{
					const AUEcineRig* Rig = GetRig();
					return Rig != nullptr && Rig->Marks.Num() >= 2;
				})
				.OnClicked(this, &SUEcineGripTab::OnBakeClicked)
			]
		];
}

TSharedRef<SWidget> SUEcineGripTab::MakeSupportMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection(NAME_None);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("NoSupport", "No support"),
		LOCTEXT("NoSupportToolTip", "Take the camera off its support. The camera stays where it is; the support and its marks are removed."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSPLambda(this, [this] { UEcine::GripEditing::TakeCameraOffRig(Target->GetCamera()); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateSPLambda(this, [this] { return GetRig() == nullptr; })),
		NAME_None,
		EUserInterfaceActionType::RadioButton);
	MenuBuilder.EndSection();

	// Presets come grouped by type: one menu section per kind of support.
	TOptional<EUEcineRigType> OpenType;
	for (const FUEcineRigPreset& Preset : UEcine::Rig::GetPresets())
	{
		if (OpenType != Preset.Spec.Type)
		{
			if (OpenType.IsSet())
			{
				MenuBuilder.EndSection();
			}
			MenuBuilder.BeginSection(NAME_None, UEcine::Rig::GetTypeName(Preset.Spec.Type));
			OpenType = Preset.Spec.Type;
		}

		const FUEcineRigPreset* PresetPtr = &Preset;
		MenuBuilder.AddMenuEntry(
			Preset.Name,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSPLambda(this, [this, PresetPtr] { UEcine::GripEditing::PutCameraOnRig(Target->GetCamera(), *PresetPtr); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateSPLambda(this, [this, PresetPtr]
				{
					const AUEcineRig* Rig = GetRig();
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

TSharedRef<SWidget> SUEcineGripTab::MakeOperatorMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	const UEnum* Styles = StaticEnum<EUEcineOperatorStyle>();
	// The last entry of a reflected enum is its hidden maximum.
	for (int32 Index = 0; Index < Styles->NumEnums() - 1; ++Index)
	{
		const EUEcineOperatorStyle Style = static_cast<EUEcineOperatorStyle>(Styles->GetValueByIndex(Index));
		MenuBuilder.AddMenuEntry(
			UEcine::Operator::GetStyleName(Style),
			Styles->GetToolTipTextByIndex(Index),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSPLambda(this, [this, Style]
				{
					UEcine::GripEditing::EditRig(GetRig(), LOCTEXT("ChangeOperator", "Change Operator"), [Style](AUEcineRig& Rig) { Rig.Operator.Style = Style; });
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateSPLambda(this, [this, Style]
				{
					const AUEcineRig* Rig = GetRig();
					return Rig != nullptr && Rig->Operator.Style == Style;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SUEcineGripTab::MakeRigSpinBox(FSpinBoxArgs Args, TFunction<float(const AUEcineRig&)> GetValue, TFunction<void(AUEcineRig&, float)> SetValue)
{
	Args.Value = TAttribute<float>::CreateSPLambda(this, [this, GetValue = MoveTemp(GetValue)]
	{
		const AUEcineRig* Rig = GetRig();
		return Rig != nullptr ? GetValue(*Rig) : 0.0f;
	});
	Args.SaveForUndo = [this]
	{
		if (AUEcineRig* Rig = GetRig())
		{
			Rig->Modify();
		}
	};
	Args.OnDrag = [this, SetValue](float NewValue)
	{
		if (AUEcineRig* Rig = GetRig())
		{
			SetValue(*Rig, NewValue);
			GEditor->RedrawLevelEditingViewports();
		}
	};
	Args.OnCommit = [this, SetValue, Description = Args.TransactionName](float NewValue)
	{
		UEcine::GripEditing::EditRig(GetRig(), Description, [&SetValue, NewValue](AUEcineRig& Rig) { SetValue(Rig, NewValue); });
	};
	return MakeSpinBox(MoveTemp(Args));
}

AUEcineRig* SUEcineGripTab::GetRig() const
{
	return UEcine::GripEditing::FindRig(Target->GetCamera());
}

FText SUEcineGripTab::GetSupportName() const
{
	const AUEcineRig* Rig = GetRig();
	if (Rig == nullptr)
	{
		return LOCTEXT("NoSupport", "No support");
	}
	const FUEcineRigSpec Spec = Rig->GetSpec();
	const FUEcineRigPreset* Preset = UEcine::Rig::FindPreset(Spec.PresetId);
	return Preset != nullptr ? Preset->Name : UEcine::Rig::GetTypeName(Spec.Type);
}

FText SUEcineGripTab::GetMoveWarnings(int32 MarkIndex) const
{
	const AUEcineRig* Rig = GetRig();
	if (Rig == nullptr || MarkIndex < 1 || !Rig->Marks.IsValidIndex(MarkIndex))
	{
		return FText::GetEmpty();
	}

	const FUEcineRigSpec Spec = Rig->GetSpec();
	const FUEcineRigMark& From = Rig->Marks[MarkIndex - 1];
	const FUEcineRigMark& To = Rig->Marks[MarkIndex];
	TArray<FText> Lines;
	for (const FUEcineMoveFinding& Finding : UEcine::Move::MostLimiting(UEcine::Move::Check(Spec, From.Pose, To.Pose, To.Arrival)))
	{
		Lines.Add(FText::Format(LOCTEXT("WarningPrefix", "Warning: {0}"), UEcine::Move::Describe(Finding, Spec.Type, UUEcineEditorSettings::GetDistanceUnits())));
	}
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

FText SUEcineGripTab::GetBakeStatus() const
{
	const AUEcineRig* Rig = GetRig();
	if (Rig == nullptr || Rig->Marks.Num() < 2)
	{
		return LOCTEXT("BakeStatusNeedsMarks", "Mark at least two positions, then bake the move between them to Sequencer.");
	}

	const FText Move = FText::Format(LOCTEXT("BakeStatusMove", "The move runs through {0} marks in {1} s."),
		FText::AsNumber(Rig->Marks.Num()), UEcine::Units::FormatNumber(UEcine::Move::GetTotalDuration(Rig->Marks), 1));
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	return OpenSequence != nullptr
		? FText::Format(LOCTEXT("BakeStatusOpenSequence", "{0} It will be baked into {1}, starting at the playhead."), Move, OpenSequence->GetDisplayName())
		: FText::Format(LOCTEXT("BakeStatusNewSequence", "{0} No sequence is open in Sequencer, so a new one will be made for it."), Move);
}

FReply SUEcineGripTab::OnBakeClicked()
{
	FText Error;
	const ULevelSequence* Sequence = UEcine::GripEditing::BakeMoveToSequencer(GetRig(), Error);
	Notify(Sequence != nullptr
		? FText::Format(LOCTEXT("BakeDone", "The move is baked into {0}. Play it in Sequencer."), Sequence->GetDisplayName())
		: Error, Sequence != nullptr);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
