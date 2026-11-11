#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UEcineCameraDatabase.h"
#include "UEcineCameraPackageComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineCameraPackageComponentApplyTest, "UEcine.Runtime.CameraPackageComponent.Apply", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineCameraPackageComponentApplyTest::RunTest(const FString& Parameters)
{
	// Uses the shipped database: an ALEXA 35 in its 6:5 anamorphic mode with a 2x anamorphic lens.
	const FUEcineCameraDatabase& Database = FUEcineCameraDatabase::Get();
	const FUEcineCameraBody* Body = Database.FindBody(TEXT("arri-alexa-35"));
	const FUEcineLensKit* Kit = Database.FindLensKit(TEXT("arri-master-anamorphic"));
	if (!TestNotNull(TEXT("The shipped database has the ALEXA 35"), Body) || !TestNotNull(TEXT("The shipped database has the Master Anamorphics"), Kit))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	ACineCameraActor* CameraActor = World->SpawnActor<ACineCameraActor>();
	UCineCameraComponent* Camera = CameraActor->GetCineCameraComponent();

	UUEcineCameraPackageComponent* PackageComponent = NewObject<UUEcineCameraPackageComponent>(CameraActor);
	CameraActor->AddInstanceComponent(PackageComponent);
	PackageComponent->RegisterComponent();

	TestTrue(TEXT("The component is found on the actor"), UUEcineCameraPackageComponent::Find(CameraActor) == PackageComponent);

	FUEcineCameraPackage& Package = PackageComponent->Package;
	Package.SelectBody(*Body);
	Package.FormatId = TEXT("3.3k-6x5");
	Package.FocalLengthMm = 50.0f;
	Package.SelectLensKit(*Kit);
	Package.TStop = 2.8f;
	Package.ShutterAngleDeg = 90.0f;

	FText Error;
	TArray<FText> Warnings;
	if (TestTrue(TEXT("The package applies"), PackageComponent->ApplyToCamera(Error, &Warnings)))
	{
		const FUEcineRecordingFormat* Format = Body->FindFormat(TEXT("3.3k-6x5"));
		TestEqual(TEXT("Filmback width"), Camera->Filmback.SensorWidth, static_cast<float>(Format->SensorWidthMm));
		TestEqual(TEXT("Filmback height"), Camera->Filmback.SensorHeight, static_cast<float>(Format->SensorHeightMm));
		TestEqual(TEXT("Squeeze"), Camera->LensSettings.SqueezeFactor, 2.0f);
		TestEqual(TEXT("Focal length"), Camera->CurrentFocalLength, 50.0f);
		TestEqual(TEXT("A prime locks the focal length range"), Camera->LensSettings.MinFocalLength, Camera->LensSettings.MaxFocalLength);
		TestEqual(TEXT("Aperture"), Camera->CurrentAperture, 2.8f);
		TestEqual(TEXT("Widest aperture limit"), Camera->LensSettings.MinFStop, 1.9f);
		TestEqual(TEXT("Iris blades"), Camera->LensSettings.DiaphragmBladeCount, Kit->IrisBlades);
		TestTrue(TEXT("Close focus is set in millimetres"), Camera->LensSettings.MinimumFocusDistance > 100.0f);

		// The engine's own field of view must agree with UEcine's optics: 2x squeeze on a 20.22 mm wide area at 50 mm.
		TestEqual(TEXT("Engine horizontal field of view"), Camera->GetHorizontalFieldOfView(), 44.03f, 0.02f);

		TestTrue(TEXT("Motion blur is overridden"), Camera->PostProcessSettings.bOverride_MotionBlurAmount);
		TestEqual(TEXT("A 90 degree shutter is a quarter-frame of motion blur"), Camera->PostProcessSettings.MotionBlurAmount, 0.25f);
		TestFalse(TEXT("Exposure is left to the scene when physical exposure is off"), Camera->PostProcessSettings.bOverride_AutoExposureMethod);
		TestEqual(TEXT("No warnings for a covered format"), Warnings.Num(), 0);
	}
	else
	{
		AddError(Error.ToString());
	}

	// Physical exposure: ISO, shutter and ND reach the post-process settings.
	Package.bPhysicalExposure = true;
	Package.Iso = 1600;
	Package.NdDensity = 0.6f;
	Package.FramesPerSecond = 25.0f;
	Package.ShutterAngleDeg = 180.0f;
	if (TestTrue(TEXT("The package applies with physical exposure"), PackageComponent->ApplyToCamera(Error)))
	{
		const FPostProcessSettings& PostProcess = Camera->PostProcessSettings;
		TestTrue(TEXT("Exposure method is overridden"), PostProcess.bOverride_AutoExposureMethod);
		TestTrue(TEXT("Exposure is manual"), PostProcess.AutoExposureMethod == AEM_Manual);
		TestTrue(TEXT("Physical camera exposure is applied"), PostProcess.bOverride_AutoExposureApplyPhysicalCameraExposure && PostProcess.AutoExposureApplyPhysicalCameraExposure);
		TestEqual(TEXT("ISO"), PostProcess.CameraISO, 1600.0f);
		TestEqual(TEXT("180 degrees at 25 fps is 1/50 s"), PostProcess.CameraShutterSpeed, 50.0f, 1e-3f);
		TestEqual(TEXT("ND 0.6 darkens by two stops"), PostProcess.AutoExposureBias, -1.9932f, 1e-3f);
	}

	// A package naming unknown equipment leaves the camera as it was.
	Package.BodyId = TEXT("not-a-camera");
	const float FocalBefore = Camera->CurrentFocalLength;
	TestFalse(TEXT("Unknown equipment does not apply"), PackageComponent->ApplyToCamera(Error));
	TestEqual(TEXT("The camera is untouched after a failed apply"), Camera->CurrentFocalLength, FocalBefore);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
