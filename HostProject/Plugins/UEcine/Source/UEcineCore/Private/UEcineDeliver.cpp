#include "UEcineDeliver.h"

#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UEcineCameraPackage.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

namespace
{
	// The names a shot's metadata is written under. Files that have shipped are read by these
	// names, so one is never renamed.
	const TCHAR* const MetaSequence = TEXT("uecine/sequence");
	const TCHAR* const MetaScene = TEXT("uecine/scene");
	const TCHAR* const MetaShot = TEXT("uecine/shot");
	const TCHAR* const MetaSize = TEXT("uecine/size");
	const TCHAR* const MetaMove = TEXT("uecine/move");
	const TCHAR* const MetaStart = TEXT("uecine/start");
	const TCHAR* const MetaDuration = TEXT("uecine/duration");
	const TCHAR* const MetaCameraName = TEXT("uecine/camera/name");
	const TCHAR* const MetaCameraBody = TEXT("uecine/camera/body");
	const TCHAR* const MetaCameraFormat = TEXT("uecine/camera/format");
	const TCHAR* const MetaSupport = TEXT("uecine/camera/support");
	const TCHAR* const MetaLensKit = TEXT("uecine/lens/kit");
	const TCHAR* const MetaFocalLength = TEXT("uecine/lens/focalLength");
	const TCHAR* const MetaTStop = TEXT("uecine/exposure/tStop");
	const TCHAR* const MetaIso = TEXT("uecine/exposure/iso");
	const TCHAR* const MetaShutter = TEXT("uecine/exposure/shutterAngle");
	const TCHAR* const MetaNd = TEXT("uecine/exposure/nd");
	const TCHAR* const MetaFrameRate = TEXT("uecine/frameRate");
	const TCHAR* const MetaDeliveryFrame = TEXT("uecine/deliveryFrame");
	const TCHAR* const MetaColourSpace = TEXT("uecine/colourSpace");
	const TCHAR* const MetaDistanceUnits = TEXT("uecine/distanceUnits");
	const TCHAR* const MetaDate = TEXT("uecine/date");
	const TCHAR* const MetaIntent = TEXT("uecine/intent");

	const TCHAR* const ImperialUnitsValue = TEXT("imperial");
	const TCHAR* const MetricUnitsValue = TEXT("metric");

	/** The renderer's own tokens, as a file name holds them. */
	const TCHAR* const VersionToken = TEXT("{version}");
	const TCHAR* const FrameToken = TEXT("{frame_number}");

	/** The frame's width when nothing says what the camera records: a 4K film scan. */
	constexpr int32 UnknownFormatWidth = 4096;
	constexpr double UnknownRecordedAspect = 16.0 / 9.0;

	int32 MakeEven(double Pixels)
	{
		return FMath::Max(2, 2 * FMath::RoundToInt32(Pixels / 2.0));
	}

	/** Pieces of text in a row with a dot between them, the empty ones left out. */
	FString JoinWithDots(std::initializer_list<FString> Pieces)
	{
		FString Joined;
		for (const FString& Piece : Pieces)
		{
			if (!Piece.IsEmpty())
			{
				Joined += Joined.IsEmpty() ? Piece : TEXT(" · ") + Piece;
			}
		}
		return Joined;
	}

	/** A word of a shot as it can stand in a file name: letters, digits, hyphens and underscores. */
	FString MakeFileNameWord(const FString& Word)
	{
		FString Safe;
		for (const TCHAR Character : Word.TrimStartAndEnd())
		{
			Safe.AppendChar(FChar::IsAlnum(Character) || Character == TEXT('-') || Character == TEXT('_') ? Character : TEXT('_'));
		}
		return Safe;
	}

	bool IsNameSeparator(TCHAR Character)
	{
		return Character == TEXT('_') || Character == TEXT('-') || Character == TEXT('.') || Character == TEXT(' ');
	}

