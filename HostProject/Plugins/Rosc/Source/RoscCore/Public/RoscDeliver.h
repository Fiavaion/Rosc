// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscPaperwork.h"
#include "RoscUnits.h"
#include "RoscDeliver.generated.h"

/** The kind of file a render writes. */
enum class ERoscRenderFormat : uint8
{
	/** Apple ProRes 422 LT in a .mov file: light enough to pass around for review. */
	ProRes422LT,
	/** Apple ProRes 4444 in a .mov file: a finished picture for the grade. */
	ProRes4444,
	/** One OpenEXR file per frame, holding every pass as a layer. */
	MultilayerExr,
	/** H.264 in an .mp4 file: small, and plays anywhere. */
	H264Mp4
};

/** A name and a value written into the files that carry metadata, and read by the slate and the burn-ins. */
struct FRoscMetadataEntry
{
	FString Name;
	FString Value;
};

/**
 * Everything the render of one shot is made of, as plain data. The Movie Render Graph
 * configuration for the shot is generated from this and from nothing else.
 */
struct FRoscRenderSpec
{
	/** The folder the files are written to. */
	FString OutputDirectory;
	/**
	 * The file's name without its extension; it may begin with folders. It may hold the
	 * renderer's own token for the frame, {frame_number}.
	 */
	FString FileName;
	/** The picture's size in pixels. */
	FIntPoint Resolution = FIntPoint(1920, 1080);
	ERoscRenderFormat Format = ERoscRenderFormat::ProRes422LT;

	/** The picture is written scene-linear, without the display's tone curve: what compositing works in. */
	bool bSceneLinear = false;
	/** Extra passes beside the picture. They need a format that holds layers. */
	bool bDepth = false;
	bool bNormals = false;
	bool bMotionVectors = false;
	/** A matte for every object. */
	bool bMattes = false;

	/**
	 * Cameras with a camera package record only their delivery frame, not the whole recording
	 * format. The picture's size is expected to have the delivery frame's shape.
	 */
	bool bDeliveryFrameOnly = false;

	/** Camera and shot data drawn over the picture. */
	bool bBurnIn = false;
	/**
	 * A slate as the first frame. The renderer can only lengthen a shot at both ends, so a
	 * slated shot is one frame longer at its head (the slate) and one at its tail.
	 */
	bool bSlate = false;

	/** How many moments of the shutter's open time make one frame; more gives smoother motion blur. */
	int32 TemporalSamples = 1;
	/** Frames played before the first one that is kept, so that lighting and exposure have settled. */
	int32 WarmUpFrames = 0;
	/** The megabits a second an H.264 movie averages. Other formats do not read it. */
	double VideoBitrateMbps = 8.0;

	TArray<FRoscMetadataEntry> Metadata;
};

/** What a delivery is for. The one choice sets everything about the render. */
UENUM()
enum class ERoscDeliverIntent : uint8
{
	/** A light movie of each shot with a slate and burn-ins, to pass around and comment on. */
	Review,
	/** Every frame as an OpenEXR file with the passes, camera and lens data that visual effects work from. */
	VfxHandoff UMETA(DisplayName = "VFX Passes"),
	/** The finished picture of each shot at full quality, clean. */
	Master,
	/** A small, quick movie of each shot to look at while working. */
	Draft,
	/** The finished picture of each shot as a file ready to upload to YouTube. */
	YouTube,
	/** The finished picture of each shot in a cinema's frame, for a DCP to be made from. */
	DcpSource UMETA(DisplayName = "DCP Source")
};

/** What Deliver knows of one shot before it is rendered. */
struct FRoscDeliverShot
{
	/** What the paperwork says of the shot; its name is Report.Shot. */
	FRoscShotReport Report;
	/** The photosites of the camera's recording format. Zero for film, or a camera with no camera package. */
	FIntPoint FormatResolution = FIntPoint::ZeroValue;
	/** Width over height of the picture the camera records, after any anamorphic desqueeze. Zero with no camera. */
	double RecordedAspect = 0.0;
	/** Width over height of the frame the shot is delivered in. Zero delivers everything recorded. */
	double DeliveryAspect = 0.0;
};

