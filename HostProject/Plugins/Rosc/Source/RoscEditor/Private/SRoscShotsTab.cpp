// Copyright 2026 Fiavaion. All rights reserved.

#include "SRoscShotsTab.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "CineCameraActor.h"
#include "HAL/PlatformProcess.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "SRoscPlanView.h"
#include "RoscCameraTarget.h"
#include "RoscEditorSettings.h"
#include "RoscShotEditing.h"
#include "RoscStyle.h"
#include "RoscWidgets.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

using namespace Rosc::Widgets;
using namespace Rosc::ShotEditing;

namespace
{
	/** Two storyboard frames sit side by side in the narrowest panel. */
	constexpr float StoryboardThumbnailWidth = 132.0f;
	constexpr float ShotNotesMinHeight = 52.0f;

	/** What a row of the shot list says: the scene and size when they are set, around the shot's name. */
	FText MakeShotLabel(const FShot& Shot)
	{
		TArray<FText> Parts;
		const FString Scene = GetScene(Shot.Sequence);
		if (!Scene.IsEmpty())
		{
			Parts.Add(FText::FromString(Scene));
		}
		Parts.Add(FText::FromString(Shot.Name));
		const FText Size = Rosc::Shots::GetShotSizeAbbreviation(GetSize(Shot.Sequence));
		if (!Size.IsEmpty())
		{
			Parts.Add(Size);
		}
		return FText::Join(FText::FromString(TEXT(" · ")), Parts);
	}

	/** A storyboard frame as something Slate can draw. Null when the picture cannot be read or there is no renderer. */
	TSharedPtr<FSlateDynamicImageBrush> MakeStoryboardBrush(const FRoscStoryboardFrame& Frame)
	{
		FImage Picture;
		if (!FImageUtils::DecompressImage(Frame.Jpeg.GetData(), Frame.Jpeg.Num(), Picture))
		{
			return nullptr;
		}
		Picture.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);

		// Slate keeps a picture under its name, so each one made gets a name of its own.
		static uint32 BrushCount = 0;
		const FName BrushName(*FString::Printf(TEXT("RoscStoryboardFrame%u"), BrushCount++));
		return FSlateDynamicImageBrush::CreateWithImageData(BrushName, FVector2D(Picture.SizeX, Picture.SizeY), TArray<uint8>(Picture.RawData));
	}

	/** The overhead plan in words, for whoever cannot make out the drawing. */
	FText DescribePlan(const FRoscPlan& Plan)
	{
		if (Plan.CameraPath.IsEmpty())
		{
			return LOCTEXT("PlanNoCamera", "This shot has no camera to draw. Add a shot with a camera chosen in the panel, or put a camera in the shot in Sequencer.");
		}

		double TravelCm = 0.0;
		for (int32 Index = 1; Index < Plan.CameraPath.Num(); ++Index)
		{
			TravelCm += FVector2D::Distance(Plan.CameraPath[Index - 1].Position, Plan.CameraPath[Index].Position);
		}

		TArray<FText> Sentences;
		Sentences.Add(TravelCm > 1.0
			? FText::Format(LOCTEXT("PlanTravel", "The camera travels {0}, from A to B."), Rosc::Units::FormatDistance(TravelCm * 10.0, URoscEditorSettings::GetDistanceUnits()))
			: LOCTEXT("PlanNoTravel", "The camera stays where it is."));
		if (!Plan.Tracks.IsEmpty())
		{
			Sentences.Add(LOCTEXT("PlanTrack", "The heavy line is its dolly track."));
		}
		if (!Plan.Subjects.IsEmpty())
		{
			TArray<FText> Names;
			for (const FRoscPlanSubject& Subject : Plan.Subjects)
			{
				Names.Add(FText::FromString(Subject.Name));
			}
			Sentences.Add(FText::Format(LOCTEXT("PlanSubjects", "Marked: {0}."), FText::Join(FText::FromString(TEXT(", ")), Names)));
		}
		Sentences.Add(LOCTEXT("PlanView", "Broken lines show what the lens takes in."));
		return FText::Join(FText::FromString(TEXT(" ")), Sentences);
	}

	/** A box for a line, or several lines, of the shot list that the user types. What is typed is kept when the box is left. */
	TSharedRef<SWidget> MakeShotTextBox(const FText& Name, const FText& ToolTip, bool bSeveralLines, TWeakObjectPtr<ULevelSequence> ShotSequence,
		TFunction<FString(const ULevelSequence*)> GetText, TFunction<void(ULevelSequence*, const FString&)> SetText)
	{
		const auto ReadText = [ShotSequence, GetText = MoveTemp(GetText)]
		{
			const ULevelSequence* Sequence = ShotSequence.Get();
			return Sequence != nullptr ? FText::FromString(GetText(Sequence)) : FText::GetEmpty();
		};
		const auto WriteText = [ShotSequence, SetText = MoveTemp(SetText)](const FText& Text, ETextCommit::Type CommitType)
		{
			ULevelSequence* Sequence = ShotSequence.Get();
			// Escape gives up what was typed.
			if (Sequence != nullptr && CommitType != ETextCommit::OnCleared)
			{
				SetText(Sequence, Text.ToString());
			}
		};

		if (bSeveralLines)
		{
			return SNew(SBox)
				.MinDesiredHeight(ShotNotesMinHeight)
				[
					SNew(SMultiLineEditableTextBox)
					.ToolTipText(ToolTip)
					.AccessibleText(Name)
					.AutoWrapText(true)
					.Text_Lambda(ReadText)
					.OnTextCommitted_Lambda(WriteText)
				];
		}
		return SNew(SBox)
			.MinDesiredHeight(FRoscStyle::MinTargetSize)
			[
				SNew(SEditableTextBox)
				.ToolTipText(ToolTip)
				.AccessibleText(Name)
				.Text_Lambda(ReadText)
				.OnTextCommitted_Lambda(WriteText)
			];
	}
}

