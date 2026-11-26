// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "SRoscFocusTab.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Math/UnitConversion.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscCameraTarget.h"
#include "RoscEditorSettings.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscOptics.h"
#include "RoscRig.h"
#include "RoscStyle.h"
#include "RoscWidgets.h"
#include "Widgets/Input/NumericUnitTypeInterface.inl"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

using namespace Rosc::Widgets;

namespace
{
	FText FormatDistanceMm(double Millimetres)
	{
		return Rosc::Units::FormatDistance(Millimetres, URoscEditorSettings::GetDistanceUnits());
	}
}

void SRoscFocusTab::Construct(const FArguments& InArgs, const TSharedRef<FRoscCameraTarget>& InTarget)
{
	Target = InTarget;
	CurrentLayoutKey = MakeLayoutKey();
	RebuildLayout();
}

void SRoscFocusTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	MarkRowFocus.Tick();
	const FLayoutKey NewKey = MakeLayoutKey();
	if (!(NewKey == CurrentLayoutKey))
	{
		CurrentLayoutKey = NewKey;
		RebuildLayout();
		MeasureRebuiltContent(*this, AllottedGeometry);
	}
	else if (GetMarkCount() != ShownMarkCount)
	{
		RebuildMarks();
		MeasureRebuiltContent(*this, AllottedGeometry);
	}
}

SRoscFocusTab::FLayoutKey SRoscFocusTab::MakeLayoutKey() const
{
	FLayoutKey Key;
	Key.Camera = Target->GetCamera();
	Key.bHasPackage = Target->GetPackageComponent() != nullptr;
	Key.Units = URoscEditorSettings::GetDistanceUnits();
	return Key;
}

void SRoscFocusTab::RebuildLayout()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	MarksBox.Reset();
	MarkFocusButton.Reset();

	if (CurrentLayoutKey.Camera.IsValid())
	{
		Add(FMargin(0.0f), MakeSectionHeader(LOCTEXT("FocusSection", "Focus")));
		Add(FRoscStyle::RowPadding, BuildDistanceRow());
		const auto MakePickButton = [this](const FText& Label, const FText& ToolTip, bool bHold)
		{
			return MakeToggleButton(Label, ToolTip,
				TAttribute<bool>::CreateSPLambda(this, [this, bHold] { return Rosc::FocusEditing::IsPickingFocus(Target->GetCamera(), bHold); }),
				[this, bHold](bool bPick)
				{
					if (bPick)
					{
						Rosc::FocusEditing::BeginPickFocus(Target->GetCamera(), bHold);
					}
					else
					{
						Rosc::FocusEditing::EndPickFocus();
					}
				});
		};
		Add(FRoscStyle::ButtonRowPadding,
			SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.0f, 4.0f))
			+ SWrapBox::Slot()
			[
				MakePickButton(LOCTEXT("PickFocusButton", "Pick Focus"),
					LOCTEXT("PickFocusToolTip", "Pick focus: then click a subject, or a part of it, in the viewport. The focus is set to that distance and stays there. Escape calls it off."), false)
			]
			+ SWrapBox::Slot()
			[
				MakePickButton(LOCTEXT("PickHoldButton", "Pick and Hold"),
					LOCTEXT("PickHoldToolTip", "Pick and hold: then click a point in the viewport, such as an eye. The focus stays on that point as the camera and the subject move. Mark it to rack to it later. Escape calls it off."), true)
			]);
		Add(FRoscStyle::RowPadding, BuildFollowRows());
		Add(FRoscStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSP(this, &SRoscFocusTab::GetFocusWarning)));

		Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("DepthSection", "Depth of Field")));
		Add(FRoscStyle::RowPadding, BuildDepthRows());

		Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("FocusMarksSection", "Focus Marks")));
		if (CurrentLayoutKey.bHasPackage)
		{
			Add(FRoscStyle::RowPadding, BuildRackTimeRow());
			Add(FMargin(0.0f), SAssignNew(MarksBox, SVerticalBox));
			const TSharedRef<SWidget> MarkFocus = MakeButton(LOCTEXT("MarkFocusButton", "Mark Focus"),
				LOCTEXT("MarkFocusToolTip", "Store what the focus is on now as the next focus mark: the point or subject it is held on, or else its distance."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					Rosc::FocusEditing::AddFocusMark(Target->GetCamera());
					return FReply::Handled();
				}));
			MarkFocusButton = MarkFocus;
			Add(FRoscStyle::ButtonRowPadding, MarkFocus);
			Add(FRoscStyle::ButtonRowPadding,
				SNew(STextBlock)
				.TextStyle(&TextStyle("Rosc.Body"))
				.AutoWrapText(true)
				.Text(this, &SRoscFocusTab::GetRackStatus));
		}
		else
		{
			Add(FMargin(0.0f), MakeEmptyState(
				LOCTEXT("MarksNeedPackageMessage", "Focus marks are kept with the camera package, and this camera has none yet."),
				LOCTEXT("MarksBuildPackageButton", "Build Camera Package"),
				LOCTEXT("MarksBuildPackageToolTip", "Add a camera package to this camera, so its focus can be marked and racked. This can be undone."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					Rosc::CameraEditing::AddCameraPackage(Target->GetCamera());
					return FReply::Handled();
				})));
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

