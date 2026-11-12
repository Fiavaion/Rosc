// Copyright 2026 Fiavaion. All rights reserved.

#include "SUEcineAITab.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "CineCameraActor.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "IDesktopPlatform.h"
#include "ISettingsModule.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UEcineAIConsent.h"
#include "UEcineAIModelStore.h"
#include "UEcineAISettings.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraTarget.h"
#include "UEcineShotAssistant.h"
#include "UEcineStyle.h"
#include "UEcineUnits.h"
#include "UEcineWidgets.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

using namespace UEcine::Widgets;

namespace
{
	/** How often the tab looks at what it depends on: settings, model files, the helper. */
	constexpr double AIRefreshSeconds = 0.25;
	constexpr float AIProgressBarHeight = 8.0f;
	constexpr float AIRequestBoxHeight = 72.0f;
	constexpr float AIResultPictureWidth = 220.0f;
	/** How far what belongs to a step of a plan is set in from the step. */
	constexpr float AIPlanIndent = 16.0f;

	const FName AIStopsIcon("Icons.ErrorWithColor");
	const FName AIDoneIcon("Icons.SuccessWithColor");
	const FName AIInfoIcon("Icons.InfoWithColor");

	UUEcineAISubsystem* GetAI()
	{
		return GEditor->GetEditorSubsystem<UUEcineAISubsystem>();
	}

	const UUEcineAISettings& GetAISettings()
	{
		return *GetDefault<UUEcineAISettings>();
	}

	/** True when the user has agreed to a host elsewhere, so that there is an agreement to withdraw. */
	bool HasAgreedTo(const FString& Host)
	{
		return !Host.IsEmpty() && !UEcine::AIConsent::IsThisMachine(Host) && UEcine::AIConsent::GetHosts().Contains(Host);
	}

	FText GetAITaskName(EUEcineAITask Task)
	{
		switch (Task)
		{
		case EUEcineAITask::Depth: return LOCTEXT("TaskDepth", "Depth map");
		case EUEcineAITask::Matte: return LOCTEXT("TaskMatte", "Matte");
		default: return LOCTEXT("TaskFocal", "Lens estimate");
		}
	}

	FText GetAIBackendName(EUEcineAIBackend Backend)
	{
		switch (Backend)
		{
		case EUEcineAIBackend::Editor: return LOCTEXT("BackendEditor", "In the editor");
		case EUEcineAIBackend::Helper: return LOCTEXT("BackendHelper", "Helper on this machine");
		default: return LOCTEXT("BackendServer", "Server elsewhere");
		}
	}

	FReply OpenAISettings()
	{
		const UUEcineAISettings& Settings = GetAISettings();
		FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer(Settings.GetContainerName(), Settings.GetCategoryName(), Settings.GetSectionName());
		return FReply::Handled();
	}

	TSharedRef<SWidget> MakeAISettingsButton()
	{
		return MakeButton(LOCTEXT("OpenAISettingsButton", "Open AI Settings"),
			LOCTEXT("OpenAISettingsToolTip", "Open AI settings: your Claude API key, the helper's Python, a server's address and the models folder, under Editor Preferences > Plugins > UEcine AI."),
			FOnClicked::CreateStatic(&OpenAISettings));
	}

	/** A row of buttons that folds onto a second line when the panel is narrow. */
	TSharedRef<SWrapBox> MakeAIButtonRow(TArray<TSharedRef<SWidget>> Buttons = {})
	{
		const TSharedRef<SWrapBox> Row = SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.0, 4.0));
		for (const TSharedRef<SWidget>& Button : Buttons)
		{
			Row->AddSlot()[ Button ];
		}
		return Row;
	}

	/** A file or folder: it has no spaces to break at, so it is allowed to break anywhere. */
	TSharedRef<SWidget> MakeAIPathText(const FString& Path)
	{
		return SNew(STextBlock)
			.TextStyle(&TextStyle("UEcine.Body"))
			.AutoWrapText(true)
			.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
			.Text(FText::FromString(Path));
	}

	/** A result picture as something Slate can draw. Null when it cannot be read or there is no renderer. */
	TSharedPtr<FSlateDynamicImageBrush> MakeAIResultBrush(const FString& File)
	{
		FImage Picture;
		if (!FApp::CanEverRender() || !FImageUtils::LoadImage(*File, Picture))
		{
			return nullptr;
		}
		Picture.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);

		// Slate keeps a picture under its name, so each one made gets a name of its own.
		static uint32 BrushCount = 0;
		const FName BrushName(*FString::Printf(TEXT("UEcineAIResult%u"), BrushCount++));
		return FSlateDynamicImageBrush::CreateWithImageData(BrushName, FVector2D(Picture.SizeX, Picture.SizeY), TArray<uint8>(Picture.RawData));
	}
}

