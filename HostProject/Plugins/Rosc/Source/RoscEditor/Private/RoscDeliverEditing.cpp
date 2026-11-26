// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscDeliverEditing.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "ColorManagement/ColorSpace.h"
#include "Editor.h"
#include "Engine/RendererSettings.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "ScopedTransaction.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "RoscCameraDatabase.h"
#include "RoscCameraEditing.h"
#include "RoscCameraPackageComponent.h"
#include "RoscEditorSettings.h"
#include "RoscHandoff.h"
#include "RoscRenderSubsystem.h"
#include "RoscSequenceUtils.h"
#include "RoscShotEditing.h"

#define LOCTEXT_NAMESPACE "RoscEditor"

namespace
{
	using Rosc::ShotEditing::FShot;

	TOptional<UE::Color::FColorSpace> ColourSpaceAtStartup;

	/** The colour space the editor's pictures are in. */
	const UE::Color::FColorSpace& GetPictureColourSpace()
	{
		return ColourSpaceAtStartup.IsSet() ? ColourSpaceAtStartup.GetValue() : UE::Color::FColorSpace::GetWorking();
	}

	/** What a scene-linear file says its colour space is. */
	FString GetColourSpaceName(const UE::Color::FColorSpace& ColourSpace)
	{
		using UE::Color::EColorSpace;
		static const TPair<EColorSpace, const TCHAR*> Known[] =
		{
			{ EColorSpace::sRGB, TEXT("Linear Rec.709 (sRGB)") },
			{ EColorSpace::ACESAP1, TEXT("ACEScg") },
			{ EColorSpace::ACESAP0, TEXT("ACES2065-1") },
			{ EColorSpace::Rec2020, TEXT("Linear Rec.2020") },
			{ EColorSpace::P3DCI, TEXT("Linear P3-DCI") },
			{ EColorSpace::P3D65, TEXT("Linear P3-D65") },
		};
		for (const TPair<EColorSpace, const TCHAR*>& Space : Known)
		{
			if (ColourSpace.Equals(UE::Color::FColorSpace(Space.Key)))
			{
				return Space.Value;
			}
		}
		return TEXT("Custom");
	}

	/**
	 * True when nothing holds the exposure of a camera's picture still: not the camera, not a
	 * post-process volume over the whole level, and the project lets exposure adapt.
	 */
	bool IsExposureLeftToAdapt(const ACineCameraActor& Camera)
	{
		if (!GetDefault<URendererSettings>()->bDefaultFeatureAutoExposure)
		{
			return false;
		}
		const auto HoldsExposure = [](const FPostProcessSettings& Settings)
		{
			return (Settings.bOverride_AutoExposureMethod && Settings.AutoExposureMethod == AEM_Manual)
				|| (Settings.bOverride_AutoExposureMinBrightness && Settings.bOverride_AutoExposureMaxBrightness && Settings.AutoExposureMinBrightness == Settings.AutoExposureMaxBrightness);
		};

		const UCineCameraComponent* Lens = Camera.GetCineCameraComponent();
		if (Lens->PostProcessBlendWeight > 0.0f && HoldsExposure(Lens->PostProcessSettings))
		{
			return false;
		}
		for (IInterface_PostProcessVolume& Volume : Camera.GetWorld()->GetPostProcessVolumeIterator())
		{
			const FPostProcessVolumeProperties Properties = Volume.GetProperties();
			if (Properties.bIsEnabled && Properties.bIsUnbound && Properties.BlendWeight > 0.0f && Properties.Settings != nullptr && HoldsExposure(*Properties.Settings))
			{
				return false;
			}
		}
		return true;
	}

	/** Plays the open sequence to a shot's first frame, so that a camera the shot spawns is there. */
	void PlayToShotStart(const ULevelSequence* OpenSequence, const FShot& Shot)
	{
		const UMovieScene* MovieScene = OpenSequence->GetMovieScene();
		Rosc::SequenceUtils::SetPlayheadTicks(ConvertFrameTime(FFrameTime(Shot.StartFrame), MovieScene->GetDisplayRate(), MovieScene->GetTickResolution()));
	}

	/** A shot of the open sequence, with what a delivery and preflight are told of it. */
	struct FGatheredShot
	{
		FShot Shot;
		FRoscDeliverShot Deliver;
		FRoscPreflightShot Preflight;
	};