void SRoscShotsTab::Construct(const FArguments& InArgs, const TSharedRef<FRoscCameraTarget>& InTarget)
{
	Target = InTarget;
	CurrentLayoutKey = ReadShots();
	RebuildLayout();
}

void SRoscShotsTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	StoryboardFocus.Tick();
	const FLayoutKey NewKey = ReadShots();
	if (!(NewKey == CurrentLayoutKey))
	{
		CurrentLayoutKey = NewKey;
		RebuildLayout();
	}

	// Drawing the plan plays through the shot, which must not happen under a sequence that is playing.
	if (PlanView.IsValid() && PlanSequence != CurrentShotSequence && !ULevelSequenceEditorBlueprintLibrary::IsPlaying())
	{
		RedrawPlan();
	}
}

SRoscShotsTab::FLayoutKey SRoscShotsTab::ReadShots()
{
	FLayoutKey Key;
	Key.Units = URoscEditorSettings::GetDistanceUnits();
	Key.OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();

	const TArray<FShot> Shots = GetShots();
	Key.CurrentShot = FindShotAtPlayhead(Shots);
	ShotLabels.Reset();
	for (const FShot& Shot : Shots)
	{
		Key.ShotSequences.Add(Shot.Sequence);
		ShotLabels.Add(MakeShotLabel(Shot));
	}

	if (Shots.IsValidIndex(Key.CurrentShot))
	{
		const FShot& Shot = Shots[Key.CurrentShot];
		CurrentShotSequence = Shot.Sequence;
		CurrentReport = MakeShotReport(Shot);
		for (const FRoscStoryboardFrame& Frame : GetStoryboardFrames(Shot.Sequence))
		{
			Key.StoryboardSignature = HashCombine(Key.StoryboardSignature, HashCombine(GetTypeHash(Frame.ShotFrame), GetTypeHash(Frame.Jpeg.Num())));
		}
	}
	else
	{
		CurrentShotSequence.Reset();
		CurrentReport = FRoscShotReport();
	}
	return Key;
}

