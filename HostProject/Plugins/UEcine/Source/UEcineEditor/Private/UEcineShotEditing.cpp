#include "UEcineShotEditing.h"

#include "Algo/BinarySearch.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SplineComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Timecode.h"
#include "MovieScene.h"
#include "MovieSceneMetaData.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSequenceID.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneToolHelpers.h"
#include "MovieSceneToolsProjectSettings.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "TextureResource.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineEditorSettings.h"
#include "UEcineFocusEditing.h"
#include "UEcineGripEditing.h"
#include "UEcineRig.h"
#include "UEcineSequenceUtils.h"
#include "UEcineShotMetaData.h"

#define LOCTEXT_NAMESPACE "UEcineEditor"

namespace
{
	/** Storyboard pictures are this wide: enough to read on a printed page, small enough to keep inside the sequence. */
	constexpr int32 StoryboardWidth = 640;
	/** How hard a storyboard picture is squeezed, from 1 to 100. This keeps a frame to a few tens of kilobytes with no marks to see. */
	constexpr int32 StoryboardJpegQuality = 85;
	/** How many moments of a shot the overhead plan looks at to draw where the camera goes. */
	constexpr int32 MaxPlanSamples = 49;
	/** How many straight pieces a length of dolly track is drawn with. */
	constexpr int32 PlanTrackSteps = 32;
	/** The overhead plan's size on the printed page. */
	const FVector2D PaperworkPlanSize(640.0, 400.0);

	FFrameNumber TicksToFrame(const UMovieScene* MovieScene, FFrameNumber Ticks)
	{
		return ConvertFrameTime(FFrameTime(Ticks), MovieScene->GetTickResolution(), MovieScene->GetDisplayRate()).FloorToFrame();
	}

	FFrameNumber FrameToTicks(const UMovieScene* MovieScene, FFrameNumber Frame)
	{
		return ConvertFrameTime(FFrameTime(Frame), MovieScene->GetDisplayRate(), MovieScene->GetTickResolution()).FloorToFrame();
	}

	/** Where Sequencer's playhead is in the open sequence, whichever shot is being worked in. */
	FFrameNumber GetPlayheadFrame(const ULevelSequence* OpenSequence)
	{
		const FFrameTime Ticks = ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition(EMovieSceneTimeUnit::TickResolution).Frame;
		return TicksToFrame(OpenSequence->GetMovieScene(), Ticks.FloorToFrame());
	}

	void SetPlayheadFrame(const ULevelSequence* OpenSequence, FFrameNumber Frame)
	{
		UEcine::SequenceUtils::SetPlayheadTicks(FFrameTime(FrameToTicks(OpenSequence->GetMovieScene(), Frame)));
	}

	UEcine::ShotEditing::FShot MakeShot(ULevelSequence* Sequence, UMovieSceneCinematicShotSection* Section, const FString& Name, const UMovieScene* OpenMovieScene, const TRange<FFrameNumber>& Ticks)
	{
		UEcine::ShotEditing::FShot Shot;
		Shot.Sequence = Sequence;
		Shot.Section = Section;
		Shot.Name = Name;
		const FFrameNumber StartTicks = UE::MovieScene::DiscreteInclusiveLower(Ticks);
		const FFrameNumber EndTicks = UE::MovieScene::DiscreteExclusiveUpper(Ticks);
		Shot.StartFrame = TicksToFrame(OpenMovieScene, StartTicks);
		Shot.DurationFrames = ConvertFrameTime(FFrameTime(EndTicks - StartTicks), OpenMovieScene->GetTickResolution(), OpenMovieScene->GetDisplayRate()).RoundToFrame().Value;
		return Shot;
	}

