// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscShotEditing.h"

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
#include "RoscCameraDatabase.h"
#include "RoscCameraPackageComponent.h"
#include "RoscEditorSettings.h"
#include "RoscFocusEditing.h"
#include "RoscGripEditing.h"
#include "RoscRig.h"
#include "RoscSequenceUtils.h"
#include "RoscShotMetaData.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

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
		Rosc::SequenceUtils::SetPlayheadTicks(FFrameTime(FrameToTicks(OpenSequence->GetMovieScene(), Frame)));
	}

	Rosc::ShotEditing::FShot MakeShot(ULevelSequence* Sequence, UMovieSceneCinematicShotSection* Section, const FString& Name, const UMovieScene* OpenMovieScene, const TRange<FFrameNumber>& Ticks)
	{
		Rosc::ShotEditing::FShot Shot;
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
	void EditShotData(ULevelSequence* ShotSequence, const FText& Description, TFunctionRef<void(URoscShotMetaData&)> Edit)
	{
		const FScopedTransaction Transaction(Description);
		ShotSequence->Modify();
		URoscShotMetaData* Data = ShotSequence->FindOrAddMetaData<URoscShotMetaData>();
		Data->SetFlags(RF_Transactional);
		Data->Modify();
		Edit(*Data);
	}

	ACineCameraActor* FindCameraOfBinding(const Rosc::ShotEditing::FShot& Shot, const FGuid& Binding)
	{
		for (UObject* Object : Rosc::ShotEditing::ResolveBinding(Shot, Binding))
		{
			if (ACineCameraActor* Camera = Cast<ACineCameraActor>(Object))
			{
				return Camera;
			}
		}
		return nullptr;
	}

	void AddPlanSubject(FRoscPlan& Plan, const AActor* Actor)
	{
		if (Actor == nullptr || Actor->IsA<ACineCameraActor>() || Actor->IsA<ARoscRig>())
		{
			return;
		}
		const FString Name = Actor->GetActorLabel();
		if (!Plan.Subjects.ContainsByPredicate([&Name](const FRoscPlanSubject& Subject) { return Subject.Name == Name; }))
		{
			Plan.Subjects.Add({ Name, FVector2D(Actor->GetActorLocation()) });
		}
	}

	FString FormatShotDuration(int32 Frames, FFrameRate DisplayRate)
	{
		return FText::Format(LOCTEXT("ShotDuration", "{0} s ({1} {2}|plural(one=frame,other=frames))"),
			Rosc::Units::FormatNumber(DisplayRate.AsSeconds(FFrameTime(Frames)), 2), Rosc::Units::FormatNumber(Frames, 0), Frames).ToString();
	}

	FText GetNoSequenceOpenError()
	{
		return LOCTEXT("NoSequenceOpen", "No sequence is open in Sequencer. Open the sequence, or add a shot to start one.");
	}
}

TArray<Rosc::ShotEditing::FShot> Rosc::ShotEditing::GetShots()
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

FString Rosc::ShotEditing::GetScene(const ULevelSequence* ShotSequence)
{
	const URoscShotMetaData* Data = ShotSequence->FindMetaData<URoscShotMetaData>();
	return Data != nullptr ? Data->Scene : FString();
}

ERoscShotSize Rosc::ShotEditing::GetSize(const ULevelSequence* ShotSequence)
{
	const URoscShotMetaData* Data = ShotSequence->FindMetaData<URoscShotMetaData>();
	return Data != nullptr ? Data->Size : ERoscShotSize::Unset;
}

FString Rosc::ShotEditing::GetMove(const ULevelSequence* ShotSequence)
{
	const URoscShotMetaData* Data = ShotSequence->FindMetaData<URoscShotMetaData>();
	return Data != nullptr ? Data->Move : FString();
}

