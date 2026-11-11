#include "SUEcineFocusTab.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Math/UnitConversion.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineCameraTarget.h"
#include "UEcineEditorSettings.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineOptics.h"
#include "UEcineRig.h"
#include "UEcineStyle.h"
#include "UEcineWidgets.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

using namespace UEcine::Widgets;

namespace
{
	FText FormatDistanceMm(double Millimetres)
	{
		return UEcine::Units::FormatDistance(Millimetres, UUEcineEditorSettings::GetDistanceUnits());
	}
}

void SUEcineFocusTab::Construct(const FArguments& InArgs, const TSharedRef<FUEcineCameraTarget>& InTarget)
{
	Target = InTarget;
	CurrentLayoutKey = MakeLayoutKey();
	RebuildLayout();
}

void SUEcineFocusTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const FLayoutKey NewKey = MakeLayoutKey();
	if (!(NewKey == CurrentLayoutKey))
	{
		CurrentLayoutKey = NewKey;
		RebuildLayout();
	}
	else if (GetMarkCount() != ShownMarkCount)
	{
		RebuildMarks();
	}
}

SUEcineFocusTab::FLayoutKey SUEcineFocusTab::MakeLayoutKey() const
{
	FLayoutKey Key;
	Key.Camera = Target->GetCamera();
	Key.bHasPackage = Target->GetPackageComponent() != nullptr;
	Key.Units = UUEcineEditorSettings::GetDistanceUnits();
	return Key;
}

void SUEcineFocusTab::RebuildLayout()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	MarksBox.Reset();

	if (CurrentLayoutKey.Camera.IsValid())
	{
		Add(FMargin(0.0f), MakeSectionHeader(LOCTEXT("FocusSection", "Focus")));
		Add(FUEcineStyle::RowPadding, BuildDistanceRow());
		Add(FUEcineStyle::ButtonRowPadding, MakeToggleButton(LOCTEXT("PickFocusButton", "Pick Focus"),
			LOCTEXT("PickFocusToolTip", "Pick focus: then click a subject in the viewport to focus on it. Escape calls it off."),
			TAttribute<bool>::CreateSPLambda(this, [this] { return UEcine::FocusEditing::IsPickingFocus(Target->GetCamera()); }),
			[this](bool bPick)
			{
				if (bPick)
				{
					UEcine::FocusEditing::BeginPickFocus(Target->GetCamera());
				}
				else
				{
					UEcine::FocusEditing::EndPickFocus();
				}
			}));
		Add(FUEcineStyle::RowPadding, BuildFollowRows());
		Add(FUEcineStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SUEcineFocusTab::GetFocusWarning)));

		Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("DepthSection", "Depth of Field")));
		Add(FUEcineStyle::RowPadding, BuildDepthRows());

		Add(FUEcineStyle::SectionPadding, MakeSectionHeader(LOCTEXT("FocusMarksSection", "Focus Marks")));
		if (CurrentLayoutKey.bHasPackage)
		{
			Add(FUEcineStyle::RowPadding, BuildRackTimeRow());
			Add(FMargin(0.0f), SAssignNew(MarksBox, SVerticalBox));
			Add(FUEcineStyle::ButtonRowPadding, MakeButton(LOCTEXT("MarkFocusButton", "Mark Focus"),
				LOCTEXT("MarkFocusToolTip", "Store where the focus is now as the next focus mark."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					UEcine::FocusEditing::AddFocusMark(Target->GetCamera());
					return FReply::Handled();
				})));
			Add(FUEcineStyle::ButtonRowPadding,
				SNew(STextBlock)
				.TextStyle(&TextStyle("UEcine.Body"))
				.AutoWrapText(true)
				.Text(this, &SUEcineFocusTab::GetRackStatus));
		}
		else
		{
			Add(FMargin(0.0f), MakeEmptyState(
				LOCTEXT("MarksNeedPackageMessage", "Focus marks are kept with the camera package, and this camera has none yet."),
				LOCTEXT("MarksBuildPackageButton", "Build Camera Package"),
				LOCTEXT("MarksBuildPackageToolTip", "Add a camera package to this camera, so its focus can be marked and racked. This can be undone."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					UEcine::CameraEditing::AddCameraPackage(Target->GetCamera());
					return FReply::Handled();
				})));
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

void SUEcineFocusTab::RebuildMarks()
{
	ShownMarkCount = GetMarkCount();
	if (!MarksBox.IsValid())
	{
		return;
	}

	MarksBox->ClearChildren();
	if (ShownMarkCount == 0)
	{
		MarksBox->AddSlot().AutoHeight().Padding(FUEcineStyle::RowPadding)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("UEcine.Body"))
			.AutoWrapText(true)
			.Text(LOCTEXT("NoFocusMarksMessage", "No focus marks yet. Focus on the first subject and mark it, then do the same for the next. A rack then pulls focus from where it is to a mark."))
		];
	}
	for (int32 MarkIndex = 0; MarkIndex < ShownMarkCount; ++MarkIndex)
	{
		MarksBox->AddSlot().AutoHeight().Padding(FUEcineStyle::ButtonRowPadding)[ BuildMarkRow(MarkIndex) ];
	}
}