	/** Changes what the shot list says about a shot, as one undoable step. */
	void EditShotData(ULevelSequence* ShotSequence, const FText& Description, TFunctionRef<void(UUEcineShotMetaData&)> Edit)
	{
		const FScopedTransaction Transaction(Description);
		ShotSequence->Modify();
		UUEcineShotMetaData* Data = ShotSequence->FindOrAddMetaData<UUEcineShotMetaData>();
		Data->SetFlags(RF_Transactional);
		Data->Modify();
		Edit(*Data);
	}

	ACineCameraActor* FindCameraOfBinding(const UEcine::ShotEditing::FShot& Shot, const FGuid& Binding)
	{
		for (UObject* Object : UEcine::ShotEditing::ResolveBinding(Shot, Binding))
		{
			if (ACineCameraActor* Camera = Cast<ACineCameraActor>(Object))
			{
				return Camera;
			}
		}
		return nullptr;
	}

	/** Takes a picture through a camera as the level stands. False when there is no renderer to take it with. */
	bool TakeStoryboardPicture(const ACineCameraActor& Camera, FUEcineStoryboardFrame& OutFrame)
	{
		FMinimalViewInfo View;
		Camera.GetCineCameraComponent()->GetCameraView(0.0f, View);

		const int32 Width = StoryboardWidth;
		const int32 Height = FMath::Max(FMath::RoundToInt32(Width / View.AspectRatio), 1);

		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		// Eight bits that hold the picture as displayed, like a viewport.
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->TargetGamma = 2.2f;
		Target->InitAutoFormat(Width, Height);
		Target->UpdateResourceImmediate(true);

		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
		Capture->TextureTarget = Target;
		Capture->CaptureSource = SCS_FinalColorLDR;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->SetCameraView(View);
		Capture->PostProcessSettings = View.PostProcessSettings;
		Capture->PostProcessBlendWeight = View.PostProcessBlendWeight;
		Capture->RegisterComponentWithWorld(Camera.GetWorld());
		Capture->CaptureScene();

		TArray<FColor> Pixels;
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		const bool bRead = Resource != nullptr && Resource->ReadPixels(Pixels) && Pixels.Num() == Width * Height;
		Capture->DestroyComponent();
		if (!bRead)
		{
			return false;
		}

		TArray64<uint8> Jpeg;
		if (!FImageUtils::CompressImage(Jpeg, TEXT("jpg"), FImageView(Pixels.GetData(), Width, Height), StoryboardJpegQuality))
		{
			return false;
		}
		OutFrame.Width = Width;
		OutFrame.Height = Height;
		OutFrame.Jpeg.Reset();
		OutFrame.Jpeg.Append(Jpeg.GetData(), IntCastChecked<int32>(Jpeg.Num()));
		return true;
	}

	void AddPlanSubject(FUEcinePlan& Plan, const AActor* Actor)
	{
		if (Actor == nullptr || Actor->IsA<ACineCameraActor>() || Actor->IsA<AUEcineRig>())
		{
			return;
		}
		const FString Name = Actor->GetActorLabel();
		if (!Plan.Subjects.ContainsByPredicate([&Name](const FUEcinePlanSubject& Subject) { return Subject.Name == Name; }))
		{
			Plan.Subjects.Add({ Name, FVector2D(Actor->GetActorLocation()) });
		}
	}

	FString FormatShotDuration(int32 Frames, FFrameRate DisplayRate)
	{
		return FText::Format(LOCTEXT("ShotDuration", "{0} s ({1} {2}|plural(one=frame,other=frames))"),
			UEcine::Units::FormatNumber(DisplayRate.AsSeconds(FFrameTime(Frames)), 2), UEcine::Units::FormatNumber(Frames, 0), Frames).ToString();
	}

	FText GetNoSequenceOpenError()
	{
		return LOCTEXT("NoSequenceOpen", "No sequence is open in Sequencer. Open the sequence, or add a shot to start one.");
	}
}