void SUEcineAITab::Construct(const FArguments& InArgs, TSharedRef<FUEcineCameraTarget> InTarget)
{
	Target = InTarget;
	JobFinishedHandle = GetAI()->OnJobFinished().AddSP(this, &SUEcineAITab::ShowJobResult);

	const FText RequestName = LOCTEXT("RequestName", "Shot description");
	const FText RequestToolTip = LOCTEXT("RequestToolTip", "Describe the shot: the gear, the move, how long it takes, what to keep sharp. Enter asks for the plan; Shift+Enter starts a new line.");
	const FText PictureLabel = LOCTEXT("PictureLabel", "Picture");
	const FText PictureToolTip = LOCTEXT("PictureToolTip", "The still or frame of a plate to work on: a PNG, JPEG, EXR or TIFF file.");

	const TSharedRef<SWidget> PictureControl = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredHeight(FUEcineStyle::MinTargetSize)
			[
				SNew(SEditableTextBox)
				.ToolTipText(PictureToolTip)
				.AccessibleText(PictureLabel)
				.Text_Lambda([this] { return FText::FromString(PictureFile); })
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
				{
					// Escape gives up what was typed.
					if (CommitType != ETextCommit::OnCleared)
					{
						PictureFile = Text.ToString().TrimStartAndEnd().TrimQuotes();
					}
				})
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeIconButton("Icons.FolderOpen", LOCTEXT("BrowsePictureToolTip", "Choose the picture"), FOnClicked::CreateSP(this, &SUEcineAITab::OnBrowsePictureClicked))
		];

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(FUEcineStyle::PanelPadding)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeSectionHeader(LOCTEXT("AssistantSection", "Shot Assistant"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FUEcineStyle::RowPadding)
			[
				MakeNote(LOCTEXT("AssistantNote", "Describe the shot in your own words, such as \"Crane up over the table to reveal Anna, about eight seconds, and keep her sharp.\" The assistant plans it for this camera with the gear UEcine has. Nothing changes until you have read the plan and applied it."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FUEcineStyle::RowPadding)
			[
				SNew(SBox)
				.MinDesiredHeight(AIRequestBoxHeight)
				[
					SNew(SMultiLineEditableTextBox)
					.ToolTipText(RequestToolTip)
					.AccessibleText(RequestName)
					.AutoWrapText(true)
					.ModiferKeyForNewLine(EModifierKey::Shift)
					.Text_Lambda([this] { return Words; })
					.OnTextChanged_Lambda([this](const FText& Text) { Words = Text; })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
					{
						Words = Text;
						if (CommitType == ETextCommit::OnEnter && CanAskForPlan())
						{
							OnPlanClicked();
						}
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FUEcineStyle::ButtonRowPadding).HAlign(HAlign_Left)
			[
				MakeButton(LOCTEXT("PlanButton", "Plan Shot"),
					LOCTEXT("PlanToolTip", "Plan shot: ask the assistant to turn the description into steps. You read them before anything changes."),
					FOnClicked::CreateSP(this, &SUEcineAITab::OnPlanClicked),
					TAttribute<bool>::CreateSP(this, &SUEcineAITab::CanAskForPlan))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(AssistantBox, SBox)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FUEcineStyle::SectionPadding)
			[
				MakeSectionHeader(LOCTEXT("PicturesSection", "Pictures"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FUEcineStyle::RowPadding)
			[
				MakeNote(LOCTEXT("PicturesNote", "A depth map, a matte or a lens estimate from a still or a frame of a plate: pictures Unreal did not render. Depth and mattes of what Unreal renders come with a VFX delivery."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FUEcineStyle::RowPadding)
			[
				MakeRow(PictureLabel, PictureToolTip, PictureControl)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(PicturesBox, SBox)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FUEcineStyle::SectionPadding)
			[
				MakeSectionHeader(LOCTEXT("ModelsSection", "Models"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(ModelsBox, SBox)
			]
		]
	];

	AssistantKey = ReadAssistantKey();
	PicturesKey = ReadPicturesKey();
	ModelsKey = ReadModelsKey();
	RebuildAssistant();
	RebuildPictures();
	RebuildModels();
}

SUEcineAITab::~SUEcineAITab()
{
	if (GEditor != nullptr)
	{
		if (UUEcineAISubsystem* AI = GetAI())
		{
			AI->OnJobFinished().Remove(JobFinishedHandle);
		}
	}
}

void SUEcineAITab::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (InCurrentTime - LastRefreshTime < AIRefreshSeconds)
	{
		return;
	}
	LastRefreshTime = InCurrentTime;

	FAssistantKey NewAssistant = ReadAssistantKey();
	if (!(NewAssistant == AssistantKey))
	{
		AssistantKey = MoveTemp(NewAssistant);
		RebuildAssistant();
	}
	FPicturesKey NewPictures = ReadPicturesKey();
	if (!(NewPictures == PicturesKey))
	{
		PicturesKey = MoveTemp(NewPictures);
		RebuildPictures();
	}
	FModelsKey NewModels = ReadModelsKey();
	if (!(NewModels == ModelsKey))
	{
		ModelsKey = MoveTemp(NewModels);
		RebuildModels();
	}
}

void SUEcineAITab::ShowPlan(const FUEcineShotPlanResult& Result)
{
	Plan.Reset();
	PlanOutcome = FText::GetEmpty();
	// A request the user gave up has no reason to show.
	PlanError = Result.bSuccess ? FText::GetEmpty() : Result.Error;
	if (Result.bSuccess)
	{
		Plan = Result;
	}
	++AssistantRevision;
	RefreshSoon();
}

void SUEcineAITab::ShowJobResult(const FUEcineAIResult& Result)
{
	JobResult = Result;
	ResultBrush = Result.bSuccess && !Result.File.IsEmpty() ? MakeAIResultBrush(Result.File) : nullptr;
	++PicturesRevision;
	RefreshSoon();
}

SUEcineAITab::FAssistantKey SUEcineAITab::ReadAssistantKey() const
{
	const UUEcineAISubsystem* AI = GetAI();
	FAssistantKey Key;
	Key.Camera = Target->GetCamera();
	FText WhyNot;
	if (!AI->CanPlanShot(WhyNot))
	{
		Key.WhyNot = WhyNot.ToString();
	}
	Key.Host = UEcine::AIConsent::GetHost(GetAISettings().AssistantUrl);
	Key.bConsentNeeded = !AI->GetAssistantHostNeedingConsent().IsEmpty();
	Key.bConsentGiven = HasAgreedTo(Key.Host);
	Key.bPlanning = AI->IsPlanningShot();
	if (Plan.IsSet() && Key.Camera.IsValid())
	{
		for (const UEcine::ShotAssistant::FPlanLine& Line : UEcine::ShotAssistant::Review(Plan->Plan, Key.Camera.Get()))
		{
			Key.PlanReview += Line.Text.ToString() + LINE_TERMINATOR + Line.Problem.ToString() + LINE_TERMINATOR;
			for (const FText& Warning : Line.Warnings)
			{
				Key.PlanReview += Warning.ToString() + LINE_TERMINATOR;
			}
		}
	}
	Key.Revision = AssistantRevision;
	return Key;
}

SUEcineAITab::FPicturesKey SUEcineAITab::ReadPicturesKey() const
{
	const UUEcineAISubsystem* AI = GetAI();
	FPicturesKey Key;
	Key.Backend = GetAISettings().Backend;
	for (const EUEcineAITask Task : { EUEcineAITask::Depth, EUEcineAITask::Matte, EUEcineAITask::FocalLength })
	{
		FText WhyNot;
		Key.WhyNot.Add(AI->CanRun(Task, WhyNot) ? FString() : WhyNot.ToString());
	}
	if (Key.Backend == EUEcineAIBackend::Server)
	{
		Key.Host = UEcine::AIConsent::GetHost(GetAISettings().ServerUrl);
	}
	Key.bConsentNeeded = !AI->GetHostNeedingConsent().IsEmpty();
	Key.bConsentGiven = HasAgreedTo(Key.Host);
	Key.bWorking = AI->IsWorking();
	Key.Revision = PicturesRevision;
	return Key;
}

SUEcineAITab::FModelsKey SUEcineAITab::ReadModelsKey() const
{
	const UUEcineAISubsystem* AI = GetAI();
	FModelsKey Key;
	for (const FUEcineAIModel& Model : FUEcineAIModelList::Get().GetModels())
	{
		Key.Installed.Add(UEcine::AIModelStore::IsInstalled(Model));
	}
	if (const FUEcineAIModel* Downloading = AI->GetDownloadingModel())
	{
		Key.DownloadingId = Downloading->Id;
	}
	Key.DownloadError = AI->GetDownloadError().ToString();
	return Key;
}

void SUEcineAITab::RebuildAssistant()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	ACineCameraActor* Camera = AssistantKey.Camera.Get();
	const FString Host = AssistantKey.Host;
	const FText HostText = FText::FromString(Host);

	if (Camera == nullptr)
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIInfoIcon, LOCTEXT("AssistantNeedsCamera", "Choose or make a camera above, and the assistant can plan a shot for it.")));
	}
	else if (!AssistantKey.WhyNot.IsEmpty())
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIInfoIcon, FText::FromString(AssistantKey.WhyNot)));
		Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({ MakeAISettingsButton() }));
	}
	else if (AssistantKey.bConsentNeeded)
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIInfoIcon, FText::Format(LOCTEXT("AssistantConsent",
			"The assistant is a Claude model at {0}. Planning a shot sends it the words you type and a description of the set: this camera and its support, the names, sizes and positions of the things near the camera, and the names of the shots in the open sequence. No pictures are sent. Each request is billed to your Claude API key."), HostText)));
		Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({
			// The line above names the host: an address is as long as the user typed it, and a button does not wrap.
			MakeButton(LOCTEXT("AssistantAgreeButton", "Agree to Send Shot Descriptions"),
				FText::Format(LOCTEXT("AssistantAgreeToolTip", "Agree: let the shot assistant send shot descriptions from this project to {0}. You can withdraw this here at any time."), HostText),
				FOnClicked::CreateSPLambda(this, [this, Host]
				{
					UEcine::AIConsent::Give(Host);
					RefreshSoon();
					return FReply::Handled();
				}))
		}));
	}

	if (AssistantKey.bPlanning)
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIInfoIcon, FText::Format(LOCTEXT("AssistantAsking", "Asking {0} for a plan. This can take a minute."), FText::FromString(GetAISettings().AssistantModel))));
		Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({
			MakeButton(LOCTEXT("StopPlanButton", "Stop"),
				LOCTEXT("StopPlanToolTip", "Stop: give up this request. A request already sent may still be billed."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					GetAI()->CancelShotPlan();
					RefreshSoon();
					return FReply::Handled();
				}))
		}));
	}
	if (!PlanError.IsEmpty())
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIStopsIcon, PlanError));
	}
	if (!PlanOutcome.IsEmpty())
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIDoneIcon, PlanOutcome));
	}
	if (Plan.IsSet() && Camera != nullptr)
	{
		AddPlanReview(*Content, Camera);
	}

	if (AssistantKey.bConsentGiven)
	{
		Add(FUEcineStyle::SectionPadding, MakeNote(FText::Format(LOCTEXT("AssistantAgreed", "You agreed to send shot descriptions from this project to {0}."), HostText)));
		Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({
			MakeButton(LOCTEXT("AssistantWithdrawButton", "Withdraw Agreement"),
				FText::Format(LOCTEXT("AssistantWithdrawToolTip", "Withdraw agreement: send nothing more to {0} until you agree again."), HostText),
				FOnClicked::CreateSPLambda(this, [this, Host]
				{
					UEcine::AIConsent::Withdraw(Host);
					RefreshSoon();
					return FReply::Handled();
				}))
		}));
	}
	AssistantBox->SetContent(Content);
}