	void AddMetadata(TArray<FUEcineMetadataEntry>& Metadata, const TCHAR* Name, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Metadata.Add({ Name, Value });
		}
	}

	void AddSlateLine(FUEcineSlate& Slate, const FText& Label, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Slate.Lines.Add({ Label, Value });
		}
	}

	TSharedPtr<FJsonValue> MakeJsonNumber(double Value)
	{
		// Four decimals: a tenth of a millimetre in a position, far finer than a lens scale.
		return MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Value * 10000.0) / 10000.0);
	}

	FUEcinePreflightFinding MakeFinding(EUEcinePreflightCheck Check, bool bStopsRender, const FString& Shot, const FText& Title, const FText& Detail, const FText& FixLabel = FText::GetEmpty())
	{
		FUEcinePreflightFinding Finding;
		Finding.Check = Check;
		Finding.bStopsRender = bStopsRender;
		Finding.Shot = Shot;
		Finding.Title = Title;
		Finding.Detail = Detail;
		Finding.FixLabel = FixLabel;
		return Finding;
	}
}

int32 UEcine::Deliver::GetHandleFrames(const FUEcineRenderSpec& Spec)
{
	return Spec.bSlate ? 1 : 0;
}

const TCHAR* UEcine::Deliver::GetFileExtension(EUEcineRenderFormat Format)
{
	return Format == EUEcineRenderFormat::MultilayerExr ? TEXT("exr") : TEXT("mov");
}

TConstArrayView<EUEcineDeliverIntent> UEcine::Deliver::GetIntents()
{
	static const EUEcineDeliverIntent Intents[] = { EUEcineDeliverIntent::Review, EUEcineDeliverIntent::VfxHandoff, EUEcineDeliverIntent::Master };
	return Intents;
}

FText UEcine::Deliver::GetIntentName(EUEcineDeliverIntent Intent)
{
	switch (Intent)
	{
	case EUEcineDeliverIntent::Review: return LOCTEXT("IntentReview", "Review");
	case EUEcineDeliverIntent::VfxHandoff: return LOCTEXT("IntentVfx", "VFX Handoff");
	default: return LOCTEXT("IntentMaster", "Master");
	}
}

FText UEcine::Deliver::GetIntentDescription(EUEcineDeliverIntent Intent)
{
	switch (Intent)
	{
	case EUEcineDeliverIntent::Review:
		return LOCTEXT("IntentReviewDescription", "A light ProRes movie of each shot in its delivery frame, with a slate and burn-ins, to pass around and comment on.");
	case EUEcineDeliverIntent::VfxHandoff:
		return LOCTEXT("IntentVfxDescription", "Every frame of each shot as an OpenEXR file, scene-linear, with depth, normals, motion vectors and mattes, the camera as FBX and a file of camera and lens data.");
	default:
		return LOCTEXT("IntentMasterDescription", "The finished picture of each shot as a ProRes 4444 movie in its delivery frame, with smooth motion blur and nothing drawn over it.");
	}
}

const TCHAR* UEcine::Deliver::GetIntentToken(EUEcineDeliverIntent Intent)
{
	switch (Intent)
	{
	case EUEcineDeliverIntent::Review: return TEXT("review");
	case EUEcineDeliverIntent::VfxHandoff: return TEXT("vfx");
	default: return TEXT("master");
	}
}

const TCHAR* UEcine::Deliver::GetIntentFolder(EUEcineDeliverIntent Intent)
{
	switch (Intent)
	{
	case EUEcineDeliverIntent::Review: return TEXT("Review");
	case EUEcineDeliverIntent::VfxHandoff: return TEXT("VFX");
	default: return TEXT("Master");
	}
}

