// Copyright 2026 Fiavaion. All rights reserved.

#include "SRoscDeliverTab.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "RoscEditorSettings.h"
#include "RoscHandoff.h"
#include "RoscRenderSubsystem.h"
#include "RoscShotEditing.h"
#include "RoscStyle.h"
#include "RoscWidgets.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

using namespace Rosc::Widgets;
using namespace Rosc::DeliverEditing;

namespace
{
	/** How many shots' first files are listed before the rest are only counted. */
	constexpr int32 DeliverPreviewFileCount = 3;
	constexpr float DeliverProgressBarHeight = 8.0f;

	const FName DeliverStopsIcon("Icons.ErrorWithColor");
	const FName DeliverWorthKnowingIcon("Icons.WarningWithColor");
	const FName DeliverReadyIcon("Icons.SuccessWithColor");
	const FName DeliverWaitingIcon("Icons.InfoWithColor");

	ERoscDeliverIntent GetChosenIntent()
	{
		return GetDefault<URoscEditorSettings>()->DeliverIntent;
	}

	URoscRenderSubsystem* GetRenderer()
	{
		return GEditor->GetEditorSubsystem<URoscRenderSubsystem>();
	}

	/** A folder or file name: it has no spaces to break at, so it is allowed to break anywhere. */
	TSharedRef<SWidget> MakePathText(const FText& Path, const FName StyleName)
	{
		return SNew(STextBlock)
			.TextStyle(&TextStyle(StyleName))
			.AutoWrapText(true)
			.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
			.Text(Path);
	}

	/** Preflight's findings in the order found, those that read the same for several shots put together. */
	TArray<TArray<FRoscPreflightFinding>> GroupDeliverFindings(TConstArrayView<FRoscPreflightFinding> Findings)
	{
		TArray<TArray<FRoscPreflightFinding>> Groups;
		for (const FRoscPreflightFinding& Finding : Findings)
		{
			TArray<FRoscPreflightFinding>* Group = Groups.FindByPredicate([&Finding](const TArray<FRoscPreflightFinding>& Candidate)
			{
				const FRoscPreflightFinding& First = Candidate[0];
				return First.Check == Finding.Check && First.bStopsRender == Finding.bStopsRender && !First.Shot.IsEmpty() && !Finding.Shot.IsEmpty()
					&& First.Title.EqualTo(Finding.Title) && First.Detail.EqualTo(Finding.Detail) && First.FixLabel.EqualTo(Finding.FixLabel);
			});
			if (Group == nullptr)
			{
				Group = &Groups.AddDefaulted_GetRef();
			}
			Group->Add(Finding);
		}
		return Groups;
	}

	/** A row of buttons that folds onto a second line when the panel is narrow. */
	TSharedRef<SWrapBox> MakeButtonRow()
	{
		return SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.0, 4.0));
	}
}

void SRoscDeliverTab::Construct(const FArguments& InArgs)
{
	GetRenderer()->OnRenderFinished().AddSP(this, &SRoscDeliverTab::HandleRenderFinished);
	// Preflight reads the cameras and shots, which any edit can change: in another tab, in the
	// Details panel or in Sequencer. A hidden tab looks again when it is next shown.
	CastChecked<UTransBuffer>(GEditor->Trans)->OnTransactionStateChanged().AddSPLambda(this, [this](const FTransactionContext&, ETransactionStateEventType State)
	{
		bCheckWanted |= State == ETransactionStateEventType::TransactionFinalized || State == ETransactionStateEventType::UndoRedoFinalized;
	});
	CurrentLayoutKey = ReadLayoutKey();
	RebuildLayout();
}

void SRoscDeliverTab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	FLayoutKey NewKey = ReadLayoutKey();
	bCheckWanted |= !NewKey.HasSameDelivery(CurrentLayoutKey);

	// Preflight plays to the start of each shot, which must not happen under a sequence that is playing or rendering.
	if (bCheckWanted && !NewKey.bRendering && !ULevelSequenceEditorBlueprintLibrary::IsPlaying())
	{
		Check = CheckDelivery(NewKey.Intent);
		bChecked = true;
		bCheckWanted = false;
		bFolderExists = NewKey.OpenSequence.IsValid() && IFileManager::Get().DirectoryExists(*GetSettings().OutputDirectory);
		NewKey.Revision = ++Revision;
	}

	if (!(NewKey == CurrentLayoutKey))
	{
		CurrentLayoutKey = MoveTemp(NewKey);
		RebuildLayout();
	}
}