TArray<UEcine::ShotEditing::FShot> UEcine::ShotEditing::GetShots()
{
	TArray<FShot> Shots;
	ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		return Shots;
	}

	const UMovieScene* MovieScene = OpenSequence->GetMovieScene();
	if (const UMovieSceneCinematicShotTrack* Track = MovieScene->FindTrack<UMovieSceneCinematicShotTrack>())
	{
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			UMovieSceneCinematicShotSection* ShotSection = Cast<UMovieSceneCinematicShotSection>(Section);
			ULevelSequence* ShotSequence = ShotSection != nullptr ? Cast<ULevelSequence>(ShotSection->GetSequence()) : nullptr;
			const TRange<FFrameNumber> Range = Section->GetRange();
			if (ShotSequence != nullptr && ShotSection->IsActive() && Range.HasLowerBound() && Range.HasUpperBound())
			{
				Shots.Add(MakeShot(ShotSequence, ShotSection, ShotSection->GetShotDisplayName(), MovieScene, Range));
			}
		}
		Shots.StableSort([](const FShot& A, const FShot& B) { return A.StartFrame < B.StartFrame; });
	}

	// A sequence that is not cut into shots is one shot.
	if (Shots.IsEmpty())
	{
		const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
		if (PlaybackRange.HasLowerBound() && PlaybackRange.HasUpperBound())
		{
			Shots.Add(MakeShot(OpenSequence, nullptr, OpenSequence->GetName(), MovieScene, PlaybackRange));
		}
	}
	return Shots;
}

FString UEcine::ShotEditing::GetScene(const ULevelSequence* ShotSequence)
{
	const UUEcineShotMetaData* Data = ShotSequence->FindMetaData<UUEcineShotMetaData>();
	return Data != nullptr ? Data->Scene : FString();
}

EUEcineShotSize UEcine::ShotEditing::GetSize(const ULevelSequence* ShotSequence)
{
	const UUEcineShotMetaData* Data = ShotSequence->FindMetaData<UUEcineShotMetaData>();
	return Data != nullptr ? Data->Size : EUEcineShotSize::Unset;
}

FString UEcine::ShotEditing::GetMove(const ULevelSequence* ShotSequence)
{
	const UUEcineShotMetaData* Data = ShotSequence->FindMetaData<UUEcineShotMetaData>();
	return Data != nullptr ? Data->Move : FString();
}

FString UEcine::ShotEditing::GetNotes(const ULevelSequence* ShotSequence)
{
	const UMovieSceneMetaData* Data = ShotSequence->FindMetaData<UMovieSceneMetaData>();
	return Data != nullptr ? Data->GetNotes() : FString();
}

void UEcine::ShotEditing::SetScene(ULevelSequence* ShotSequence, const FString& Scene)
{
	if (GetScene(ShotSequence) != Scene)
	{
		EditShotData(ShotSequence, LOCTEXT("SetShotScene", "Change Shot Scene"), [&Scene](UUEcineShotMetaData& Data) { Data.Scene = Scene; });
	}
}

void UEcine::ShotEditing::SetSize(ULevelSequence* ShotSequence, EUEcineShotSize Size)
{
	if (GetSize(ShotSequence) != Size)
	{
		EditShotData(ShotSequence, LOCTEXT("SetShotSize", "Change Shot Size"), [Size](UUEcineShotMetaData& Data) { Data.Size = Size; });
	}
}

void UEcine::ShotEditing::SetMove(ULevelSequence* ShotSequence, const FString& Move)
{
	if (GetMove(ShotSequence) != Move)
	{
		EditShotData(ShotSequence, LOCTEXT("SetShotMove", "Change Shot Move"), [&Move](UUEcineShotMetaData& Data) { Data.Move = Move; });
	}
}

void UEcine::ShotEditing::SetNotes(ULevelSequence* ShotSequence, const FString& Notes)
{
	if (GetNotes(ShotSequence) == Notes)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SetShotNotes", "Change Shot Notes"));
	ShotSequence->Modify();
	UMovieSceneMetaData* Data = ShotSequence->FindOrAddMetaData<UMovieSceneMetaData>();
	Data->SetFlags(RF_Transactional);
	Data->Modify();
	Data->SetNotes(Notes);
}