void SUEcineAITab::AddPlanReview(SVerticalBox& Content, ACineCameraActor* Camera)
{
	using namespace UEcine::ShotAssistant;

	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content.AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	const FMargin Indented(AIPlanIndent, FUEcineStyle::RowSpacing, 0.0f, 0.0f);

	Add(FUEcineStyle::SectionPadding, SNew(STextBlock).TextStyle(&TextStyle("UEcine.Label")).Text(LOCTEXT("PlanHeading", "The plan")));
	Add(FUEcineStyle::RowPadding, MakeNote(FText::FromString(Plan->Plan.Summary)));

	const TArray<FPlanLine> Lines = Review(Plan->Plan, Camera);
	int32 Problems = 0;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FPlanLine& Line = Lines[Index];
		Add(FUEcineStyle::RowPadding, MakeNote(FText::Format(LOCTEXT("PlanStep", "{0}. {1}"), FText::AsNumber(Index + 1), Line.Text)));
		if (!Line.Problem.IsEmpty())
		{
			++Problems;
			Add(Indented, MakeStatusLine(AIStopsIcon, Line.Problem));
		}
		for (const FText& Warning : Line.Warnings)
		{
			Add(Indented, MakeWarning(Warning));
		}
	}

	const bool bCanApply = CanApply(Lines);
	if (Lines.IsEmpty())
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIInfoIcon, LOCTEXT("PlanEmpty", "The plan has no steps: the assistant found nothing here that UEcine's tabs can do.")));
	}
	else if (!bCanApply)
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIStopsIcon, FText::Format(LOCTEXT("PlanStopped",
			"{0}|plural(one=A step,other=# steps) of this plan cannot be carried out, so it cannot be applied. Describe the shot another way, or put right what the plan lacks and ask again."), Problems)));
	}

	Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({
			MakeButton(LOCTEXT("ApplyPlanButton", "Apply Plan"),
				LOCTEXT("ApplyPlanToolTip", "Apply plan: carry out every step for this camera. All of it is one step in the undo history."),
				FOnClicked::CreateSP(this, &SUEcineAITab::OnApplyPlanClicked),
				bCanApply),
			MakeButton(LOCTEXT("DiscardPlanButton", "Discard"),
				LOCTEXT("DiscardPlanToolTip", "Discard: put the plan aside. Nothing was changed."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					Plan.Reset();
					++AssistantRevision;
					RefreshSoon();
					return FReply::Handled();
				}))
		}));
	Add(FUEcineStyle::RowPadding, MakeNote(FText::Format(LOCTEXT("PlanTokens", "This request came to {0} tokens in and {1} out, billed to your Claude API key."),
		FText::AsNumber(Plan->InputTokens), FText::AsNumber(Plan->OutputTokens))));
}

