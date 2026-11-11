#pragma once

#include "CoreMinimal.h"
#include "Graph/MovieGraphBurnInWidget.h"
#include "UEcineDeliver.h"
#include "UEcineBurnInWidget.generated.h"

class SUEcineBurnIn;
class UMovieGraphEvaluatedConfig;
class UMovieGraphPipeline;

/**
 * What Movie Render Graph draws over a UEcine render: the slate on the first frame kept of a
 * shot and the camera and shot data over the others. A native widget, so the plugin ships no
 * Blueprint.
 *
 * Everything it shows comes from the render itself: the "uecine/..." metadata of the shot's
 * graph and what the renderer says of the frame.
 *
 * The slate is why a shot has handle frames, and the widget is the part of a render that is
 * told of each frame, so it also keeps the sequence playing through the last of them.
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

	/**
	 * How many frames the renderer has told any burn-in widget about since the editor started.
	 * It tells of the frames it plays before the first one it keeps, too.
	 */
	static int32 GetUpdateCount();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void UpdateForFrame(const UMovieGraphPipeline* Pipeline, const UMovieGraphEvaluatedConfig* Config, int32 CameraIndex);

	TSharedPtr<SUEcineBurnIn> Drawn;

	// What is drawn over the frame being rendered. Kept here because the renderer tells the
	// widget about its first frame before the drawn part of it exists.
	/** Over the whole picture. Empty when the frame is not a slate. */
	FUEcineSlate Slate;
	/** In the corners of the picture. Empty on a slate, and when the render has no burn-ins. */
	FUEcineBurnIn BurnIn;
	/** The picture's height in pixels; the text is sized from it. */
	int32 PictureHeight = 1080;
};