FString Rosc::ShotEditing::GetNotes(const ULevelSequence* ShotSequence)
{
	const UMovieSceneMetaData* Data = ShotSequence->FindMetaData<UMovieSceneMetaData>();
	return Data != nullptr ? Data->GetNotes() : FString();
}

void Rosc::ShotEditing::SetScene(ULevelSequence* ShotSequence, const FString& Scene)
{
	if (GetScene(ShotSequence) != Scene)
	{
		EditShotData(ShotSequence, LOCTEXT("SetShotScene", "Change Shot Scene"), [&Scene](URoscShotMetaData& Data) { Data.Scene = Scene; });
	}
}

void Rosc::ShotEditing::SetSize(ULevelSequence* ShotSequence, ERoscShotSize Size)
{
	if (GetSize(ShotSequence) != Size)
	{
		EditShotData(ShotSequence, LOCTEXT("SetShotSize", "Change Shot Size"), [Size](URoscShotMetaData& Data) { Data.Size = Size; });
	}
}

void Rosc::ShotEditing::SetMove(ULevelSequence* ShotSequence, const FString& Move)
{
	if (GetMove(ShotSequence) != Move)
	{
		EditShotData(ShotSequence, LOCTEXT("SetShotMove", "Change Shot Move"), [&Move](URoscShotMetaData& Data) { Data.Move = Move; });
	}
}

void Rosc::ShotEditing::SetNotes(ULevelSequence* ShotSequence, const FString& Notes)
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

ULevelSequence* Rosc::ShotEditing::FindOrOpenSequence(const ACineCameraActor* Camera, FText& OutError)
{
	using namespace Rosc::SequenceUtils;

	ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		OpenSequence = CreateSequence(FString(SequenceFolder) / TEXT("Sequence"), GetCameraFrameRate(Camera));
		if (OpenSequence == nullptr || !ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(OpenSequence))
		{
			OutError = LOCTEXT("SequenceNotMade", "A sequence for the shots could not be made. Check that the project's Content folder can be written to.");
			return nullptr;
		}
	}
	return OpenSequence;
}

bool Rosc::ShotEditing::AddShot(ACineCameraActor* Camera, FText& OutError)
{
	using namespace Rosc::SequenceUtils;

	// Before the undoable step begins: an asset made inside a transaction leaves the transaction open.
	ULevelSequence* OpenSequence = FindOrOpenSequence(Camera, OutError);
	if (OpenSequence == nullptr)
	{
		return false;
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
	NotifySequencer(true);
	return true;
}

void Rosc::ShotEditing::GoToShot(const FShot& Shot)
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

int32 Rosc::ShotEditing::FindShotAtPlayhead(TConstArrayView<FShot> Shots)
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

ACineCameraActor* Rosc::ShotEditing::FindShotCamera(const FShot& Shot)
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

TArray<UObject*> Rosc::ShotEditing::ResolveBinding(const FShot& Shot, const FGuid& Binding)
{
	const FMovieSceneSequenceID SequenceID = Shot.Section != nullptr ? Shot.Section->GetSequenceID() : MovieSceneSequenceID::Root;
	return ULevelSequenceEditorBlueprintLibrary::GetBoundObjects(UE::MovieScene::FFixedObjectBindingID(Binding, SequenceID));
}

TConstArrayView<FRoscStoryboardFrame> Rosc::ShotEditing::GetStoryboardFrames(const ULevelSequence* ShotSequence)
{
	const URoscShotMetaData* Data = ShotSequence->FindMetaData<URoscShotMetaData>();
	return Data != nullptr ? TConstArrayView<FRoscStoryboardFrame>(Data->StoryboardFrames) : TConstArrayView<FRoscStoryboardFrame>();
}

bool Rosc::ShotEditing::TakeStoryboardPicture(const ACineCameraActor& Camera, FRoscStoryboardFrame& OutFrame)
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
	// Without a rendering state of its own the capture has no exposure, and the camera's ISO,
	// shutter, stop and ND go unused: a daylight stage reads as pure white.
	Capture->bAlwaysPersistRenderingState = true;
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

bool Rosc::ShotEditing::GrabStoryboardFrame(const FShot& Shot, FText& OutError)
{
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		OutError = GetNoSequenceOpenError();
		return false;
	}

	FRoscStoryboardFrame Frame;
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

	EditShotData(Shot.Sequence, LOCTEXT("GrabStoryboardFrame", "Grab Storyboard Frame"), [&Frame](URoscShotMetaData& Data)
	{
		// One picture per frame of the shot: grabbing a frame again replaces its picture.
		Data.StoryboardFrames.RemoveAll([&Frame](const FRoscStoryboardFrame& Existing) { return Existing.ShotFrame == Frame.ShotFrame; });
		const int32 Index = Algo::LowerBoundBy(Data.StoryboardFrames, Frame.ShotFrame, &FRoscStoryboardFrame::ShotFrame);
		Data.StoryboardFrames.Insert(MoveTemp(Frame), Index);
	});
	return true;
}