void SRoscFocusTab::RebuildMarks()
{
	ShownMarkCount = GetMarkCount();
	if (!MarksBox.IsValid())
	{
		return;
	}

	MarksBox->ClearChildren();
	if (ShownMarkCount == 0)
	{
		MarksBox->AddSlot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Body"))
			.AutoWrapText(true)
			.Text(LOCTEXT("NoFocusMarksMessage", "No focus marks yet. Focus on the first subject and mark it, then do the same for the next. A rack then pulls focus from where it is to a mark."))
		];
	}
	TArray<TSharedRef<SWidget>> MarkRows;
	for (int32 MarkIndex = 0; MarkIndex < ShownMarkCount; ++MarkIndex)
	{
		MarkRows.Add(BuildMarkRow(MarkIndex));
		MarksBox->AddSlot().AutoHeight().Padding(FRoscStyle::ButtonRowPadding)[ MarkRows.Last() ];
	}
	MarkRowFocus.RowsRebuilt(MarkRows, MarkFocusButton);
}

TSharedRef<SWidget> SRoscFocusTab::BuildDistanceRow()
{
	FSpinBoxArgs Args;
	Args.Name = LOCTEXT("FocusDistanceLabel", "Distance");
	Args.ToolTip = LOCTEXT("FocusDistanceToolTip", "How far from the camera the lens is focused. Drag it, or type a distance such as 3.2 m or 10' 6\". While the focus follows a subject this is the distance to the subject; setting a distance here stops following.");
	Args.TransactionName = LOCTEXT("DragFocus", "Set Focus");
	Args.Value = TAttribute<float>::CreateSP(this, &SRoscFocusTab::GetLensDistance);
	Args.MinValue = TAttribute<TOptional<float>>::CreateSPLambda(this, [this] { return TOptional<float>(Rosc::FocusEditing::GetCloseFocus(Target->GetCamera())); });
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
	Args.OnDrag = [this](float NewValue) { Rosc::FocusEditing::SetFocusDistance(Target->GetCamera(), NewValue); };
	Args.OnCommit = Args.OnDrag;
	return MakeRow(Args.Name, Args.ToolTip, MakeSpinBox(Args));
}