FIntPoint UEcine::Deliver::GetOutputResolution(EUEcineDeliverIntent Intent, const FUEcineDeliverShot& Shot)
{
	const double Recorded = Shot.RecordedAspect > 0.0 ? Shot.RecordedAspect : UnknownRecordedAspect;
	const double Delivery = Shot.DeliveryAspect > 0.0 ? Shot.DeliveryAspect : Recorded;

	if (Intent == EUEcineDeliverIntent::Review)
	{
		return Delivery >= 16.0 / 9.0 ? FIntPoint(1920, MakeEven(1920.0 / Delivery)) : FIntPoint(MakeEven(1080.0 * Delivery), 1080);
	}

	// Everything recorded: the smallest frame of the recorded shape that loses none of the
	// format's photosites. An anamorphic picture is therefore unsqueezed to its full width.
	const FIntPoint Format = Shot.FormatResolution.X > 0 && Shot.FormatResolution.Y > 0
		? Shot.FormatResolution
		: FIntPoint(UnknownFormatWidth, FMath::RoundToInt32(UnknownFormatWidth / Recorded));
	const double WholeWidth = FMath::Max<double>(Format.X, Format.Y * Recorded);
	const FIntPoint Whole(MakeEven(WholeWidth), MakeEven(WholeWidth / Recorded));
	if (Intent == EUEcineDeliverIntent::VfxHandoff || FMath::IsNearlyEqual(Delivery, Recorded, 0.005))
	{
		return Whole;
	}
	return Delivery > Recorded ? FIntPoint(Whole.X, MakeEven(Whole.X / Delivery)) : FIntPoint(MakeEven(Whole.Y * Delivery), Whole.Y);
}

TConstArrayView<FUEcineNameToken> UEcine::Deliver::GetNameTokens()
{
	static const TArray<FUEcineNameToken> Tokens =
	{
		{ TEXT("{sequence}"), LOCTEXT("TokenSequence", "The sequence's name") },
		{ TEXT("{scene}"), LOCTEXT("TokenScene", "The shot's scene") },
		{ TEXT("{shot}"), LOCTEXT("TokenShot", "The shot's name") },
		{ TEXT("{size}"), LOCTEXT("TokenSize", "The shot's size") },
		{ TEXT("{camera}"), LOCTEXT("TokenCamera", "The camera's name") },
		{ TEXT("{intent}"), LOCTEXT("TokenIntent", "What the delivery is for: review, vfx or master") },
		{ TEXT("{date}"), LOCTEXT("TokenDate", "The day of the delivery") },
		{ VersionToken, LOCTEXT("TokenVersion", "The version, counted up from the files already in the folder") }
	};
	return Tokens;
}

FText UEcine::Deliver::ValidateNamePattern(const FString& Pattern)
{
	if (Pattern.TrimStartAndEnd().IsEmpty())
	{
		return LOCTEXT("PatternEmpty", "The file name is empty.");
	}
	if (!Pattern.Contains(TEXT("{shot}")))
	{
		return LOCTEXT("PatternNeedsShot", "The file name needs {shot} in it, or every shot would be written over the one before.");
	}
	for (const TCHAR Character : Pattern)
	{
		if (FString(TEXT("\\/:*?\"<>|")).Contains(FString::Chr(Character)))
		{
			return FText::Format(LOCTEXT("PatternBadCharacter", "A file name cannot hold the character {0}."), FText::FromString(FString::Chr(Character)));
		}
	}
	return FText::GetEmpty();
}

FString UEcine::Deliver::ResolveFileName(const FString& Pattern, EUEcineDeliverIntent Intent, const FUEcineDeliverSettings& Settings, const FUEcineDeliverShot& Shot)
{
	FString Name = Pattern.TrimStartAndEnd();
	Name.ReplaceInline(TEXT("{sequence}"), *MakeFileNameWord(Settings.Sequence));
	Name.ReplaceInline(TEXT("{scene}"), *MakeFileNameWord(Shot.Report.Scene));
	Name.ReplaceInline(TEXT("{shot}"), *MakeFileNameWord(Shot.Report.Shot));
	Name.ReplaceInline(TEXT("{size}"), *MakeFileNameWord(Shot.Report.Size));
	Name.ReplaceInline(TEXT("{camera}"), *MakeFileNameWord(Shot.Report.Camera));
	Name.ReplaceInline(TEXT("{intent}"), GetIntentToken(Intent));
	Name.ReplaceInline(TEXT("{date}"), *MakeFileNameWord(Settings.Date));

	// A token with nothing to say leaves two separators side by side, or one at an end.
	FString Tidy;
	for (const TCHAR Character : Name)
	{
		if (!IsNameSeparator(Character) || (!Tidy.IsEmpty() && !IsNameSeparator(Tidy[Tidy.Len() - 1])))
		{
			Tidy.AppendChar(Character);
		}
	}
	while (!Tidy.IsEmpty() && IsNameSeparator(Tidy[Tidy.Len() - 1]))
	{
		Tidy.LeftChopInline(1);
	}
	return Tidy;
}