TSharedRef<SWidget> SUEcineFocusTab::BuildDistanceRow()
{
	FSpinBoxArgs Args;
	Args.Name = LOCTEXT("FocusDistanceLabel", "Distance");
	Args.ToolTip = LOCTEXT("FocusDistanceToolTip", "How far from the camera the lens is focused. Drag it, or type a distance such as 3.2 m or 10' 6\". While the focus follows a subject this is the distance to the subject; setting a distance here stops following.");
	Args.TransactionName = LOCTEXT("DragFocus", "Set Focus");
	Args.Value = TAttribute<float>::CreateSP(this, &SUEcineFocusTab::GetLensDistance);
	Args.MinValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [this] { return TOptional<float>(UEcine::FocusEditing::GetCloseFocus(Target->GetCamera())); });
	// Typed distances can go out to a kilometre; a drag covers the first fifty metres, slowly up close where it matters.
	Args.MaxValue = TOptional<float>(100000.0f);
	Args.MaxSliderValue = 5000.0f;
	Args.SliderExponent = 3.0f;
	Args.Delta = 1.0f;
	Args.TypeInterface = MakeDistanceInterface();
	Args.SaveForUndo = [this]
	{
		if (const ACineCameraActor* Camera = Target->GetCamera())
		{
			Camera->GetCineCameraComponent()->Modify();
		}
	};
	Args.OnDrag = [this](float NewValue) { UEcine::FocusEditing::SetFocusDistance(Target->GetCamera(), NewValue); };
	Args.OnCommit = Args.OnDrag;
	return MakeRow(Args.Name, Args.ToolTip, MakeSpinBox(Args));
}

TSharedRef<SWidget> SUEcineFocusTab::BuildFollowRows()
{
	const TAttribute<FText> FollowToolTip = TAttribute<FText>::CreateSPLambda(this, [this]
	{
		const AActor* Subject = GetSelectedSubject();
		return Subject != nullptr
			? FText::Format(LOCTEXT("FollowSelectedToolTip", "Keep the focus on {0} as it and the camera move."), FText::FromString(Subject->GetActorLabel()))
			: LOCTEXT("FollowNothingSelectedToolTip", "Select the subject in the level or the Outliner, then press this to keep the focus on it as it and the camera move. The panel stays on this camera.");
	});

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeReadoutRow(LOCTEXT("FollowingLabel", "Following"),
				LOCTEXT("FollowingToolTip", "The subject the focus is kept on as it and the camera move."),
				TAttribute<FText>::CreateSP(this, &SUEcineFocusTab::GetFollowedName))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FUEcineStyle::ButtonRowPadding)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				MakeButton(LOCTEXT("FollowSelectedButton", "Follow Selected"), FollowToolTip,
					FOnClicked::CreateSPLambda(this, [this]
					{
						UEcine::FocusEditing::FollowActor(Target->GetCamera(), GetSelectedSubject());
						return FReply::Handled();
					}),
					TAttribute<bool>::CreateSPLambda(this, [this] { return GetSelectedSubject() != nullptr; }))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				MakeButton(LOCTEXT("StopFollowingButton", "Stop Following"),
					LOCTEXT("StopFollowingToolTip", "Stop following the subject. The focus stays where it is."),
					FOnClicked::CreateSPLambda(this, [this]
					{
						UEcine::FocusEditing::StopFollowing(Target->GetCamera());
						return FReply::Handled();
					}),
					TAttribute<bool>::CreateSP(this, &SUEcineFocusTab::IsFollowing))
			]
		];
}