void SUEcineAITab::RebuildPictures()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	const bool bWorking = PicturesKey.bWorking;
	const FString Host = PicturesKey.Host;
	const FText HostText = FText::FromString(Host);

	TArray<FChoice> Backends;
	for (const EUEcineAIBackend Backend : { EUEcineAIBackend::Editor, EUEcineAIBackend::Helper, EUEcineAIBackend::Server })
	{
		Backends.Add(FChoice{ GetAIBackendName(Backend),
			[Backend] { return GetAISettings().Backend == Backend; },
			[this, Backend]
			{
				UUEcineAISettings* Settings = GetMutableDefault<UUEcineAISettings>();
				Settings->Backend = Backend;
				Settings->SaveConfig();
				RefreshSoon();
			} });
	}
	Add(FUEcineStyle::RowPadding, MakeChoiceRow(LOCTEXT("RunOnLabel", "Run on"),
		LOCTEXT("RunOnToolTip", "Where picture jobs are run. In the editor: on this machine's graphics card. Helper on this machine: in a Python program UEcine starts, for machines the editor cannot run models on. Server elsewhere: on a machine you or your studio run; the picture is sent there."),
		TAttribute<FText>::CreateLambda([] { return GetAIBackendName(GetAISettings().Backend); }), MoveTemp(Backends), !bWorking));

	if (PicturesKey.Backend == EUEcineAIBackend::Helper)
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIInfoIcon, TAttribute<FText>::CreateLambda([] { return GetAI()->GetHelperStatus(); })));
	}
	if (PicturesKey.Backend == EUEcineAIBackend::Server)
	{
		if (PicturesKey.bConsentNeeded)
		{
			Add(FUEcineStyle::RowPadding, MakeStatusLine(AIInfoIcon, FText::Format(LOCTEXT("PicturesConsent",
				"Picture jobs on a server send the picture you choose to {0}, with the key in your AI settings. Nothing else is sent, and nothing is sent until you agree."), HostText)));
		}
		const TSharedRef<SWrapBox> ServerButtons = MakeAIButtonRow();
		if (PicturesKey.bConsentNeeded)
		{
			ServerButtons->AddSlot()
			[
				// As for the assistant: the line above names the server.
				MakeButton(LOCTEXT("PicturesAgreeButton", "Agree to Send Pictures"),
					FText::Format(LOCTEXT("PicturesAgreeToolTip", "Agree: let picture jobs from this project send pictures to {0}. You can withdraw this here at any time."), HostText),
					FOnClicked::CreateSPLambda(this, [this, Host]
					{
						UEcine::AIConsent::Give(Host);
						RefreshSoon();
						return FReply::Handled();
					}))
			];
		}
		else if (PicturesKey.bConsentGiven)
		{
			ServerButtons->AddSlot()
			[
				MakeButton(LOCTEXT("PicturesWithdrawButton", "Withdraw Agreement"),
					FText::Format(LOCTEXT("PicturesWithdrawToolTip", "Withdraw agreement: send no more pictures to {0} until you agree again."), HostText),
					FOnClicked::CreateSPLambda(this, [this, Host]
					{
						UEcine::AIConsent::Withdraw(Host);
						RefreshSoon();
						return FReply::Handled();
					}))
			];
		}
		if (!Host.IsEmpty())
		{
			ServerButtons->AddSlot()
			[
				MakeButton(LOCTEXT("CheckServerButton", "Check Server"),
					LOCTEXT("CheckServerToolTip", "Check server: ask the server whether it answers and what it can do. Sends the key and nothing else."),
					FOnClicked::CreateSPLambda(this, [this]
					{
						GetAI()->CheckServer([WeakTab = TWeakPtr<SUEcineAITab>(SharedThis(this))](bool bReachable, const FText& Message)
						{
							if (const TSharedPtr<SUEcineAITab> Tab = WeakTab.Pin())
							{
								Tab->bServerReachable = bReachable;
								Tab->ServerCheckText = Message;
								++Tab->PicturesRevision;
								Tab->RefreshSoon();
							}
						});
						return FReply::Handled();
					}))
			];
		}
		ServerButtons->AddSlot()[ MakeAISettingsButton() ];
		Add(FUEcineStyle::ButtonRowPadding, ServerButtons);
		if (!ServerCheckText.IsEmpty())
		{
			Add(FUEcineStyle::RowPadding, MakeStatusLine(bServerReachable ? AIDoneIcon : AIStopsIcon, ServerCheckText));
		}
		if (PicturesKey.bConsentGiven)
		{
			Add(FUEcineStyle::RowPadding, MakeNote(FText::Format(LOCTEXT("PicturesAgreed", "You agreed to send pictures from this project to {0}."), HostText)));
		}
	}

	// What stands in the way of a job, said once however many jobs it stops.
	TArray<FString> Reasons;
	for (const FString& WhyNot : PicturesKey.WhyNot)
	{
		if (!WhyNot.IsEmpty())
		{
			Reasons.AddUnique(WhyNot);
		}
	}
	for (const FString& Reason : Reasons)
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIInfoIcon, FText::FromString(Reason)));
	}

	if (bWorking)
	{
		Add(FUEcineStyle::RowPadding, MakeNote(TAttribute<FText>::CreateLambda([]
		{
			FText Stage;
			float Fraction = 0.0f;
			GetAI()->GetProgress(Stage, Fraction);
			return Stage.IsEmpty() ? LOCTEXT("JobWorking", "Working…") : Stage;
		})));
		Add(FUEcineStyle::RowPadding, SNew(SBox)
			.HeightOverride(AIProgressBarHeight)
			[
				SNew(SProgressBar)
				.Percent_Lambda([]
				{
					FText Stage;
					float Fraction = 0.0f;
					GetAI()->GetProgress(Stage, Fraction);
					return TOptional<float>(Fraction);
				})
			]);
		Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({
			MakeButton(LOCTEXT("StopJobButton", "Stop"),
				LOCTEXT("StopJobToolTip", "Stop: give up the picture job that is running."),
				FOnClicked::CreateSPLambda(this, [this]
				{
					GetAI()->CancelAll();
					RefreshSoon();
					return FReply::Handled();
				}))
		}));
	}
	else
	{
		const TSharedRef<SWrapBox> Jobs = MakeAIButtonRow();
		const auto AddJob = [this, &Jobs](EUEcineAITask Task, const FText& Label, const FText& ToolTip)
		{
			Jobs->AddSlot()
			[
				MakeButton(Label, ToolTip, FOnClicked::CreateSP(this, &SUEcineAITab::OnRunClicked, Task),
					PicturesKey.WhyNot[static_cast<int32>(Task)].IsEmpty() && !PicturesKey.bConsentNeeded)
			];
		};
		AddJob(EUEcineAITask::Depth, LOCTEXT("DepthButton", "Depth Map"),
			LOCTEXT("DepthToolTip", "Depth map: a grey picture of how near things are in the picture, near being bright. It is relative depth, not distances."));
		AddJob(EUEcineAITask::Matte, LOCTEXT("MatteButton", "Matte"),
			LOCTEXT("MatteToolTip", "Matte: a grey picture that is white where the picture's subject is."));
		AddJob(EUEcineAITask::FocalLength, LOCTEXT("EstimateLensButton", "Estimate Lens"),
			LOCTEXT("EstimateLensToolTip", "Estimate lens: work out the field of view the picture was taken with, and the lens of this camera's kit that matches it. It is an estimate; check it against the picture."));
		Add(FUEcineStyle::ButtonRowPadding, Jobs);
		AddJobResult(*Content);
	}
	PicturesBox->SetContent(Content);
}