SRoscDeliverTab::FLayoutKey SRoscDeliverTab::ReadLayoutKey() const
{
	FLayoutKey Key;
	Key.OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	for (const Rosc::ShotEditing::FShot& Shot : Rosc::ShotEditing::GetShots())
	{
		Key.ShotSequences.Add(Shot.Sequence);
		Key.ShotNames.Add(Shot.Name);
	}
	Key.Intent = GetChosenIntent();
	Key.bRendering = GetRenderer()->IsRendering();
	Key.Revision = Revision;
	return Key;
}

void SRoscDeliverTab::RebuildLayout()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	const bool bRendering = CurrentLayoutKey.bRendering;

	Add(FMargin(0.0f), MakeSectionHeader(LOCTEXT("DeliverySection", "Delivery")));
	Add(FRoscStyle::ButtonRowPadding, BuildIntentChoice());
	Add(FRoscStyle::RowPadding, MakeNote(Rosc::Deliver::GetIntentDescription(CurrentLayoutKey.Intent)));
	if (CurrentLayoutKey.Intent == ERoscDeliverIntent::Review)
	{
		Add(FRoscStyle::RowPadding, BuildReviewOptions());
	}
	else if (CurrentLayoutKey.Intent == ERoscDeliverIntent::YouTube || CurrentLayoutKey.Intent == ERoscDeliverIntent::DcpSource)
	{
		Add(FRoscStyle::RowPadding, BuildSizeOptions());
	}

	Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("DeliverFilesSection", "Files")));
	Add(FRoscStyle::RowPadding, BuildFolderRow());
	Add(FRoscStyle::RowPadding, BuildNameRow());
	Add(FRoscStyle::ButtonRowPadding, BuildNameButtons());
	Add(FRoscStyle::RowPadding, MakeWarning(TAttribute<FText>::CreateSPLambda(this, [this]
	{
		return NamePatternProblem.IsEmpty() ? FText::GetEmpty()
			: FText::Format(LOCTEXT("NamePatternRefused", "{0} Until it is put right, files are named {1}."),
				NamePatternProblem, FText::FromString(GetDefault<URoscEditorSettings>()->DeliverNamePattern));
	})));
	Add(FRoscStyle::RowPadding, BuildFilePreview());

	Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("PreflightSection", "Preflight")));
	const TArray<FFindingGroup> Groups = GroupDeliverFindings(Check.Findings);
	Add(FRoscStyle::RowPadding, BuildPreflightStatus(Groups));
	if (bChecked)
	{
		for (const FFindingGroup& Group : Groups)
		{
			Add(FRoscStyle::SectionPadding, BuildFinding(Group));
		}
	}
	Add(FRoscStyle::SectionPadding, MakeButton(LOCTEXT("CheckAgainButton", "Check Again"),
		LOCTEXT("CheckAgainToolTip", "Check again: look through the open sequence once more for what would spoil this delivery. Preflight also runs when this tab opens, when the kind of delivery or the shot list changes, and before every render."),
		FOnClicked::CreateSPLambda(this, [this]
		{
			bCheckWanted = true;
			return FReply::Handled();
		}),
		!bRendering));

	Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("RenderSection", "Render")));
	Add(FMargin(0.0f), BuildRender());

	Add(FRoscStyle::SectionPadding, MakeSectionHeader(LOCTEXT("HandoffSection", "Handoff")));
	Add(FMargin(0.0f), BuildHandoff());

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

TSharedRef<SWidget> SRoscDeliverTab::BuildIntentChoice()
{
	const TSharedRef<SWrapBox> Intents = MakeButtonRow();
	for (const ERoscDeliverIntent Intent : Rosc::Deliver::GetIntents())
	{
		Intents->AddSlot()
		[
			MakeToggleButton(Rosc::Deliver::GetIntentName(Intent),
				FText::Format(LOCTEXT("IntentToolTip", "{0}: {1}"), Rosc::Deliver::GetIntentName(Intent), Rosc::Deliver::GetIntentDescription(Intent)),
				TAttribute<bool>::CreateLambda([Intent] { return GetChosenIntent() == Intent; }),
				[this, Intent](bool) { EditChoices([Intent](URoscEditorSettings& Choices) { Choices.DeliverIntent = Intent; }); })
		];
	}
	Intents->SetEnabled(!CurrentLayoutKey.bRendering);
	return Intents;
}