bool UEcine::ShotEditing::AddShot(ACineCameraActor* Camera, FText& OutError)
{
	using namespace UEcine::SequenceUtils;

	// Making and opening sequences cannot be undone, so it is done before the undoable step begins:
	// an asset made inside a transaction leaves the transaction open.
	ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		OpenSequence = CreateSequence(FString(SequenceFolder) / TEXT("Sequence"), GetCameraFrameRate(Camera));
		if (OpenSequence == nullptr || !ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(OpenSequence))
		{
			OutError = LOCTEXT("SequenceNotMade", "A sequence for the shots could not be made. Check that the project's Content folder can be written to.");
			return false;
		}
	}

	UMovieScene* MovieScene = OpenSequence->GetMovieScene();
	if (MovieScene->IsReadOnly())
	{
		OutError = LOCTEXT("SequenceLocked", "The sequence is locked. Unlock it in Sequencer, then add the shot.");
		return false;
	}

	// The new shot follows the last one, or starts the sequence when it is the first. A sequence
	// made a moment ago has nothing to play yet, and its shots start at zero.
	const UMovieSceneCinematicShotTrack* ExistingTrack = MovieScene->FindTrack<UMovieSceneCinematicShotTrack>();
	const TArray<UMovieSceneSection*> ExistingShots = ExistingTrack != nullptr ? ExistingTrack->GetAllSections() : TArray<UMovieSceneSection*>();
	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	FFrameNumber StartTicks = PlaybackRange.IsEmpty() ? FFrameNumber(0) : UE::MovieScene::DiscreteInclusiveLower(PlaybackRange);
	bool bFirstShot = true;
	for (const UMovieSceneSection* Section : ExistingShots)
	{
		if (Section->HasEndFrame())
		{
			StartTicks = bFirstShot ? Section->GetExclusiveEndFrame() : FMath::Max(StartTicks, Section->GetExclusiveEndFrame());
			bFirstShot = false;
		}
	}

	// Named, filed and timed the way Sequencer does it for a shot added there.
	const UMovieSceneToolsProjectSettings* SequencerSettings = GetDefault<UMovieSceneToolsProjectSettings>();
	const int32 DurationTicks = FMath::Max(MovieScene->GetTickResolution().AsFrameNumber(SequencerSettings->DefaultDuration).Value, 1);
	FString ShotName = MovieSceneToolHelpers::GenerateNewSubsequenceName(ExistingShots, SequencerSettings->ShotPrefix, StartTicks);
	const FString ShotFolder = MovieSceneToolHelpers::GenerateNewSubsequencePath(MovieScene, SequencerSettings->ShotDirectory, ShotName);

	ULevelSequence* ShotSequence = CreateSequence(ShotFolder / ShotName, MovieScene->GetDisplayRate());
	if (ShotSequence == nullptr)
	{
		OutError = LOCTEXT("ShotNotMade", "The shot's sequence could not be made. Check that the project's Content folder can be written to.");
		return false;
	}
	UMovieScene* ShotMovieScene = ShotSequence->GetMovieScene();
	ShotMovieScene->SetTickResolutionDirectly(MovieScene->GetTickResolution());
	ShotMovieScene->SetPlaybackRange(0, DurationTicks);

	if (Camera != nullptr)
	{
		// The shot is seen through the camera from its first frame to its last.
		const FGuid CameraBinding = FindOrAddBinding(ShotSequence, Camera);
		UMovieSceneCameraCutTrack* CameraCuts = CastChecked<UMovieSceneCameraCutTrack>(ShotMovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
		UMovieSceneCameraCutSection* CameraCut = CameraCuts->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(CameraBinding), 0);
		CameraCut->SetRange(ShotMovieScene->GetPlaybackRange());
	}

	{
		const FScopedTransaction Transaction(LOCTEXT("AddShot", "Add Shot"));
		MovieScene->Modify();
		UMovieSceneCinematicShotTrack* Track = MovieScene->FindTrack<UMovieSceneCinematicShotTrack>();
		if (Track == nullptr)
		{
			Track = MovieScene->AddTrack<UMovieSceneCinematicShotTrack>();
		}
		Track->Modify();
		Track->AddSequence(ShotSequence, StartTicks, DurationTicks);
		IncludeInPlaybackRange(MovieScene, TRange<FFrameNumber>(StartTicks, StartTicks + DurationTicks));
	}
	// At once, so that the new shot's camera can be found through Sequencer straight away.
	ULevelSequenceEditorBlueprintLibrary::ForceUpdate();
	return true;
}