void SUEcineAITab::AddJobResult(SVerticalBox& Content)
{
	if (!JobResult.IsSet())
	{
		return;
	}
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content.AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	const FUEcineAIResult& Result = *JobResult;
	const FText TaskName = GetAITaskName(Result.Request.Task);

	if (!Result.bSuccess)
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIStopsIcon, Result.Error.IsEmpty()
			? FText::Format(LOCTEXT("JobStopped", "{0}: stopped before it was done."), TaskName)
			: FText::Format(LOCTEXT("JobFailed", "{0}: {1}"), TaskName, Result.Error)));
		return;
	}

	if (Result.Request.Task == EUEcineAITask::FocalLength)
	{
		Add(FUEcineStyle::RowPadding, MakeStatusLine(AIDoneIcon, TAttribute<FText>::CreateSP(this, &SUEcineAITab::GetLensMatchText)));
		Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({
			MakeButton(TAttribute<FText>::CreateSP(this, &SUEcineAITab::GetPutOnLensLabel),
				LOCTEXT("PutOnLensToolTip", "Put on the lens of this camera's kit that comes nearest the picture's field of view. One step in the undo history."),
				FOnClicked::CreateSP(this, &SUEcineAITab::OnPutOnLensClicked),
				TAttribute<bool>::CreateSPLambda(this, [this]
				{
					UEcine::CameraEditing::FLensMatch Match;
					return JobResult.IsSet() && UEcine::CameraEditing::MatchLensToFieldOfView(Target->GetCamera(), JobResult->FieldOfViewDeg, Match);
				}))
		}));
		return;
	}

	Add(FUEcineStyle::RowPadding, MakeStatusLine(AIDoneIcon, FText::Format(LOCTEXT("JobWritten", "{0} written:"), TaskName)));
	Add(FUEcineStyle::RowPadding, MakeAIPathText(Result.File));
	if (ResultBrush.IsValid())
	{
		const FVector2f Size = ResultBrush->GetImageSize();
		Add(FUEcineStyle::RowPadding, SNew(SBox)
			.HAlign(HAlign_Left)
			[
				SNew(SBox)
				.WidthOverride(AIResultPictureWidth)
				.HeightOverride(AIResultPictureWidth * Size.Y / FMath::Max(Size.X, 1.0f))
				[
					SNew(SImage)
					.Image(ResultBrush.Get())
					.AccessibleText(TaskName)
				]
			]);
	}
	Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({
		MakeButton(LOCTEXT("OpenResultFolderButton", "Open Folder"),
			LOCTEXT("OpenResultFolderToolTip", "Open folder: show the result's file in the file browser."),
			FOnClicked::CreateLambda([Folder = FPaths::GetPath(Result.File)]
			{
				FPlatformProcess::ExploreFolder(*Folder);
				return FReply::Handled();
			}))
	}));
}