	/** Looks at every shot of the open sequence as it starts. Puts the playhead back. */
	TArray<FGatheredShot> GatherShots(const ULevelSequence* OpenSequence)
	{
		TArray<FGatheredShot> Gathered;
		if (OpenSequence == nullptr)
		{
			return Gathered;
		}

		const FRoscCameraDatabase& Database = FRoscCameraDatabase::Get();
		const Rosc::SequenceUtils::FScopedPlayhead PutPlayheadBack;
		for (const FShot& Shot : Rosc::ShotEditing::GetShots())
		{
			// The files say what the camera is set to as the shot starts.
			PlayToShotStart(OpenSequence, Shot);

			FGatheredShot& Found = Gathered.AddDefaulted_GetRef();
			Found.Shot = Shot;
			Found.Deliver.Report = Rosc::ShotEditing::MakeShotReport(Shot);
			Found.Preflight.Name = Shot.Name;
			Found.Preflight.StartFrame = Shot.StartFrame.Value;
			if (const UMovieSceneTrack* CameraCuts = Shot.Sequence->GetMovieScene()->GetCameraCutTrack())
			{
				Found.Preflight.CameraCutCount = CameraCuts->GetAllSections().Num();
			}

			const ACineCameraActor* Camera = Rosc::ShotEditing::FindShotCamera(Shot);
			if (Camera == nullptr)
			{
				continue;
			}
			Found.Preflight.bHasCamera = true;
			Found.Preflight.bAutoExposure = IsExposureLeftToAdapt(*Camera);

			const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
			if (Lens->Filmback.SensorHeight > 0.0f)
			{
				Found.Deliver.RecordedAspect = static_cast<double>(Lens->Filmback.SensorWidth) * Lens->LensSettings.SqueezeFactor / Lens->Filmback.SensorHeight;
			}
			if (const URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(Camera))
			{
				const FRoscCameraPackage& Package = Component->Package;
				Found.Preflight.bHasPackage = true;
				Found.Preflight.PackageFramesPerSecond = Package.FramesPerSecond;
				Found.Deliver.DeliveryAspect = Package.DeliveryAspect;
				const FRoscCameraBody* Body = Database.FindBody(Package.BodyId);
				if (const FRoscRecordingFormat* Format = Body != nullptr ? Body->FindFormat(Package.FormatId) : nullptr)
				{
					Found.Deliver.FormatResolution = Format->Resolution;
				}
			}
		}
		return Gathered;
	}

	FRoscPreflightFacts MakePreflightFacts(ERoscDeliverIntent Intent, const ULevelSequence* OpenSequence, TConstArrayView<FGatheredShot> Shots)
	{
		FRoscPreflightFacts Facts;
		Facts.Intent = Intent;
		Facts.bHasSequence = OpenSequence != nullptr;
		if (OpenSequence != nullptr)
		{
			Facts.SequenceFramesPerSecond = OpenSequence->GetMovieScene()->GetDisplayRate().AsDecimal();
		}
		Facts.bSlate = GetDefault<URoscEditorSettings>()->bDeliverSlate;
		Facts.bWorkingSpaceIsAcesCg = GetPictureColourSpace().Equals(UE::Color::FColorSpace(UE::Color::EColorSpace::ACESAP1));
		Facts.bAcesCgWaitsForRestart = !Facts.bWorkingSpaceIsAcesCg && GetDefault<URendererSettings>()->WorkingColorSpaceChoice == EWorkingColorSpace::ACESAP1;
		for (const FGatheredShot& Shot : Shots)
		{
			Facts.Shots.Add(Shot.Preflight);
		}
		return Facts;
	}

	/** The version after the latest of any of these names already in a folder. The names still hold {version}. */
	int32 FindNextVersion(const FString& Directory, TConstArrayView<FString> Names)
	{
		int32 Latest = 0;
		IFileManager::Get().IterateDirectory(*Directory, [&Latest, &Names](const TCHAR* Path, bool bIsDirectory)
		{
			// A movie is a file of the name; a shot's frames are in a folder of the name.
			const FString Existing = bIsDirectory ? FPaths::GetCleanFilename(Path) : FPaths::GetBaseFilename(Path);
			for (const FString& Name : Names)
			{
				Latest = FMath::Max(Latest, Rosc::Deliver::ReadVersion(Name, Existing));
			}
			return true;
		});
		return Latest + 1;
	}