TSharedRef<SWidget> SRoscFocusTab::BuildFollowRows()
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
			MakeReadoutRow(LOCTEXT("FollowingLabel", "Held on"),
				LOCTEXT("FollowingToolTip", "The subject or the picked point the focus is kept on as it and the camera move."),
				TAttribute<FText>::CreateSP(this, &SRoscFocusTab::GetFollowedName))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::ButtonRowPadding)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				MakeButton(LOCTEXT("FollowSelectedButton", "Follow Selected"), FollowToolTip,
					FOnClicked::CreateSPLambda(this, [this]
					{
						Rosc::FocusEditing::FollowActor(Target->GetCamera(), GetSelectedSubject());
						return FReply::Handled();
					}),
					TAttribute<bool>::CreateSPLambda(this, [this] { return GetSelectedSubject() != nullptr; }))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				MakeButton(LOCTEXT("StopFollowingButton", "Let Go"),
					LOCTEXT("StopFollowingToolTip", "Let go of the subject or point. The focus stays at the distance it is at."),
					FOnClicked::CreateSPLambda(this, [this]
					{
						Rosc::FocusEditing::StopFollowing(Target->GetCamera());
						return FReply::Handled();
					}),
					TAttribute<bool>::CreateSP(this, &SRoscFocusTab::IsFollowing))
			]
		];
}

TSharedRef<SWidget> SRoscFocusTab::BuildDepthRows()
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
		.Padding(FRoscStyle::RowPadding)
		[
			MakeDepthRow(LOCTEXT("FarLimitLabel", "Far limit"),
				LOCTEXT("FarLimitToolTip", "The farthest distance that is acceptably sharp at the current focus, focal length and T-stop."),
				[](const FDepth& Depth) { return Depth.bFarIsInfinite ? LOCTEXT("FarLimitInfinity", "Infinity") : FormatDistanceMm(Depth.FarMm); })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeDepthRow(LOCTEXT("DepthLabel", "Depth of field"),
				LOCTEXT("DepthToolTip", "How much of the scene, front to back, is acceptably sharp. Stop down, go wider or focus farther away for more."),
				[](const FDepth& Depth) { return Depth.bFarIsInfinite ? LOCTEXT("DepthToInfinity", "To infinity") : FormatDistanceMm(Depth.FarMm - Depth.NearMm); })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeDepthRow(LOCTEXT("FocusHyperfocalLabel", "Hyperfocal"),
				LOCTEXT("FocusHyperfocalToolTip", "Focus here and everything from half this distance to infinity is acceptably sharp at the current T-stop."),
				[](const FDepth& Depth) { return FormatDistanceMm(Depth.HyperfocalMm); })
		];
}

TSharedRef<SWidget> SRoscFocusTab::BuildRackTimeRow()
{
	FSpinBoxArgs Args;
	Args.Name = LOCTEXT("RackTimeLabel", "Rack time");
	Args.ToolTip = LOCTEXT("RackTimeToolTip", "How long a rack takes to pull focus from where it is to a mark. The focus eases off the old distance and settles onto the new one.");
	Args.TransactionName = LOCTEXT("DragRackTime", "Change Rack Time");
	Args.Value = TAttribute<float>::CreateSPLambda(this, [this]
	{
		const URoscCameraPackageComponent* Package = Target->GetPackageComponent();
		return Package != nullptr ? Package->RackSeconds : 0.0f;
	});
	Args.MinValue = TOptional<float>(0.1f);
	Args.MaxValue = TOptional<float>(60.0f);
	Args.MaxSliderValue = 10.0f;
	Args.Delta = 0.1f;
	Args.TypeInterface = MakeShared<TNumericUnitTypeInterface<float>>(EUnit::Seconds);
	Args.SaveForUndo = [this]
	{
		if (URoscCameraPackageComponent* Package = Target->GetPackageComponent())
		{
			Package->Modify();
		}
	};
	Args.OnDrag = [this](float NewValue) { Rosc::FocusEditing::SetRackTime(Target->GetCamera(), NewValue); };
	Args.OnCommit = Args.OnDrag;
	return MakeRow(Args.Name, Args.ToolTip, MakeSpinBox(Args));
}