void SUEcineAITab::RebuildModels()
{
	const TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Add = [&Content](const FMargin& Padding, const TSharedRef<SWidget>& Widget)
	{
		Content->AddSlot().AutoHeight().Padding(Padding)[ Widget ];
	};
	Add(FUEcineStyle::RowPadding, MakeNote(LOCTEXT("ModelsNote", "Models are not part of UEcine. Each is downloaded once from its makers' page, under their licence, into a folder all your projects share. Read the licence before you download.")));

	const TConstArrayView<FUEcineAIModel> Models = FUEcineAIModelList::Get().GetModels();
	for (int32 Index = 0; Index < Models.Num(); ++Index)
	{
		const FUEcineAIModel& Model = Models[Index];
		const FText ModelName = FText::FromString(Model.Name);
		Add(FUEcineStyle::SectionPadding, SNew(STextBlock).TextStyle(&TextStyle("UEcine.Value")).Text(ModelName));
		Add(FUEcineStyle::RowPadding, MakeNote(FText::Format(LOCTEXT("ModelFacts", "{0}. {1} MB. Licence: {2}."),
			GetAITaskName(Model.Task), UEcine::Units::FormatNumber(Model.SizeBytes / 1.0e6, 0), FText::FromString(Model.Licence))));

		const bool bDownloading = ModelsKey.DownloadingId == Model.Id;
		if (ModelsKey.Installed[Index])
		{
			Add(FUEcineStyle::RowPadding, MakeStatusLine(AIDoneIcon, LOCTEXT("ModelInstalled", "Downloaded.")));
		}
		else if (bDownloading)
		{
			Add(FUEcineStyle::RowPadding, MakeNote(TAttribute<FText>::CreateLambda([]
			{
				return FText::Format(LOCTEXT("ModelDownloading", "Downloading: {0}%"), FText::AsNumber(FMath::RoundToInt32(GetAI()->GetDownloadFraction() * 100.0f)));
			})));
			Add(FUEcineStyle::RowPadding, SNew(SBox)
				.HeightOverride(AIProgressBarHeight)
				[
					SNew(SProgressBar)
					.Percent_Lambda([] { return TOptional<float>(GetAI()->GetDownloadFraction()); })
				]);
		}

		const TSharedRef<SWrapBox> Buttons = MakeAIButtonRow();
		if (bDownloading)
		{
			Buttons->AddSlot()
			[
				MakeButton(LOCTEXT("StopDownloadButton", "Stop Download"),
					FText::Format(LOCTEXT("StopDownloadToolTip", "Stop download: give up downloading {0}. What has come so far is thrown away."), ModelName),
					FOnClicked::CreateSPLambda(this, [this]
					{
						GetAI()->CancelDownload();
						RefreshSoon();
						return FReply::Handled();
					}))
			];
		}
		else if (!ModelsKey.Installed[Index])
		{
			Buttons->AddSlot()
			[
				MakeButton(LOCTEXT("DownloadModelButton", "Download"),
					FText::Format(LOCTEXT("DownloadModelToolTip", "Download {0} from {1}. By downloading it you take it under its makers' licence, {2}."),
						ModelName, FText::FromString(UEcine::AIConsent::GetHost(Model.Url)), FText::FromString(Model.Licence)),
					FOnClicked::CreateSPLambda(this, [this, Model]
					{
						GetAI()->DownloadModel(Model);
						RefreshSoon();
						return FReply::Handled();
					}),
					ModelsKey.DownloadingId.IsEmpty())
			];
		}
		Buttons->AddSlot()
		[
			MakeButton(LOCTEXT("ModelLicenceButton", "Licence"),
				FText::Format(LOCTEXT("ModelLicenceToolTip", "Licence: open the page that states the licence of {0} in your browser."), ModelName),
				FOnClicked::CreateLambda([Page = Model.LicencePage]
				{
					FPlatformProcess::LaunchURL(*Page, nullptr, nullptr);
					return FReply::Handled();
				}))
		];
		Add(FUEcineStyle::ButtonRowPadding, Buttons);
	}

	if (!ModelsKey.DownloadError.IsEmpty())
	{
		Add(FUEcineStyle::SectionPadding, MakeStatusLine(AIStopsIcon, FText::FromString(ModelsKey.DownloadError)));
	}
	Add(FUEcineStyle::SectionPadding, MakeNote(LOCTEXT("ModelsFolderNote", "The models are kept in:")));
	Add(FUEcineStyle::RowPadding, MakeAIPathText(UEcine::AIModelStore::GetFolder()));
	Add(FUEcineStyle::ButtonRowPadding, MakeAIButtonRow({
		MakeButton(LOCTEXT("OpenModelsFolderButton", "Open Models Folder"),
			LOCTEXT("OpenModelsFolderToolTip", "Open models folder: show the folder the models are kept in. It is there once a model has been downloaded."),
			FOnClicked::CreateLambda([]
			{
				FPlatformProcess::ExploreFolder(*UEcine::AIModelStore::GetFolder());
				return FReply::Handled();
			}))
	}));
	ModelsBox->SetContent(Content);
}

