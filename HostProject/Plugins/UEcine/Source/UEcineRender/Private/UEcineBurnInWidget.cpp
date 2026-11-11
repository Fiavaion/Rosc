#include "UEcineBurnInWidget.h"

#include "Graph/MovieGraphBlueprintLibrary.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphPipeline.h"
#include "Graph/Nodes/MovieGraphSetMetadataAttributesNode.h"
#include "Misc/Timecode.h"
#include "MoviePipelineQueue.h"
#include "Styling/CoreStyle.h"
#include "UEcineDeliver.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	int32 BurnInUpdateCount = 0;
}

/** The drawn part of the burn-in: a strip of text along the bottom of the picture, or a slate over all of it. */
class SUEcineBurnIn : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcineBurnIn) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		const auto MakeFont = [this](float PartOfHeight)
		{
			return TAttribute<FSlateFontInfo>::CreateLambda([this, PartOfHeight]
			{
				// A font size is in points at 96 to the inch: three quarters of its height in pixels.
				return FCoreStyle::GetDefaultFontStyle("Regular", FMath::Max(FMath::RoundToInt32(PictureHeight * PartOfHeight * 0.75f), 6));
			});
		};

		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.VAlign(VAlign_Bottom)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f))
				.Padding_Lambda([this] { return FMargin(PictureHeight * 0.02f, PictureHeight * 0.008f); })
				.Visibility_Lambda([this] { return Strip.IsEmpty() || !Slate.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				[
					SNew(STextBlock)
					.Font(MakeFont(0.025f))
					.ColorAndOpacity(FLinearColor::White)
					.Text_Lambda([this] { return Strip; })
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
				.Padding_Lambda([this] { return FMargin(PictureHeight * 0.08f); })
				.Visibility_Lambda([this] { return Slate.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				[
					SNew(STextBlock)
					.Font(MakeFont(0.04f))
					.ColorAndOpacity(FLinearColor::White)
					.Text_Lambda([this] { return Slate; })
				]
			]
		];
	}

	void SetFrame(const FText& InStrip, const FText& InSlate, int32 InPictureHeight)
	{
		Strip = InStrip;
		Slate = InSlate;
		PictureHeight = InPictureHeight;
		Invalidate(EInvalidateWidgetReason::Layout);
	}

private:
	FText Strip;
	FText Slate;
	int32 PictureHeight = 1080;
};

void UUEcineBurnInWidget::ProcessEvent(UFunction* Function, void* Parms)
{
	static const FName UpdateEventName(TEXT("UpdateForGraph"));
	if (Function != nullptr && Function->GetFName() == UpdateEventName)
	{
		const FObjectPropertyBase* PipelineParameter = FindFProperty<FObjectPropertyBase>(Function, TEXT("InGraphPipeline"));
		const FObjectPropertyBase* ConfigParameter = FindFProperty<FObjectPropertyBase>(Function, TEXT("InEvaluatedConfig"));
		const FIntProperty* CameraParameter = FindFProperty<FIntProperty>(Function, TEXT("InCameraIndex"));
		if (ensure(PipelineParameter != nullptr && ConfigParameter != nullptr && CameraParameter != nullptr))
		{
			UpdateForFrame(Cast<UMovieGraphPipeline>(PipelineParameter->GetObjectPropertyValue_InContainer(Parms)),
				Cast<UMovieGraphEvaluatedConfig>(ConfigParameter->GetObjectPropertyValue_InContainer(Parms)),
				CameraParameter->GetPropertyValue_InContainer(Parms));
		}
	}
	Super::ProcessEvent(Function, Parms);
}

void UUEcineBurnInWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	BurnIn.Reset();
}

int32 UUEcineBurnInWidget::GetUpdateCount()
{
	return BurnInUpdateCount;
}

TSharedRef<SWidget> UUEcineBurnInWidget::RebuildWidget()
{
	SAssignNew(BurnIn, SUEcineBurnIn);
	BurnIn->SetFrame(Strip, Slate, PictureHeight);
	return BurnIn.ToSharedRef();
}

void UUEcineBurnInWidget::UpdateForFrame(const UMovieGraphPipeline* Pipeline, const UMovieGraphEvaluatedConfig* Config, int32 CameraIndex)
{
	++BurnInUpdateCount;
	if (Pipeline == nullptr || Config == nullptr)
	{
		return;
	}

	TMap<FString, FString> Metadata;
	if (const UMovieGraphSetMetadataAttributesNode* MetadataNode = Config->GetSettingForBranch<UMovieGraphSetMetadataAttributesNode>(UMovieGraphNode::GlobalsPinName, false))
	{
		for (const FMovieGraphMetadataAttribute& Attribute : MetadataNode->MetadataAttributeCollection->MetadataAttributes)
		{
			Metadata.Add(Attribute.Name, Attribute.Value);
		}
	}

	// The slate takes the first frame rendered of each shot.
	const FFrameNumber RootFrame = UMovieGraphBlueprintLibrary::GetRootFrameNumber(Pipeline);
	const UMoviePipelineExecutorShot* Shot = UMovieGraphBlueprintLibrary::GetCurrentExecutorShot(Pipeline);
	if (Shot != CurrentShot.Get())
	{
		CurrentShot = Shot;
		FirstFrameOfShot = RootFrame;
	}

	Strip = FText::GetEmpty();
	Slate = FText::GetEmpty();
	PictureHeight = UMovieGraphBlueprintLibrary::GetDesiredOutputResolution(const_cast<UMovieGraphEvaluatedConfig*>(Config)).Y;
	if (Metadata.Contains(UEcine::Deliver::SlateMetadataName) && RootFrame == FirstFrameOfShot)
	{
		TArray<FString> Lines;
		for (const TPair<FString, FString>& Entry : Metadata)
		{
			Lines.Add(Entry.Key + TEXT("  ") + Entry.Value);
		}
		Slate = FText::FromString(FString::Join(Lines, TEXT("\n")));
	}
	else if (Metadata.Contains(UEcine::Deliver::BurnInMetadataName))
	{
		Strip = FText::FromString(FString::Printf(TEXT("%s   %s   %.0f mm"),
			*Metadata.FindRef(TEXT("uecine/shot")),
			*UMovieGraphBlueprintLibrary::GetRootTimecode(Pipeline).ToString(),
			UMovieGraphBlueprintLibrary::GetCurrentFocalLength(Pipeline, CameraIndex)));
	}
	if (BurnIn.IsValid())
	{
		BurnIn->SetFrame(Strip, Slate, PictureHeight);
	}
}
