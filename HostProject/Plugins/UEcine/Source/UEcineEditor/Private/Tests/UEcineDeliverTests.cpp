#include "CineCameraActor.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieSceneToolsProjectSettings.h"
#include "ObjectTools.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "Tests/UEcineRenderedFiles.h"
#include "UEcineBurnInWidget.h"
#include "UEcineCameraEditing.h"
#include "UEcineRenderSubsystem.h"
#include "UEcineShotEditing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** What a render test holds on to while the render runs over the editor's frames. */
	struct FDeliverTestState
	{
		TArray<TWeakObjectPtr<AActor>> SpawnedActors;
		TArray<TWeakObjectPtr<ULevelSequence>> CreatedSequences;
		TArray<UEcine::ShotEditing::FShot> Shots;
		float ShotSecondsBefore = 0.0f;
		TRange<FFrameNumber> PlaybackRangeBefore;
		TArray<TRange<FFrameNumber>> ShotRangesBefore;
		int32 BurnInUpdatesBefore = 0;
		FString OutputDirectory;
		double StartSeconds = 0.0;
	};

	void CleanUpDeliverTest(FAutomationTestBase& Test, FDeliverTestState& State)
	{
		GetMutableDefault<UMovieSceneToolsProjectSettings>()->DefaultDuration = State.ShotSecondsBefore;
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		GEditor->SelectNone(false, true, false);
		for (const TWeakObjectPtr<AActor>& Actor : State.SpawnedActors)
		{
			if (Actor.IsValid())
			{
				Actor->GetWorld()->EditorDestroyActor(Actor.Get(), false);
			}
		}
		GEditor->ResetTransaction(FText::FromString(TEXT("UEcine deliver test")));
		for (const TWeakObjectPtr<ULevelSequence>& Sequence : State.CreatedSequences)
		{
			if (Sequence.IsValid())
			{
				Test.TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence.Get(), false));
			}
		}
	}

	/** The mean brightness of a band of rows of a picture, from 0 to 1 of the way down it. */
	double MeasureBandBrightness(const FImage& Picture, double FromPart, double ToPart)
	{
		const TArrayView64<const FLinearColor> Pixels = Picture.AsRGBA32F();
		const int32 FirstRow = FMath::Clamp(FMath::FloorToInt32(FromPart * Picture.SizeY), 0, Picture.SizeY - 1);
		const int32 LastRow = FMath::Clamp(FMath::CeilToInt32(ToPart * Picture.SizeY) - 1, FirstRow, Picture.SizeY - 1);
		double Sum = 0.0;
		for (int32 Row = FirstRow; Row <= LastRow; ++Row)
		{
			for (int32 Column = 0; Column < Picture.SizeX; ++Column)
			{
				const FLinearColor& Pixel = Pixels[static_cast<int64>(Row) * Picture.SizeX + Column];
				Sum += (Pixel.R + Pixel.G + Pixel.B) / 3.0;
			}
		}
		return Sum / (static_cast<double>(LastRow - FirstRow + 1) * Picture.SizeX);
	}

	/** Reads a layer of a rendered OpenEXR file, and keeps a copy to look at as a PNG beside the test pictures. */
	bool LoadRenderedLayer(const FString& Filename, const TCHAR* Layer, const TCHAR* LookAtName, FImage& OutPicture)
	{
		if (!UEcine::RenderedFiles::ReadExrLayer(Filename, Layer, OutPicture))
		{
			return false;
		}
		FImage ToLookAt;
		OutPicture.CopyTo(ToLookAt, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FImageUtils::SaveImageByExtension(*(FPaths::ProjectSavedDir() / TEXT("UEcine/Screenshots") / LookAtName), ToLookAt);
		return true;
	}

	/** The mean of a picture's alpha over a band of its rows, from 0 to 1 of the way down it. */
	double MeasureBandCover(const FImage& Picture, double FromPart, double ToPart)
	{
		const TArrayView64<const FLinearColor> Pixels = Picture.AsRGBA32F();
		const int32 FirstRow = FMath::Clamp(FMath::FloorToInt32(FromPart * Picture.SizeY), 0, Picture.SizeY - 1);
		const int32 LastRow = FMath::Clamp(FMath::CeilToInt32(ToPart * Picture.SizeY) - 1, FirstRow, Picture.SizeY - 1);
		double Sum = 0.0;
		for (int64 Index = static_cast<int64>(FirstRow) * Picture.SizeX; Index < static_cast<int64>(LastRow + 1) * Picture.SizeX; ++Index)
		{
			Sum += Pixels[Index].A;
		}
		return Sum / (static_cast<double>(LastRow - FirstRow + 1) * Picture.SizeX);
	}
}