bool SUEcineAITab::CanAskForPlan() const
{
	return AssistantKey.Camera.IsValid() && AssistantKey.WhyNot.IsEmpty() && !AssistantKey.bConsentNeeded && !AssistantKey.bPlanning && !Words.ToString().TrimStartAndEnd().IsEmpty();
}

FReply SUEcineAITab::OnPlanClicked()
{
	Plan.Reset();
	PlanOutcome = FText::GetEmpty();
	PlanError = FText::GetEmpty();
	FText Error;
	const bool bAsked = GetAI()->PlanShot(Words.ToString(), UEcine::ShotAssistant::DescribeSet(Target->GetCamera()), Error,
		[WeakTab = TWeakPtr<SUEcineAITab>(SharedThis(this))](const FUEcineShotPlanResult& Result)
		{
			if (const TSharedPtr<SUEcineAITab> Tab = WeakTab.Pin())
			{
				Tab->ShowPlan(Result);
			}
		});
	if (!bAsked)
	{
		PlanError = Error;
	}
	++AssistantRevision;
	RefreshSoon();
	return FReply::Handled();
}

FReply SUEcineAITab::OnApplyPlanClicked()
{
	FText Error;
	if (Plan.IsSet() && UEcine::ShotAssistant::Apply(Plan->Plan, Target->GetCamera(), Error))
	{
		PlanOutcome = LOCTEXT("PlanApplied", "The plan was applied. Undo takes all of it back in one step.");
	}
	else
	{
		PlanError = Error;
	}
	Plan.Reset();
	++AssistantRevision;
	RefreshSoon();
	return FReply::Handled();
}