FString UEcine::Deliver::WithVersion(const FString& Name, int32 Version)
{
	return Name.Replace(VersionToken, *FString::Printf(TEXT("v%03d"), Version));
}

int32 UEcine::Deliver::ReadVersion(const FString& Name, const FString& ExistingName)
{
	FString Before;
	FString After;
	if (!Name.Split(VersionToken, &Before, &After) || !ExistingName.StartsWith(Before + TEXT("v")))
	{
		return 0;
	}
	int32 End = Before.Len() + 1;
	while (End < ExistingName.Len() && FChar::IsDigit(ExistingName[End]))
	{
		++End;
	}
	const FString Digits = ExistingName.Mid(Before.Len() + 1, End - Before.Len() - 1);
	return !Digits.IsEmpty() && ExistingName.Mid(End) == After ? FCString::Atoi(*Digits) : 0;
}

TArray<FUEcineMetadataEntry> UEcine::Deliver::MakeMetadata(EUEcineDeliverIntent Intent, const FUEcineDeliverSettings& Settings, const FUEcineDeliverShot& Shot)
{
	const FUEcineShotReport& Report = Shot.Report;
	TArray<FUEcineMetadataEntry> Metadata;
	AddMetadata(Metadata, MetaSequence, Settings.Sequence);
	AddMetadata(Metadata, MetaScene, Report.Scene);
	AddMetadata(Metadata, MetaShot, Report.Shot);
	AddMetadata(Metadata, MetaSize, Report.Size);
	AddMetadata(Metadata, MetaMove, Report.Move);
	AddMetadata(Metadata, MetaStart, Report.Start);
	AddMetadata(Metadata, MetaDuration, Report.Duration);
	AddMetadata(Metadata, MetaCameraName, Report.Camera);
	AddMetadata(Metadata, MetaCameraBody, Report.Body);
	AddMetadata(Metadata, MetaCameraFormat, Report.Format);
	AddMetadata(Metadata, MetaSupport, Report.Support);
	AddMetadata(Metadata, MetaLensKit, Report.LensKit);
	AddMetadata(Metadata, MetaFocalLength, Report.FocalLength);
	AddMetadata(Metadata, MetaTStop, Report.TStop);
	AddMetadata(Metadata, MetaIso, Report.Iso);
	AddMetadata(Metadata, MetaShutter, Report.Shutter);
	AddMetadata(Metadata, MetaNd, Report.Nd);
	AddMetadata(Metadata, MetaFrameRate, Report.FrameRate);
	if (Shot.DeliveryAspect > 0.0)
	{
		AddMetadata(Metadata, MetaDeliveryFrame, UEcine::GetDeliveryAspectName(static_cast<float>(Shot.DeliveryAspect)).ToString());
	}
	// A picture made for a display is Rec.709, which is what the engine's ProRes files are.
	AddMetadata(Metadata, MetaColourSpace, Intent == EUEcineDeliverIntent::VfxHandoff ? Settings.SceneLinearColourSpace : FString(TEXT("Rec.709")));
	AddMetadata(Metadata, MetaDistanceUnits, Settings.DistanceUnits == EUEcineDistanceUnits::Imperial ? ImperialUnitsValue : MetricUnitsValue);
	AddMetadata(Metadata, MetaDate, Settings.Date);
	AddMetadata(Metadata, MetaIntent, GetIntentName(Intent).ToString());
	return Metadata;
}