/** What the user has chosen for a delivery, beside its intent. */
struct FRoscDeliverSettings
{
	/** The folder deliveries go under. Each intent has a folder of its own in it. */
	FString OutputDirectory;
	/** The name of the sequence being delivered. */
	FString Sequence;
	/** How files are named. See GetNameTokens. */
	FString NamePattern;
	/** The day of the delivery, as 2026-11-11. */
	FString Date;
	/** The name of the colour space scene-linear files are in, such as "ACEScg". */
	FString SceneLinearColourSpace;
	ERoscDistanceUnits DistanceUnits = ERoscDistanceUnits::Metric;
	/** Review movies open with a slate. */
	bool bSlate = true;
	/** Review movies carry camera and shot data over the picture. */
	bool bBurnIn = true;
	/** YouTube files and DCP sources are made at 4K, not at 1080p and 2K. */
	bool bFourK = false;
};

/** A word in braces that stands for something of the shot in a file name. */
struct FRoscNameToken
{
	/** As written in a pattern, with its braces: "{shot}". */
	FString Token;
	FText Meaning;
};

/** One line of a slate. */
struct FRoscSlateLine
{
	FText Label;
	FString Value;
};

/** What a slate says. */
struct FRoscSlate
{
	/** The production, in large letters: the name of the sequence. */
	FString Title;
	TArray<FRoscSlateLine> Lines;
};

/** What changes from frame to frame in the burn-ins. */
struct FRoscBurnInFrame
{
	FString Timecode;
	/** The frame of the sequence. */
	int32 Frame = 0;
	/** Zero or less when the frame was not seen through a camera. */
	double FocalLengthMm = 0.0;
	double TStop = 0.0;
	double FocusDistanceCm = 0.0;
};

/** The text drawn over a frame, one piece in each corner of the picture. A piece may be empty. */
struct FRoscBurnIn
{
	FString TopLeft;
	FString TopRight;
	FString BottomLeft;
	FString BottomRight;
};

/** Where a camera was and how its lens was set at one frame of a shot, for the file that goes with a VFX handoff. */
struct FRoscCameraSample
{
	/** The frame of the sequence. */
	int32 Frame = 0;
	/** In the level's space, in centimetres. */
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	double FocalLengthMm = 0.0;
	double TStop = 0.0;
	double FocusDistanceCm = 0.0;
};

/** The mistakes preflight looks for. */
enum class ERoscPreflightCheck : uint8
{
	/** No sequence is open in Sequencer. */
	NoSequence,
	/** Two shots have the same name, so their files would have too. */
	DuplicateShotName,
	/** A shot has no camera to be seen through. */
	ShotWithoutCamera,
	/** A shot has a camera but no camera cut, so the renderer would not look through it. */
	ShotWithoutCameraCut,
	/** A shot cuts between cameras; a delivery is one camera to a shot. */
	SeveralCameraCuts,
	/** A shot's camera has no camera package, so its files cannot say what it was shot on. */
	CameraWithoutPackage,
	/** A camera's frame rate is not the sequence's, so its shutter angle gives the wrong motion blur. */
	FrameRateMismatch,
	/** A camera's exposure is left to adapt by itself, so brightness drifts during the shot. */
	AutoExposure,
	/** A VFX handoff is asked for and the project does not work in ACEScg. */
	WorkingColourSpace,
	/** A review movie's slate falls before the first frame of the sequence, where there is no timecode for it. */
	SlateBeforeSequenceStart,
	/** A DCP source is asked for and the sequence does not play at 24 frames a second. */
	DcpFrameRate,
	/** The engine's Virtual Camera plugin is on, with which a render can lose the shot's camera. */
	VirtualCameraOn
};