void Rosc::ShotEditing::RemoveStoryboardFrame(ULevelSequence* ShotSequence, int32 FrameIndex)
{
	if (GetStoryboardFrames(ShotSequence).IsValidIndex(FrameIndex))
	{
		EditShotData(ShotSequence, LOCTEXT("RemoveStoryboardFrame", "Remove Storyboard Frame"), [FrameIndex](URoscShotMetaData& Data)
		{
			Data.StoryboardFrames.RemoveAt(FrameIndex);
		});
	}
}

FRoscShotReport Rosc::ShotEditing::MakeShotReport(const FShot& Shot)
{
	using Rosc::Units::FormatNumber;

	FRoscShotReport Report;
	Report.Scene = GetScene(Shot.Sequence);
	Report.Shot = Shot.Name;
	const ERoscShotSize Size = GetSize(Shot.Sequence);
	if (Size != ERoscShotSize::Unset)
	{
		Report.Size = Rosc::Shots::GetShotSizeName(Size).ToString();
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

	const ERoscDistanceUnits Units = URoscEditorSettings::GetDistanceUnits();
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	Report.Camera = Camera->GetActorLabel();
	Report.FocalLength = FText::Format(LOCTEXT("ReportFocalLength", "{0} mm"), FormatNumber(Lens->CurrentFocalLength, 0)).ToString();
	Report.TStop = FText::Format(LOCTEXT("ReportTStop", "T{0}"), FormatNumber(Lens->CurrentAperture, 1)).ToString();
	Report.Focus = Rosc::Units::FormatDistance(Rosc::FocusEditing::GetLensDistance(Camera) * 10.0, Units).ToString();

	if (const URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(Camera))
	{
		const FRoscCameraPackage& Package = Component->Package;
		const FRoscCameraDatabase& Database = FRoscCameraDatabase::Get();
		if (const FRoscCameraBody* Body = Database.FindBody(Package.BodyId))
		{
			Report.Body = Body->GetDisplayName();
			if (const FRoscRecordingFormat* Format = Body->FindFormat(Package.FormatId))
			{
				Report.Format = Format->Name;
			}
		}
		if (const FRoscLensKit* Kit = Database.FindLensKit(Package.LensKitId))
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

	if (const ARoscRig* Rig = Rosc::GripEditing::FindRig(Camera))
	{
		Report.Support = Rosc::Rig::GetTypeName(Rig->GetSpec().Type).ToString();
	}
	return Report;
}

FRoscPlan Rosc::ShotEditing::MakePlan(const FShot& Shot)
{
	FRoscPlan Plan;
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr || Shot.DurationFrames <= 0)
	{
		return Plan;
	}

	const Rosc::SequenceUtils::FScopedPlayhead PutPlayheadBack;
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
			const ARoscRig* Rig = Rosc::GripEditing::FindRig(Camera);
			if (Rig != nullptr && Rig->GetSpec().Type == ERoscRigType::Dolly)
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
			AddPlanSubject(Plan, Rosc::FocusEditing::GetFollowedActor(Camera));
		}
	}
	return Plan;
}