FUEcineRenderSpec UEcine::Deliver::MakeRenderSpec(EUEcineDeliverIntent Intent, const FUEcineDeliverSettings& Settings, const FUEcineDeliverShot& Shot)
{
	FUEcineRenderSpec Spec;
	Spec.OutputDirectory = Settings.OutputDirectory / GetIntentFolder(Intent);
	Spec.FileName = ResolveFileName(Settings.NamePattern.IsEmpty() ? FString(DefaultNamePattern) : Settings.NamePattern, Intent, Settings, Shot);
	Spec.Resolution = GetOutputResolution(Intent, Shot);
	Spec.Metadata = MakeMetadata(Intent, Settings, Shot);
	// Long enough for exposure and the picture's history to settle before the first frame kept.
	Spec.WarmUpFrames = 8;

	switch (Intent)
	{
	case EUEcineDeliverIntent::Review:
		Spec.Format = EUEcineRenderFormat::ProRes422LT;
		Spec.bDeliveryFrameOnly = true;
		Spec.bSlate = Settings.bSlate;
		Spec.bBurnIn = Settings.bBurnIn;
		break;
	case EUEcineDeliverIntent::VfxHandoff:
		Spec.Format = EUEcineRenderFormat::MultilayerExr;
		// A shot's frames go in a folder of their own.
		Spec.FileName = Spec.FileName / (Spec.FileName + TEXT(".") + FrameToken);
		Spec.bSceneLinear = true;
		Spec.bDepth = Spec.bNormals = Spec.bMotionVectors = Spec.bMattes = true;
		break;
	default:
		Spec.Format = EUEcineRenderFormat::ProRes4444;
		Spec.bDeliveryFrameOnly = true;
		Spec.TemporalSamples = 8;
		break;
	}
	return Spec;
}

FString UEcine::Deliver::PreviewFile(EUEcineDeliverIntent Intent, const FUEcineRenderSpec& Spec)
{
	FString File = FString(GetIntentFolder(Intent)) / Spec.FileName + TEXT(".") + GetFileExtension(Spec.Format);
	File.ReplaceInline(FrameToken, TEXT("0000"));
	return WithVersion(File, 1);
}

FString UEcine::Deliver::GetCompanionFile(const FUEcineRenderSpec& Spec, const TCHAR* Extension)
{
	const FString Name = Spec.FileName.Replace(*(FString(TEXT(".")) + FrameToken), TEXT("")).Replace(FrameToken, TEXT(""));
	return Spec.OutputDirectory / Name + TEXT(".") + Extension;
}

FUEcineSlate UEcine::Deliver::MakeSlate(const TMap<FString, FString>& Metadata)
{
	const auto Get = [&Metadata](const TCHAR* Name) { return Metadata.FindRef(Name); };

	FUEcineSlate Slate;
	Slate.Title = Get(MetaSequence);
	AddSlateLine(Slate, LOCTEXT("SlateScene", "Scene"), Get(MetaScene));
	AddSlateLine(Slate, LOCTEXT("SlateShot", "Shot"), Get(MetaShot));
	AddSlateLine(Slate, LOCTEXT("SlateSize", "Size"), Get(MetaSize));
	AddSlateLine(Slate, LOCTEXT("SlateMove", "Move"), Get(MetaMove));
	AddSlateLine(Slate, LOCTEXT("SlateCamera", "Camera"), JoinWithDots({ Get(MetaCameraName), Get(MetaCameraBody), Get(MetaCameraFormat) }));
	AddSlateLine(Slate, LOCTEXT("SlateSupport", "Support"), Get(MetaSupport));
	AddSlateLine(Slate, LOCTEXT("SlateLens", "Lens"), JoinWithDots({ Get(MetaLensKit), Get(MetaFocalLength) }));
	const FString Iso = Get(MetaIso);
	AddSlateLine(Slate, LOCTEXT("SlateExposure", "Exposure"), JoinWithDots(
		{
			Iso.IsEmpty() ? FString() : FText::Format(LOCTEXT("SlateIso", "ISO {0}"), FText::FromString(Iso)).ToString(),
			Get(MetaShutter), Get(MetaTStop), Get(MetaNd)
		}));
	AddSlateLine(Slate, LOCTEXT("SlateFrameRate", "Frame rate"), Get(MetaFrameRate));
	AddSlateLine(Slate, LOCTEXT("SlateFrame", "Frame"), Get(MetaDeliveryFrame));
	AddSlateLine(Slate, LOCTEXT("SlateStart", "Start"), Get(MetaStart));
	AddSlateLine(Slate, LOCTEXT("SlateDuration", "Duration"), Get(MetaDuration));
	AddSlateLine(Slate, LOCTEXT("SlateDate", "Date"), Get(MetaDate));
	AddSlateLine(Slate, LOCTEXT("SlateIntent", "Delivery"), Get(MetaIntent));
	return Slate;
}