TSharedRef<SWidget> SUEcineFocusTab::BuildDepthRows()
{
	const auto MakeDepthRow = [this](const FText& Label, const FText& ToolTip, TFunction<FText(const FDepth&)> Format)
	{
		return MakeReadoutRow(Label, ToolTip, TAttribute<FText>::CreateSPLambda(this, [this, Format = MoveTemp(Format)]
		{
			const TOptional<FDepth> Depth = GetDepth();
			return Depth.IsSet() ? Format(Depth.GetValue()) : FText::GetEmpty();
		}));
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeDepthRow(LOCTEXT("NearLimitLabel", "Near limit"),
				LOCTEXT("NearLimitToolTip", "The nearest distance that is acceptably sharp at the current focus, focal length and T-stop."),
				[](const FDepth& Depth) { return FormatDistanceMm(Depth.NearMm); })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FUEcineStyle::RowPadding)
		[
			MakeDepthRow(LOCTEXT("FarLimitLabel", "Far limit"),
				LOCTEXT("FarLimitToolTip", "The farthest distance that is acceptably sharp at the current focus, focal length and T-stop."),
				[](const FDepth& Depth) { return Depth.bFarIsInfinite ? LOCTEXT("FarLimitInfinity", "Infinity") : FormatDistanceMm(Depth.FarMm); })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FUEcineStyle::RowPadding)
		[
			MakeDepthRow(LOCTEXT("DepthLabel", "Depth of field"),
				LOCTEXT("DepthToolTip", "How much of the scene, front to back, is acceptably sharp. Stop down, go wider or focus farther away for more."),
				[](const FDepth& Depth) { return Depth.bFarIsInfinite ? LOCTEXT("DepthToInfinity", "To infinity") : FormatDistanceMm(Depth.FarMm - Depth.NearMm); })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FUEcineStyle::RowPadding)
		[
			MakeDepthRow(LOCTEXT("FocusHyperfocalLabel", "Hyperfocal"),
				LOCTEXT("FocusHyperfocalToolTip", "Focus here and everything from half this distance to infinity is acceptably sharp at the current T-stop."),
				[](const FDepth& Depth) { return FormatDistanceMm(Depth.HyperfocalMm); })
		];
}

TSharedRef<SWidget> SUEcineFocusTab::BuildRackTimeRow()
{
	FSpinBoxArgs Args;
	Args.Name = LOCTEXT("RackTimeLabel", "Rack time");
	Args.ToolTip = LOCTEXT("RackTimeToolTip", "How long a rack takes to pull focus from where it is to a mark. The focus eases off the old distance and settles onto the new one.");
	Args.TransactionName = LOCTEXT("DragRackTime", "Change Rack Time");
	Args.Value = TAttribute<float>::CreateSPLambda(this, [this]
	{
		const UUEcineCameraPackageComponent* Package = Target->GetPackageComponent();
		return Package != nullptr ? Package->RackSeconds : 0.0f;
	});
	Args.MinValue = TOptional<float>(0.1f);
	Args.MaxValue = TOptional<float>(60.0f);
	Args.MaxSliderValue = 10.0f;
	Args.Delta = 0.1f;
	Args.TypeInterface = MakeShared<TNumericUnitTypeInterface<float>>(EUnit::Seconds);
	Args.SaveForUndo = [this]
	{
		if (UUEcineCameraPackageComponent* Package = Target->GetPackageComponent())
		{
			Package->Modify();
		}
	};
	Args.OnDrag = [this](float NewValue) { UEcine::FocusEditing::SetRackTime(Target->GetCamera(), NewValue); };
	Args.OnCommit = Args.OnDrag;
	return MakeRow(Args.Name, Args.ToolTip, MakeSpinBox(Args));
}

TSharedRef<SWidget> SUEcineFocusTab::BuildMarkRow(int32 MarkIndex)
{
	const FText Number = FText::AsNumber(MarkIndex + 1);
	const TAttribute<FText> MarkLabel = TAttribute<FText>::CreateSPLambda(this, [this, MarkIndex, Number]
	{
		const UUEcineCameraPackageComponent* Package = Target->GetPackageComponent();
		return Package != nullptr && Package->FocusMarksCm.IsValidIndex(MarkIndex)
			? FText::Format(LOCTEXT("FocusMarkButton", "Mark {0}: {1}"), Number, FormatDistanceMm(Package->FocusMarksCm[MarkIndex] * 10.0))
			: FText::GetEmpty();
	});

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			MakeButton(MarkLabel,
				FText::Format(LOCTEXT("FocusMarkButtonToolTip", "Put the focus on mark {0} at once."), Number),
				FOnClicked::CreateSPLambda(this, [this, MarkIndex]
				{
					UEcine::FocusEditing::GoToFocusMark(Target->GetCamera(), MarkIndex);
					return FReply::Handled();
				}))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeButton(LOCTEXT("RackButton", "Rack"),
				FText::Format(LOCTEXT("RackToolTip", "Rack to mark {0}: write a focus pull into Sequencer, from where the focus is at the playhead to this mark, over the rack time."), Number),
				FOnClicked::CreateSP(this, &SUEcineFocusTab::OnRackClicked, MarkIndex))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeButton(LOCTEXT("FocusRemarkButton", "Re-mark"),
				FText::Format(LOCTEXT("FocusRemarkToolTip", "Move mark {0} to where the focus is now."), Number),
				FOnClicked::CreateSPLambda(this, [this, MarkIndex]
				{
					UEcine::FocusEditing::UpdateFocusMark(Target->GetCamera(), MarkIndex);
					return FReply::Handled();
				}))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeIconButton("Icons.Delete", FText::Format(LOCTEXT("RemoveFocusMarkToolTip", "Remove focus mark {0}"), Number), FOnClicked::CreateSPLambda(this, [this, MarkIndex]
			{
				UEcine::FocusEditing::RemoveFocusMark(Target->GetCamera(), MarkIndex);
				return FReply::Handled();
			}))
		];
}