/** What preflight is told of one shot. */
struct FRoscPreflightShot
{
	FString Name;
	/** The frame of the sequence the shot starts on. */
	int32 StartFrame = 0;
	bool bHasCamera = false;
	/** How many camera cuts the shot is made of. */
	int32 CameraCutCount = 0;
	bool bHasPackage = false;
	bool bAutoExposure = false;
	/** The frame rate of the camera package; only read when the shot has one. */
	double PackageFramesPerSecond = 0.0;
};

/** Everything preflight is told. */
struct FRoscPreflightFacts
{
	ERoscDeliverIntent Intent = ERoscDeliverIntent::Review;
	bool bHasSequence = false;
	double SequenceFramesPerSecond = 0.0;
	/** Review movies open with a slate. */
	bool bSlate = false;
	/** The colour space the editor is working in now. */
	bool bWorkingSpaceIsAcesCg = false;
	/** ACEScg has been chosen for the project and waits for the editor to be restarted. */
	bool bAcesCgWaitsForRestart = false;
	/** The engine's Virtual Camera plugin is on (Show on Device). */
	bool bVirtualCameraOn = false;
	TArray<FRoscPreflightShot> Shots;
};

/** One thing preflight found. */
struct FRoscPreflightFinding
{
	ERoscPreflightCheck Check = ERoscPreflightCheck::NoSequence;
	/** The render would be wrong or would not run; otherwise it is worth knowing but can be delivered. */
	bool bStopsRender = false;
	/** The shot it is about, or empty when it is about the whole delivery. */
	FString Shot;
	/** What is wrong, in a few words. */
	FText Title;
	/** Why it matters and what to do about it. */
	FText Detail;
	/** What the button that puts it right says. Empty when it has to be put right by hand. */
	FText FixLabel;
};

namespace Rosc::Deliver
{
	/** How many frames are added to each end of a shot for a spec: one when it is slated. */
	ROSCCORE_API int32 GetHandleFrames(const FRoscRenderSpec& Spec);

	/** The extension of the files a format is written in, without the dot. */
	ROSCCORE_API const TCHAR* GetFileExtension(ERoscRenderFormat Format);

	/** Every intent, in the order Deliver offers them. */
	ROSCCORE_API TConstArrayView<ERoscDeliverIntent> GetIntents();

	/** The name of a kind of delivery: "Review", "VFX Passes", "DCP Source". */
	ROSCCORE_API FText GetIntentName(ERoscDeliverIntent Intent);

	/** One sentence on what an intent makes. */
	ROSCCORE_API FText GetIntentDescription(ERoscDeliverIntent Intent);

	/** What stands for an intent in file names: "draft", "review", "vfx", "master", "youtube", "dcp". */
	ROSCCORE_API const TCHAR* GetIntentToken(ERoscDeliverIntent Intent);

	/**
	 * The size in pixels a shot is rendered at. Review fits the delivery frame into 1920 by
	 * 1080 and Draft into half that. A VFX handoff is everything the camera records, at the
	 * recording format's width. Master is the delivery frame cut from that. YouTube fits the
	 * delivery frame into 1920 by 1080, or 3840 by 2160 at 4K; a DCP source fits it into the
	 * cinema's 2048 by 1080, or 4096 by 2160 at 4K. Sides are even numbers.
	 */
	ROSCCORE_API FIntPoint GetOutputResolution(ERoscDeliverIntent Intent, const FRoscDeliverShot& Shot, bool bFourK = false);

	/** The pattern files are named by until the user writes another. */
	inline const TCHAR* const DefaultNamePattern = TEXT("{sequence}_{shot}_{intent}_{version}");

	/** The tokens a name pattern may hold. */
	ROSCCORE_API TConstArrayView<FRoscNameToken> GetNameTokens();

	/** Empty when a pattern can name files; otherwise what is wrong with it. */
	ROSCCORE_API FText ValidateNamePattern(const FString& Pattern);