/**
 * Spike #3 (ADR-008): two shots of a sequence are rendered in one job through generated graphs,
 * the first to multilayer EXR with every pass, the second to Apple ProRes, each with a slate and
 * burn-ins, and the files are read back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverRenderTwoShotsTest, "UEcine.Editor.Deliver.RenderTwoShots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverRenderTwoShotsTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::ShotEditing;

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: rendering needs a renderer (tools/test.ps1 -Rendering)."));
		return true;
	}

	// The engine logs this for every graph render started in the editor: it asks for the version
	// number before it has the graph to read it from (LESSON-API-010). The render is not affected.
	AddExpectedError(TEXT("Cannot resolve version number without a valid evaluated graph"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Script call stack"), EAutomationExpectedErrorFlags::Contains, 0);

	const TSharedRef<FDeliverTestState> State = MakeShared<FDeliverTestState>();
	UMovieSceneToolsProjectSettings* SequencerSettings = GetMutableDefault<UMovieSceneToolsProjectSettings>();
	State->ShotSecondsBefore = SequencerSettings->DefaultDuration;
	// Six frames a shot keep the render short.
	constexpr int32 ShotFrames = 6;
	SequencerSettings->DefaultDuration = ShotFrames / 24.0f;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
	ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform(FRotator(-15.0, 0.0, 0.0), FVector(-500.0, 0.0, 250.0)));
	AStaticMeshActor* Cube = World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, 60.0), FRotator(0.0, 30.0, 0.0));
	State->SpawnedActors = { Camera, Cube };
	if (!TestNotNull(TEXT("A camera"), Camera) || !TestNotNull(TEXT("A cube"), Cube))
	{
		CleanUpDeliverTest(*this, *State);
		return false;
	}
	Cube->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));

	FText Error;
	const bool bShotsAdded = AddShot(Camera, Error) && AddShot(Camera, Error);
	ULevelSequence* Cut = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	State->Shots = GetShots();
	State->CreatedSequences.Add(Cut);
	for (const FShot& Shot : State->Shots)
	{
		State->CreatedSequences.AddUnique(Shot.Sequence);
	}
	if (!TestTrue(TEXT("Two shots to render"), bShotsAdded && State->Shots.Num() == 2))
	{
		CleanUpDeliverTest(*this, *State);
		return false;
	}

	State->OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEcine/Renders/RenderTwoShots"));
	IFileManager::Get().DeleteDirectory(*State->OutputDirectory, false, true);

	TArray<FUEcineRenderShot> RenderShots;
	FUEcineRenderShot& Layers = RenderShots.AddDefaulted_GetRef();
	Layers.ShotName = State->Shots[0].Name;
	Layers.Spec.OutputDirectory = State->OutputDirectory;
	Layers.Spec.FileName = State->Shots[0].Name + TEXT("_vfx.{frame_number}");
	Layers.Spec.Resolution = FIntPoint(640, 360);
	Layers.Spec.Format = EUEcineRenderFormat::MultilayerExr;
	Layers.Spec.bSceneLinear = true;
	Layers.Spec.bDepth = Layers.Spec.bNormals = Layers.Spec.bMotionVectors = Layers.Spec.bMattes = true;
	Layers.Spec.bBurnIn = Layers.Spec.bSlate = true;
	Layers.Spec.Metadata = { { TEXT("uecine/shot"), State->Shots[0].Name }, { TEXT("uecine/lens/kit"), TEXT("First Shot Primes") } };

	FUEcineRenderShot& Movie = RenderShots.AddDefaulted_GetRef();
	Movie.ShotName = State->Shots[1].Name;
	Movie.Spec.OutputDirectory = State->OutputDirectory;
	Movie.Spec.FileName = State->Shots[1].Name + TEXT("_review");
	Movie.Spec.Resolution = FIntPoint(640, 360);
	Movie.Spec.Format = EUEcineRenderFormat::ProRes422LT;
	Movie.Spec.bBurnIn = Movie.Spec.bSlate = true;
	Movie.Spec.Metadata = { { TEXT("uecine/shot"), State->Shots[1].Name }, { TEXT("uecine/lens/kit"), TEXT("Second Shot Zoom") } };

	State->PlaybackRangeBefore = Cut->GetMovieScene()->GetPlaybackRange();
	for (const FShot& Shot : State->Shots)
	{
		State->ShotRangesBefore.Add(Shot.Section->GetRange());
	}
	State->BurnInUpdatesBefore = UUEcineBurnInWidget::GetUpdateCount();
	State->StartSeconds = FPlatformTime::Seconds();

	UUEcineRenderSubsystem* Render = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>();
	if (!TestTrue(TEXT("The render starts"), Render->StartRender(Cut, RenderShots, Error)))
	{
		AddError(Error.ToString());
		CleanUpDeliverTest(*this, *State);
		return false;
	}
	TestTrue(TEXT("A render is running"), Render->IsRendering());
	TestFalse(TEXT("A second render is refused while one runs"), Render->StartRender(Cut, RenderShots, Error));

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State, Render, ShotFrames]
	{
		constexpr double TimeoutSeconds = 600.0;
		if (Render->IsRendering() && FPlatformTime::Seconds() - State->StartSeconds < TimeoutSeconds)
		{
			return false;
		}
		if (Render->IsRendering())
		{
			AddError(TEXT("The render did not finish in ten minutes"));
			Render->CancelRender();
			CleanUpDeliverTest(*this, *State);
			return true;
		}

		using namespace UEcine::RenderedFiles;
		const FUEcineRenderResult& Result = Render->GetLastResult();
		AddInfo(FString::Printf(TEXT("The render took %.0f s and wrote %d files"), FPlatformTime::Seconds() - State->StartSeconds, Result.Files.Num()));
		TestTrue(TEXT("The render succeeded"), Result.bSuccess);

		// A slated shot is one frame longer at each end.
		constexpr int32 SlatedFrames = ShotFrames + 2;
		TArray<FString> ExrFiles = Result.Files.FilterByPredicate([](const FString& File) { return File.EndsWith(TEXT(".exr")); });
		const TArray<FString> MovFiles = Result.Files.FilterByPredicate([](const FString& File) { return File.EndsWith(TEXT(".mov")); });
		ExrFiles.Sort();
		TestEqual(TEXT("The first shot is one EXR file a frame"), ExrFiles.Num(), SlatedFrames);
		TestEqual(TEXT("The second shot is one movie file"), MovFiles.Num(), 1);
		for (const FString& File : Result.Files)
		{
			TestTrue(*FString::Printf(TEXT("%s is on disk, in the folder asked for"), *File), IFileManager::Get().FileExists(*File) && FPaths::IsUnderDirectory(File, State->OutputDirectory));
		}
		TestEqual(TEXT("The burn-in widget is told about every frame of both shots"), UUEcineBurnInWidget::GetUpdateCount() - State->BurnInUpdatesBefore, 2 * SlatedFrames);

		if (ExrFiles.Num() == SlatedFrames)
		{
			TestTrue(TEXT("The EXR files are named for their shot"), FPaths::GetCleanFilename(ExrFiles[0]).StartsWith(State->Shots[0].Name + TEXT("_vfx.")));

			FExrHeader Header;
			if (TestTrue(TEXT("An EXR file's header is read"), ReadExrHeader(ExrFiles[3], Header)))
			{
				AddInfo(FString::Printf(TEXT("EXR %d x %d, %d parts, layers: %s"), Header.Size.X, Header.Size.Y, Header.PartCount, *FString::Join(GetExrLayers(Header), TEXT(" | "))));
				TArray<FString> MetadataNames;
				Header.Text.GetKeys(MetadataNames);
				AddInfo(FString::Printf(TEXT("EXR text attributes: %s"), *FString::Join(MetadataNames, TEXT(" | "))));
				TestEqual(TEXT("The EXR is the size asked for"), Header.Size, FIntPoint(640, 360));
				TestEqual(TEXT("The EXR says which shot it is"), Header.Text.FindRef(TEXT("uecine/shot")), State->Shots[0].Name);
				TestEqual(TEXT("The EXR carries the first shot's metadata, not the second's"), Header.Text.FindRef(TEXT("uecine/lens/kit")), FString(TEXT("First Shot Primes")));
			}

			// In a file of layers the slate and the burn-ins are a layer of their own, over nothing.
			FImage Slate;
			FImage BurnIn;
			FImage Picture;
			FImage Depth;
			if (TestTrue(TEXT("The slate frame's burn-in layer is read"), LoadRenderedLayer(ExrFiles[0], TEXT("BurnIn"), TEXT("deliver-spike-slate.png"), Slate))
				&& TestTrue(TEXT("A picture frame's burn-in layer is read"), LoadRenderedLayer(ExrFiles[3], TEXT("BurnIn"), TEXT("deliver-spike-burn-in.png"), BurnIn))
				&& TestTrue(TEXT("A picture frame is read"), LoadRenderedLayer(ExrFiles[3], TEXT("FinalImage"), TEXT("deliver-spike-picture.png"), Picture))
				&& TestTrue(TEXT("A picture frame's depth is read"), LoadRenderedLayer(ExrFiles[3], TEXT("Deferred_depth"), TEXT("deliver-spike-depth.png"), Depth)))
			{
				const double SlateMiddle = MeasureBandCover(Slate, 0.45, 0.55);
				const double BurnInMiddle = MeasureBandCover(BurnIn, 0.45, 0.55);
				const double BurnInStrip = MeasureBandCover(BurnIn, 0.97, 1.0);
				const double PictureMiddle = MeasureBandBrightness(Picture, 0.45, 0.55);
				const FLinearColor DepthAtCentre = Depth.AsRGBA32F()[static_cast<int64>(Depth.SizeY / 2) * Depth.SizeX + Depth.SizeX / 2];
				AddInfo(FString::Printf(TEXT("Cover: slate middle %.3f; burn-in middle %.3f, strip %.3f. Picture middle brightness %.3f. Depth at the centre %.1f"), SlateMiddle, BurnInMiddle, BurnInStrip, PictureMiddle, DepthAtCentre.R));
				TestTrue(TEXT("The first frame is the slate: it covers the middle of the picture"), SlateMiddle > 0.9);
				TestTrue(TEXT("A later frame has the burn-in strip along the bottom and nothing over the middle"), BurnInStrip > 0.5 && BurnInMiddle < 0.05);
				TestTrue(TEXT("The picture is not empty"), PictureMiddle > 0.01);
				// The camera looks down at a cube about five metres away.
				TestTrue(TEXT("The depth layer measures the distance to what the camera sees, in centimetres"), DepthAtCentre.R > 300.0f && DepthAtCentre.R < 800.0f);
			}
		}
		if (MovFiles.Num() == 1)
		{
			TestEqual(TEXT("The movie is named for its shot"), FPaths::GetCleanFilename(MovFiles[0]), State->Shots[1].Name + TEXT("_review.mov"));
			TestEqual(TEXT("The movie holds the slate, the shot and the tail frame"), CountMovFrames(MovFiles[0]), SlatedFrames);
		}

		// The sequence is as it was.
		const ULevelSequence* Cut = State->CreatedSequences[0].Get();
		if (TestNotNull(TEXT("The sequence is still there"), Cut))
		{
			TestTrue(TEXT("The sequence's playback range is unchanged"), Cut->GetMovieScene()->GetPlaybackRange() == State->PlaybackRangeBefore);
			for (int32 Index = 0; Index < State->Shots.Num(); ++Index)
			{
				TestTrue(*FString::Printf(TEXT("Shot %d is where it was"), Index + 1), State->Shots[Index].Section->GetRange() == State->ShotRangesBefore[Index]);
			}
		}

		CleanUpDeliverTest(*this, *State);
		return true;
	}));
	return true;
}

#endif