FRoscPaperwork Rosc::ShotEditing::BuildPaperwork()
{
	FRoscPaperwork Paperwork;
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		return Paperwork;
	}

	Paperwork.Title = OpenSequence->GetName();
	Paperwork.Date = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
	Paperwork.Language = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName();

	const ERoscDistanceUnits Units = URoscEditorSettings::GetDistanceUnits();
	const Rosc::SequenceUtils::FScopedPlayhead PutPlayheadBack;
	for (const FShot& Shot : GetShots())
	{
		// The report says what the camera is set to as the shot starts.
		SetPlayheadFrame(OpenSequence, Shot.StartFrame);
		FRoscShotReport& Report = Paperwork.Shots.Add_GetRef(MakeShotReport(Shot));

		for (const FRoscStoryboardFrame& Frame : GetStoryboardFrames(Shot.Sequence))
		{
			// Counted the way the shot is watched: its first frame is frame 1.
			const FText Caption = FText::Format(LOCTEXT("StoryboardCaption", "Frame {0}"), Rosc::Units::FormatNumber(Frame.ShotFrame + 1, 0));
			Report.Storyboard.Add({ Caption.ToString(), Frame.Jpeg });
		}

		const FRoscPlanDrawing Drawing = Rosc::Plan::Layout(MakePlan(Shot), PaperworkPlanSize, Units);
		if (!Drawing.Lines.IsEmpty())
		{
			Report.PlanSvg = Rosc::Plan::WriteSvg(Drawing, FText::Format(LOCTEXT("PlanDescription", "Overhead plan of {0}"), FText::FromString(Shot.Name)).ToString());
		}
	}
	return Paperwork;
}

bool Rosc::ShotEditing::ExportPaperwork(const FString& Directory, TArray<FString>& OutFiles, FText& OutError)
{
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	if (OpenSequence == nullptr)
	{
		OutError = GetNoSequenceOpenError();
		return false;
	}

	const FRoscPaperwork Paperwork = BuildPaperwork();
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
		{ BaseName + TEXT("_ShotList.csv"), Rosc::Paperwork::WriteShotListCsv(Paperwork), FFileHelper::EEncodingOptions::ForceUTF8 },
		{ BaseName + TEXT("_CameraReport.csv"), Rosc::Paperwork::WriteCameraReportCsv(Paperwork), FFileHelper::EEncodingOptions::ForceUTF8 },
		{ BaseName + TEXT("_Paperwork.html"), Rosc::Paperwork::WriteHtml(Paperwork), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM },
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

FString Rosc::ShotEditing::GetPaperworkFolder()
{
	const FString& Chosen = GetDefault<URoscEditorSettings>()->PaperworkFolder.Path;
	return FPaths::ConvertRelativePathToFull(Chosen.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Rosc") / TEXT("Paperwork") : Chosen);
}

void Rosc::ShotEditing::SetPaperworkFolder(const FString& Folder)
{
	URoscEditorSettings* Choices = GetMutableDefault<URoscEditorSettings>();
	Choices->PaperworkFolder.Path = Folder;
	Choices->SaveConfig();
}

FString Rosc::ShotEditing::GetPaperworkDirectory(const ULevelSequence* Sequence)
{
	return GetPaperworkFolder() / Sequence->GetName();
}

FGuid Rosc::ShotEditing::GetShotSignature(const ULevelSequence* Sequence)
{
	const UMovieScene* MovieScene = Sequence != nullptr ? Sequence->GetMovieScene() : nullptr;
	return MovieScene != nullptr ? MovieScene->GetSignature() : FGuid();
}

#undef LOCTEXT_NAMESPACE