void UEcine::ShotEditing::GoToShot(const FShot& Shot)
{
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		return;
	}

	// A shot is entered from the whole sequence, so step out of the shot being worked in first.
	for (int32 Depth = ULevelSequenceEditorBlueprintLibrary::GetSubSequenceHierarchy().Num(); Depth > 0; --Depth)
	{
		ULevelSequenceEditorBlueprintLibrary::FocusParentSequence();
	}
	SetPlayheadFrame(OpenSequence, Shot.StartFrame);
	if (Shot.Section != nullptr)
	{
		// Inside the shot, Sequencer shows the shot's own tracks, and moves and racks are baked into it.
		ULevelSequenceEditorBlueprintLibrary::FocusLevelSequence(Shot.Section);
	}
}

int32 UEcine::ShotEditing::FindShotAtPlayhead(TConstArrayView<FShot> Shots)
{
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		return INDEX_NONE;
	}
	const FFrameNumber Playhead = GetPlayheadFrame(OpenSequence);
	return Shots.IndexOfByPredicate([Playhead](const FShot& Shot)
	{
		return Playhead >= Shot.StartFrame && Playhead < Shot.StartFrame + Shot.DurationFrames;
	});
}

ACineCameraActor* UEcine::ShotEditing::FindShotCamera(const FShot& Shot)
{
	const UMovieScene* MovieScene = Shot.Sequence->GetMovieScene();

	if (const UMovieSceneTrack* CameraCuts = MovieScene->GetCameraCutTrack())
	{
		const UMovieSceneCameraCutSection* FirstCut = nullptr;
		for (const UMovieSceneSection* Section : CameraCuts->GetAllSections())
		{
			const UMovieSceneCameraCutSection* Cut = Cast<UMovieSceneCameraCutSection>(Section);
			if (Cut != nullptr && Cut->HasStartFrame() && (FirstCut == nullptr || Cut->GetInclusiveStartFrame() < FirstCut->GetInclusiveStartFrame()))
			{
				FirstCut = Cut;
			}
		}
		if (FirstCut != nullptr)
		{
			if (ACineCameraActor* Camera = FindCameraOfBinding(Shot, FirstCut->GetCameraBindingID().GetGuid()))
			{
				return Camera;
			}
		}
	}

	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		if (ACineCameraActor* Camera = FindCameraOfBinding(Shot, Binding.GetObjectGuid()))
		{
			return Camera;
		}
	}
	return nullptr;
}

TArray<UObject*> UEcine::ShotEditing::ResolveBinding(const FShot& Shot, const FGuid& Binding)
{
	const FMovieSceneSequenceID SequenceID = Shot.Section != nullptr ? Shot.Section->GetSequenceID() : MovieSceneSequenceID::Root;
	return ULevelSequenceEditorBlueprintLibrary::GetBoundObjects(UE::MovieScene::FFixedObjectBindingID(Binding, SequenceID));
}

TConstArrayView<FUEcineStoryboardFrame> UEcine::ShotEditing::GetStoryboardFrames(const ULevelSequence* ShotSequence)
{
	const UUEcineShotMetaData* Data = ShotSequence->FindMetaData<UUEcineShotMetaData>();
	return Data != nullptr ? TConstArrayView<FUEcineStoryboardFrame>(Data->StoryboardFrames) : TConstArrayView<FUEcineStoryboardFrame>();
}