TSharedRef<SWidget> SRoscDeliverTab::BuildReviewOptions()
{
	const TSharedRef<SWrapBox> Options = MakeButtonRow();
	Options->AddSlot()
	[
		MakeToggleButton(LOCTEXT("SlateToggle", "Slate"),
			LOCTEXT("SlateToggleToolTip", "Slate: open each review movie with a frame that says what the shot is: production, scene, shot, camera, lens and date."),
			TAttribute<bool>::CreateLambda([] { return GetDefault<URoscEditorSettings>()->bDeliverSlate; }),
			[this](bool bTurnOn) { EditChoices([bTurnOn](URoscEditorSettings& Choices) { Choices.bDeliverSlate = bTurnOn; }); })
	];
	Options->AddSlot()
	[
		MakeToggleButton(LOCTEXT("BurnInToggle", "Burn-ins"),
			LOCTEXT("BurnInToggleToolTip", "Burn-ins: draw timecode, shot, lens and exposure over the picture of each review movie."),
			TAttribute<bool>::CreateLambda([] { return GetDefault<URoscEditorSettings>()->bDeliverBurnIn; }),
			[this](bool bTurnOn) { EditChoices([bTurnOn](URoscEditorSettings& Choices) { Choices.bDeliverBurnIn = bTurnOn; }); })
	];
	Options->SetEnabled(!CurrentLayoutKey.bRendering);
	return MakeRow(LOCTEXT("ReviewOptionsLabel", "Movie has"), LOCTEXT("ReviewOptionsToolTip", "What is added to each review movie."), Options);
}

TSharedRef<SWidget> SRoscDeliverTab::BuildSizeOptions()
{
	const bool bCinema = CurrentLayoutKey.Intent == ERoscDeliverIntent::DcpSource;
	const auto AddSize = [this](const TSharedRef<SWrapBox>& Sizes, const FText& Label, const FText& ToolTip, const bool bFourK)
	{
		Sizes->AddSlot()
		[
			MakeToggleButton(Label, ToolTip,
				TAttribute<bool>::CreateLambda([bFourK] { return GetDefault<URoscEditorSettings>()->bDeliverFourK == bFourK; }),
				[this, bFourK](bool) { EditChoices([bFourK](URoscEditorSettings& Choices) { Choices.bDeliverFourK = bFourK; }); })
		];
	};
	const TSharedRef<SWrapBox> Sizes = MakeButtonRow();
	if (bCinema)
	{
		AddSize(Sizes, LOCTEXT("SizeDci2K", "2K"), LOCTEXT("SizeDci2KToolTip", "2K: the delivery frame fitted into 2048 by 1080, which every cinema projector plays."), false);
		AddSize(Sizes, LOCTEXT("SizeDci4K", "4K"), LOCTEXT("SizeDci4KToolTip", "4K: the delivery frame fitted into 4096 by 2160. It takes about four times as long to render."), true);
	}
	else
	{
		AddSize(Sizes, LOCTEXT("SizeHd", "1080p"), LOCTEXT("SizeHdToolTip", "1080p: the delivery frame fitted into 1920 by 1080."), false);
		AddSize(Sizes, LOCTEXT("SizeUhd", "4K"), LOCTEXT("SizeUhdToolTip", "4K: the delivery frame fitted into 3840 by 2160. It takes about four times as long to render, and YouTube keeps more of the picture's quality."), true);
	}
	Sizes->SetEnabled(!CurrentLayoutKey.bRendering);
	return MakeRow(LOCTEXT("SizeOptionsLabel", "Size"), LOCTEXT("SizeOptionsToolTip", "How large the picture is made."), Sizes);
}

