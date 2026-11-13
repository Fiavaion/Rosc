// Copyright 2026 Fiavaion. All rights reserved.

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UEcineDeliver.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FUEcineDeliverSettings MakeRecipeTestSettings()
	{
		FUEcineDeliverSettings Settings;
		Settings.OutputDirectory = TEXT("D:/Deliveries");
		Settings.Sequence = TEXT("Harbour <Night>");
		Settings.NamePattern = UEcine::Deliver::DefaultNamePattern;
		Settings.Date = TEXT("2026-11-11");
		Settings.SceneLinearColourSpace = TEXT("ACEScg");
		return Settings;
	}

	/** A shot on a 4.6K 3:2 open gate format, delivered in 2.39:1. */
	FUEcineDeliverShot MakeRecipeTestShot()
	{
		FUEcineDeliverShot Shot;
		Shot.Report.Scene = TEXT("12");
		Shot.Report.Shot = TEXT("shot0010");
		Shot.Report.Size = TEXT("Close-up");
		Shot.Report.Start = TEXT("00:00:04:00");
		Shot.Report.Duration = TEXT("4 s 12 fr");
		Shot.Report.Camera = TEXT("A Cam");
		Shot.Report.Body = TEXT("ARRI ALEXA 35");
		Shot.Report.Format = TEXT("4.6K 3:2 Open Gate");
		Shot.Report.LensKit = TEXT("ARRI Signature Prime");
		Shot.Report.FocalLength = TEXT("35 mm");
		Shot.Report.TStop = TEXT("T2.8");
		Shot.Report.Iso = TEXT("800");
		Shot.Report.Shutter = TEXT("180\u00B0");
		Shot.Report.FrameRate = TEXT("24 fps");
		Shot.Report.Support = TEXT("Telescoping crane");
		Shot.FormatResolution = FIntPoint(4608, 3164);
		Shot.RecordedAspect = 4608.0 / 3164.0;
		Shot.DeliveryAspect = 2.39;
		return Shot;
	}

	TMap<FString, FString> MakeRecipeTestMetadataMap(EUEcineDeliverIntent Intent, const FUEcineDeliverSettings& Settings, const FUEcineDeliverShot& Shot)
	{
		TMap<FString, FString> Map;
		for (const FUEcineMetadataEntry& Entry : UEcine::Deliver::MakeMetadata(Intent, Settings, Shot))
		{
			Map.Add(Entry.Name, Entry.Value);
		}
		return Map;
	}

	FUEcinePreflightFacts MakeCleanPreflightFacts(EUEcineDeliverIntent Intent)
	{
		FUEcinePreflightFacts Facts;
		Facts.Intent = Intent;
		Facts.bHasSequence = true;
		Facts.SequenceFramesPerSecond = 24.0;
		Facts.bWorkingSpaceIsAcesCg = true;
		FUEcinePreflightShot& Shot = Facts.Shots.AddDefaulted_GetRef();
		Shot.Name = TEXT("shot0010");
		Shot.bHasCamera = Shot.bHasPackage = true;
		Shot.CameraCutCount = 1;
		Shot.PackageFramesPerSecond = 24.0;
		return Facts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverResolutionTest, "UEcine.Core.Deliver.Resolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverResolutionTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Deliver;

	FUEcineDeliverShot Shot = MakeRecipeTestShot();
	TestEqual(TEXT("Review in 2.39:1 is 1920 wide"), GetOutputResolution(EUEcineDeliverIntent::Review, Shot), FIntPoint(1920, 804));
	TestEqual(TEXT("A VFX handoff is the whole recording format"), GetOutputResolution(EUEcineDeliverIntent::VfxHandoff, Shot), FIntPoint(4608, 3164));
	TestEqual(TEXT("Master is the delivery frame cut from the format's width"), GetOutputResolution(EUEcineDeliverIntent::Master, Shot), FIntPoint(4608, 1928));
	TestEqual(TEXT("A draft is half the review size"), GetOutputResolution(EUEcineDeliverIntent::Draft, Shot), FIntPoint(960, 402));
	TestEqual(TEXT("YouTube in 2.39:1 is 1920 wide"), GetOutputResolution(EUEcineDeliverIntent::YouTube, Shot), FIntPoint(1920, 804));
	TestEqual(TEXT("YouTube at 4K is 3840 wide"), GetOutputResolution(EUEcineDeliverIntent::YouTube, Shot, true), FIntPoint(3840, 1606));
	TestEqual(TEXT("A DCP source in 2.39:1 is the cinema's scope frame"), GetOutputResolution(EUEcineDeliverIntent::DcpSource, Shot), FIntPoint(2048, 858));
	TestEqual(TEXT("And at 4K"), GetOutputResolution(EUEcineDeliverIntent::DcpSource, Shot, true), FIntPoint(4096, 1716));
	Shot.DeliveryAspect = 1.85;
	TestEqual(TEXT("A DCP source in 1.85:1 is the cinema's flat frame"), GetOutputResolution(EUEcineDeliverIntent::DcpSource, Shot), FIntPoint(1998, 1080));
	Shot.DeliveryAspect = 16.0 / 9.0;
	TestEqual(TEXT("A 16:9 DCP source is fitted into the cinema's height"), GetOutputResolution(EUEcineDeliverIntent::DcpSource, Shot), FIntPoint(1920, 1080));
	TestEqual(TEXT("Size at 4K is for YouTube and DCP sources only"), GetOutputResolution(EUEcineDeliverIntent::Review, Shot, true), FIntPoint(1920, 1080));

	Shot.DeliveryAspect = 4.0 / 3.0;
	TestEqual(TEXT("Review in 4:3 is 1080 high"), GetOutputResolution(EUEcineDeliverIntent::Review, Shot), FIntPoint(1440, 1080));
	TestEqual(TEXT("A delivery frame narrower than the format is cut from its height"), GetOutputResolution(EUEcineDeliverIntent::Master, Shot), FIntPoint(4218, 3164));

	Shot.DeliveryAspect = 0.0;
	TestEqual(TEXT("With no delivery frame the master is the whole format"), GetOutputResolution(EUEcineDeliverIntent::Master, Shot), FIntPoint(4608, 3164));
	TestEqual(TEXT("With no delivery frame review has the format's shape"), GetOutputResolution(EUEcineDeliverIntent::Review, Shot), FIntPoint(1572, 1080));

	// A 2x anamorphic lens on a 2880 by 2160 format records a picture 2.67 wide.
	Shot.FormatResolution = FIntPoint(2880, 2160);
	Shot.RecordedAspect = 2.0 * 2880.0 / 2160.0;
	TestEqual(TEXT("An anamorphic picture is unsqueezed to its full width"), GetOutputResolution(EUEcineDeliverIntent::VfxHandoff, Shot), FIntPoint(5760, 2160));

	const FUEcineDeliverShot NoCamera;
	TestEqual(TEXT("With nothing known of the camera a handoff is 4K, 16:9"), GetOutputResolution(EUEcineDeliverIntent::VfxHandoff, NoCamera), FIntPoint(4096, 2304));
	TestEqual(TEXT("With nothing known of the camera review is HD"), GetOutputResolution(EUEcineDeliverIntent::Review, NoCamera), FIntPoint(1920, 1080));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverNamesTest, "UEcine.Core.Deliver.Names", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverNamesTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Deliver;

	const FUEcineDeliverSettings Settings = MakeRecipeTestSettings();
	FUEcineDeliverShot Shot = MakeRecipeTestShot();

	TestEqual(TEXT("The default pattern"), ResolveFileName(DefaultNamePattern, EUEcineDeliverIntent::Review, Settings, Shot), FString(TEXT("Harbour_Night_shot0010_review_{version}")));
	TestEqual(TEXT("Every token of the shot"), ResolveFileName(TEXT("{date}-{scene}-{shot}-{size}-{camera}"), EUEcineDeliverIntent::Master, Settings, Shot), FString(TEXT("2026-11-11-12-shot0010-Close-up-A_Cam")));
	Shot.Report.Scene.Reset();
	TestEqual(TEXT("A token with nothing to say leaves no gap"), ResolveFileName(TEXT("{scene}_{shot}_{scene}_{intent}_{scene}"), EUEcineDeliverIntent::VfxHandoff, Settings, Shot), FString(TEXT("shot0010_vfx")));

	const FString Named = TEXT("Harbour_shot0010_review_{version}");
	TestEqual(TEXT("A version is written with three digits"), WithVersion(Named, 7), FString(TEXT("Harbour_shot0010_review_v007")));
	TestEqual(TEXT("A name with no version in it stays as it is"), WithVersion(TEXT("Harbour_shot0010"), 7), FString(TEXT("Harbour_shot0010")));
	TestEqual(TEXT("The version of a file on disk is read"), ReadVersion(Named, TEXT("Harbour_shot0010_review_v012")), 12);
	TestEqual(TEXT("A version in the middle of a name is read"), ReadVersion(TEXT("Harbour_{version}_shot0010"), TEXT("Harbour_v003_shot0010")), 3);
	TestEqual(TEXT("Another shot's file is not a version of this one"), ReadVersion(Named, TEXT("Harbour_shot0020_review_v012")), 0);
	TestEqual(TEXT("A longer name is not a version of this one"), ReadVersion(Named, TEXT("Harbour_shot0010_review_v012_old")), 0);
	TestEqual(TEXT("A name with no number is not a version"), ReadVersion(Named, TEXT("Harbour_shot0010_review_v")), 0);
	TestEqual(TEXT("A name that holds no version has no versions on disk"), ReadVersion(TEXT("Harbour_shot0010"), TEXT("Harbour_shot0010")), 0);

	TestTrue(TEXT("The default pattern is valid"), ValidateNamePattern(DefaultNamePattern).IsEmpty());
	TestFalse(TEXT("An empty pattern is refused"), ValidateNamePattern(TEXT("  ")).IsEmpty());
	TestFalse(TEXT("A pattern without the shot is refused"), ValidateNamePattern(TEXT("{sequence}_{intent}")).IsEmpty());
	TestFalse(TEXT("A pattern with a folder in it is refused"), ValidateNamePattern(TEXT("{sequence}/{shot}")).IsEmpty());
	for (const FUEcineNameToken& Token : GetNameTokens())
	{
		TestFalse(*FString::Printf(TEXT("%s is explained"), *Token.Token), Token.Meaning.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverSpecTest, "UEcine.Core.Deliver.Spec", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverSpecTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Deliver;

	FUEcineDeliverSettings Settings = MakeRecipeTestSettings();
	const FUEcineDeliverShot Shot = MakeRecipeTestShot();

	const FUEcineRenderSpec Review = MakeRenderSpec(EUEcineDeliverIntent::Review, Settings, Shot);
	TestEqual(TEXT("Review is a light ProRes"), Review.Format, EUEcineRenderFormat::ProRes422LT);
	TestTrue(TEXT("Review has a slate and burn-ins"), Review.bSlate && Review.bBurnIn);
	TestTrue(TEXT("Review is the delivery frame"), Review.bDeliveryFrameOnly && Review.Resolution == FIntPoint(1920, 804));
	TestFalse(TEXT("Review is made for a display, with no passes"), Review.bSceneLinear || Review.bDepth || Review.bNormals || Review.bMotionVectors || Review.bMattes);
	TestEqual(TEXT("Review goes in its own folder"), Review.OutputDirectory, FString(TEXT("D:/Deliveries/Review")));
	TestEqual(TEXT("Review's file as it will be on disk"), PreviewFile(EUEcineDeliverIntent::Review, Review), FString(TEXT("Review/Harbour_Night_shot0010_review_v001.mov")));
	TestEqual(TEXT("A slated shot has a frame added at each end"), GetHandleFrames(Review), 1);

	Settings.bSlate = Settings.bBurnIn = false;
	const FUEcineRenderSpec PlainReview = MakeRenderSpec(EUEcineDeliverIntent::Review, Settings, Shot);
	TestFalse(TEXT("The slate and burn-ins can be left off a review"), PlainReview.bSlate || PlainReview.bBurnIn);
	TestEqual(TEXT("A shot with no slate is not lengthened"), GetHandleFrames(PlainReview), 0);
	Settings.bSlate = Settings.bBurnIn = true;

	const FUEcineRenderSpec Vfx = MakeRenderSpec(EUEcineDeliverIntent::VfxHandoff, Settings, Shot);
	TestEqual(TEXT("A VFX handoff is OpenEXR with layers"), Vfx.Format, EUEcineRenderFormat::MultilayerExr);
	TestTrue(TEXT("A VFX handoff is scene-linear with every pass"), Vfx.bSceneLinear && Vfx.bDepth && Vfx.bNormals && Vfx.bMotionVectors && Vfx.bMattes);
	TestFalse(TEXT("A VFX handoff is clean, and is everything the camera records"), Vfx.bSlate || Vfx.bBurnIn || Vfx.bDeliveryFrameOnly);
	TestEqual(TEXT("A VFX handoff's frames have a folder of their own"), PreviewFile(EUEcineDeliverIntent::VfxHandoff, Vfx),
		FString(TEXT("VFX/Harbour_Night_shot0010_vfx_v001/Harbour_Night_shot0010_vfx_v001.0000.exr")));

	const FUEcineRenderSpec Master = MakeRenderSpec(EUEcineDeliverIntent::Master, Settings, Shot);
	TestEqual(TEXT("Master is ProRes 4444"), Master.Format, EUEcineRenderFormat::ProRes4444);
	TestTrue(TEXT("Master is the delivery frame at full size, with smooth motion blur"), Master.bDeliveryFrameOnly && Master.Resolution == FIntPoint(4608, 1928) && Master.TemporalSamples > 1);
	TestFalse(TEXT("Master is clean"), Master.bSlate || Master.bBurnIn);

	const FUEcineRenderSpec Draft = MakeRenderSpec(EUEcineDeliverIntent::Draft, Settings, Shot);
	TestEqual(TEXT("A draft is an MP4 movie"), Draft.Format, EUEcineRenderFormat::H264Mp4);
	TestTrue(TEXT("A draft is small and quick, with burn-ins and no slate"), Draft.Resolution == FIntPoint(960, 402) && Draft.TemporalSamples == 1 && Draft.bBurnIn && !Draft.bSlate && Draft.bDeliveryFrameOnly);
	TestEqual(TEXT("A draft's file as it will be on disk"), PreviewFile(EUEcineDeliverIntent::Draft, Draft), FString(TEXT("Draft/Harbour_Night_shot0010_draft_v001.mp4")));

	const FUEcineRenderSpec YouTube = MakeRenderSpec(EUEcineDeliverIntent::YouTube, Settings, Shot);
	TestEqual(TEXT("A YouTube file is an MP4 movie"), YouTube.Format, EUEcineRenderFormat::H264Mp4);
	TestTrue(TEXT("A YouTube file is the finished picture, clean"), YouTube.bDeliveryFrameOnly && YouTube.TemporalSamples > 1 && !YouTube.bSlate && !YouTube.bBurnIn && !YouTube.bSceneLinear);
	TestTrue(TEXT("At 1080p it has the bit rate YouTube asks for"), YouTube.Resolution == FIntPoint(1920, 804) && YouTube.VideoBitrateMbps >= 8.0 && YouTube.VideoBitrateMbps <= 12.0);
	TestEqual(TEXT("A YouTube file as it will be on disk"), PreviewFile(EUEcineDeliverIntent::YouTube, YouTube), FString(TEXT("YouTube/Harbour_Night_shot0010_youtube_v001.mp4")));
	Settings.bFourK = true;
	const FUEcineRenderSpec YouTube4K = MakeRenderSpec(EUEcineDeliverIntent::YouTube, Settings, Shot);
	TestTrue(TEXT("At 4K it is larger, at a higher bit rate"), YouTube4K.Resolution == FIntPoint(3840, 1606) && YouTube4K.VideoBitrateMbps >= 35.0 && YouTube4K.VideoBitrateMbps <= 68.0);
	TestEqual(TEXT("A DCP source at 4K is the cinema's 4K scope frame"), MakeRenderSpec(EUEcineDeliverIntent::DcpSource, Settings, Shot).Resolution, FIntPoint(4096, 1716));
	TestEqual(TEXT("Master is not changed by the 4K choice"), MakeRenderSpec(EUEcineDeliverIntent::Master, Settings, Shot).Resolution, Master.Resolution);
	Settings.bFourK = false;

	const FUEcineRenderSpec Dcp = MakeRenderSpec(EUEcineDeliverIntent::DcpSource, Settings, Shot);
	TestEqual(TEXT("A DCP source is ProRes 4444"), Dcp.Format, EUEcineRenderFormat::ProRes4444);
	TestTrue(TEXT("A DCP source is the finished picture in the cinema's frame, clean"), Dcp.Resolution == FIntPoint(2048, 858) && Dcp.bDeliveryFrameOnly && Dcp.TemporalSamples > 1 && !Dcp.bSlate && !Dcp.bBurnIn);
	TestEqual(TEXT("A DCP source's file as it will be on disk"), PreviewFile(EUEcineDeliverIntent::DcpSource, Dcp), FString(TEXT("DCP/Harbour_Night_shot0010_dcp_v001.mov")));

	TestEqual(TEXT("Deliver offers six kinds of delivery, the quickest first"), GetIntents().Num(), 6);
	TestTrue(TEXT("Draft comes first"), GetIntents()[0] == EUEcineDeliverIntent::Draft);
	TestEqual(TEXT("Passes for visual effects are named for what they are"), GetIntentName(EUEcineDeliverIntent::VfxHandoff).ToString(), FString(TEXT("VFX Passes")));

	Settings.NamePattern.Reset();
	TestEqual(TEXT("With no pattern written the default names the file"), MakeRenderSpec(EUEcineDeliverIntent::Master, Settings, Shot).FileName, Master.FileName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverTextTest, "UEcine.Core.Deliver.SlateAndBurnIn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverTextTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Deliver;

	FUEcineDeliverSettings Settings = MakeRecipeTestSettings();
	const FUEcineDeliverShot Shot = MakeRecipeTestShot();
	const TMap<FString, FString> Review = MakeRecipeTestMetadataMap(EUEcineDeliverIntent::Review, Settings, Shot);

	TestEqual(TEXT("The lens kit is written"), Review.FindRef(TEXT("uecine/lens/kit")), FString(TEXT("ARRI Signature Prime")));
	TestEqual(TEXT("The delivery frame is written by name"), Review.FindRef(TEXT("uecine/deliveryFrame")), FString(TEXT("2.39:1 Scope")));
	TestEqual(TEXT("A review is Rec.709"), Review.FindRef(TEXT("uecine/colourSpace")), FString(TEXT("Rec.709")));
	TestEqual(TEXT("A VFX handoff is in the scene-linear colour space"),
		MakeRecipeTestMetadataMap(EUEcineDeliverIntent::VfxHandoff, Settings, Shot).FindRef(TEXT("uecine/colourSpace")), FString(TEXT("ACEScg")));
	TestFalse(TEXT("What the shot does not say is left out"), Review.Contains(TEXT("uecine/exposure/nd")) || Review.Contains(TEXT("uecine/move")));

	const FUEcineSlate Slate = MakeSlate(Review);
	TestEqual(TEXT("The slate is headed by the sequence"), Slate.Title, FString(TEXT("Harbour <Night>")));
	const auto FindLine = [&Slate](const TCHAR* Label)
	{
		const FUEcineSlateLine* Line = Slate.Lines.FindByPredicate([Label](const FUEcineSlateLine& Candidate) { return Candidate.Label.ToString() == Label; });
		return Line != nullptr ? Line->Value : FString();
	};
	TestEqual(TEXT("Slate: scene"), FindLine(TEXT("Scene")), FString(TEXT("12")));
	TestEqual(TEXT("Slate: shot"), FindLine(TEXT("Shot")), FString(TEXT("shot0010")));
	TestEqual(TEXT("Slate: camera"), FindLine(TEXT("Camera")), FString(TEXT("A Cam \u00B7 ARRI ALEXA 35 \u00B7 4.6K 3:2 Open Gate")));
	TestEqual(TEXT("Slate: lens"), FindLine(TEXT("Lens")), FString(TEXT("ARRI Signature Prime \u00B7 35 mm")));
	TestEqual(TEXT("Slate: exposure"), FindLine(TEXT("Exposure")), FString(TEXT("ISO 800 \u00B7 180\u00B0 \u00B7 T2.8")));
	TestEqual(TEXT("Slate: what the delivery is for"), FindLine(TEXT("Delivery")), FString(TEXT("Review")));
	TestTrue(TEXT("Slate: nothing is said of a move the shot does not have"), FindLine(TEXT("Move")).IsEmpty());

	FUEcineBurnInFrame Frame;
	Frame.Timecode = TEXT("00:00:04:07");
	Frame.Frame = 103;
	Frame.FocalLengthMm = 35.0;
	Frame.TStop = 2.8;
	Frame.FocusDistanceCm = 305.0;
	FUEcineBurnIn BurnIn = MakeBurnIn(Review, Frame);
	TestEqual(TEXT("Burn-in, top left: where the frame belongs"), BurnIn.TopLeft, FString(TEXT("Harbour <Night> \u00B7 Sc 12 \u00B7 shot0010 \u00B7 Close-up")));
	TestEqual(TEXT("Burn-in, top right: what it was shot on"), BurnIn.TopRight, FString(TEXT("ARRI ALEXA 35 \u00B7 ARRI Signature Prime")));
	TestEqual(TEXT("Burn-in, bottom left: when"), BurnIn.BottomLeft, FString(TEXT("00:00:04:07 \u00B7 frame 103")));
	TestEqual(TEXT("Burn-in, bottom right: the lens at this frame"), BurnIn.BottomRight, FString(TEXT("35 mm \u00B7 T2.8 \u00B7 3.05 m \u00B7 ISO 800 \u00B7 180\u00B0")));

	Settings.DistanceUnits = EUEcineDistanceUnits::Imperial;
	BurnIn = MakeBurnIn(MakeRecipeTestMetadataMap(EUEcineDeliverIntent::Review, Settings, Shot), Frame);
	TestEqual(TEXT("Burn-in distances follow the units of the delivery"), BurnIn.BottomRight, FString(TEXT("35 mm \u00B7 T2.8 \u00B7 10' 0\" \u00B7 ISO 800 \u00B7 180\u00B0")));

	Frame.FocalLengthMm = 0.0;
	TestTrue(TEXT("A frame not seen through a camera has no lens line"), MakeBurnIn(Review, Frame).BottomRight.IsEmpty());

	// A plain camera, with no camera package.
	FUEcineDeliverShot Plain;
	Plain.Report.Shot = TEXT("shot0020");
	Plain.Report.Camera = TEXT("CineCameraActor");
	BurnIn = MakeBurnIn(MakeRecipeTestMetadataMap(EUEcineDeliverIntent::Review, Settings, Plain), Frame);
	TestEqual(TEXT("With no camera package the camera is named"), BurnIn.TopRight, FString(TEXT("CineCameraActor")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverSidecarTest, "UEcine.Core.Deliver.Sidecar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverSidecarTest::RunTest(const FString& Parameters)
{
	const FUEcineRenderSpec Spec = UEcine::Deliver::MakeRenderSpec(EUEcineDeliverIntent::VfxHandoff, MakeRecipeTestSettings(), MakeRecipeTestShot());
	TArray<FUEcineCameraSample> Camera;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FUEcineCameraSample& Sample = Camera.AddDefaulted_GetRef();
		Sample.Frame = 96 + Index;
		Sample.Location = FVector(100.0 + Index, -250.123456, 160.0);
		Sample.Rotation = FRotator(-5.0, 90.0 + Index, 0.0);
		Sample.FocalLengthMm = 35.0;
		Sample.TStop = 2.8;
		Sample.FocusDistanceCm = 305.0;
	}

	TSharedPtr<FJsonObject> Root;
	const FString Json = UEcine::Deliver::WriteSidecarJson(Spec, 24.0, Camera);
	if (!TestTrue(TEXT("The file is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("It says what kind of file it is"), Root->GetStringField(TEXT("schema")), FString(TEXT("uecine.sidecar/1")));
	TestEqual(TEXT("It names the files it goes with"), Root->GetStringField(TEXT("files")), FString(TEXT("Harbour_Night_shot0010_vfx_{version}.####.exr")));
	TestTrue(TEXT("The sidecar goes beside the frames, named as they are without a frame number"),
		UEcine::Deliver::GetCompanionFile(Spec, TEXT("json")).EndsWith(TEXT("/VFX/Harbour_Night_shot0010_vfx_{version}/Harbour_Night_shot0010_vfx_{version}.json")));
	TestEqual(TEXT("It gives the picture's width"), Root->GetIntegerField(TEXT("width")), 4608);
	TestEqual(TEXT("It gives the frame rate"), Root->GetNumberField(TEXT("framesPerSecond")), 24.0);
	TestEqual(TEXT("It repeats the metadata"), Root->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("uecine/camera/body")), FString(TEXT("ARRI ALEXA 35")));
	TestFalse(TEXT("It says what its numbers are measured in"), Root->GetObjectField(TEXT("units"))->GetStringField(TEXT("axes")).IsEmpty());

	const TArray<TSharedPtr<FJsonValue>>& Frames = Root->GetArrayField(TEXT("camera"));
	if (TestEqual(TEXT("It has the camera at every frame"), Frames.Num(), 3))
	{
		const TSharedPtr<FJsonObject> Last = Frames[2]->AsObject();
		TestEqual(TEXT("A frame says which it is"), Last->GetIntegerField(TEXT("frame")), 98);
		TestEqual(TEXT("A frame's location, to a tenth of a millimetre"), Last->GetArrayField(TEXT("location"))[1]->AsNumber(), -250.1235);
		TestEqual(TEXT("A frame's rotation: yaw"), Last->GetArrayField(TEXT("rotation"))[1]->AsNumber(), 92.0);
		TestEqual(TEXT("A frame's focus distance"), Last->GetNumberField(TEXT("focusDistance")), 305.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverPreflightTest, "UEcine.Core.Deliver.Preflight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverPreflightTest::RunTest(const FString& Parameters)
{
	using UEcine::Deliver::RunPreflight;

	TestTrue(TEXT("A delivery with nothing wrong has no findings"), RunPreflight(MakeCleanPreflightFacts(EUEcineDeliverIntent::VfxHandoff)).IsEmpty());

	TArray<FUEcinePreflightFinding> Findings = RunPreflight(FUEcinePreflightFacts());
	TestTrue(TEXT("No sequence is one finding that stops the render"), Findings.Num() == 1 && Findings[0].Check == EUEcinePreflightCheck::NoSequence && Findings[0].bStopsRender);

	FUEcinePreflightFacts Facts = MakeCleanPreflightFacts(EUEcineDeliverIntent::Review);
	Facts.bWorkingSpaceIsAcesCg = false;
	TestTrue(TEXT("The working colour space does not matter to a review"), RunPreflight(Facts).IsEmpty());
	Facts.Intent = EUEcineDeliverIntent::VfxHandoff;
	Findings = RunPreflight(Facts);
	TestTrue(TEXT("A VFX handoff outside ACEScg is found, can be delivered, and can be put right"),
		Findings.Num() == 1 && Findings[0].Check == EUEcinePreflightCheck::WorkingColourSpace && !Findings[0].bStopsRender && !Findings[0].FixLabel.IsEmpty() && Findings[0].Shot.IsEmpty());

	Facts.bAcesCgWaitsForRestart = true;
	Findings = RunPreflight(Facts);
	TestTrue(TEXT("Once ACEScg is chosen the finding says to restart, and has nothing left to put right"),
		Findings.Num() == 1 && Findings[0].Check == EUEcinePreflightCheck::WorkingColourSpace && Findings[0].FixLabel.IsEmpty());
	Facts.bAcesCgWaitsForRestart = false;

	FUEcinePreflightFacts Cinema = MakeCleanPreflightFacts(EUEcineDeliverIntent::DcpSource);
	Cinema.bWorkingSpaceIsAcesCg = false;
	TestTrue(TEXT("A DCP source at 24 frames a second has no findings"), RunPreflight(Cinema).IsEmpty());
	Cinema.SequenceFramesPerSecond = 25.0;
	Cinema.Shots[0].PackageFramesPerSecond = 25.0;
	Findings = RunPreflight(Cinema);
	TestTrue(TEXT("A DCP source at another rate is found, and can be delivered"),
		Findings.Num() == 1 && Findings[0].Check == EUEcinePreflightCheck::DcpFrameRate && !Findings[0].bStopsRender && Findings[0].Detail.ToString().Contains(TEXT("25")));
	Cinema.Intent = EUEcineDeliverIntent::YouTube;
	TestTrue(TEXT("The rate does not matter to YouTube"), RunPreflight(Cinema).IsEmpty());

	// A second shot with everything wrong that can be wrong with a camera, and a third with none.
	FUEcinePreflightShot& Second = Facts.Shots.AddDefaulted_GetRef();
	Second.Name = TEXT("shot0020");
	Second.bHasCamera = Second.bHasPackage = true;
	Second.bAutoExposure = true;
	Second.PackageFramesPerSecond = 25.0;
	FUEcinePreflightShot& Third = Facts.Shots.AddDefaulted_GetRef();
	Third.Name = TEXT("shot0030");
	Findings = RunPreflight(Facts);

	const auto Find = [&Findings](EUEcinePreflightCheck Check)
	{
		return Findings.FindByPredicate([Check](const FUEcinePreflightFinding& Finding) { return Finding.Check == Check; });
	};
	TestEqual(TEXT("Five findings"), Findings.Num(), 5);
	TestTrue(TEXT("The findings that stop the render come first"), Findings.Num() == 5 && Findings[0].bStopsRender && Findings[1].bStopsRender && !Findings[2].bStopsRender);
	if (const FUEcinePreflightFinding* NoCut = Find(EUEcinePreflightCheck::ShotWithoutCameraCut); TestNotNull(TEXT("A shot without a camera cut is found"), NoCut))
	{
		TestTrue(TEXT("It names the shot, stops the render and can be put right"), NoCut->Shot == TEXT("shot0020") && NoCut->bStopsRender && !NoCut->FixLabel.IsEmpty());
	}
	if (const FUEcinePreflightFinding* NoCamera = Find(EUEcinePreflightCheck::ShotWithoutCamera); TestNotNull(TEXT("A shot without a camera is found"), NoCamera))
	{
		TestTrue(TEXT("It names the shot, stops the render and has to be put right by hand"), NoCamera->Shot == TEXT("shot0030") && NoCamera->bStopsRender && NoCamera->FixLabel.IsEmpty());
	}
	if (const FUEcinePreflightFinding* FrameRate = Find(EUEcinePreflightCheck::FrameRateMismatch); TestNotNull(TEXT("A camera at another frame rate is found"), FrameRate))
	{
		TestTrue(TEXT("It says both rates"), FrameRate->Detail.ToString().Contains(TEXT("25")) && FrameRate->Detail.ToString().Contains(TEXT("24")));
		TestEqual(TEXT("Its fix says what it will do"), FrameRate->FixLabel.ToString(), FString(TEXT("Set camera to 24 fps")));
	}
	TestNotNull(TEXT("Exposure left to adapt is found"), Find(EUEcinePreflightCheck::AutoExposure));
	TestNull(TEXT("A shot with no camera is not also told about its camera's package"), Findings.FindByPredicate([](const FUEcinePreflightFinding& Finding)
		{ return Finding.Shot == TEXT("shot0030") && Finding.Check != EUEcinePreflightCheck::ShotWithoutCamera; }));

	Facts = MakeCleanPreflightFacts(EUEcineDeliverIntent::Review);
	Facts.Shots[0].bHasPackage = false;
	Facts.Shots[0].PackageFramesPerSecond = 0.0;
	Findings = RunPreflight(Facts);
	TestTrue(TEXT("A camera with no camera package is one finding, and its frame rate is not asked about"),
		Findings.Num() == 1 && Findings[0].Check == EUEcinePreflightCheck::CameraWithoutPackage && !Findings[0].bStopsRender);

	// A slate is the frame before its shot, and a shot on the first frame of the sequence has none before it.
	Facts = MakeCleanPreflightFacts(EUEcineDeliverIntent::Review);
	Facts.bSlate = true;
	Facts.Shots[0].StartFrame = 0;
	Findings = RunPreflight(Facts);
	TestTrue(TEXT("A slated shot on the first frame of the sequence is found, and can still be delivered"),
		Findings.Num() == 1 && Findings[0].Check == EUEcinePreflightCheck::SlateBeforeSequenceStart && !Findings[0].bStopsRender);
	Facts.Shots[0].StartFrame = 1;
	TestTrue(TEXT("A slated shot with a frame before it is not found"), RunPreflight(Facts).IsEmpty());
	Facts.Shots[0].StartFrame = 0;
	Facts.bSlate = false;
	TestTrue(TEXT("With no slate there is nothing to fall before the sequence"), RunPreflight(Facts).IsEmpty());
	Facts.bSlate = true;
	Facts.Intent = EUEcineDeliverIntent::Master;
	TestTrue(TEXT("Only review movies have slates"), RunPreflight(Facts).IsEmpty());

	Facts = MakeCleanPreflightFacts(EUEcineDeliverIntent::Master);
	Facts.Shots[0].CameraCutCount = 2;
	const FUEcinePreflightShot SameAgain = Facts.Shots[0];
	Facts.Shots.Add(SameAgain);
	Facts.Shots.Add(SameAgain);
	Findings = RunPreflight(Facts);
	TestEqual(TEXT("Three shots of one name are found once, and each shot that cuts between cameras is found"), Findings.Num(), 4);
	TestTrue(TEXT("A name used twice stops the render"), Findings.Num() == 4 && Findings[0].Check == EUEcinePreflightCheck::DuplicateShotName && Findings[0].bStopsRender);
	TestTrue(TEXT("Cutting between cameras in a shot stops the render"), Findings.Num() == 4 && Findings[1].Check == EUEcinePreflightCheck::SeveralCameraCuts && Findings[1].bStopsRender);
	return true;
}

#endif
