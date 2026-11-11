#include "UEcineBurnInWidget.h"

#include "Graph/MovieGraphBlueprintLibrary.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphPipeline.h"
#include "Graph/Nodes/MovieGraphSetMetadataAttributesNode.h"
#include "Misc/Timecode.h"
#include "MoviePipelineQueue.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	int32 BurnInUpdateCount = 0;

	// The picture is somebody else's: the text is drawn in plain white and grey over black,
	// which reads over anything.
	const FLinearColor BurnInTextColour = FLinearColor::White;
	const FLinearColor SlateLabelColour(0.45f, 0.45f, 0.45f);
	const FLinearColor SlateCardColour(0.02f, 0.02f, 0.02f, 1.0f);
	const FLinearColor BurnInStripColour(0.0f, 0.0f, 0.0f, 0.65f);

	/** A font whose letters are a part of the picture's height tall. */
	FSlateFontInfo MakeBurnInFont(const TCHAR* Weight, int32 PictureHeight, float PartOfHeight)
	{
		// A font size is in points at 96 to the inch: three quarters of its height in pixels.
		return FCoreStyle::GetDefaultFontStyle(Weight, FMath::Max(FMath::RoundToInt32(PictureHeight * PartOfHeight * 0.75f), 6));
	}
}

/** The drawn part of the burn-in: text in the corners of the picture, or a slate over all of it. */
class SUEcineBurnIn : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcineBurnIn) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
	}

	/** Draws a frame: the slate when it says anything, otherwise the burn-ins, otherwise nothing. */
	void SetFrame(const FUEcineSlate& Slate, const FUEcineBurnIn& BurnIn, int32 PictureHeight)
	{
		if (!Slate.Title.IsEmpty() || !Slate.Lines.IsEmpty())
		{
			ChildSlot[MakeSlate(Slate, PictureHeight)];
		}
		else
		{
			ChildSlot
			[
				SNew(SOverlay)
				+ SOverlay::Slot().VAlign(VAlign_Top)[MakeStrip(BurnIn.TopLeft, BurnIn.TopRight, PictureHeight)]
				+ SOverlay::Slot().VAlign(VAlign_Bottom)[MakeStrip(BurnIn.BottomLeft, BurnIn.BottomRight, PictureHeight)]
			];
		}
		Invalidate(EInvalidateWidgetReason::Layout);
	}

private:
	static TSharedRef<SWidget> MakeStrip(const FString& Left, const FString& Right, int32 PictureHeight)
	{
		if (Left.IsEmpty() && Right.IsEmpty())
		{
			return SNullWidget::NullWidget;
		}
		const FSlateFontInfo Font = MakeBurnInFont(TEXT("Regular"), PictureHeight, 0.024f);
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(BurnInStripColour)
			.Padding(FMargin(PictureHeight * 0.02f, PictureHeight * 0.008f))
			[
				SNew(SHorizontalBox)
				// The left piece gives way when the picture is too narrow for both.
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock).Font(Font).ColorAndOpacity(BurnInTextColour).Text(FText::FromString(Left)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(PictureHeight * 0.02f, 0.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock).Font(Font).ColorAndOpacity(BurnInTextColour).Text(FText::FromString(Right))
				]
			];
	}

	static TSharedRef<SWidget> MakeSlate(const FUEcineSlate& Slate, int32 PictureHeight)
	{
		// The lines share the height left under the title, so a slate with every line still fits.
		const float LineHeight = FMath::Min(0.045f, 0.62f / FMath::Max(Slate.Lines.Num(), 1));
		const FSlateFontInfo LineFont = MakeBurnInFont(TEXT("Regular"), PictureHeight, LineHeight * 0.7f);

		const TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, PictureHeight * 0.04f))
			[
				SNew(STextBlock)
				.Font(MakeBurnInFont(TEXT("Bold"), PictureHeight, 0.075f))
				.ColorAndOpacity(BurnInTextColour)
				.Text(FText::FromString(Slate.Title))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			];
		for (const FUEcineSlateLine& Line : Slate.Lines)
		{
			Lines->AddSlot().AutoHeight()
			[
				SNew(SBox).HeightOverride(PictureHeight * LineHeight)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(PictureHeight * 0.3f)
						[
							SNew(STextBlock).Font(LineFont).ColorAndOpacity(SlateLabelColour).Text(Line.Label)
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(LineFont).ColorAndOpacity(BurnInTextColour).Text(FText::FromString(Line.Value)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
				]
			];
		}

		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(SlateCardColour)
			.Padding(FMargin(PictureHeight * 0.1f, PictureHeight * 0.08f))
			[
				Lines
			];
	}
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
	Drawn.Reset();
}

int32 UUEcineBurnInWidget::GetUpdateCount()
{
	return BurnInUpdateCount;
}

TSharedRef<SWidget> UUEcineBurnInWidget::RebuildWidget()
{
	SAssignNew(Drawn, SUEcineBurnIn);
	Drawn->SetFrame(Slate, BurnIn, PictureHeight);
	return Drawn.ToSharedRef();
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

	// The slate takes the first frame kept of each shot. The renderer tells of the frames it plays
	// before that one too, so the frame is asked of the shot and not counted here.
	const FFrameNumber RootFrame = UMovieGraphBlueprintLibrary::GetRootFrameNumber(Pipeline);
	const UMoviePipelineExecutorShot* Shot = UMovieGraphBlueprintLibrary::GetCurrentExecutorShot(Pipeline);
	const bool bFirstFrameKept = Shot != nullptr && Shot->ShotInfo.TotalOutputRangeRoot.HasLowerBound()
		&& RootFrame == FFrameRate::TransformTime(FFrameTime(Shot->ShotInfo.TotalOutputRangeRoot.GetLowerBoundValue()), Shot->ShotInfo.CachedTickResolution, Shot->ShotInfo.CachedFrameRate).FloorToFrame();

	Slate = FUEcineSlate();
	BurnIn = FUEcineBurnIn();
	PictureHeight = UMovieGraphBlueprintLibrary::GetDesiredOutputResolution(const_cast<UMovieGraphEvaluatedConfig*>(Config)).Y;
	if (Metadata.Contains(UEcine::Deliver::SlateMetadataName) && bFirstFrameKept)
	{
		Slate = UEcine::Deliver::MakeSlate(Metadata);
	}
	else if (Metadata.Contains(UEcine::Deliver::BurnInMetadataName))
	{
		FUEcineBurnInFrame Frame;
		Frame.Timecode = UMovieGraphBlueprintLibrary::GetRootTimecode(Pipeline).ToString();
		Frame.Frame = RootFrame.Value;
		Frame.FocalLengthMm = UMovieGraphBlueprintLibrary::GetCurrentFocalLength(Pipeline, CameraIndex);
		Frame.TStop = UMovieGraphBlueprintLibrary::GetCurrentAperture(Pipeline, CameraIndex);
		Frame.FocusDistanceCm = UMovieGraphBlueprintLibrary::GetCurrentFocusDistance(Pipeline, CameraIndex);
		BurnIn = UEcine::Deliver::MakeBurnIn(Metadata, Frame);
	}
	if (Drawn.IsValid())
	{
		Drawn->SetFrame(Slate, BurnIn, PictureHeight);
	}
}