TSharedRef<SWidget> SRoscDeliverTab::BuildFolderRow()
{
	const FText Label = LOCTEXT("DeliverFolderLabel", "Folder");
	const FText ToolTip = LOCTEXT("DeliverFolderToolTip", "The folder deliveries are written under. Each sequence has a folder of its own in it, with a folder for each kind of delivery inside. Clear the box to go back to the project's own folder, Saved/Rosc/Deliveries.");

	const TSharedRef<SWidget> Folder = MakeFolderControl(Label, ToolTip, LOCTEXT("BrowseFolderToolTip", "Choose the delivery folder"),
		[] { return GetDeliveryFolder(); },
		[this](const FString& Chosen) { EditChoices([&Chosen](URoscEditorSettings& Choices) { Choices.DeliverFolder.Path = Chosen; }); });
	Folder->SetEnabled(!CurrentLayoutKey.bRendering);
	return MakeRow(Label, ToolTip, Folder);
}

TSharedRef<SWidget> SRoscDeliverTab::BuildNameRow()
{
	const FText Label = LOCTEXT("DeliverNameLabel", "Name");
	const FText ToolTip = LOCTEXT("DeliverNameToolTip", "How files are named. Words in braces stand for something of the shot; Add Token lists them. The name needs {shot} in it, so that no shot is written over another.");

	const TSharedRef<SWidget> Name = SNew(SBox)
		.MinDesiredHeight(FRoscStyle::MinTargetSize)
		[
			SNew(SEditableTextBox)
			.ToolTipText(ToolTip)
			.AccessibleText(Label)
			.Text_Lambda([this] { return FText::FromString(GetShownNamePattern()); })
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
			{
				if (CommitType != ETextCommit::OnCleared)
				{
					ChooseNamePattern(Text.ToString().TrimStartAndEnd());
				}
			})
		];
	Name->SetEnabled(!CurrentLayoutKey.bRendering);
	return MakeRow(Label, ToolTip, Name);
}

TSharedRef<SWidget> SRoscDeliverTab::BuildNameButtons()
{
	const TSharedRef<SWrapBox> Buttons = MakeButtonRow();
	Buttons->AddSlot()
	[
		MakeComboButton(LOCTEXT("AddTokenName", "Add token"),
			LOCTEXT("AddTokenToolTip", "Add token: put a word that stands for something of the shot at the end of the file name."),
			LOCTEXT("AddTokenButton", "Add Token"),
			FOnGetContent::CreateSP(this, &SRoscDeliverTab::MakeTokenMenu))
	];
	Buttons->AddSlot()
	[
		MakeButton(LOCTEXT("DefaultNameButton", "Default Name"),
			FText::Format(LOCTEXT("DefaultNameToolTip", "Default name: go back to the name files have to begin with, {0}."), FText::FromString(Rosc::Deliver::DefaultNamePattern)),
			FOnClicked::CreateSPLambda(this, [this]
			{
				ChooseNamePattern(Rosc::Deliver::DefaultNamePattern);
				return FReply::Handled();
			}),
			TAttribute<bool>::CreateSPLambda(this, [this] { return GetShownNamePattern() != Rosc::Deliver::DefaultNamePattern; }))
	];
	Buttons->SetEnabled(!CurrentLayoutKey.bRendering);
	return Buttons;
}

TSharedRef<SWidget> SRoscDeliverTab::MakeTokenMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	for (const FRoscNameToken& Token : Rosc::Deliver::GetNameTokens())
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("TokenEntry", "{0}  {1}"), FText::FromString(Token.Token), Token.Meaning),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(this, [this, TokenText = Token.Token]
			{
				// Added to what is in the box, so a refused name can be put right with the token it lacks.
				const FString Pattern = GetShownNamePattern();
				ChooseNamePattern(Pattern.IsEmpty() ? TokenText : Pattern + TEXT("_") + TokenText);
			})));
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SRoscDeliverTab::BuildFilePreview()
{
	const TSharedRef<SVerticalBox> Preview = SNew(SVerticalBox);
	if (!bChecked || Check.Files.IsEmpty())
	{
		return Preview;
	}

	Preview->AddSlot().AutoHeight()
	[
		MakePathText(FText::Format(LOCTEXT("FilePreviewIntro", "The first file of each shot, in {0}:"), FText::FromString(GetSettings().OutputDirectory)), "Rosc.Body")
	];
	const int32 ShownCount = FMath::Min(Check.Files.Num(), DeliverPreviewFileCount);
	for (int32 FileIndex = 0; FileIndex < ShownCount; ++FileIndex)
	{
		Preview->AddSlot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakePathText(FText::FromString(Check.Files[FileIndex]), "Rosc.Readout")
		];
	}
	if (Check.Files.Num() > ShownCount)
	{
		Preview->AddSlot().AutoHeight().Padding(FRoscStyle::RowPadding)
		[
			MakeNote(FText::Format(LOCTEXT("FilePreviewMore", "and {0} more {0}|plural(one=shot,other=shots)."), Check.Files.Num() - ShownCount))
		];
	}
	return Preview;
}

