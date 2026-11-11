#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/RendererSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "MovieSceneToolsProjectSettings.h"
#include "ObjectTools.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/UEcineDeliverChoicesBefore.h"
#include "Tests/UEcineRenderedFiles.h"
#include "UEcineBurnInWidget.h"
#include "UEcineCameraEditing.h"
#include "UEcineCameraPackageComponent.h"
#include "UEcineDeliverEditing.h"
#include "UEcineEditorSettings.h"
#include "UEcineHandoff.h"
#include "UEcineMonitorSubsystem.h"
#include "UEcineRenderSubsystem.h"
#include "UEcineSequenceUtils.h"
#include "UEcineShotEditing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Six frames a shot keep the renders short. */
	constexpr int32 DeliverTestShotFrames = 6;

	/** What a deliver test holds on to while its renders run over the editor's frames. */
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

		/** The user's own delivery choices and monitor tools, to put back. */
		TOptional<FUEcineDeliverChoicesBefore> ChoicesBefore;
		TOptional<FUEcineMonitorSettings> MonitorBefore;

		/** The render being waited for, as an index into the test's stages. */
		int32 Stage = INDEX_NONE;
		double StartSeconds = 0.0;
		/** The furthest the render being waited for has said it is, from 0 to 1. */
		float ProgressSeen = 0.0f;
		double TimeoutSeconds = 600.0;
		UEcine::DeliverEditing::FDelivery Delivery;
		/** A frame of the first VFX handoff, to hold the second against. */
		FImage PictureBefore;
	};

	ACineCameraActor* GetDeliverTestCamera(const FDeliverTestState& State)
	{
		return State.SpawnedActors.IsEmpty() ? nullptr : Cast<ACineCameraActor>(State.SpawnedActors[0].Get());
	}

	void CleanUpDeliverTest(FAutomationTestBase& Test, FDeliverTestState& State)
	{
		GetMutableDefault<UMovieSceneToolsProjectSettings>()->DefaultDuration = State.ShotSecondsBefore;
		if (State.ChoicesBefore.IsSet())
		{
			State.ChoicesBefore->PutBack();
		}
		if (State.MonitorBefore.IsSet())
		{
			UUEcineMonitorSubsystem::Get()->SetSettings(State.MonitorBefore.GetValue());
		}

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

		// The handoff copies a VFX handoff made go first: they play the sequences they were made from.
		TArray<ULevelSequence*> ToRemove;
		for (const TWeakObjectPtr<ULevelSequence>& Sequence : State.CreatedSequences)
		{
			if (Sequence.IsValid())
			{
				const FString CopyPackage = UEcine::Handoff::GetHandoffPackageName(Sequence.Get());
				if (ULevelSequence* Copy = FindObject<ULevelSequence>(nullptr, *(CopyPackage + TEXT(".") + FPackageName::GetLongPackageAssetName(CopyPackage))))
				{
					ToRemove.Add(Copy);
				}
			}
		}
		for (const TWeakObjectPtr<ULevelSequence>& Sequence : State.CreatedSequences)
		{
			if (Sequence.IsValid())
			{
				ToRemove.Add(Sequence.Get());
			}
		}
		for (ULevelSequence* Sequence : ToRemove)
		{
			Test.TestTrue(TEXT("The test's sequence asset is removed"), ObjectTools::DeleteSingleObject(Sequence, false));
		}
	}

	/**
	 * A camera shooting ALEXA 35 in HD for a 2.39:1 frame, looking down at a cube, and so many
	 * six-frame shots of it in a new sequence. With no shots, no sequence is open.
	 */
	bool MakeDeliverTestScene(FAutomationTestBase& Test, FDeliverTestState& State, int32 ShotCount)
	{
		UMovieSceneToolsProjectSettings* SequencerSettings = GetMutableDefault<UMovieSceneToolsProjectSettings>();
		State.ShotSecondsBefore = SequencerSettings->DefaultDuration;
		SequencerSettings->DefaultDuration = DeliverTestShotFrames / 24.0f;

		UWorld* World = GEditor->GetEditorWorldContext().World();
		ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();
		ACineCameraActor* Camera = UEcine::CameraEditing::SpawnCamera(World, FTransform(FRotator(-15.0, 0.0, 0.0), FVector(-500.0, 0.0, 250.0)));
		AStaticMeshActor* Cube = World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, 60.0), FRotator(0.0, 30.0, 0.0));
		State.SpawnedActors = { Camera, Cube };
		if (!Test.TestNotNull(TEXT("A camera"), Camera) || !Test.TestNotNull(TEXT("A cube"), Cube))
		{
			return false;
		}
		Camera->SetActorLabel(TEXT("A Cam"));
		Cube->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));

		// The smallest recording format in the database, delivered in a frame much wider than it.
		FText Error;
		UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(Camera);
		if (!Test.TestNotNull(TEXT("The camera has a camera package"), Package)
			|| !Test.TestTrue(TEXT("The camera is set to shoot HD for a 2.39:1 frame"), UEcine::CameraEditing::EditPackage(Package, FText::FromString(TEXT("Choose Format")), [](FUEcineCameraPackage& Edited)
			{
				Edited.BodyId = TEXT("arri-alexa-35");
				Edited.FormatId = TEXT("hd-16x9-s16");
				Edited.DeliveryAspect = 2.39f;
			}, Error)))
		{
			Test.AddError(Error.ToString());
			return false;
		}

		for (int32 ShotIndex = 0; ShotIndex < ShotCount; ++ShotIndex)
		{
			if (!UEcine::ShotEditing::AddShot(Camera, Error))
			{
				Test.AddError(Error.ToString());
				return false;
			}
		}
		if (ShotCount == 0)
		{
			return true;
		}
		State.Shots = UEcine::ShotEditing::GetShots();
		State.CreatedSequences.Add(ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence());
		for (const UEcine::ShotEditing::FShot& Shot : State.Shots)
		{
			State.CreatedSequences.AddUnique(Shot.Sequence);
		}
		return Test.TestEqual(TEXT("The shots to deliver"), State.Shots.Num(), ShotCount);
	}

	/** Sends the test's deliveries to a folder of its own, emptied, with the delivery choices as they are when first installed. */
	void UseDeliverTestFolder(FDeliverTestState& State, const TCHAR* Name)
	{
		State.ChoicesBefore.Emplace();

		State.OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEcine/Renders") / Name);
		IFileManager::Get().DeleteDirectory(*State.OutputDirectory, false, true);
		UEcine::DeliverEditing::EditChoices([&State](UUEcineEditorSettings& Edited)
		{
			Edited.DeliverFolder.Path = State.OutputDirectory;
			Edited.DeliverNamePattern = UEcine::Deliver::DefaultNamePattern;
			Edited.bDeliverSlate = true;
			Edited.bDeliverBurnIn = true;
		});
	}

	/** One render of a test: started, waited for, then looked at. */
	struct FDeliverTestStage
	{
		/** Starts the render. False ends the test here. */
		TFunction<bool()> Start;
		/** Looks at what the render made, once it is over. */
		TFunction<void()> Check;
	};

	/** Runs a test's renders one after another over the editor's frames, then cleans up. */
	void RunDeliverTestStages(FAutomationTestBase& Test, const TSharedRef<FDeliverTestState>& State, TArray<FDeliverTestStage> Stages)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test = &Test, State, Stages = MoveTemp(Stages)]
		{
			UUEcineRenderSubsystem* Render = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>();
			if (Render->IsRendering())
			{
				State->ProgressSeen = FMath::Max(State->ProgressSeen, Render->GetProgress());
				if (FPlatformTime::Seconds() - State->StartSeconds < State->TimeoutSeconds)
				{
					return false;
				}
				Test->AddError(FString::Printf(TEXT("Render %d did not finish in %.0f seconds"), State->Stage + 1, State->TimeoutSeconds));
				Render->CancelRender();
				CleanUpDeliverTest(*Test, *State);
				return true;
			}

			if (Stages.IsValidIndex(State->Stage))
			{
				Test->AddInfo(FString::Printf(TEXT("Render %d took %.0f s and wrote %d files"), State->Stage + 1, FPlatformTime::Seconds() - State->StartSeconds, Render->GetLastResult().Files.Num()));
				Stages[State->Stage].Check();
			}
			++State->Stage;
			State->StartSeconds = FPlatformTime::Seconds();
			State->ProgressSeen = 0.0f;
			if (!Stages.IsValidIndex(State->Stage) || !Stages[State->Stage].Start())
			{
				CleanUpDeliverTest(*Test, *State);
				return true;
			}
			return false;
		}));
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

	/** How far apart two pictures of one size are: the mean difference of their pixels as a part of their mean brightness. */
	double MeasurePictureDifference(const FImage& First, const FImage& Second)
	{
		const TArrayView64<const FLinearColor> FirstPixels = First.AsRGBA32F();
		const TArrayView64<const FLinearColor> SecondPixels = Second.AsRGBA32F();
		double Difference = 0.0;
		double Brightness = 0.0;
		for (int64 Index = 0; Index < FirstPixels.Num(); ++Index)
		{
			const FLinearColor& A = FirstPixels[Index];
			const FLinearColor& B = SecondPixels[Index];
			Difference += (FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B)) / 3.0;
			Brightness += (A.R + A.G + A.B) / 3.0;
		}
		return Brightness > 0.0 ? Difference / Brightness : 0.0;
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

	/** True when a file holds a run of bytes: the four letters that name a movie's codec, or a word in an FBX file. */
	bool FileHoldsText(const FString& Filename, const ANSICHAR* Text)
	{
		TArray64<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Filename))
		{
			return false;
		}
		const int64 Length = FCStringAnsi::Strlen(Text);
		for (int64 Index = 0; Index + Length <= Bytes.Num(); ++Index)
		{
			if (Bytes[Index] == static_cast<uint8>(Text[0]) && FMemory::Memcmp(&Bytes[Index], Text, Length) == 0)
			{
				return true;
			}
		}
		return false;
	}

	/** The name a shot's files are delivered under, as the default pattern writes it. */
	FString GetDeliveredName(const FDeliverTestState& State, int32 ShotIndex, const TCHAR* IntentToken)
	{
		return FString::Printf(TEXT("%s_%s_%s_v%03d"), *State.CreatedSequences[0]->GetName(), *State.Shots[ShotIndex].Name, IntentToken, State.Delivery.Version);
	}

	const FUEcinePreflightFinding* FindFinding(const UEcine::DeliverEditing::FCheck& Check, EUEcinePreflightCheck Kind, const FString& Shot)
	{
		return Check.Findings.FindByPredicate([Kind, &Shot](const FUEcinePreflightFinding& Finding) { return Finding.Check == Kind && Finding.Shot == Shot; });
	}

	bool StopsRender(const UEcine::DeliverEditing::FCheck& Check)
	{
		return Check.Findings.ContainsByPredicate([](const FUEcinePreflightFinding& Finding) { return Finding.bStopsRender; });
	}

	/** Looks at a VFX handoff once rendered: frames, layers, metadata, sidecar and camera file of every shot. */
	void CheckVfxHandoff(FAutomationTestBase& Test, const FDeliverTestState& State, FImage& OutPicture)
	{
		using namespace UEcine::RenderedFiles;

		const FUEcineRenderResult& Result = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->GetLastResult();
		const FString Version = FString::Printf(TEXT("VFX handoff v%03d"), State.Delivery.Version);
		Test.TestTrue(*FString::Printf(TEXT("%s: the render succeeded"), *Version), Result.bSuccess);
		Test.TestEqual(*FString::Printf(TEXT("%s: one file a frame is rendered"), *Version), Result.Files.Num(), State.Shots.Num() * DeliverTestShotFrames);
		Test.TestEqual(*FString::Printf(TEXT("%s: a sidecar and a camera file for each shot are written before the render"), *Version), State.Delivery.Files.Num(), State.Shots.Num() * 2);

		for (int32 ShotIndex = 0; ShotIndex < State.Shots.Num(); ++ShotIndex)
		{
			const UEcine::ShotEditing::FShot& Shot = State.Shots[ShotIndex];
			const FString Name = GetDeliveredName(State, ShotIndex, TEXT("vfx"));
			const FString Folder = State.Delivery.Directory / Name;
			const FString At = FString::Printf(TEXT("%s, %s"), *Version, *Shot.Name);

			// The frames carry the frame numbers of the whole sequence, and there are no others.
			TArray<FString> Frames;
			IFileManager::Get().FindFiles(Frames, *(Folder / TEXT("*.exr")), true, false);
			Test.TestEqual(*FString::Printf(TEXT("%s: the shot's folder holds its frames and no more"), *At), Frames.Num(), DeliverTestShotFrames);
			for (int32 FrameInShot = 0; FrameInShot < DeliverTestShotFrames; ++FrameInShot)
			{
				const FString Frame = FString::Printf(TEXT("%s.%04d.exr"), *Name, Shot.StartFrame.Value + FrameInShot);
				Test.TestTrue(*FString::Printf(TEXT("%s: %s is written"), *At, *Frame), IFileManager::Get().FileExists(*(Folder / Frame)));
			}

			const FString Sidecar = Folder / Name + TEXT(".json");
			const FString CameraFile = Folder / Name + TEXT(".fbx");
			Test.TestTrue(*FString::Printf(TEXT("%s: the delivery lists the sidecar and the camera file"), *At),
				State.Delivery.Files.ContainsByPredicate([&Sidecar](const FString& File) { return FPaths::IsSamePath(File, Sidecar); })
				&& State.Delivery.Files.ContainsByPredicate([&CameraFile](const FString& File) { return FPaths::IsSamePath(File, CameraFile); }));

			FString Json;
			TSharedPtr<FJsonObject> Root;
			const ACineCameraActor* Camera = GetDeliverTestCamera(State);
			if (Test.TestTrue(*FString::Printf(TEXT("%s: the sidecar is JSON"), *At), FFileHelper::LoadFileToString(Json, *Sidecar) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid())
				&& Test.TestNotNull(TEXT("The camera is still in the level"), Camera))
			{
				Test.TestEqual(*FString::Printf(TEXT("%s: the sidecar names the frames it goes with"), *At), Root->GetStringField(TEXT("files")), Name + TEXT(".####.exr"));
				Test.TestEqual(*FString::Printf(TEXT("%s: the sidecar gives the picture's width"), *At), Root->GetIntegerField(TEXT("width")), 1920);
				Test.TestEqual(*FString::Printf(TEXT("%s: the sidecar repeats the shot's metadata"), *At), Root->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("uecine/shot")), Shot.Name);
				const TArray<TSharedPtr<FJsonValue>>& CameraFrames = Root->GetArrayField(TEXT("camera"));
				if (Test.TestEqual(*FString::Printf(TEXT("%s: the sidecar has the camera at every frame"), *At), CameraFrames.Num(), DeliverTestShotFrames))
				{
					const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
					const TSharedPtr<FJsonObject> First = CameraFrames[0]->AsObject();
					const TArray<TSharedPtr<FJsonValue>>& Location = First->GetArrayField(TEXT("location"));
					Test.TestEqual(*FString::Printf(TEXT("%s: the camera's frames are counted in the whole sequence"), *At), First->GetIntegerField(TEXT("frame")), Shot.StartFrame.Value);
					Test.TestTrue(*FString::Printf(TEXT("%s: the sidecar says where the lens is"), *At),
						Location.Num() == 3 && FVector(Location[0]->AsNumber(), Location[1]->AsNumber(), Location[2]->AsNumber()).Equals(Lens->GetComponentLocation(), 0.01));
					Test.TestEqual(*FString::Printf(TEXT("%s: the sidecar gives the focal length"), *At), First->GetNumberField(TEXT("focalLength")), static_cast<double>(Lens->CurrentFocalLength), 0.001);
				}
			}

			Test.TestTrue(*FString::Printf(TEXT("%s: the camera file is an FBX file with a camera in it"), *At),
				(FileHoldsText(CameraFile, "Kaydara FBX Binary") || FileHoldsText(CameraFile, "; FBX")) && FileHoldsText(CameraFile, "Camera"));
		}

		// One frame of the first shot is opened.
		const FString FirstName = GetDeliveredName(State, 0, TEXT("vfx"));
		const FString FrameFile = State.Delivery.Directory / FirstName / FString::Printf(TEXT("%s.%04d.exr"), *FirstName, State.Shots[0].StartFrame.Value + 3);
		FExrHeader Header;
		if (Test.TestTrue(*FString::Printf(TEXT("%s: a frame's header is read"), *Version), ReadExrHeader(FrameFile, Header)))
		{
			const TArray<FString> Layers = GetExrLayers(Header);
			Test.AddInfo(FString::Printf(TEXT("%s: %d x %d, layers: %s"), *Version, Header.Size.X, Header.Size.Y, *FString::Join(Layers, TEXT(" | "))));
			Test.TestEqual(*FString::Printf(TEXT("%s: the frame is the whole recording format"), *Version), Header.Size, FIntPoint(1920, 1080));
			for (const TCHAR* Layer : { TEXT("FinalImage"), TEXT("Deferred_depth"), TEXT("Deferred_normal"), TEXT("Deferred_motion") })
			{
				Test.TestTrue(*FString::Printf(TEXT("%s: the frame has the layer %s"), *Version, Layer), Layers.Contains(Layer));
			}
			Test.TestTrue(*FString::Printf(TEXT("%s: the frame has mattes"), *Version), Layers.ContainsByPredicate([](const FString& Layer) { return Layer.Contains(TEXT("ObjectID")); }));
			Test.TestFalse(*FString::Printf(TEXT("%s: the frame has no slate or burn-ins"), *Version), Layers.Contains(TEXT("BurnIn")));
			Test.TestEqual(*FString::Printf(TEXT("%s: the frame says which shot it is"), *Version), Header.Text.FindRef(TEXT("uecine/shot")), State.Shots[0].Name);
			Test.TestTrue(*FString::Printf(TEXT("%s: the frame says which camera body shot it (%s)"), *Version, *Header.Text.FindRef(TEXT("uecine/camera/body"))), Header.Text.FindRef(TEXT("uecine/camera/body")).Contains(TEXT("ALEXA 35")));
			Test.TestEqual(*FString::Printf(TEXT("%s: the frame says which colour space it is in"), *Version), Header.Text.FindRef(TEXT("uecine/colourSpace")), FString(TEXT("Linear Rec.709 (sRGB)")));
		}
		const FString LookAtName = FString::Printf(TEXT("deliver-vfx-v%03d.png"), State.Delivery.Version);
		if (Test.TestTrue(*FString::Printf(TEXT("%s: a frame's picture is read"), *Version), LoadRenderedLayer(FrameFile, TEXT("FinalImage"), *LookAtName, OutPicture)))
		{
			Test.TestTrue(*FString::Printf(TEXT("%s: the picture is not empty"), *Version), MeasureBandBrightness(OutPicture, 0.0, 1.0) > 0.01);
		}
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
	UEcine::RenderedFiles::ExpectRenderLogNoise(*this);

	const TSharedRef<FDeliverTestState> State = MakeShared<FDeliverTestState>();
	if (!MakeDeliverTestScene(*this, *State, 2))
	{
		CleanUpDeliverTest(*this, *State);
		return false;
	}
	ULevelSequence* Cut = State->CreatedSequences[0].Get();

	State->OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEcine/Renders/RenderTwoShots"));
	IFileManager::Get().DeleteDirectory(*State->OutputDirectory, false, true);

	TArray<FUEcineRenderShot> RenderShots;
	FUEcineRenderShot& Layers = RenderShots.AddDefaulted_GetRef();
	Layers.ShotName = State->Shots[0].Name;
	Layers.Spec.OutputDirectory = State->OutputDirectory;
	Layers.Spec.FileName = State->Shots[0].Name + TEXT("_vfx.{frame_number}");
	Layers.Spec.Resolution = FIntPoint(640, 268);
	Layers.Spec.bDeliveryFrameOnly = true;
	Layers.Spec.Format = EUEcineRenderFormat::MultilayerExr;
	Layers.Spec.bSceneLinear = true;
	Layers.Spec.bDepth = Layers.Spec.bNormals = Layers.Spec.bMotionVectors = Layers.Spec.bMattes = true;
	Layers.Spec.bBurnIn = Layers.Spec.bSlate = true;
	Layers.Spec.Metadata = { { TEXT("uecine/sequence"), TEXT("Render test") }, { TEXT("uecine/shot"), State->Shots[0].Name }, { TEXT("uecine/lens/kit"), TEXT("First Shot Primes") } };

	FUEcineRenderShot& Movie = RenderShots.AddDefaulted_GetRef();
	Movie.ShotName = State->Shots[1].Name;
	Movie.Spec.OutputDirectory = State->OutputDirectory;
	Movie.Spec.FileName = State->Shots[1].Name + TEXT("_review");
	Movie.Spec.Resolution = FIntPoint(640, 268);
	Movie.Spec.bDeliveryFrameOnly = true;
	Movie.Spec.Format = EUEcineRenderFormat::ProRes422LT;
	Movie.Spec.bBurnIn = Movie.Spec.bSlate = true;
	Movie.Spec.Metadata = { { TEXT("uecine/sequence"), TEXT("Render test") }, { TEXT("uecine/shot"), State->Shots[1].Name }, { TEXT("uecine/lens/kit"), TEXT("Second Shot Zoom") } };
	// Frames are played before the first one kept, as every delivery does: the slate must still be the first frame kept.
	constexpr int32 WarmUpFrames = 8;
	Layers.Spec.WarmUpFrames = Movie.Spec.WarmUpFrames = WarmUpFrames;

	State->PlaybackRangeBefore = Cut->GetMovieScene()->GetPlaybackRange();
	for (const FShot& Shot : State->Shots)
	{
		State->ShotRangesBefore.Add(Shot.Section->GetRange());
	}
	State->BurnInUpdatesBefore = UUEcineBurnInWidget::GetUpdateCount();

	FDeliverTestStage Render;
	Render.Start = [this, State, RenderShots]
	{
		FText Error;
		UUEcineRenderSubsystem* Subsystem = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>();
		ULevelSequence* Cut = State->CreatedSequences[0].Get();
		if (!TestTrue(TEXT("The render starts"), Subsystem->StartRender(Cut, RenderShots, Error)))
		{
			AddError(Error.ToString());
			return false;
		}
		TestTrue(TEXT("A render is running"), Subsystem->IsRendering());
		TestFalse(TEXT("A second render is refused while one runs"), Subsystem->StartRender(Cut, RenderShots, Error));
		return true;
	};
	Render.Check = [this, State, WarmUpFrames]
	{
		using namespace UEcine::RenderedFiles;
		const FUEcineRenderResult& Result = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->GetLastResult();
		TestTrue(TEXT("The render succeeded"), Result.bSuccess);
		TestTrue(TEXT("The render said how far it was while it ran"), State->ProgressSeen > 0.0f && State->ProgressSeen <= 1.0f);

		// A slated shot is one frame longer at each end.
		constexpr int32 SlatedFrames = DeliverTestShotFrames + 2;
		TArray<FString> ExrFiles = Result.Files.FilterByPredicate([](const FString& File) { return File.EndsWith(TEXT(".exr")); });
		const TArray<FString> MovFiles = Result.Files.FilterByPredicate([](const FString& File) { return File.EndsWith(TEXT(".mov")); });
		ExrFiles.Sort();
		TestEqual(TEXT("The first shot is one EXR file a frame"), ExrFiles.Num(), SlatedFrames);
		TestEqual(TEXT("The second shot is one movie file"), MovFiles.Num(), 1);
		for (const FString& File : Result.Files)
		{
			TestTrue(*FString::Printf(TEXT("%s is on disk, in the folder asked for"), *File), IFileManager::Get().FileExists(*File) && FPaths::IsUnderDirectory(File, State->OutputDirectory));
		}
		TestEqual(TEXT("The burn-in widget is told about every frame played of both shots"), UUEcineBurnInWidget::GetUpdateCount() - State->BurnInUpdatesBefore, 2 * (WarmUpFrames + SlatedFrames));

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
				TestEqual(TEXT("The EXR is the size asked for"), Header.Size, FIntPoint(640, 268));
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
				// A camera that still recorded its whole sensor would be boxed inside the wider frame.
				const TArrayView64<const FLinearColor> PicturePixels = Picture.AsRGBA32F();
				const FLinearColor LeftEdge = PicturePixels[static_cast<int64>(Picture.SizeY / 2) * Picture.SizeX + 4];
				const FLinearColor RightEdge = PicturePixels[static_cast<int64>(Picture.SizeY / 2) * Picture.SizeX + Picture.SizeX - 5];
				TestTrue(TEXT("The picture fills the delivery frame from edge to edge"), LeftEdge.GetLuminance() > 0.01f && RightEdge.GetLuminance() > 0.01f);
				// The camera looks down at a cube about five metres away.
				TestTrue(TEXT("The depth layer measures the distance to what the camera sees, in centimetres"), DepthAtCentre.R > 300.0f && DepthAtCentre.R < 800.0f);
			}
		}
		if (MovFiles.Num() == 1)
		{
			TestEqual(TEXT("The movie is named for its shot"), FPaths::GetCleanFilename(MovFiles[0]), State->Shots[1].Name + TEXT("_review.mov"));
			TestEqual(TEXT("The movie holds the slate, the shot and the tail frame"), CountMovFrames(MovFiles[0]), SlatedFrames);
		}

		// The camera in the editor was never cropped, and the level is no longer told to crop.
		if (const ACineCameraActor* Camera = GetDeliverTestCamera(*State))
		{
			TestEqual(TEXT("The editor's camera keeps its whole sensor"), Camera->GetCineCameraComponent()->CropSettings.AspectRatio, 0.0f);
		}
		const IConsoleVariable* DeliveryFrameOnly = IConsoleManager::Get().FindConsoleVariable(UEcine::Deliver::DeliveryFrameOnlyVariable);
		TestTrue(TEXT("The renderer has put the delivery frame switch back"), DeliveryFrameOnly != nullptr && DeliveryFrameOnly->GetInt() == 0);

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
	};
	RunDeliverTestStages(*this, State, { Render });
	return true;
}