FReply SUEcineAITab::OnBrowsePictureClicked()
{
	TArray<FString> Files;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform != nullptr && DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
		LOCTEXT("BrowsePictureTitle", "Choose the picture").ToString(), FPaths::GetPath(PictureFile), FString(),
		TEXT("Pictures (*.png;*.jpg;*.jpeg;*.exr;*.tif;*.tiff)|*.png;*.jpg;*.jpeg;*.exr;*.tif;*.tiff"), EFileDialogFlags::None, Files) && !Files.IsEmpty())
	{
		PictureFile = FPaths::ConvertRelativePathToFull(Files[0]);
	}
	return FReply::Handled();
}

FReply SUEcineAITab::OnRunClicked(EUEcineAITask Task)
{
	FUEcineAIRequest Request;
	Request.Task = Task;
	Request.PictureFile = PictureFile;
	FText Error;
	if (PictureFile.IsEmpty())
	{
		Error = LOCTEXT("NoPictureChosen", "Choose the picture to work on first.");
	}
	if (!Error.IsEmpty() || !GetAI()->Submit(Request, Error))
	{
		// A job that is not taken ends here, with the reason where a result would be.
		FUEcineAIResult NotTaken;
		NotTaken.Request = Request;
		NotTaken.Error = Error;
		ShowJobResult(NotTaken);
	}
	else
	{
		JobResult.Reset();
		ResultBrush.Reset();
		++PicturesRevision;
		RefreshSoon();
	}
	return FReply::Handled();
}

FReply SUEcineAITab::OnPutOnLensClicked()
{
	FText Error;
	if (JobResult.IsSet() && !UEcine::CameraEditing::PutOnLensForFieldOfView(Target->GetCamera(), JobResult->FieldOfViewDeg, Error))
	{
		Notify(Error, false);
	}
	return FReply::Handled();
}

FText SUEcineAITab::GetLensMatchText() const
{
	if (!JobResult.IsSet())
	{
		return FText::GetEmpty();
	}
	const FText FieldOfView = UEcine::Units::FormatNumber(JobResult->FieldOfViewDeg, 0);
	UEcine::CameraEditing::FLensMatch Match;
	if (!UEcine::CameraEditing::MatchLensToFieldOfView(Target->GetCamera(), JobResult->FieldOfViewDeg, Match))
	{
		return FText::Format(LOCTEXT("LensEstimateNoCamera", "The picture looks to have been taken with a horizontal field of view of about {0}°. Choose a camera, and UEcine works out the lens that matches on its recording format."), FieldOfView);
	}
	return FText::Format(LOCTEXT("LensEstimate", "The picture looks to have been taken with a horizontal field of view of about {0}°. On this camera's recording format that is a {1} mm lens; the nearest in its kit is the {2}. It is an estimate: check it against the picture."),
		FieldOfView, UEcine::Units::FormatNumber(Match.FocalLengthMm, 0), FText::FromString(Match.LensName));
}

FText SUEcineAITab::GetPutOnLensLabel() const
{
	UEcine::CameraEditing::FLensMatch Match;
	return JobResult.IsSet() && UEcine::CameraEditing::MatchLensToFieldOfView(Target->GetCamera(), JobResult->FieldOfViewDeg, Match)
		? FText::Format(LOCTEXT("PutOnLensButton", "Put On the {0}"), FText::FromString(Match.LensName))
		: LOCTEXT("PutOnLensButtonNoMatch", "Put On the Matching Lens");
}

#undef LOCTEXT_NAMESPACE