TSharedRef<SWidget> SRoscDeliverTab::BuildPreflightStatus(TConstArrayView<FFindingGroup> Groups)
{
	if (!bChecked)
	{
		return MakeStatusLine(DeliverWaitingIcon, LOCTEXT("PreflightWaiting", "Preflight looks through the sequence as soon as nothing is playing or rendering."));
	}

	int32 StopCount = 0;
	for (const FFindingGroup& Group : Groups)
	{
		StopCount += Group[0].bStopsRender ? 1 : 0;
	}
	const int32 WorthKnowingCount = Groups.Num() - StopCount;
	if (StopCount > 0)
	{
		return MakeStatusLine(DeliverStopsIcon, WorthKnowingCount > 0
			? FText::Format(LOCTEXT("PreflightStopsAndMore", "{0} {0}|plural(one=finding stops,other=findings stop) the render, and {1} more {1}|plural(one=is,other=are) worth knowing."), StopCount, WorthKnowingCount)
			: FText::Format(LOCTEXT("PreflightStops", "{0} {0}|plural(one=finding stops,other=findings stop) the render."), StopCount));
	}
	if (WorthKnowingCount > 0)
	{
		return MakeStatusLine(DeliverWorthKnowingIcon,
			FText::Format(LOCTEXT("PreflightWorthKnowing", "Ready to render, with {0} {0}|plural(one=finding,other=findings) worth knowing first."), WorthKnowingCount));
	}
	return MakeStatusLine(DeliverReadyIcon, LOCTEXT("PreflightReady", "Ready to render: preflight found nothing wrong."));
}

TSharedRef<SWidget> SRoscDeliverTab::BuildFinding(const FFindingGroup& Group)
{
	const FRoscPreflightFinding& Finding = Group[0];
	// The two kinds of finding share the shape of their icon, so each says in words which it is.
	const FText Kind = Finding.bStopsRender ? LOCTEXT("FindingStops", "Stops the render") : LOCTEXT("FindingWorthKnowing", "Worth knowing");
	TArray<FText> Shots;
	for (const FRoscPreflightFinding& OfShot : Group)
	{
		if (!OfShot.Shot.IsEmpty())
		{
			Shots.Add(FText::FromString(OfShot.Shot));
		}
	}

	const TSharedRef<SVerticalBox> Words = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&TextStyle("Rosc.Value"))
			.AutoWrapText(true)
			.Text(Finding.Title)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			MakeNote(Shots.IsEmpty() ? Kind : FText::Format(LOCTEXT("FindingKindAndShots", "{0} · {1}"), Kind, FText::Join(LOCTEXT("FindingShotSeparator", ", "), Shots)))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeNote(Finding.Detail)
		];
	if (!Finding.FixLabel.IsEmpty())
	{
		Words->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(FRoscStyle::ButtonRowPadding)
		[
			MakeButton(Finding.FixLabel, Finding.Detail, FOnClicked::CreateSP(this, &SRoscDeliverTab::OnFixClicked, Group), !CurrentLayoutKey.bRendering)
		];
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Top)
		.Padding(0.0f, 2.0f, 6.0f, 0.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush(Finding.bStopsRender ? DeliverStopsIcon : DeliverWorthKnowingIcon))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			Words
		];
}