	/** What the render of each shot is made of, every shot at the version after the latest in the folder. */
	TArray<FRoscRenderSpec> MakeSpecs(ERoscDeliverIntent Intent, const FRoscDeliverSettings& Settings, TConstArrayView<FGatheredShot> Shots, int32& OutVersion)
	{
		TArray<FString> Names;
		for (const FGatheredShot& Shot : Shots)
		{
			Names.Add(Rosc::Deliver::ResolveFileName(Settings.NamePattern, Intent, Settings, Shot.Deliver));
		}
		OutVersion = FindNextVersion(Settings.OutputDirectory / Rosc::Deliver::GetIntentFolder(Intent), Names);

		TArray<FRoscRenderSpec> Specs;
		for (const FGatheredShot& Shot : Shots)
		{
			FRoscRenderSpec& Spec = Specs.Add_GetRef(Rosc::Deliver::MakeRenderSpec(Intent, Settings, Shot.Deliver));
			Spec.FileName = Rosc::Deliver::WithVersion(Spec.FileName, OutVersion);
		}
		return Specs;
	}

	/** The binding of a camera in its shot's sequence, or an invalid guid. */
	FGuid FindCameraBinding(const FShot& Shot, const ACineCameraActor* Camera)
	{
		const UMovieScene* MovieScene = Shot.Sequence->GetMovieScene();
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			if (Rosc::ShotEditing::ResolveBinding(Shot, Binding.GetObjectGuid()).Contains(Camera))
			{
				return Binding.GetObjectGuid();
			}
		}
		return FGuid();
	}

	/** Gives a shot a camera cut to its camera, from its first frame to its last. */
	bool AddCameraCut(const FShot& Shot, const ACineCameraActor* Camera, FText& OutError)
	{
		UMovieScene* MovieScene = Shot.Sequence->GetMovieScene();
		if (MovieScene->IsReadOnly())
		{
			OutError = FText::Format(LOCTEXT("DeliverShotLocked", "{0} is locked. Unlock it in Sequencer, then add the camera cut."), FText::FromString(Shot.Name));
			return false;
		}
		const FGuid CameraBinding = FindCameraBinding(Shot, Camera);
		if (!CameraBinding.IsValid())
		{
			OutError = FText::Format(LOCTEXT("DeliverCameraNotInShot", "The camera of {0} is not in the shot's own sequence. Add it there in Sequencer, then add the camera cut."), FText::FromString(Shot.Name));
			return false;
		}

		{
			const FScopedTransaction Transaction(LOCTEXT("DeliverAddCameraCut", "Add Camera Cut"));
			MovieScene->Modify();
			UMovieSceneCameraCutTrack* CameraCuts = Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
			if (CameraCuts == nullptr)
			{
				CameraCuts = CastChecked<UMovieSceneCameraCutTrack>(MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
			}
			CameraCuts->Modify();
			const TRange<FFrameNumber> WholeShot = MovieScene->GetPlaybackRange();
			UMovieSceneCameraCutSection* CameraCut = CameraCuts->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(CameraBinding), UE::MovieScene::DiscreteInclusiveLower(WholeShot));
			CameraCut->SetRange(WholeShot);
		}
		ULevelSequenceEditorBlueprintLibrary::ForceUpdate();
		return true;
	}

	/** Sets the project to work in ACEScg from the next time the editor starts. */
	bool WorkInAcesCg(FText& OutError)
	{
		URendererSettings* Renderer = GetMutableDefault<URendererSettings>();
		const FString SettingsFile = FPaths::ConvertRelativePathToFull(Renderer->GetDefaultConfigFilename());
		if (IFileManager::Get().FileExists(*SettingsFile) && IFileManager::Get().IsReadOnly(*SettingsFile))
		{
			OutError = FText::Format(LOCTEXT("DeliverSettingsReadOnly", "The project's settings file cannot be written: {0}. Make it writable (check it out of source control), then try again."), FText::FromString(SettingsFile));
			return false;
		}

		// Only the choice is written. The editor goes on working in the colour space it started in,
		// so its pictures stay all of one kind until it is restarted.
		Renderer->WorkingColorSpaceChoice = EWorkingColorSpace::ACESAP1;
		Renderer->UpdateSinglePropertyInConfigFile(
			FindFProperty<FProperty>(URendererSettings::StaticClass(), GET_MEMBER_NAME_CHECKED(URendererSettings, WorkingColorSpaceChoice)),
			Renderer->GetDefaultConfigFilename());
		return true;
	}
}