	/**
	 * A shot's file name from a pattern, without folder or extension. Tokens are replaced by
	 * the shot's own words, made safe for file names; a token with nothing to say leaves no
	 * gap. {version} is left in: one version is counted for the whole delivery, from the files
	 * already in its folder (see ReadVersion), and written into every shot's name (see WithVersion).
	 */
	ROSCCORE_API FString ResolveFileName(const FString& Pattern, ERoscDeliverIntent Intent, const FRoscDeliverSettings& Settings, const FRoscDeliverShot& Shot);

	/** A file name with its {version} filled in, as "v003". A name with no {version} is returned as it is. */
	ROSCCORE_API FString WithVersion(const FString& Name, int32 Version);

	/**
	 * Which version a file or folder already on disk is of a name that still holds {version}.
	 * Zero when it is not that name at any version.
	 */
	ROSCCORE_API int32 ReadVersion(const FString& Name, const FString& ExistingName);

	/** What is written into a shot's files about it, and shown on its slate and burn-ins. */
	ROSCCORE_API TArray<FRoscMetadataEntry> MakeMetadata(ERoscDeliverIntent Intent, const FRoscDeliverSettings& Settings, const FRoscDeliverShot& Shot);

	/** Everything the render of a shot is made of for an intent. */
	ROSCCORE_API FRoscRenderSpec MakeRenderSpec(ERoscDeliverIntent Intent, const FRoscDeliverSettings& Settings, const FRoscDeliverShot& Shot);

	/**
	 * A spec's file as it will be named on disk, from its output folder on, with the first
	 * frame filled in, and the first version when the name still holds {version}:
	 * "Review/Cut_shot0010_review_v001.mov".
	 */
	ROSCCORE_API FString PreviewFile(ERoscDeliverIntent Intent, const FRoscRenderSpec& Spec);

	/**
	 * The path of a file that goes with a spec's own and is named as they are, without a frame
	 * number: the sidecar ("json") and the camera ("fbx") of a VFX handoff.
	 */
	ROSCCORE_API FString GetCompanionFile(const FRoscRenderSpec& Spec, const TCHAR* Extension);

	/** The folder under a delivery's output folder that an intent's files go in. */
	ROSCCORE_API const TCHAR* GetIntentFolder(ERoscDeliverIntent Intent);

	/**
	 * The console variable a render sets to 1 for as long as its cameras are to record only
	 * their delivery frame. Camera package components in the running level answer to it.
	 */
	inline const TCHAR* const DeliveryFrameOnlyVariable = TEXT("Rosc.Render.DeliveryFrameOnly");

	/** Metadata that says the file's first frame is a slate. The widget that draws over the picture reads it. */
	inline const TCHAR* const SlateMetadataName = TEXT("rosc/slate");
	/** Metadata that says camera and shot data are drawn over every frame of the picture. */
	inline const TCHAR* const BurnInMetadataName = TEXT("rosc/burnIn");

	/** A slate from a file's metadata, by name. */
	ROSCCORE_API FRoscSlate MakeSlate(const TMap<FString, FString>& Metadata);

	/** The burn-ins of one frame from a file's metadata, by name, and what the renderer says of the frame. */
	ROSCCORE_API FRoscBurnIn MakeBurnIn(const TMap<FString, FString>& Metadata, const FRoscBurnInFrame& Frame);

	/**
	 * The file that goes beside a shot's VFX handoff: the shot's metadata and its camera at
	 * every frame, as JSON that needs neither the engine nor the plugin to read.
	 */
	ROSCCORE_API FString WriteSidecarJson(const FRoscRenderSpec& Spec, double FramesPerSecond, TConstArrayView<FRoscCameraSample> Camera);

	/** What is wrong with a delivery before it is rendered, the findings that stop it first. Empty when nothing is. */
	ROSCCORE_API TArray<FRoscPreflightFinding> RunPreflight(const FRoscPreflightFacts& Facts);
}