bool UEcine::ShotEditing::GrabStoryboardFrame(const FShot& Shot, FText& OutError)
{
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		OutError = GetNoSequenceOpenError();
		return false;
	}

	FUEcineStoryboardFrame Frame;
	Frame.ShotFrame = (GetPlayheadFrame(OpenSequence) - Shot.StartFrame).Value;
	if (Frame.ShotFrame < 0 || Frame.ShotFrame >= Shot.DurationFrames)
	{
		OutError = LOCTEXT("PlayheadOutsideShot", "The playhead is not in this shot. Go to the shot, find the frame you want, then grab it.");
		return false;
	}

	const ACineCameraActor* Camera = FindShotCamera(Shot);
	if (Camera == nullptr)
	{
		OutError = LOCTEXT("ShotHasNoCamera", "This shot has no camera. Add a camera to the shot in Sequencer, then grab the frame.");
		return false;
	}
	if (!TakeStoryboardPicture(*Camera, Frame))
	{
		OutError = LOCTEXT("PictureNotTaken", "The picture could not be taken. Storyboard frames need the editor to be drawing the level.");
		return false;
	}

	EditShotData(Shot.Sequence, LOCTEXT("GrabStoryboardFrame", "Grab Storyboard Frame"), [&Frame](UUEcineShotMetaData& Data)
	{
		// One picture per frame of the shot: grabbing a frame again replaces its picture.
		Data.StoryboardFrames.RemoveAll([&Frame](const FUEcineStoryboardFrame& Existing) { return Existing.ShotFrame == Frame.ShotFrame; });
		const int32 Index = Algo::LowerBoundBy(Data.StoryboardFrames, Frame.ShotFrame, &FUEcineStoryboardFrame::ShotFrame);
		Data.StoryboardFrames.Insert(MoveTemp(Frame), Index);
	});
	return true;
}

void UEcine::ShotEditing::RemoveStoryboardFrame(ULevelSequence* ShotSequence, int32 FrameIndex)
{
	if (GetStoryboardFrames(ShotSequence).IsValidIndex(FrameIndex))
	{
		EditShotData(ShotSequence, LOCTEXT("RemoveStoryboardFrame", "Remove Storyboard Frame"), [FrameIndex](UUEcineShotMetaData& Data)
		{
			Data.StoryboardFrames.RemoveAt(FrameIndex);
		});
	}
}