void SRoscShotsTab::RebuildLayout()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	StoryboardBrushes.Reset();
	PlanView.Reset();

	const FText NewShotToolTip = LOCTEXT("NewShotToolTip", "New shot: add a shot after the last one, seen through the camera this panel is working on. It is named and timed by the project's Sequencer settings. With no sequence open, one is made first.");

	Add(FMargin(0.0f), MakeSectionHeader(LOCTEXT("ShotListSection", "Shot List")));
	if (!CurrentLayoutKey.OpenSequence.IsValid())
	{
		Add(FMargin(0.0f), MakeEmptyState(
			LOCTEXT("NoSequenceMessage", "No sequence is open in Sequencer. Add the first shot to start one, or open your sequence in Sequencer and its shots are listed here."),
			LOCTEXT("NewShotButton", "New Shot"), NewShotToolTip,
			FOnClicked::CreateSP(this, &SRoscShotsTab::OnNewShotClicked)));
	}
	else
	{
		for (int32 ShotIndex = 0; ShotIndex < ShotLabels.Num(); ++ShotIndex)
		{
			Add(FRoscStyle::RowPadding, BuildShotRow(ShotIndex));
		}
		Add(FRoscStyle::ButtonRowPadding, MakeButton(LOCTEXT("NewShotButton", "New Shot"), NewShotToolTip, FOnClicked::CreateSP(this, &SRoscShotsTab::OnNewShotClicked)));

		if (CurrentLayoutKey.CurrentShot != INDEX_NONE)
		{
			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("ShotDetailsSection", "This Shot")));
			Add(FMargin(0.0f), BuildShotFields());

			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("StoryboardSection", "Storyboard")));
			Add(FMargin(0.0f), BuildStoryboard());

			Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("PlanSection", "Overhead Plan")));
			Add(FRoscStyle::RowPadding, BuildPlan());
		}
		else
		{
			Add(FRoscStyle::ButtonRowPadding, MakeNote(LOCTEXT("PlayheadOutsideShots", "The playhead is not in a shot. Choose a shot above to work in it.")));
		}

		Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("PaperworkSection", "Paperwork")));
		Add(FMargin(0.0f), BuildPaperwork());
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
}

TSharedRef<SWidget> SRoscShotsTab::BuildShotRow(int32 ShotIndex)
{
	return MakeToggleButton(
		TAttribute<FText>::CreateSPLambda(this, [this, ShotIndex] { return ShotLabels.IsValidIndex(ShotIndex) ? ShotLabels[ShotIndex] : FText::GetEmpty(); }),
		FText::Format(LOCTEXT("ShotRowToolTip", "Work in {0}: move the playhead to its first frame and open it in Sequencer. Moves and racks are then baked into this shot."), ShotLabels[ShotIndex]),
		TAttribute<bool>::CreateSPLambda(this, [this, ShotIndex] { return CurrentLayoutKey.CurrentShot == ShotIndex; }),
		[this, ShotIndex](bool) { GoToShotAt(ShotIndex); });
}