/**
 * Preflight on a real sequence: what it finds, that each fix puts its finding right in one step
 * that can be undone, and how the files of a delivery are named and versioned.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverPreflightFixesTest, "UEcine.Editor.Deliver.Preflight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverPreflightFixesTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::DeliverEditing;
	using namespace UEcine::ShotEditing;

	FDeliverTestState State;
	ON_SCOPE_EXIT { CleanUpDeliverTest(*this, State); };
	if (!MakeDeliverTestScene(*this, State, 0))
	{
		return false;
	}
	UseDeliverTestFolder(State, TEXT("DeliverPreflight"));
	ACineCameraActor* Camera = GetDeliverTestCamera(State);
	UUEcineCameraPackageComponent* Package = UUEcineCameraPackageComponent::Find(Camera);

	// Nothing open.
	FText Error;
	FDelivery Delivery;
	FCheck Check = CheckDelivery(EUEcineDeliverIntent::Review);
	TestTrue(TEXT("With no sequence open that is the one finding, and it stops the render"),
		Check.Findings.Num() == 1 && Check.Findings[0].Check == EUEcinePreflightCheck::NoSequence && Check.Findings[0].bStopsRender);
	TestTrue(TEXT("With no sequence open there are no files to name"), Check.Files.IsEmpty());
	TestFalse(TEXT("With no sequence open nothing is delivered"), StartDelivery(EUEcineDeliverIntent::Review, Delivery, Error));
	TestFalse(TEXT("The refusal says why"), Error.IsEmpty());

	// A shot with a camera and a shot with nothing in it.
	if (!TestTrue(TEXT("Two shots are added"), AddShot(Camera, Error) && AddShot(nullptr, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	ULevelSequence* Cut = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	State.Shots = GetShots();
	State.CreatedSequences.Add(Cut);
	for (const FShot& Shot : State.Shots)
	{
		State.CreatedSequences.AddUnique(Shot.Sequence);
	}
	if (!TestEqual(TEXT("The sequence has two shots"), State.Shots.Num(), 2))
	{
		return false;
	}
	const FString& First = State.Shots[0].Name;
	const FString& Second = State.Shots[1].Name;

	Check = CheckDelivery(EUEcineDeliverIntent::Review);
	TestNotNull(TEXT("A shot with nothing in it has no camera"), FindFinding(Check, EUEcinePreflightCheck::ShotWithoutCamera, Second));
	TestNull(TEXT("A shot with a camera is not said to have none"), FindFinding(Check, EUEcinePreflightCheck::ShotWithoutCamera, First));
	TestNull(TEXT("A shot added with its camera has its camera cut"), FindFinding(Check, EUEcinePreflightCheck::ShotWithoutCameraCut, First));
	TestNull(TEXT("A camera with a camera package is not said to have none"), FindFinding(Check, EUEcinePreflightCheck::CameraWithoutPackage, First));
	TestFalse(TEXT("A shot with no camera stops the delivery"), StartDelivery(EUEcineDeliverIntent::Review, Delivery, Error));
	TestTrue(*FString::Printf(TEXT("The refusal names the shot (%s)"), *Error.ToString()), Error.ToString().Contains(Second));
	TestFalse(TEXT("A finding with no fix is not put right for the user"), Check.Findings.IsEmpty() || ApplyFix(Check.Findings[0], Error));

	// The camera is put in the second shot, with no camera cut to look through it.
	UEcine::SequenceUtils::FindOrAddBinding(State.Shots[1].Sequence, Camera);
	ULevelSequenceEditorBlueprintLibrary::ForceUpdate();
	Check = CheckDelivery(EUEcineDeliverIntent::Review);
	if (const FUEcinePreflightFinding* NoCameraCut = FindFinding(Check, EUEcinePreflightCheck::ShotWithoutCameraCut, Second);
		TestNotNull(TEXT("A camera in a shot with no camera cut is found"), NoCameraCut)
		&& TestTrue(TEXT("It stops the render and has a fix"), NoCameraCut->bStopsRender && !NoCameraCut->FixLabel.IsEmpty()))
	{
		const FUEcinePreflightFinding Finding = *NoCameraCut;
		if (!TestTrue(TEXT("The camera cut is added"), ApplyFix(Finding, Error)))
		{
			AddError(Error.ToString());
		}
		Check = CheckDelivery(EUEcineDeliverIntent::Review);
		TestNull(TEXT("With its camera cut the shot is no longer found"), FindFinding(Check, EUEcinePreflightCheck::ShotWithoutCameraCut, Second));
		TestFalse(TEXT("Nothing stops the render now"), StopsRender(Check));
		TestTrue(TEXT("Adding the camera cut is undone in one step"), GEditor->UndoTransaction());
		TestNotNull(TEXT("Undone, the shot has no camera cut again"), FindFinding(CheckDelivery(EUEcineDeliverIntent::Review), EUEcinePreflightCheck::ShotWithoutCameraCut, Second));
		TestTrue(TEXT("The camera cut is redone"), GEditor->RedoTransaction());
		TestFalse(TEXT("Redone, nothing stops the render"), StopsRender(CheckDelivery(EUEcineDeliverIntent::Review)));
	}

	// A camera shooting at another frame rate than the sequence plays at.
	TestTrue(TEXT("The camera is set to 25 fps"), UEcine::CameraEditing::EditPackage(Package, FText::FromString(TEXT("Change Frame Rate")), [](FUEcineCameraPackage& Edited) { Edited.FramesPerSecond = 25.0f; }, Error));
	Check = CheckDelivery(EUEcineDeliverIntent::Review);
	if (const FUEcinePreflightFinding* WrongRate = FindFinding(Check, EUEcinePreflightCheck::FrameRateMismatch, First);
		TestNotNull(TEXT("A camera at another frame rate than the sequence is found"), WrongRate)
		&& TestTrue(TEXT("It does not stop the render, and has a fix"), !WrongRate->bStopsRender && !WrongRate->FixLabel.IsEmpty()))
	{
		const FUEcinePreflightFinding Finding = *WrongRate;
		if (!TestTrue(TEXT("The camera's frame rate is matched to the sequence"), ApplyFix(Finding, Error)))
		{
			AddError(Error.ToString());
		}
		TestEqual(TEXT("The camera now shoots at the sequence's 24 fps"), Package->Package.FramesPerSecond, 24.0f);
		Check = CheckDelivery(EUEcineDeliverIntent::Review);
		TestTrue(TEXT("Neither shot of that camera is found any more"),
			FindFinding(Check, EUEcinePreflightCheck::FrameRateMismatch, First) == nullptr && FindFinding(Check, EUEcinePreflightCheck::FrameRateMismatch, Second) == nullptr);
		TestTrue(TEXT("Matching the frame rate is undone in one step"), GEditor->UndoTransaction() && Package->Package.FramesPerSecond == 25.0f);
		TestTrue(TEXT("Matching the frame rate is redone"), GEditor->RedoTransaction() && Package->Package.FramesPerSecond == 24.0f);
	}

	// Exposure left to adapt, then held by the camera.
	const bool bProjectAdaptsExposure = GetDefault<URendererSettings>()->bDefaultFeatureAutoExposure;
	TestEqual(TEXT("A camera that does not hold its exposure is found when the project lets exposure adapt"),
		FindFinding(Check, EUEcinePreflightCheck::AutoExposure, First) != nullptr, bProjectAdaptsExposure);
	TestTrue(TEXT("Physical exposure is turned on"), UEcine::CameraEditing::EditPackage(Package, FText::FromString(TEXT("Physical Exposure")), [](FUEcineCameraPackage& Edited) { Edited.bPhysicalExposure = true; }, Error));
	TestNull(TEXT("A camera with physical exposure holds its exposure"), FindFinding(CheckDelivery(EUEcineDeliverIntent::Review), EUEcinePreflightCheck::AutoExposure, First));

	// The project's working colour space matters to a VFX handoff only.
	TestNull(TEXT("A review is not asked about the working colour space"), FindFinding(Check, EUEcinePreflightCheck::WorkingColourSpace, FString()));
	Check = CheckDelivery(EUEcineDeliverIntent::VfxHandoff);
	if (const FUEcinePreflightFinding* ColourSpace = FindFinding(Check, EUEcinePreflightCheck::WorkingColourSpace, FString());
		TestNotNull(TEXT("A VFX handoff from a project that does not work in ACEScg is found"), ColourSpace)
		&& TestTrue(TEXT("It does not stop the render, and has a fix"), !ColourSpace->bStopsRender && !ColourSpace->FixLabel.IsEmpty()))
	{
		// The fix writes the project's settings file, which is put back as it was.
		URendererSettings* Renderer = GetMutableDefault<URendererSettings>();
		const FString SettingsFile = FPaths::ConvertRelativePathToFull(Renderer->GetDefaultConfigFilename());
		const TEnumAsByte<EWorkingColorSpace::Type> ChoiceBefore = Renderer->WorkingColorSpaceChoice;
		TArray<uint8> SettingsBefore;
		if (TestTrue(TEXT("The project's settings file is read"), FFileHelper::LoadFileToArray(SettingsBefore, *SettingsFile)))
		{
			ON_SCOPE_EXIT
			{
				Renderer->WorkingColorSpaceChoice = ChoiceBefore;
				TestTrue(TEXT("The project's settings file is put back"), FFileHelper::SaveArrayToFile(SettingsBefore, *SettingsFile));
			};

			const FUEcinePreflightFinding Finding = *ColourSpace;
			const FString SpaceBefore = GetSettings().SceneLinearColourSpace;
			if (!TestTrue(TEXT("The project is set to work in ACEScg"), ApplyFix(Finding, Error)))
			{
				AddError(Error.ToString());
			}
			FString SettingsAfter;
			TestTrue(TEXT("The choice is written to the project's settings file"), FFileHelper::LoadFileToString(SettingsAfter, *SettingsFile) && SettingsAfter.Contains(TEXT("WorkingColorSpaceChoice=ACESAP1")));
			const FCheck After = CheckDelivery(EUEcineDeliverIntent::VfxHandoff);
			const FUEcinePreflightFinding* Waiting = FindFinding(After, EUEcinePreflightCheck::WorkingColourSpace, FString());
			TestTrue(TEXT("The finding now says the change waits for a restart, with nothing left to put right"), Waiting != nullptr && Waiting->FixLabel.IsEmpty() && !Waiting->Title.EqualTo(Finding.Title));
			TestEqual(TEXT("Until the restart the files say the colour space the editor started in"), GetSettings().SceneLinearColourSpace, SpaceBefore);
		}
	}

	// The first shot starts the sequence, so its slate has no frame of the sequence to be on.
	Check = CheckDelivery(EUEcineDeliverIntent::Review);
	const FUEcinePreflightFinding* SlateBeforeStart = FindFinding(Check, EUEcinePreflightCheck::SlateBeforeSequenceStart, First);
	TestTrue(TEXT("The slate of a shot on the sequence's first frame is found, and does not stop the render"), SlateBeforeStart != nullptr && !SlateBeforeStart->bStopsRender);
	TestNull(TEXT("The slate of a later shot is not found"), FindFinding(Check, EUEcinePreflightCheck::SlateBeforeSequenceStart, Second));
	EditChoices([](UUEcineEditorSettings& Choices) { Choices.bDeliverSlate = false; });
	TestNull(TEXT("With slates turned off it is not found"), FindFinding(CheckDelivery(EUEcineDeliverIntent::Review), EUEcinePreflightCheck::SlateBeforeSequenceStart, First));
	EditChoices([](UUEcineEditorSettings& Choices) { Choices.bDeliverSlate = true; });

	// How the files are named, and how versions are counted.
	Check = CheckDelivery(EUEcineDeliverIntent::Review);
	const FString FirstFile = FString::Printf(TEXT("Review/%s_%s_review"), *Cut->GetName(), *First);
	if (TestEqual(TEXT("A file is named for each shot"), Check.Files.Num(), 2))
	{
		TestEqual(TEXT("The first delivery to a folder is version 1"), Check.Files[0], FirstFile + TEXT("_v001.mov"));
	}
	const FString EarlierDelivery = GetSettings().OutputDirectory / FString::Printf(TEXT("Review/%s_%s_review_v004.mov"), *Cut->GetName(), *Second);
	TestTrue(TEXT("The deliveries of a sequence go in a folder of its own under the delivery folder"), FPaths::IsUnderDirectory(EarlierDelivery, State.OutputDirectory / Cut->GetName()));
	TestTrue(TEXT("A file from an earlier delivery of the other shot is put in the folder"), FFileHelper::SaveStringToFile(FString(), *EarlierDelivery));
	Check = CheckDelivery(EUEcineDeliverIntent::Review);
	TestTrue(TEXT("Every shot of the next delivery is one version on from the latest of any shot"), Check.Files.Num() == 2 && Check.Files[0] == FirstFile + TEXT("_v005.mov"));
	Check = CheckDelivery(EUEcineDeliverIntent::Master);
	TestTrue(TEXT("Each intent counts its own versions"), Check.Files.Num() == 2 && Check.Files[0].StartsWith(TEXT("Master/")) && Check.Files[0].EndsWith(TEXT("_master_v001.mov")));

	EditChoices([](UUEcineEditorSettings& Choices) { Choices.DeliverNamePattern = TEXT("{sequence}_{version}"); });
	TestTrue(TEXT("A pattern that gives every shot the same name names no files"), CheckDelivery(EUEcineDeliverIntent::Review).Files.IsEmpty());
	TestFalse(TEXT("A pattern that gives every shot the same name stops the delivery"), StartDelivery(EUEcineDeliverIntent::Review, Delivery, Error));
	return true;
}

/**
 * The Phase 5 workflow: a two-shot sequence is delivered for review, as a VFX handoff (twice,
 * the second time with every monitor tool on) and as a master, and the files are read back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverIntentsTest, "UEcine.Editor.Deliver.Intents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverIntentsTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::DeliverEditing;

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: rendering needs a renderer (tools/test.ps1 -Rendering)."));
		return true;
	}
	UEcine::RenderedFiles::ExpectRenderLogNoise(*this);

	const TSharedRef<FDeliverTestState> State = MakeShared<FDeliverTestState>();
	if (!MakeDeliverTestScene(*this, *State, 2))
	{
		CleanUpDeliverTest(*this, *State);
		return false;
	}
	UseDeliverTestFolder(*State, TEXT("DeliverIntents"));
	TestFalse(TEXT("Preflight finds nothing that stops the render"), StopsRender(CheckDelivery(EUEcineDeliverIntent::Review)));

	const auto Start = [this, State](EUEcineDeliverIntent Intent)
	{
		FText Error;
		if (!StartDelivery(Intent, State->Delivery, Error))
		{
			AddError(FString::Printf(TEXT("The %s delivery did not start: %s"), *UEcine::Deliver::GetIntentName(Intent).ToString(), *Error.ToString()));
			return false;
		}
		return true;
	};
	const auto CheckMovies = [this, State](const TCHAR* IntentToken, int32 Frames, const ANSICHAR* Codec)
	{
		const FUEcineRenderResult& Result = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->GetLastResult();
		TestTrue(*FString::Printf(TEXT("%hs: the render succeeded"), Codec), Result.bSuccess);
		TestEqual(*FString::Printf(TEXT("%hs: one movie a shot is rendered"), Codec), Result.Files.Num(), State->Shots.Num());
		TestEqual(*FString::Printf(TEXT("%hs: the first delivery to the folder is version 1"), Codec), State->Delivery.Version, 1);
		TestTrue(*FString::Printf(TEXT("%hs: nothing is written beside the movies"), Codec), State->Delivery.Files.IsEmpty());
		for (int32 ShotIndex = 0; ShotIndex < State->Shots.Num(); ++ShotIndex)
		{
			const FString Movie = State->Delivery.Directory / GetDeliveredName(*State, ShotIndex, IntentToken) + TEXT(".mov");
			if (TestTrue(*FString::Printf(TEXT("%s is written"), *Movie), IFileManager::Get().FileExists(*Movie)))
			{
				TestEqual(*FString::Printf(TEXT("%s plays the right number of frames"), *Movie), UEcine::RenderedFiles::CountMovFrames(Movie), Frames);
				TestTrue(*FString::Printf(TEXT("%s is in the codec %hs"), *Movie, Codec), FileHoldsText(Movie, Codec));
			}
		}
	};

	TArray<FDeliverTestStage> Stages;

	// Review: ProRes 422 LT, with a slate before the shot and a frame after it.
	Stages.Add({ [Start] { return Start(EUEcineDeliverIntent::Review); }, [this, State, CheckMovies]
	{
		CheckMovies(TEXT("review"), DeliverTestShotFrames + 2, "apcs");
		TestTrue(TEXT("Review movies go in the Review folder of the sequence's deliveries"), FPaths::IsSamePath(State->Delivery.Directory, State->OutputDirectory / State->CreatedSequences[0]->GetName() / TEXT("Review")));
	} });

	// VFX handoff: frames with every pass, a sidecar and an FBX camera.
	Stages.Add({ [Start] { return Start(EUEcineDeliverIntent::VfxHandoff); }, [this, State]
	{
		TestEqual(TEXT("The first VFX handoff is version 1"), State->Delivery.Version, 1);
		CheckVfxHandoff(*this, *State, State->PictureBefore);
		TestTrue(TEXT("The sequence being worked on is still the one open"), ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence() == State->CreatedSequences[0].Get());
	} });

	// The same again with every monitor tool on: a new version, and none of the tools in the picture.
	Stages.Add({ [State, Start]
	{
		UUEcineMonitorSubsystem* Monitor = UUEcineMonitorSubsystem::Get();
		State->MonitorBefore = Monitor->GetSettings();
		FUEcineMonitorSettings EveryTool = Monitor->GetSettings();
		EveryTool.bFalseColour = EveryTool.bZebras = EveryTool.bPeaking = EveryTool.bWaveform = EveryTool.bHistogram = true;
		EveryTool.bFrameLines = EveryTool.bSafeAreas = EveryTool.bCentreMark = EveryTool.bDataReadout = true;
		Monitor->SetSettings(EveryTool);
		return Start(EUEcineDeliverIntent::VfxHandoff);
	}, [this, State]
	{
		TestEqual(TEXT("The second VFX handoff is version 2"), State->Delivery.Version, 2);
		FImage Picture;
		CheckVfxHandoff(*this, *State, Picture);
		const FString FirstVersion = State->Delivery.Directory / GetDeliveredName(*State, 0, TEXT("vfx")).Replace(TEXT("_v002"), TEXT("_v001"));
		TestTrue(TEXT("The first version is still there beside the second"), IFileManager::Get().DirectoryExists(*FirstVersion));
		if (TestTrue(TEXT("Both versions have the frame to compare"), Picture.SizeX > 0 && Picture.SizeX == State->PictureBefore.SizeX && Picture.SizeY == State->PictureBefore.SizeY))
		{
			const double Difference = MeasurePictureDifference(State->PictureBefore, Picture);
			AddInfo(FString::Printf(TEXT("With every monitor tool on, the frame differs from the frame without by %.4f of its brightness"), Difference));
			TestTrue(TEXT("A render made with every monitor tool on has none of them in the picture"), Difference < 0.05);
		}
	} });

	// Master: ProRes 4444, the shot and nothing else.
	Stages.Add({ [Start] { return Start(EUEcineDeliverIntent::Master); }, [CheckMovies]
	{
		CheckMovies(TEXT("master"), DeliverTestShotFrames, "ap4h");
	} });

	RunDeliverTestStages(*this, State, MoveTemp(Stages));
	return true;
}

/**
 * A sequence that is not cut into shots is delivered as one shot, once its missing camera cut
 * has been put right from preflight; and a render that is stopped ends as one that did not succeed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineDeliverUncutTest, "UEcine.Editor.Deliver.UncutAndStop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineDeliverUncutTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::DeliverEditing;

	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("Skipped: rendering needs a renderer (tools/test.ps1 -Rendering)."));
		return true;
	}
	UEcine::RenderedFiles::ExpectRenderLogNoise(*this);

	const TSharedRef<FDeliverTestState> State = MakeShared<FDeliverTestState>();
	State->TimeoutSeconds = 180.0;
	if (!MakeDeliverTestScene(*this, *State, 0))
	{
		CleanUpDeliverTest(*this, *State);
		return false;
	}
	UseDeliverTestFolder(*State, TEXT("DeliverUncut"));

	// A sequence with the camera in it and nothing else: no shots, no camera cut.
	ULevelSequence* Uncut = UEcine::SequenceUtils::CreateSequence(FString(UEcine::SequenceUtils::SequenceFolder) / TEXT("DeliverUncut"), FFrameRate(24, 1));
	if (!TestNotNull(TEXT("A sequence is made"), Uncut))
	{
		CleanUpDeliverTest(*this, *State);
		return false;
	}
	State->CreatedSequences.Add(Uncut);
	UMovieScene* MovieScene = Uncut->GetMovieScene();
	MovieScene->SetPlaybackRange(0, MovieScene->GetTickResolution().AsFrameNumber(DeliverTestShotFrames / 24.0).Value);
	UEcine::SequenceUtils::FindOrAddBinding(Uncut, GetDeliverTestCamera(*State));
	TestTrue(TEXT("The sequence is opened"), ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Uncut));
	State->Shots = UEcine::ShotEditing::GetShots();

	FText Error;
	FCheck Check = CheckDelivery(EUEcineDeliverIntent::Review);
	const FUEcinePreflightFinding* NoCameraCut = FindFinding(Check, EUEcinePreflightCheck::ShotWithoutCameraCut, Uncut->GetName());
	if (!TestEqual(TEXT("A sequence that is not cut into shots is one shot"), State->Shots.Num(), 1)
		|| !TestNotNull(TEXT("Its missing camera cut is found"), NoCameraCut)
		|| !TestTrue(TEXT("Its camera cut is added from preflight"), ApplyFix(FUEcinePreflightFinding(*NoCameraCut), Error)))
	{
		AddError(Error.ToString());
		CleanUpDeliverTest(*this, *State);
		return false;
	}
	TestFalse(TEXT("Nothing stops the render now"), StopsRender(CheckDelivery(EUEcineDeliverIntent::Review)));

	TArray<FDeliverTestStage> Stages;
	Stages.Add({ [this, State]
	{
		FText StartError;
		if (!StartDelivery(EUEcineDeliverIntent::Review, State->Delivery, StartError))
		{
			AddError(FString::Printf(TEXT("The delivery of the uncut sequence did not start: %s"), *StartError.ToString()));
			return false;
		}
		return true;
	}, [this, State]
	{
		const FUEcineRenderResult& Result = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->GetLastResult();
		TestTrue(TEXT("The uncut sequence is rendered"), Result.bSuccess);
		const FString Movie = State->Delivery.Directory / GetDeliveredName(*State, 0, TEXT("review")) + TEXT(".mov");
		if (TestTrue(*FString::Printf(TEXT("%s is written"), *Movie), IFileManager::Get().FileExists(*Movie)))
		{
			TestEqual(TEXT("The movie holds the slate, the sequence and the tail frame"), UEcine::RenderedFiles::CountMovFrames(Movie), DeliverTestShotFrames + 2);
		}
		TestEqual(TEXT("It is the one file rendered"), Result.Files.Num(), 1);
	} });

	// A render stopped as soon as it has started.
	Stages.Add({ [this, State]
	{
		FText StartError;
		if (!StartDelivery(EUEcineDeliverIntent::Master, State->Delivery, StartError))
		{
			AddError(FString::Printf(TEXT("The delivery to stop did not start: %s"), *StartError.ToString()));
			return false;
		}
		GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->CancelRender();
		return true;
	}, [this]
	{
		TestFalse(TEXT("A render that was stopped did not succeed"), GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->GetLastResult().bSuccess);
		TestFalse(TEXT("A render that was stopped leaves no play session running"), GEditor->IsPlaySessionInProgress());
	} });

	// A render whose play session the editor does not start, as when a Blueprint does not compile.
	const bool bFixedTimeStep = FApp::UseFixedTimeStep();
	const double FixedDeltaTime = FApp::GetFixedDeltaTime();
	Stages.Add({ [this, State]
	{
		FText StartError;
		if (!StartDelivery(EUEcineDeliverIntent::Master, State->Delivery, StartError))
		{
			AddError(FString::Printf(TEXT("The delivery with no play session did not start: %s"), *StartError.ToString()));
			return false;
		}
		GEditor->CancelRequestPlaySession();
		return true;
	}, [this, bFixedTimeStep, FixedDeltaTime]
	{
		TestFalse(TEXT("A render whose play session did not start did not succeed"), GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->GetLastResult().bSuccess);
		TestTrue(TEXT("The editor keeps time as it did before that render"), FApp::UseFixedTimeStep() == bFixedTimeStep && FApp::GetFixedDeltaTime() == FixedDeltaTime);
	} });

	// The editor is left fit to render: the next delivery is made in full.
	Stages.Add({ [this, State]
	{
		FText StartError;
		if (!StartDelivery(EUEcineDeliverIntent::Master, State->Delivery, StartError))
		{
			AddError(FString::Printf(TEXT("The delivery after the stopped ones did not start: %s"), *StartError.ToString()));
			return false;
		}
		return true;
	}, [this]
	{
		const FUEcineRenderResult& Result = GEditor->GetEditorSubsystem<UUEcineRenderSubsystem>()->GetLastResult();
		TestTrue(TEXT("The delivery after the stopped ones is rendered"), Result.bSuccess);
		if (TestEqual(TEXT("It is the one file rendered"), Result.Files.Num(), 1))
		{
			TestEqual(TEXT("The master holds every frame of the sequence"), UEcine::RenderedFiles::CountMovFrames(Result.Files[0]), DeliverTestShotFrames);
		}
		TestFalse(TEXT("No play session is left running"), GEditor->IsPlaySessionInProgress());
	} });

	RunDeliverTestStages(*this, State, MoveTemp(Stages));
	return true;
}

#endif