FUEcineShotReport UEcine::ShotEditing::MakeShotReport(const FShot& Shot)
{
	using UEcine::Units::FormatNumber;

	FUEcineShotReport Report;
	Report.Scene = GetScene(Shot.Sequence);
	Report.Shot = Shot.Name;
	const EUEcineShotSize Size = GetSize(Shot.Sequence);
	if (Size != EUEcineShotSize::Unset)
	{
		Report.Size = UEcine::Shots::GetShotSizeName(Size).ToString();
	}
	Report.Move = GetMove(Shot.Sequence);
	Report.Notes = GetNotes(Shot.Sequence);

	if (const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence())
	{
		const FFrameRate DisplayRate = OpenSequence->GetMovieScene()->GetDisplayRate();
		Report.Start = FTimecode::FromFrameNumber(Shot.StartFrame, DisplayRate).ToString();
		Report.Duration = FormatShotDuration(Shot.DurationFrames, DisplayRate);
	}

	const ACineCameraActor* Camera = FindShotCamera(Shot);
	if (Camera == nullptr)
	{
		return Report;
	}

	const EUEcineDistanceUnits Units = UUEcineEditorSettings::GetDistanceUnits();
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	Report.Camera = Camera->GetActorLabel();
	Report.FocalLength = FText::Format(LOCTEXT("ReportFocalLength", "{0} mm"), FormatNumber(Lens->CurrentFocalLength, 0)).ToString();
	Report.TStop = FText::Format(LOCTEXT("ReportTStop", "T{0}"), FormatNumber(Lens->CurrentAperture, 1)).ToString();
	Report.Focus = UEcine::Units::FormatDistance(UEcine::FocusEditing::GetLensDistance(Camera) * 10.0, Units).ToString();

	if (const UUEcineCameraPackageComponent* Component = UUEcineCameraPackageComponent::Find(Camera))
	{
		const FUEcineCameraPackage& Package = Component->Package;
		const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();
		if (const FUEcineCameraBody* Body = Database.FindBody(Package.BodyId))
		{
			Report.Body = Body->GetDisplayName();
			if (const FUEcineRecordingFormat* Format = Body->FindFormat(Package.FormatId))
			{
				Report.Format = Format->Name;
			}
		}
		if (const FUEcineLensKit* Kit = Database.FindLensKit(Package.LensKitId))
		{
			Report.LensKit = Kit->GetDisplayName();
		}
		Report.Iso = FormatNumber(Package.Iso, 0).ToString();
		Report.Shutter = FText::Format(LOCTEXT("ReportShutter", "{0}°"), FormatNumber(Package.ShutterAngleDeg, 1)).ToString();
		Report.FrameRate = FText::Format(LOCTEXT("ReportFrameRate", "{0} fps"), FormatNumber(Package.FramesPerSecond, 3)).ToString();
		if (Package.NdDensity > 0.0f)
		{
			Report.Nd = FText::Format(LOCTEXT("ReportNd", "ND {0}"), FormatNumber(Package.NdDensity, 1)).ToString();
		}
	}

	if (const AUEcineRig* Rig = UEcine::GripEditing::FindRig(Camera))
	{
		Report.Support = UEcine::Rig::GetTypeName(Rig->GetSpec().Type).ToString();
	}
	return Report;
}

FUEcinePlan UEcine::ShotEditing::MakePlan(const FShot& Shot)
{
	FUEcinePlan Plan;
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr || Shot.DurationFrames <= 0)
	{
		return Plan;
	}

	const UEcine::SequenceUtils::FScopedPlayhead PutPlayheadBack;
	const int32 SampleCount = FMath::Min(Shot.DurationFrames, MaxPlanSamples);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const int32 FrameInShot = SampleCount > 1 ? FMath::RoundToInt32(static_cast<double>(SampleIndex) * (Shot.DurationFrames - 1) / (SampleCount - 1)) : 0;
		SetPlayheadFrame(OpenSequence, Shot.StartFrame + FrameInShot);

		const ACineCameraActor* Camera = FindShotCamera(Shot);
		if (Camera == nullptr)
		{
			continue;
		}
		const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
		Plan.CameraPath.Add({ FVector2D(Lens->GetComponentLocation()), Lens->GetComponentRotation().Yaw, Lens->GetHorizontalFieldOfView() });

		// What the camera runs on and what it looks at are drawn where they are when the camera is first seen.
		if (Plan.CameraPath.Num() == 1)
		{
			const AUEcineRig* Rig = UEcine::GripEditing::FindRig(Camera);
			if (Rig != nullptr && Rig->GetSpec().Type == EUEcineRigType::Dolly)
			{
				const USplineComponent* Track = Rig->GetTrack();
				const float TrackLength = Track->GetSplineLength();
				TArray<FVector2D>& TrackLine = Plan.Tracks.AddDefaulted_GetRef();
				for (int32 Step = 0; Step <= PlanTrackSteps; ++Step)
				{
					TrackLine.Add(FVector2D(Track->GetLocationAtDistanceAlongSpline(TrackLength * Step / PlanTrackSteps, ESplineCoordinateSpace::World)));
				}
			}

			const UMovieScene* ShotMovieScene = Shot.Sequence->GetMovieScene();
			for (const FMovieSceneBinding& Binding : ShotMovieScene->GetBindings())
			{
				for (const UObject* Object : ResolveBinding(Shot, Binding.GetObjectGuid()))
				{
					AddPlanSubject(Plan, Cast<AActor>(Object));
				}
			}
			AddPlanSubject(Plan, UEcine::FocusEditing::GetFollowedActor(Camera));
		}
	}
	return Plan;
}