TSharedRef<SWidget> SRoscShotsTab::BuildShotFields()
{
	const TWeakObjectPtr<ULevelSequence> ShotSequence = CurrentShotSequence;
	const TSharedRef<SVerticalBox> Fields = SNew(SVerticalBox);
	const auto Add = [&Fields](const TSharedRef<SWidget>& Widget)
	{
		Fields->AddSlot().AutoHeight().Padding(FRoscStyle::RowPadding)[ Widget ];
	};
	const auto AddReadout = [this, &Add](const FText& Label, const FText& ToolTip, FString FRoscShotReport::* Field, const FText& WhenEmpty)
	{
		Add(MakeReadoutRow(Label, ToolTip, TAttribute<FText>::CreateSPLambda(this, [this, Field, WhenEmpty]
		{
			const FString& Value = CurrentReport.*Field;
			return Value.IsEmpty() ? WhenEmpty : FText::FromString(Value);
		})));
	};

	AddReadout(LOCTEXT("ShotNameLabel", "Shot"), LOCTEXT("ShotNameToolTip", "The shot's name, as Sequencer shows it. Rename it in Sequencer."), &FRoscShotReport::Shot, FText::GetEmpty());

	const FText SceneLabel = LOCTEXT("ShotSceneLabel", "Scene");
	const FText SceneToolTip = LOCTEXT("ShotSceneToolTip", "The scene of the script this shot belongs to, as written there: 12, 12A.");
	Add(MakeRow(SceneLabel, SceneToolTip, MakeShotTextBox(SceneLabel, SceneToolTip, false, ShotSequence, &GetScene, &SetScene)));

	TArray<FChoice> Sizes;
	for (const ERoscShotSize Size : Rosc::Shots::GetShotSizes())
	{
		Sizes.Add({ Rosc::Shots::GetShotSizeName(Size),
			[ShotSequence, Size] { return ShotSequence.IsValid() && GetSize(ShotSequence.Get()) == Size; },
			[ShotSequence, Size]
			{
				if (ULevelSequence* Sequence = ShotSequence.Get())
				{
					SetSize(Sequence, Size);
				}
			} });
	}
	Add(MakeChoiceRow(LOCTEXT("ShotSizeLabel", "Size"), LOCTEXT("ShotSizeToolTip", "How much of the subject the shot takes in, from an extreme wide to an extreme close-up."),
		TAttribute<FText>::CreateLambda([ShotSequence] { return Rosc::Shots::GetShotSizeName(ShotSequence.IsValid() ? GetSize(ShotSequence.Get()) : ERoscShotSize::Unset); }),
		MoveTemp(Sizes)));

	const FText MoveLabel = LOCTEXT("ShotMoveLabel", "Move");
	const FText MoveToolTip = LOCTEXT("ShotMoveToolTip", "What the camera does, in the crew's words: Static, Push in, Crane up and hold.");
	Add(MakeRow(MoveLabel, MoveToolTip, MakeShotTextBox(MoveLabel, MoveToolTip, false, ShotSequence, &GetMove, &SetMove)));

	const FText NotesLabel = LOCTEXT("ShotNotesLabel", "Notes");
	const FText NotesToolTip = LOCTEXT("ShotNotesToolTip", "Anything the crew should know about this shot. These are the sequence's own notes, which Sequencer shows too.");
	Add(MakeRow(NotesLabel, NotesToolTip, MakeShotTextBox(NotesLabel, NotesToolTip, true, ShotSequence, &GetNotes, &SetNotes)));

	AddReadout(LOCTEXT("ShotCameraLabel", "Camera"), LOCTEXT("ShotCameraToolTip", "The camera this shot is seen through: the camera of its first camera cut in Sequencer."),
		&FRoscShotReport::Camera, LOCTEXT("ShotNoCamera", "No camera in this shot"));
	AddReadout(LOCTEXT("ShotLensLabel", "Lens"), LOCTEXT("ShotLensToolTip", "The focal length on the shot's camera at the playhead. Change it in the Camera tab."),
		&FRoscShotReport::FocalLength, FText::GetEmpty());
	AddReadout(LOCTEXT("ShotSupportLabel", "Support"), LOCTEXT("ShotSupportToolTip", "What the shot's camera is on. Change it in the Grip tab."),
		&FRoscShotReport::Support, LOCTEXT("ShotNoSupport", "Not on a support"));
	AddReadout(LOCTEXT("ShotStartLabel", "Starts"), LOCTEXT("ShotStartToolTip", "The timecode of the shot's first frame in the sequence. Move or trim the shot in Sequencer."),
		&FRoscShotReport::Start, FText::GetEmpty());
	AddReadout(LOCTEXT("ShotDurationLabel", "Runs"), LOCTEXT("ShotDurationToolTip", "How long the shot runs. Trim the shot in Sequencer."),
		&FRoscShotReport::Duration, FText::GetEmpty());
	return Fields;
}