TSharedRef<SWidget> SRoscDeliverTab::BuildRender()
{
	const TSharedRef<SVerticalBox> Render = SNew(SVerticalBox);
	const auto Add = [&Render](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Render->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};

	if (CurrentLayoutKey.bRendering)
	{
		Add(FRoscStyle::RowPadding, MakeNote(TAttribute<FText>::CreateSP(this, &SRoscDeliverTab::GetRenderProgressText)));
		Add(FRoscStyle::RowPadding, SNew(SBox)
			.HeightOverride(DeliverProgressBarHeight)
			[
				SNew(SProgressBar)
				.Percent_Lambda([] { return TOptional<float>(GetRenderer()->GetProgress()); })
			]);
		Add(FRoscStyle::ButtonRowPadding, MakeButton(
			TAttribute<FText>::CreateSPLambda(this, [this] { return bStopAsked ? LOCTEXT("StoppingButton", "Stopping") : LOCTEXT("StopButton", "Stop"); }),
			LOCTEXT("StopToolTip", "Stop: end the render now. What it has written so far is kept."),
			FOnClicked::CreateSP(this, &SRoscDeliverTab::OnStopClicked),
			TAttribute<bool>::CreateSPLambda(this, [this] { return !bStopAsked; })));
	}
	else
	{
		const FText IntentName = Rosc::Deliver::GetIntentName(CurrentLayoutKey.Intent);
		const bool bCanRender = bChecked && !IsRenderStopped();
		Add(FRoscStyle::ButtonRowPadding, SNew(SBox)
			.MinDesiredHeight(FRoscStyle::MinTargetSize)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(FText::Format(LOCTEXT("RenderButton", "Render {0}"), IntentName))
				.ToolTipText(FText::Format(LOCTEXT("RenderToolTip", "Render {0}: preflight looks once more, then every shot of the open sequence is rendered. The editor plays the sequence in a window of its own until the render is over."), IntentName))
				.IsEnabled(bCanRender)
				.OnClicked(this, &SRoscDeliverTab::OnRenderClicked)
			]);
		if (bChecked && !bCanRender)
		{
			Add(FRoscStyle::RowPadding, MakeNote(LOCTEXT("RenderWaitsForPreflight", "Put right what stops the render, under Preflight, then render.")));
		}
	}

	if (!ResultText.IsEmpty())
	{
		Add(FRoscStyle::ButtonRowPadding, MakeStatusLine(ResultIcon, ResultText));
	}
	Add(FRoscStyle::ButtonRowPadding, MakeButton(LOCTEXT("OpenDeliveryFolderButton", "Open Folder"),
		LOCTEXT("OpenDeliveryFolderToolTip", "Open the folder this sequence's deliveries are written to."),
		FOnClicked::CreateLambda([]
		{
			FPlatformProcess::ExploreFolder(*GetSettings().OutputDirectory);
			return FReply::Handled();
		}),
		bFolderExists));
	return Render;
}