FUEcinePaperwork UEcine::ShotEditing::BuildPaperwork()
{
	FUEcinePaperwork Paperwork;
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		return Paperwork;
	}

	Paperwork.Title = OpenSequence->GetName();
	Paperwork.Date = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
	Paperwork.Language = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName();

	const EUEcineDistanceUnits Units = UUEcineEditorSettings::GetDistanceUnits();
	const UEcine::SequenceUtils::FScopedPlayhead PutPlayheadBack;
	for (const FShot& Shot : GetShots())
	{
		// The report says what the camera is set to as the shot starts.
		SetPlayheadFrame(OpenSequence, Shot.StartFrame);
		FUEcineShotReport& Report = Paperwork.Shots.Add_GetRef(MakeShotReport(Shot));

		for (const FUEcineStoryboardFrame& Frame : GetStoryboardFrames(Shot.Sequence))
		{
			// Counted the way the shot is watched: its first frame is frame 1.
			const FText Caption = FText::Format(LOCTEXT("StoryboardCaption", "Frame {0}"), UEcine::Units::FormatNumber(Frame.ShotFrame + 1, 0));
			Report.Storyboard.Add({ Caption.ToString(), Frame.Jpeg });
		}

		const FUEcinePlanDrawing Drawing = UEcine::Plan::Layout(MakePlan(Shot), PaperworkPlanSize, Units);
		if (!Drawing.Lines.IsEmpty())
		{
			Report.PlanSvg = UEcine::Plan::WriteSvg(Drawing, FText::Format(LOCTEXT("PlanDescription", "Overhead plan of {0}"), FText::FromString(Shot.Name)).ToString());
		}
	}
	return Paperwork;
}

bool UEcine::ShotEditing::ExportPaperwork(const FString& Directory, TArray<FString>& OutFiles, FText& OutError)
{
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		OutError = GetNoSequenceOpenError();
		return false;
	}

	const FUEcinePaperwork Paperwork = BuildPaperwork();
	const FString BaseName = Directory / OpenSequence->GetName();

	struct FPaperworkFile
	{
		FString Path;
		FString Text;
		FFileHelper::EEncodingOptions Encoding;
	};
	// The spreadsheets carry a byte-order mark so that Excel reads them as UTF-8. The page says so itself.
	const FPaperworkFile Files[] =
	{
		{ BaseName + TEXT("_ShotList.csv"), UEcine::Paperwork::WriteShotListCsv(Paperwork), FFileHelper::EEncodingOptions::ForceUTF8 },
		{ BaseName + TEXT("_CameraReport.csv"), UEcine::Paperwork::WriteCameraReportCsv(Paperwork), FFileHelper::EEncodingOptions::ForceUTF8 },
		{ BaseName + TEXT("_Paperwork.html"), UEcine::Paperwork::WriteHtml(Paperwork), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM },
	};

	OutFiles.Reset();
	for (const FPaperworkFile& File : Files)
	{
		if (!FFileHelper::SaveStringToFile(File.Text, *File.Path, File.Encoding))
		{
			OutError = FText::Format(LOCTEXT("PaperworkNotWritten", "{0} could not be written. Check that the folder exists and the file is not open in another program."), FText::FromString(File.Path));
			return false;
		}
		OutFiles.Add(File.Path);
	}
	return true;
}

FString UEcine::ShotEditing::GetDefaultPaperworkDirectory(const ULevelSequence* Sequence)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEcine") / TEXT("Paperwork") / Sequence->GetName());
}

#undef LOCTEXT_NAMESPACE