TSharedRef<SWidget> SRoscShotsTab::BuildStoryboard()
{
	const TWeakObjectPtr<ULevelSequence> ShotSequence = CurrentShotSequence;
	const TSharedRef<SVerticalBox> Storyboard = SNew(SVerticalBox);
	const TSharedRef<SWidget> GrabFrame = MakeButton(LOCTEXT("GrabFrameButton", "Grab Frame"),
		LOCTEXT("GrabFrameToolTip", "Grab frame: take a picture through the shot's camera at the playhead and keep it with the shot as a storyboard frame. Grabbing the same frame again replaces its picture."),
		FOnClicked::CreateSP(this, &SRoscShotsTab::OnGrabFrameClicked));
	Storyboard->AddSlot().AutoHeight().Padding(FRoscStyle::ButtonRowPadding)[ GrabFrame ];

	const TConstArrayView<FRoscStoryboardFrame> Frames = ShotSequence.IsValid() ? GetStoryboardFrames(ShotSequence.Get()) : TConstArrayView<FRoscStoryboardFrame>();
	if (Frames.IsEmpty())
	{
		Storyboard->AddSlot().AutoHeight().Padding(FRoscStyle::ButtonRowPadding)
		[
			MakeNote(LOCTEXT("NoStoryboardFrames", "No storyboard frames yet. Move the playhead to a moment worth showing, then grab it."))
		];
		StoryboardFocus.RowsRebuilt({}, GrabFrame);
		return Storyboard;
	}

	const TSharedRef<SWrapBox> Pictures = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(8.0, 6.0));
	TArray<TSharedRef<SWidget>> FrameCells;
	for (int32 FrameIndex = 0; FrameIndex < Frames.Num(); ++FrameIndex)
	{
		const FRoscStoryboardFrame& Frame = Frames[FrameIndex];
		// Counted the way the shot is watched: its first frame is frame 1.
		const FText FrameNumber = Rosc::Units::FormatNumber(Frame.ShotFrame + 1, 0);
		const FText Caption = FText::Format(LOCTEXT("StoryboardFrameCaption", "Frame {0}"), FrameNumber);

		const TSharedRef<SVerticalBox> Picture = SNew(SVerticalBox);
		const TSharedPtr<FSlateDynamicImageBrush> Brush = MakeStoryboardBrush(Frame);
		if (Brush.IsValid())
		{
			StoryboardBrushes.Add(Brush);
			Picture->AddSlot().AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(StoryboardThumbnailWidth)
				.HeightOverride(StoryboardThumbnailWidth * Frame.Height / FMath::Max(Frame.Width, 1))
				[
					SNew(SImage)
					.Image(Brush.Get())
					.AccessibleText(FText::Format(LOCTEXT("StoryboardFrameName", "Storyboard frame {0}"), FrameNumber))
				]
			];
		}
		Picture->AddSlot().AutoHeight()
		[
			SNew(SBox)
			.WidthOverride(StoryboardThumbnailWidth)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&TextStyle("Rosc.Body"))
					.Text(Caption)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeIconButton("Icons.Delete", FText::Format(LOCTEXT("RemoveStoryboardFrameToolTip", "Remove storyboard frame {0}"), FrameNumber),
						FOnClicked::CreateSPLambda(this, [this, ShotSequence, FrameIndex]
						{
							if (ULevelSequence* Sequence = ShotSequence.Get())
							{
								StoryboardFocus.RowRemoved(FrameIndex);
								RemoveStoryboardFrame(Sequence, FrameIndex);
							}
							return FReply::Handled();
						}))
				]
			]
		];
		Pictures->AddSlot()[ Picture ];
		FrameCells.Add(Picture);
	}
	StoryboardFocus.RowsRebuilt(FrameCells, GrabFrame);
	Storyboard->AddSlot().AutoHeight().Padding(FRoscStyle::ButtonRowPadding)[ Pictures ];
	return Storyboard;
}

TSharedRef<SWidget> SRoscShotsTab::BuildPlan()
{
	const TSharedRef<SWidget> Plan = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(PlanView, SRoscPlanView)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeNote(TAttribute<FText>::CreateSPLambda(this, [this]
			{
				return PlanSequence == CurrentShotSequence ? PlanCaption : LOCTEXT("PlanWaitsForPlayback", "The plan of this shot is drawn when playback stops.");
			}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeStatusLine("Icons.InfoWithColor", TAttribute<FText>::CreateSP(this, &SRoscShotsTab::GetPlanAgeText))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::ButtonRowPadding)
		[
			MakeButton(LOCTEXT("RedrawPlanButton", "Redraw Plan"),
				LOCTEXT("RedrawPlanToolTip", "Redraw plan: play through the shot again and draw where the camera goes now. Use it after changing the shot's move."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					RedrawPlan();
					return FReply::Handled();
				}))
		];

	// A rebuild for another reason, such as a new storyboard frame, keeps the plan already drawn.
	if (PlanSequence == CurrentShotSequence)
	{
		PlanView->SetPlan(CurrentPlan);
	}
	return Plan;
}