FUEcineBurnIn UEcine::Deliver::MakeBurnIn(const TMap<FString, FString>& Metadata, const FUEcineBurnInFrame& Frame)
{
	using UEcine::Units::FormatNumber;
	const auto Get = [&Metadata](const TCHAR* Name) { return Metadata.FindRef(Name); };

	FUEcineBurnIn BurnIn;
	const FString Scene = Get(MetaScene);
	BurnIn.TopLeft = JoinWithDots(
		{
			Get(MetaSequence),
			Scene.IsEmpty() ? FString() : FText::Format(LOCTEXT("BurnInScene", "Sc {0}"), FText::FromString(Scene)).ToString(),
			Get(MetaShot), Get(MetaSize)
		});
	BurnIn.TopRight = JoinWithDots({ Get(MetaCameraBody), Get(MetaLensKit) });
	if (BurnIn.TopRight.IsEmpty())
	{
		BurnIn.TopRight = Get(MetaCameraName);
	}
	BurnIn.BottomLeft = JoinWithDots({ Frame.Timecode, FText::Format(LOCTEXT("BurnInFrame", "frame {0}"), FormatNumber(Frame.Frame, 0)).ToString() });

	if (Frame.FocalLengthMm > 0.0)
	{
		const EUEcineDistanceUnits Units = Get(MetaDistanceUnits) == ImperialUnitsValue ? EUEcineDistanceUnits::Imperial : EUEcineDistanceUnits::Metric;
		const FString Iso = Get(MetaIso);
		BurnIn.BottomRight = JoinWithDots(
			{
				FText::Format(LOCTEXT("BurnInFocalLength", "{0} mm"), FormatNumber(Frame.FocalLengthMm, 0)).ToString(),
				FText::Format(LOCTEXT("BurnInTStop", "T{0}"), FormatNumber(Frame.TStop, 1)).ToString(),
				UEcine::Units::FormatDistance(Frame.FocusDistanceCm * 10.0, Units).ToString(),
				Iso.IsEmpty() ? FString() : FText::Format(LOCTEXT("BurnInIso", "ISO {0}"), FText::FromString(Iso)).ToString(),
				Get(MetaShutter)
			});
	}
	return BurnIn;
}

FString UEcine::Deliver::WriteSidecarJson(const FUEcineRenderSpec& Spec, double FramesPerSecond, TConstArrayView<FUEcineCameraSample> Camera)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("uecine.sidecar/1"));
	// The frame number is written the way compositing programs write a run of files.
	Root->SetStringField(TEXT("files"), (FPaths::GetCleanFilename(Spec.FileName) + TEXT(".") + GetFileExtension(Spec.Format)).Replace(FrameToken, TEXT("####")));
	Root->SetNumberField(TEXT("width"), Spec.Resolution.X);
	Root->SetNumberField(TEXT("height"), Spec.Resolution.Y);
	Root->SetNumberField(TEXT("framesPerSecond"), FramesPerSecond);

	const TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
	for (const FUEcineMetadataEntry& Entry : Spec.Metadata)
	{
		Metadata->SetStringField(Entry.Name, Entry.Value);
	}
	Root->SetObjectField(TEXT("metadata"), Metadata);

	// What the numbers below are measured in, for a reader who has never seen the engine.
	const TSharedRef<FJsonObject> Units = MakeShared<FJsonObject>();
	Units->SetStringField(TEXT("location"), TEXT("centimetres"));
	Units->SetStringField(TEXT("rotation"), TEXT("degrees: pitch, yaw, roll"));
	Units->SetStringField(TEXT("focalLength"), TEXT("millimetres"));
	Units->SetStringField(TEXT("focusDistance"), TEXT("centimetres"));
	Units->SetStringField(TEXT("axes"), TEXT("Unreal Engine: X forward, Y right, Z up, left-handed"));
	Root->SetObjectField(TEXT("units"), Units);

	TArray<TSharedPtr<FJsonValue>> Frames;
	for (const FUEcineCameraSample& Sample : Camera)
	{
		const TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
		Frame->SetNumberField(TEXT("frame"), Sample.Frame);
		Frame->SetArrayField(TEXT("location"), TArray<TSharedPtr<FJsonValue>>{ MakeJsonNumber(Sample.Location.X), MakeJsonNumber(Sample.Location.Y), MakeJsonNumber(Sample.Location.Z) });
		Frame->SetArrayField(TEXT("rotation"), TArray<TSharedPtr<FJsonValue>>{ MakeJsonNumber(Sample.Rotation.Pitch), MakeJsonNumber(Sample.Rotation.Yaw), MakeJsonNumber(Sample.Rotation.Roll) });
		Frame->SetField(TEXT("focalLength"), MakeJsonNumber(Sample.FocalLengthMm));
		Frame->SetField(TEXT("tStop"), MakeJsonNumber(Sample.TStop));
		Frame->SetField(TEXT("focusDistance"), MakeJsonNumber(Sample.FocusDistanceCm));
		Frames.Add(MakeShared<FJsonValueObject>(Frame));
	}
	Root->SetArrayField(TEXT("camera"), Frames);

	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	return Json;
}