TSharedRef<SWidget> SRoscFocusTab::BuildMarkRow(int32 MarkIndex)
{
	const FText Number = FText::AsNumber(MarkIndex + 1);
	const TAttribute<FText> MarkLabel = TAttribute<FText>::CreateSPLambda(this, [this, MarkIndex, Number]
	{
		const URoscCameraPackageComponent* Package = Target->GetPackageComponent();
		if (Package == nullptr || !Package->FocusMarks.IsValidIndex(MarkIndex))
		{
			return FText::GetEmpty();
		}
		return FText::Format(LOCTEXT("FocusMarkButton", "Mark {0}: {1}"), Number, FormatDistanceMm(Rosc::FocusEditing::GetFocusMarkDistance(Target->GetCamera(), MarkIndex) * 10.0));
	});
	// A mark on a subject or a point says what it is on, on a line of its own under the row.
	const TAttribute<FText> MarkSubject = TAttribute<FText>::CreateSPLambda(this, [this, MarkIndex]
	{
		const URoscCameraPackageComponent* Package = Target->GetPackageComponent();
		if (Package == nullptr || !Package->FocusMarks.IsValidIndex(MarkIndex) || !Package->FocusMarks[MarkIndex].bPoint)
		{
			return FText::GetEmpty();
		}
		const FRoscFocusMark& Mark = Package->FocusMarks[MarkIndex];
		const AActor* Subject = Mark.Subject.Get();
		if (Subject == nullptr)
		{
			return Mark.Subject.IsNull() ? LOCTEXT("FocusMarkOnPlace", "On a point in the level") : LOCTEXT("FocusMarkSubjectGone", "On a subject no longer in the level");
		}
		return FText::Format(Mark.Offset.IsNearlyZero() ? LOCTEXT("FocusMarkOnSubject", "On {0}") : LOCTEXT("FocusMarkOnPoint", "On a point on {0}"), FText::FromString(Subject->GetActorLabel()));
	});

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			MakeButton(MarkLabel,
				FText::Format(LOCTEXT("FocusMarkButtonToolTip", "Put the focus on mark {0} at once. A mark on a subject or a point holds the focus on it from then on."), Number),
				FOnClicked::CreateSPLambda(this, [this, MarkIndex]
				{
					Rosc::FocusEditing::GoToFocusMark(Target->GetCamera(), MarkIndex);
					return FReply::Handled();
				}))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeButton(LOCTEXT("RackButton", "Rack"),
				FText::Format(LOCTEXT("RackToolTip", "Rack to mark {0}: write a focus pull into Sequencer, from where the focus is at the playhead to this mark, over the rack time. A mark on a subject or a point is then kept sharp to the end of the shot, wherever the camera goes."), Number),
				FOnClicked::CreateSP(this, &SRoscFocusTab::OnRackClicked, MarkIndex))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeButton(LOCTEXT("FocusRemarkButton", "Re-mark"),
				FText::Format(LOCTEXT("FocusRemarkToolTip", "Move mark {0} to what the focus is on now."), Number),
				FOnClicked::CreateSPLambda(this, [this, MarkIndex]
				{
					Rosc::FocusEditing::UpdateFocusMark(Target->GetCamera(), MarkIndex);
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
				MarkRowFocus.RowRemoved(MarkIndex);
				Rosc::FocusEditing::RemoveFocusMark(Target->GetCamera(), MarkIndex);
				return FReply::Handled();
			}))
		]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Body"))
			.AutoWrapText(true)
			.Text(MarkSubject)
			.Visibility_Lambda([MarkSubject] { return MarkSubject.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		];
}

float SRoscFocusTab::GetLensDistance() const
{
	return Rosc::FocusEditing::GetLensDistance(Target->GetCamera());
}

