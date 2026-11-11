#pragma once

#include "CoreMinimal.h"
#include "Graph/MovieGraphBurnInWidget.h"
#include "UEcineBurnInWidget.generated.h"

class SUEcineBurnIn;
class UMovieGraphEvaluatedConfig;
class UMovieGraphPipeline;
class UMoviePipelineExecutorShot;

/**
 * What Movie Render Graph draws over a UEcine render: the slate on a shot's first frame and
 * the camera and shot data over the others. A native widget, so the plugin ships no Blueprint.
 *
 * Everything it shows comes from the render itself: the "uecine/..." metadata of the shot's
 * graph and what the renderer says of the frame.
 */
UCLASS()
class UECINERENDER_API UUEcineBurnInWidget : public UMovieGraphBurnInWidget
{
	GENERATED_BODY()

public:
	/**
	 * The renderer tells a burn-in widget about each frame through a Blueprint event, which a
	 * native class cannot implement; the event is taken here as it is dispatched.
	 */
	virtual void ProcessEvent(UFunction* Function, void* Parms) override;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	/** How many frames the renderer has told any burn-in widget about since the editor started. */
	static int32 GetUpdateCount();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void UpdateForFrame(const UMovieGraphPipeline* Pipeline, const UMovieGraphEvaluatedConfig* Config, int32 CameraIndex);

	TSharedPtr<SUEcineBurnIn> BurnIn;

	// What is drawn over the frame being rendered. Kept here because the renderer tells the
	// widget about its first frame before the drawn part of it exists.
	/** Along the bottom of the picture. Empty for no strip. */
	FText Strip;
	/** Over the whole picture. Empty for no slate. */
	FText Slate;
	/** The picture's height in pixels; the text is sized from it. */
	int32 PictureHeight = 1080;

	/** The shot being rendered and the frame its render began on, to know the slate's frame. */
	TWeakObjectPtr<const UMoviePipelineExecutorShot> CurrentShot;
	FFrameNumber FirstFrameOfShot;
};