float SUEcineFocusTab::GetLensDistance() const
{
	return UEcine::FocusEditing::GetLensDistance(Target->GetCamera());
}

TOptional<SUEcineFocusTab::FDepth> SUEcineFocusTab::GetDepth() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	if (Camera == nullptr)
	{
		return TOptional<FDepth>();
	}
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	const double CircleOfConfusion = UEcine::Optics::DefaultCircleOfConfusionMm(Lens->Filmback.SensorWidth, Lens->Filmback.SensorHeight);
	const UEcine::Optics::FDepthOfField Limits = UEcine::Optics::DepthOfField(Lens->CurrentFocalLength, Lens->CurrentAperture, CircleOfConfusion, GetLensDistance() * 10.0);

	FDepth Depth;
	Depth.NearMm = Limits.NearMm;
	Depth.FarMm = Limits.FarMm;
	Depth.bFarIsInfinite = Limits.bFarIsInfinite;
	Depth.HyperfocalMm = UEcine::Optics::HyperfocalDistanceMm(Lens->CurrentFocalLength, Lens->CurrentAperture, CircleOfConfusion);
	return Depth;
}

bool SUEcineFocusTab::IsFollowing() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	return Camera != nullptr && Camera->GetCineCameraComponent()->FocusSettings.FocusMethod == ECameraFocusMethod::Tracking;
}

AActor* SUEcineFocusTab::GetSelectedSubject() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	if (Camera == nullptr)
	{
		return nullptr;
	}
	// Anything but the camera itself and what it is standing on.
	const AActor* Rig = UEcine::GripEditing::FindRig(Camera);
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (Actor != nullptr && Actor != Camera && Actor != Rig)
		{
			return Actor;
		}
	}
	return nullptr;
}

int32 SUEcineFocusTab::GetMarkCount() const
{
	const UUEcineCameraPackageComponent* Package = Target->GetPackageComponent();
	return Package != nullptr ? Package->FocusMarksCm.Num() : 0;
}

FText SUEcineFocusTab::GetFollowedName() const
{
	if (!IsFollowing())
	{
		return LOCTEXT("FollowingNothing", "Nothing");
	}
	const AActor* Subject = UEcine::FocusEditing::GetFollowedActor(Target->GetCamera());
	return Subject != nullptr ? FText::FromString(Subject->GetActorLabel()) : LOCTEXT("FollowingMissing", "A subject no longer in the level");
}

FText SUEcineFocusTab::GetFocusWarning() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	const float CloseFocus = UEcine::FocusEditing::GetCloseFocus(Camera);
	if (Camera == nullptr || UEcine::FocusEditing::GetFocusDistance(Camera) >= CloseFocus - 0.01f)
	{
		return FText::GetEmpty();
	}
	const FText Reach = FormatDistanceMm(CloseFocus * 10.0);
	return IsFollowing()
		? FText::Format(LOCTEXT("SubjectTooCloseWarning", "Warning: the subject is nearer than this lens can focus, so it is soft. The lens stays at its close focus, {0}. Move the camera back or change to a lens that focuses closer."), Reach)
		: FText::Format(LOCTEXT("FocusTooCloseWarning", "Warning: the focus is set nearer than this lens can focus. The lens stays at its close focus, {0}. Set the focus again or change to a lens that focuses closer."), Reach);
}

FText SUEcineFocusTab::GetRackStatus() const
{
	if (GetMarkCount() == 0)
	{
		return FText::GetEmpty();
	}
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	return OpenSequence != nullptr
		? FText::Format(LOCTEXT("RackStatusOpenSequence", "A rack is written into {0}, starting at the playhead."), OpenSequence->GetDisplayName())
		: LOCTEXT("RackStatusNewSequence", "No sequence is open in Sequencer, so the first rack makes a new one.");
}

FReply SUEcineFocusTab::OnRackClicked(int32 MarkIndex)
{
	FText Error;
	const ULevelSequence* Sequence = UEcine::FocusEditing::RackToFocusMark(Target->GetCamera(), MarkIndex, Error);
	Notify(Sequence != nullptr
		? FText::Format(LOCTEXT("RackDone", "The rack to mark {0} is baked into {1}. Play it in Sequencer."), FText::AsNumber(MarkIndex + 1), Sequence->GetDisplayName())
		: Error, Sequence != nullptr);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