TOptional<SRoscFocusTab::FDepth> SRoscFocusTab::GetDepth() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	if (Camera == nullptr)
	{
		return TOptional<FDepth>();
	}
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	const double CircleOfConfusion = Rosc::Optics::DefaultCircleOfConfusionMm(Lens->Filmback.SensorWidth, Lens->Filmback.SensorHeight);
	const Rosc::Optics::FDepthOfField Limits = Rosc::Optics::DepthOfField(Lens->CurrentFocalLength, Lens->CurrentAperture, CircleOfConfusion, GetLensDistance() * 10.0);

	FDepth Depth;
	Depth.NearMm = Limits.NearMm;
	Depth.FarMm = Limits.FarMm;
	Depth.bFarIsInfinite = Limits.bFarIsInfinite;
	Depth.HyperfocalMm = Rosc::Optics::HyperfocalDistanceMm(Lens->CurrentFocalLength, Lens->CurrentAperture, CircleOfConfusion);
	return Depth;
}

bool SRoscFocusTab::IsFollowing() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	return Camera != nullptr && Camera->GetCineCameraComponent()->FocusSettings.FocusMethod == ECameraFocusMethod::Tracking;
}

AActor* SRoscFocusTab::GetSelectedSubject() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	if (Camera == nullptr)
	{
		return nullptr;
	}
	// Anything but the camera itself and what it is standing on.
	const AActor* Rig = Rosc::GripEditing::FindRig(Camera);
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

int32 SRoscFocusTab::GetMarkCount() const
{
	const URoscCameraPackageComponent* Package = Target->GetPackageComponent();
	return Package != nullptr ? Package->FocusMarks.Num() : 0;
}

FText SRoscFocusTab::GetFollowedName() const
{
	if (!IsFollowing())
	{
		return LOCTEXT("FollowingNothing", "Nothing");
	}
	const ACineCameraActor* Camera = Target->GetCamera();
	const AActor* Subject = Rosc::FocusEditing::GetFollowedActor(Camera);
	if (!Rosc::FocusEditing::IsHoldingPoint(Camera))
	{
		return Subject != nullptr ? FText::FromString(Subject->GetActorLabel()) : LOCTEXT("FollowingMissing", "A subject no longer in the level");
	}
	return Subject != nullptr
		? FText::Format(LOCTEXT("HoldingPointOn", "A point on {0}"), FText::FromString(Subject->GetActorLabel()))
		: LOCTEXT("HoldingPlace", "A point in the level");
}

FText SRoscFocusTab::GetFocusWarning() const
{
	const ACineCameraActor* Camera = Target->GetCamera();
	const float CloseFocus = Rosc::FocusEditing::GetCloseFocus(Camera);
	if (Camera == nullptr || Rosc::FocusEditing::GetFocusDistance(Camera) >= CloseFocus - 0.01f)
	{
		return FText::GetEmpty();
	}
	const FText Reach = FormatDistanceMm(CloseFocus * 10.0);
	return IsFollowing()
		? FText::Format(LOCTEXT("SubjectTooCloseWarning", "The subject is nearer than this lens can focus, so it is soft. The lens stays at its close focus, {0}. Move the camera back or change to a lens that focuses closer."), Reach)
		: FText::Format(LOCTEXT("FocusTooCloseWarning", "The focus is set nearer than this lens can focus. The lens stays at its close focus, {0}. Set the focus again or change to a lens that focuses closer."), Reach);
}

FText SRoscFocusTab::GetRackStatus() const
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

FReply SRoscFocusTab::OnRackClicked(int32 MarkIndex)
{
	FText Error;
	const ULevelSequence* Sequence = Rosc::FocusEditing::RackToFocusMark(Target->GetCamera(), MarkIndex, Error);
	Notify(Sequence != nullptr
		? FText::Format(LOCTEXT("RackDone", "The rack to mark {0} is baked into {1}. Play it in Sequencer."), FText::AsNumber(MarkIndex + 1), Sequence->GetDisplayName())
		: Error, Sequence != nullptr);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