TSharedRef<SWidget> SRoscShotsTab::BuildPaperwork()
{
	const FText FolderLabel = LOCTEXT("PaperworkFolderLabel", "Folder");
	const FText FolderToolTip = LOCTEXT("PaperworkFolderToolTip", "The folder paperwork is written under. Each sequence has a folder of its own in it. Clear the box to go back to the project's own folder, Saved/Rosc/Paperwork.");
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeRow(FolderLabel, FolderToolTip, MakeFolderControl(FolderLabel, FolderToolTip, LOCTEXT("BrowsePaperworkFolderToolTip", "Choose the paperwork folder"),
				[] { return GetPaperworkFolder(); },
				[](const FString& Folder) { SetPaperworkFolder(Folder); }))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::ButtonRowPadding)
		[
			MakeButton(LOCTEXT("ExportPaperworkButton", "Export Paperwork"),
				LOCTEXT("ExportPaperworkToolTip", "Export paperwork: write the shot list and the camera report as spreadsheets (CSV), and one printable page with every shot's storyboard frames, overhead plan and camera report. The page prints, or saves as a PDF, from a web browser."),
				FOnClicked::CreateSP(this, &SRoscShotsTab::OnExportClicked))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			// A folder path has no spaces to break at, so it is allowed to break anywhere.
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Body"))
			.AutoWrapText(true)
			.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
			.Visibility_Lambda([this] { return LastExportDirectory.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			.Text_Lambda([this] { return FText::Format(LOCTEXT("PaperworkWrittenTo", "Last written to {0}"), FText::FromString(LastExportDirectory)); })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::ButtonRowPadding)
		[
			MakeButton(LOCTEXT("OpenPaperworkFolderButton", "Open Folder"),
				LOCTEXT("OpenPaperworkFolderToolTip", "Open the folder the paperwork was last written to."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					FPlatformProcess::ExploreFolder(*LastExportDirectory);
					return FReply::Handled();
				}),
				TAttribute<bool>::CreateSPLambda(this, [this] { return !LastExportDirectory.IsEmpty(); }))
		];
}

FText SRoscShotsTab::GetPlanAgeText() const
{
	const ULevelSequence* Sequence = PlanSequence.Get();
	return Sequence != nullptr && PlanSequence == CurrentShotSequence && GetShotSignature(Sequence) != PlanSignature
		? LOCTEXT("PlanOutOfDate", "The shot has changed since this plan was drawn. Redraw the plan to see where the camera goes now.")
		: FText::GetEmpty();
}

void SRoscShotsTab::RedrawPlan()
{
	const TArray<FShot> Shots = GetShots();
	const int32 CurrentShot = FindShotAtPlayhead(Shots);
	if (!Shots.IsValidIndex(CurrentShot))
	{
		return;
	}

	CurrentPlan = MakePlan(Shots[CurrentShot]);
	PlanSequence = Shots[CurrentShot].Sequence;
	PlanSignature = GetShotSignature(PlanSequence.Get());
	PlanCaption = DescribePlan(CurrentPlan);
	if (PlanView.IsValid())
	{
		PlanView->SetPlan(CurrentPlan);
	}
}

FReply SRoscShotsTab::OnNewShotClicked()
{
	FText Error;
	if (AddShot(Target->GetCamera(), Error))
	{
		// Straight into the new shot, which is the last one.
		const TArray<FShot> Shots = GetShots();
		if (!Shots.IsEmpty())
		{
			GoToShot(Shots.Last());
		}
	}
	else
	{
		Notify(Error, false);
	}
	return FReply::Handled();
}

FReply SRoscShotsTab::OnGrabFrameClicked()
{
	const TArray<FShot> Shots = GetShots();
	const int32 CurrentShot = FindShotAtPlayhead(Shots);
	FText Error;
	if (Shots.IsValidIndex(CurrentShot) && !GrabStoryboardFrame(Shots[CurrentShot], Error))
	{
		Notify(Error, false);
	}
	return FReply::Handled();
}

FReply SRoscShotsTab::OnExportClicked()
{
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		return FReply::Handled();
	}

	const FString Directory = GetPaperworkDirectory(OpenSequence);
	TArray<FString> Files;
	FText Error;
	if (ExportPaperwork(Directory, Files, Error))
	{
		LastExportDirectory = Directory;
		Notify(FText::Format(LOCTEXT("PaperworkExported", "The shot list, camera report and printable page of {0} are written. Open Folder in the Shots tab shows them."), FText::FromString(OpenSequence->GetName())), true);
	}
	else
	{
		Notify(Error, false);
	}
	return FReply::Handled();
}

void SRoscShotsTab::GoToShotAt(int32 ShotIndex)
{
	const TArray<FShot> Shots = GetShots();
	if (Shots.IsValidIndex(ShotIndex))
	{
		GoToShot(Shots[ShotIndex]);
	}
}

#undef LOCTEXT_NAMESPACE