TArray<FUEcinePreflightFinding> UEcine::Deliver::RunPreflight(const FUEcinePreflightFacts& Facts)
{
	using UEcine::Units::FormatNumber;

	TArray<FUEcinePreflightFinding> Findings;
	if (!Facts.bHasSequence)
	{
		Findings.Add(MakeFinding(EUEcinePreflightCheck::NoSequence, true, FString(),
			LOCTEXT("PreflightNoSequence", "No sequence is open"),
			LOCTEXT("PreflightNoSequenceDetail", "A delivery is made of the sequence open in Sequencer. Open one, or add a shot in the Shots tab.")));
		return Findings;
	}

	TSet<FString> Names;
	TSet<FString> NamesFoundTwice;
	for (const FUEcinePreflightShot& Shot : Facts.Shots)
	{
		bool bSeenBefore = false;
		Names.Add(Shot.Name, &bSeenBefore);
		if (bSeenBefore && !NamesFoundTwice.Contains(Shot.Name))
		{
			NamesFoundTwice.Add(Shot.Name);
			Findings.Add(MakeFinding(EUEcinePreflightCheck::DuplicateShotName, true, Shot.Name,
				LOCTEXT("PreflightDuplicateName", "Two shots have this name"),
				LOCTEXT("PreflightDuplicateNameDetail", "Their files would have the same name, and one would be written over the other. Rename one of the shots in Sequencer.")));
		}
	}

	for (const FUEcinePreflightShot& Shot : Facts.Shots)
	{
		if (!Shot.bHasCamera)
		{
			Findings.Add(MakeFinding(EUEcinePreflightCheck::ShotWithoutCamera, true, Shot.Name,
				LOCTEXT("PreflightNoCamera", "The shot has no camera"),
				LOCTEXT("PreflightNoCameraDetail", "There is nothing to see the shot through. Add a Cine Camera to the shot's sequence in Sequencer.")));
		}
		else if (Shot.CameraCutCount == 0)
		{
			Findings.Add(MakeFinding(EUEcinePreflightCheck::ShotWithoutCameraCut, true, Shot.Name,
				LOCTEXT("PreflightNoCameraCut", "The shot has no camera cut"),
				LOCTEXT("PreflightNoCameraCutDetail", "Without a camera cut the renderer does not look through the shot's camera, and the picture comes out from the wrong place."),
				LOCTEXT("PreflightNoCameraCutFix", "Add camera cut")));
		}
		else if (Shot.CameraCutCount > 1)
		{
			Findings.Add(MakeFinding(EUEcinePreflightCheck::SeveralCameraCuts, true, Shot.Name,
				LOCTEXT("PreflightSeveralCuts", "The shot cuts between cameras"),
				LOCTEXT("PreflightSeveralCutsDetail", "A delivery is one camera to a shot: one movie, one lens, one line of the shot list. Make each camera cut a shot of its own in Sequencer.")));
		}
	}

	for (const FUEcinePreflightShot& Shot : Facts.Shots)
	{
		if (!Shot.bHasCamera)
		{
			continue;
		}
		if (!Shot.bHasPackage)
		{
			Findings.Add(MakeFinding(EUEcinePreflightCheck::CameraWithoutPackage, false, Shot.Name,
				LOCTEXT("PreflightNoPackage", "The camera has no camera package"),
				LOCTEXT("PreflightNoPackageDetail", "The shot's files will not say which camera, format and lens it was shot on, and it is delivered in the whole of its frame. Choose a camera package in the Camera tab.")));
		}
		else if (Facts.SequenceFramesPerSecond > 0.0 && !FMath::IsNearlyEqual(Shot.PackageFramesPerSecond, Facts.SequenceFramesPerSecond, 0.001))
		{
			Findings.Add(MakeFinding(EUEcinePreflightCheck::FrameRateMismatch, false, Shot.Name,
				LOCTEXT("PreflightFrameRate", "The camera's frame rate is not the sequence's"),
				FText::Format(LOCTEXT("PreflightFrameRateDetail", "The camera is set to {0} fps and the sequence plays at {1} fps, so the shutter angle gives the wrong amount of motion blur."),
					FormatNumber(Shot.PackageFramesPerSecond, 3), FormatNumber(Facts.SequenceFramesPerSecond, 3)),
				FText::Format(LOCTEXT("PreflightFrameRateFix", "Set camera to {0} fps"), FormatNumber(Facts.SequenceFramesPerSecond, 3))));
		}
		if (Shot.bAutoExposure)
		{
			Findings.Add(MakeFinding(EUEcinePreflightCheck::AutoExposure, false, Shot.Name,
				LOCTEXT("PreflightAutoExposure", "Exposure is left to adapt by itself"),
				LOCTEXT("PreflightAutoExposureDetail", "The picture's brightness will drift as the shot plays, and differ from one render to the next. Turn on Physical exposure in the Camera tab, or set the level's exposure to Manual.")));
		}
	}

	if (Facts.Intent == EUEcineDeliverIntent::VfxHandoff && !Facts.bWorkingSpaceIsAcesCg && Facts.bAcesCgWaitsForRestart)
	{
		Findings.Add(MakeFinding(EUEcinePreflightCheck::WorkingColourSpace, false, FString(),
			LOCTEXT("PreflightColourSpaceRestart", "ACEScg waits for a restart"),
			LOCTEXT("PreflightColourSpaceRestartDetail", "The project has been set to work in ACEScg, which takes effect when the editor is restarted. Until then the files are scene-linear in the colour space the editor started in, and say so.")));
	}
	else if (Facts.Intent == EUEcineDeliverIntent::VfxHandoff && !Facts.bWorkingSpaceIsAcesCg)
	{
		Findings.Add(MakeFinding(EUEcinePreflightCheck::WorkingColourSpace, false, FString(),
			LOCTEXT("PreflightColourSpace", "The project does not work in ACEScg"),
			LOCTEXT("PreflightColourSpaceDetail", "Visual effects usually ask for ACEScg. The files will be scene-linear in the project's own colour space, and say so. Changing the project's working colour space needs the editor to be restarted, and changes how colours in the project are read."),
			LOCTEXT("PreflightColourSpaceFix", "Work in ACEScg")));
	}

	if (Facts.Intent == EUEcineDeliverIntent::Review && Facts.bSlate)
	{
		for (const FUEcinePreflightShot& Shot : Facts.Shots)
		{
			// The slate is the frame before the shot's first.
			if (Shot.StartFrame < 1)
			{
				Findings.Add(MakeFinding(EUEcinePreflightCheck::SlateBeforeSequenceStart, false, Shot.Name,
					LOCTEXT("PreflightSlateBeforeStart", "The slate falls before the start of the sequence"),
					LOCTEXT("PreflightSlateBeforeStartDetail", "The shot starts on the first frame of the sequence, so its slate has no timecode of its own and the timecode of its movie cannot be trusted. Start the sequence later in Sequencer (at one hour is usual), or deliver without a slate.")));
			}
		}
	}

	// The findings that stop the render first, each kind in the order found.
	Findings.StableSort([](const FUEcinePreflightFinding& A, const FUEcinePreflightFinding& B) { return A.bStopsRender && !B.bStopsRender; });
	return Findings;
}

#undef LOCTEXT_NAMESPACE