TSharedRef<SWidget> SRoscDeliverTab::BuildHandoff()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::RowPadding)
		[
			MakeNote(LOCTEXT("HandoffNote", "A copy of the sequence and its shots that plays the same pictures on plain cameras and Sequencer keys, to open in a project without Rosc. A VFX Passes delivery makes it by itself."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FRoscStyle::ButtonRowPadding)
		[
			MakeButton(LOCTEXT("BakeForHandoffButton", "Bake for Handoff"),
				LOCTEXT("BakeForHandoffToolTip", "Bake for handoff: make the copy now, beside the sequence, with _Handoff after its name. A copy from an earlier bake is replaced. The sequence being worked on is left as it is."),
				FOnClicked::CreateSP(this, &SRoscDeliverTab::OnBakeForHandoffClicked),
				CurrentLayoutKey.OpenSequence.IsValid() && !CurrentLayoutKey.bRendering)
		];
}

bool SRoscDeliverTab::IsRenderStopped() const
{
	return Check.Findings.ContainsByPredicate([](const FRoscPreflightFinding& Finding) { return Finding.bStopsRender; });
}

void SRoscDeliverTab::EditChoices(TFunctionRef<void(URoscEditorSettings&)> Edit)
{
	Rosc::DeliverEditing::EditChoices(Edit);
	bCheckWanted = true;
}

void SRoscDeliverTab::ChooseNamePattern(const FString& Pattern)
{
	NamePatternProblem = Rosc::Deliver::ValidateNamePattern(Pattern);
	if (!NamePatternProblem.IsEmpty())
	{
		RefusedNamePattern = Pattern;
		return;
	}
	RefusedNamePattern.Reset();
	EditChoices([&Pattern](URoscEditorSettings& Choices) { Choices.DeliverNamePattern = Pattern; });
}

FString SRoscDeliverTab::GetShownNamePattern() const
{
	return NamePatternProblem.IsEmpty() ? GetDefault<URoscEditorSettings>()->DeliverNamePattern : RefusedNamePattern;
}

FReply SRoscDeliverTab::OnFixClicked(FFindingGroup Group)
{
	// Putting several shots right is still one thing the user did, and one step to undo.
	const TOptional<FScopedTransaction> Transaction = Group.Num() > 1 ? TOptional<FScopedTransaction>(InPlace, Group[0].FixLabel) : TOptional<FScopedTransaction>();
	bool bFixed = true;
	for (const FRoscPreflightFinding& Finding : Group)
	{
		FText Error;
		if (!ApplyFix(Finding, Error))
		{
			Notify(Error, false);
			bFixed = false;
			break;
		}
	}
	if (bFixed && Group[0].Check == ERoscPreflightCheck::WorkingColourSpace)
	{
		Notify(LOCTEXT("AcesCgChosen", "The project is set to work in ACEScg. Restart the editor for it to take effect."), true);
	}
	bCheckWanted = true;
	return FReply::Handled();
}

FReply SRoscDeliverTab::OnRenderClicked()
{
	const ERoscDeliverIntent Intent = GetChosenIntent();
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	DeliveryName = FText::Format(LOCTEXT("DeliveryName", "{0} of {1}"), Rosc::Deliver::GetIntentName(Intent), FText::FromString(OpenSequence != nullptr ? OpenSequence->GetName() : FString()));
	bStopAsked = false;

	// A VFX handoff bakes for handoff and writes its cameras before the render starts.
	FScopedSlowTask Preparing(0.0f, LOCTEXT("PreparingDelivery", "Preparing the delivery"));
	Preparing.MakeDialogDelayed(0.5f);

	FText Error;
	bDeliveryRunning = StartDelivery(Intent, Delivery, Error);
	if (bDeliveryRunning)
	{
		ResultText = FText::GetEmpty();
	}
	else
	{
		ResultIcon = DeliverStopsIcon;
		ResultText = Error;
		// What stopped it is most likely something preflight can now show.
		bCheckWanted = true;
	}
	++Revision;
	return FReply::Handled();
}

FReply SRoscDeliverTab::OnStopClicked()
{
	bStopAsked = true;
	GetRenderer()->CancelRender();
	return FReply::Handled();
}

FReply SRoscDeliverTab::OnBakeForHandoffClicked()
{
	FScopedSlowTask Baking(0.0f, LOCTEXT("BakingForHandoff", "Baking for handoff"));
	Baking.MakeDialogDelayed(0.5f);

	FText Error;
	if (const ULevelSequence* Copy = Rosc::Handoff::BakeForHandoff(Error))
	{
		Notify(FText::Format(LOCTEXT("BakedForHandoff", "{0} is made beside the sequence, with a copy of each shot. It opens without Rosc."), FText::FromString(Copy->GetName())), true);
	}
	else
	{
		Notify(Error, false);
	}
	return FReply::Handled();
}

void SRoscDeliverTab::HandleRenderFinished(const FRoscRenderResult& Result)
{
	// The next delivery is the next version, whoever made this one.
	bCheckWanted = true;
	if (!bDeliveryRunning)
	{
		return;
	}
	bDeliveryRunning = false;

	if (Result.bSuccess)
	{
		ResultIcon = DeliverReadyIcon;
		ResultText = FText::Format(LOCTEXT("DeliveryMade", "{0} is delivered: {1} {1}|plural(one=file,other=files)."), DeliveryName, Result.Files.Num() + Delivery.Files.Num());
	}
	else if (bStopAsked)
	{
		ResultIcon = DeliverWorthKnowingIcon;
		ResultText = FText::Format(LOCTEXT("DeliveryStopped", "{0} was stopped. What it had written is kept."), DeliveryName);
	}
	else
	{
		ResultIcon = DeliverStopsIcon;
		ResultText = FText::Format(LOCTEXT("DeliveryFailed", "{0} did not finish. The Output Log says why."), DeliveryName);
	}
	Notify(ResultText, Result.bSuccess);
}

FText SRoscDeliverTab::GetRenderProgressText() const
{
	const FText Percent = Rosc::Units::FormatNumber(GetRenderer()->GetProgress() * 100.0, 0);
	return bDeliveryRunning
		? FText::Format(LOCTEXT("RenderProgressNamed", "Rendering {0}: {1} %"), DeliveryName, Percent)
		: FText::Format(LOCTEXT("RenderProgress", "Rendering: {0} %"), Percent);
}

#undef LOCTEXT_NAMESPACE