void Rosc::DeliverEditing::NoteColourSpaceAtStartup()
{
	ColourSpaceAtStartup = UE::Color::FColorSpace::GetWorking();
}

FString Rosc::DeliverEditing::GetDeliveryFolder()
{
	const FString& Chosen = GetDefault<URoscEditorSettings>()->DeliverFolder.Path;
	return FPaths::ConvertRelativePathToFull(Chosen.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Rosc") / TEXT("Deliveries") : Chosen);
}

FRoscDeliverSettings Rosc::DeliverEditing::GetSettings()
{
	const URoscEditorSettings* Choices = GetDefault<URoscEditorSettings>();
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();

	FRoscDeliverSettings Settings;
	Settings.Sequence = OpenSequence != nullptr ? OpenSequence->GetName() : FString();
	Settings.OutputDirectory = GetDeliveryFolder() / Settings.Sequence;
	Settings.NamePattern = Choices->DeliverNamePattern;
	Settings.Date = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
	Settings.SceneLinearColourSpace = GetColourSpaceName(GetPictureColourSpace());
	Settings.DistanceUnits = Choices->DistanceUnits;
	Settings.bSlate = Choices->bDeliverSlate;
	Settings.bBurnIn = Choices->bDeliverBurnIn;
	Settings.bFourK = Choices->bDeliverFourK;
	return Settings;
}

void Rosc::DeliverEditing::EditChoices(TFunctionRef<void(URoscEditorSettings&)> Edit)
{
	URoscEditorSettings* Choices = GetMutableDefault<URoscEditorSettings>();
	Edit(*Choices);
	Choices->SaveConfig();
}

Rosc::DeliverEditing::FCheck Rosc::DeliverEditing::CheckDelivery(ERoscDeliverIntent Intent)
{
	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	const TArray<FGatheredShot> Shots = GatherShots(OpenSequence);

	FCheck Check;
	Check.Findings = Rosc::Deliver::RunPreflight(MakePreflightFacts(Intent, OpenSequence, Shots));

	const FRoscDeliverSettings Settings = GetSettings();
	if (Rosc::Deliver::ValidateNamePattern(Settings.NamePattern).IsEmpty())
	{
		int32 Version = 0;
		for (const FRoscRenderSpec& Spec : MakeSpecs(Intent, Settings, Shots, Version))
		{
			Check.Files.Add(Rosc::Deliver::PreviewFile(Intent, Spec));
		}
	}
	return Check;
}

bool Rosc::DeliverEditing::ApplyFix(const FRoscPreflightFinding& Finding, FText& OutError)
{
	if (Finding.Check == ERoscPreflightCheck::WorkingColourSpace)
	{
		return WorkInAcesCg(OutError);
	}
	if (Finding.Check != ERoscPreflightCheck::ShotWithoutCameraCut && Finding.Check != ERoscPreflightCheck::FrameRateMismatch)
	{
		OutError = LOCTEXT("DeliverNoFix", "This has to be put right by hand.");
		return false;
	}

	const ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	const TArray<FShot> Shots = Rosc::ShotEditing::GetShots();
	const FShot* Shot = Shots.FindByPredicate([&Finding](const FShot& Candidate) { return Candidate.Name == Finding.Shot; });
	if (OpenSequence == nullptr || Shot == nullptr)
	{
		OutError = FText::Format(LOCTEXT("DeliverShotGone", "{0} is no longer a shot of the open sequence. Check again."), FText::FromString(Finding.Shot));
		return false;
	}

	const Rosc::SequenceUtils::FScopedPlayhead PutPlayheadBack;
	PlayToShotStart(OpenSequence, *Shot);
	const ACineCameraActor* Camera = Rosc::ShotEditing::FindShotCamera(*Shot);
	if (Camera == nullptr)
	{
		OutError = FText::Format(LOCTEXT("DeliverCameraGone", "{0} no longer has a camera. Check again."), FText::FromString(Finding.Shot));
		return false;
	}

	if (Finding.Check == ERoscPreflightCheck::ShotWithoutCameraCut)
	{
		return AddCameraCut(*Shot, Camera, OutError);
	}

	URoscCameraPackageComponent* Component = URoscCameraPackageComponent::Find(Camera);
	if (Component == nullptr)
	{
		OutError = FText::Format(LOCTEXT("DeliverPackageGone", "The camera of {0} no longer has a camera package. Check again."), FText::FromString(Finding.Shot));
		return false;
	}
	const float FramesPerSecond = static_cast<float>(OpenSequence->GetMovieScene()->GetDisplayRate().AsDecimal());
	return Rosc::CameraEditing::EditPackage(Component, LOCTEXT("DeliverMatchFrameRate", "Match Camera Frame Rate"),
		[FramesPerSecond](FRoscCameraPackage& Package) { Package.FramesPerSecond = FramesPerSecond; }, OutError);
}

bool Rosc::DeliverEditing::StartDelivery(ERoscDeliverIntent Intent, FDelivery& OutDelivery, FText& OutError)
{
	using namespace Rosc::Deliver;

	OutDelivery = FDelivery();
	const FRoscDeliverSettings Settings = GetSettings();
	const FText PatternError = ValidateNamePattern(Settings.NamePattern);
	if (!PatternError.IsEmpty())
	{
		OutError = PatternError;
		return false;
	}

	URoscRenderSubsystem* Render = GEditor->GetEditorSubsystem<URoscRenderSubsystem>();
	if (Render->IsRendering())
	{
		OutError = LOCTEXT("DeliverAlreadyRendering", "A render is already running. Wait for it to finish, or stop it.");
		return false;
	}

	ULevelSequence* OpenSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	const TArray<FGatheredShot> Shots = GatherShots(OpenSequence);
	for (const FRoscPreflightFinding& Finding : RunPreflight(MakePreflightFacts(Intent, OpenSequence, Shots)))
	{
		if (Finding.bStopsRender)
		{
			OutError = Finding.Shot.IsEmpty()
				? FText::Format(LOCTEXT("DeliverStopped", "{0}. {1}"), Finding.Title, Finding.Detail)
				: FText::Format(LOCTEXT("DeliverStoppedByShot", "{0}: {1}. {2}"), FText::FromString(Finding.Shot), Finding.Title, Finding.Detail);
			return false;
		}
	}

	OutDelivery.Directory = Settings.OutputDirectory / GetIntentFolder(Intent);
	const TArray<FRoscRenderSpec> Specs = MakeSpecs(Intent, Settings, Shots, OutDelivery.Version);

	if (Intent == ERoscDeliverIntent::VfxHandoff)
	{
		// The camera of each shot is handed over as it was baked for handoff: free of rig and plugin.
		TArray<Rosc::Handoff::FBakedShot> BakedShots;
		ULevelSequence* CutCopy = Rosc::Handoff::BakeForHandoff(OutError, &BakedShots);
		if (CutCopy == nullptr)
		{
			return false;
		}
		// Both lists are the shots of the open sequence in the order they play.
		check(BakedShots.Num() == Shots.Num());

		const double FramesPerSecond = OpenSequence->GetMovieScene()->GetDisplayRate().AsDecimal();
		for (int32 ShotIndex = 0; ShotIndex < Shots.Num(); ++ShotIndex)
		{
			const FString Sidecar = GetCompanionFile(Specs[ShotIndex], TEXT("json"));
			const FString Camera = GetCompanionFile(Specs[ShotIndex], TEXT("fbx"));
			if (!FFileHelper::SaveStringToFile(WriteSidecarJson(Specs[ShotIndex], FramesPerSecond, BakedShots[ShotIndex].Samples), *Sidecar, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				|| !Rosc::Handoff::ExportCameraFbx(CutCopy, BakedShots[ShotIndex], Camera))
			{
				OutError = FText::Format(LOCTEXT("DeliverCameraNotWritten", "The camera files of {0} could not be written to {1}. Check that the folder can be written to."),
					FText::FromString(Shots[ShotIndex].Shot.Name), FText::FromString(FPaths::GetPath(Sidecar)));
				return false;
			}
			OutDelivery.Files.Add(Sidecar);
			OutDelivery.Files.Add(Camera);
		}
	}

	TArray<FRoscRenderShot> RenderShots;
	for (int32 ShotIndex = 0; ShotIndex < Shots.Num(); ++ShotIndex)
	{
		// A sequence that is not cut into shots is rendered as one, whatever the renderer calls it.
		RenderShots.Add({ Shots[ShotIndex].Shot.Section != nullptr ? Shots[ShotIndex].Shot.Name : FString(), Specs[ShotIndex] });
	}
	return Render->StartRender(OpenSequence, RenderShots, OutError);
}

#undef LOCTEXT_NAMESPACE
