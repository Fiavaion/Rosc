#include "CineCameraActor.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UEcineRig.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineRigActorTest, "UEcine.Runtime.Rig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineRigActorTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	// Stand the rig somewhere other than the origin, turned, so local and world space differ.
	const FTransform RigTransform(FRotator(0.0, 90.0, 0.0), FVector(1000.0, 2000.0, 50.0));
	AUEcineRig* Rig = World->SpawnActor<AUEcineRig>(AUEcineRig::StaticClass(), RigTransform);
	ACineCameraActor* Camera = World->SpawnActor<ACineCameraActor>();
	const double Tolerance = 0.05;

	TestTrue(TEXT("A new rig is a tripod"), Rig->GetSpec().Type == EUEcineRigType::Tripod);
	TestNull(TEXT("A new rig carries no camera"), Rig->GetMountedCamera());

	Rig->MountCamera(Camera);
	TestTrue(TEXT("The mounted camera is found"), Rig->GetMountedCamera() == Camera);
	TestTrue(TEXT("The camera sits on top of the tripod"), Camera->GetActorLocation().Equals(FVector(1000.0, 2000.0, 200.0), Tolerance));

	// Crane: the camera goes where the arm takes it, in world space.
	const FUEcineRigPreset& Crane = *UEcine::Rig::FindPreset(TEXT("crane-30ft"));
	FUEcineRigPose Pose = Crane.DefaultPose;
	Pose.Lift = 200.0;
	Pose.Extension = 500.0;
	Pose.Boom = 30.0;
	Rig->SetSpec(Crane.Spec, Pose);
	// Arm points along the rig's forward axis, which faces world +Y: 433 cm out and 250 cm up from the pivot.
	TestTrue(TEXT("Crane: the camera is at the end of the arm"), Camera->GetActorLocation().Equals(FVector(1000.0, 2433.0127, 500.0), Tolerance));

	// The setters Sequencer calls move the camera too.
	Rig->SetSwing(-90.0f);
	TestTrue(TEXT("Crane: swinging the arm carries the camera round"), Camera->GetActorLocation().Equals(FVector(1433.0127, 2000.0, 500.0), Tolerance));
	Rig->SetPan(90.0f);
	TestTrue(TEXT("Crane: panning the head turns the camera"), Camera->GetActorForwardVector().Equals(FVector(0.0, 1.0, 0.0), 1.e-3));
	TestEqual(TEXT("The pose reads back what was set"), Rig->GetPose().Swing, -90.0, 1.e-4);

	// Every axis has a property that Sequencer can key and a setter it will call.
	for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
	{
		const FName PropertyName = AUEcineRig::GetAxisPropertyName(static_cast<EUEcineRigAxis>(AxisIndex));
		TestNotNull(*FString::Printf(TEXT("Axis property %s exists"), *PropertyName.ToString()), FindFProperty<FFloatProperty>(AUEcineRig::StaticClass(), PropertyName));
		TestNotNull(*FString::Printf(TEXT("Axis property %s has a setter"), *PropertyName.ToString()), AUEcineRig::StaticClass()->FindFunctionByName(*(TEXT("Set") + PropertyName.ToString())));
	}

	// Dolly: travel follows the track, and the travel range is the length of track laid.
	const FUEcineRigPreset& Dolly = UEcine::Rig::GetDefaultPreset(EUEcineRigType::Dolly);
	Rig->SetSpec(Dolly.Spec, Dolly.DefaultPose);
	TestEqual(TEXT("Dolly: default track length"), Rig->GetTrackLength(), 600.0f, 0.1f);
	Rig->SetStraightTrack(900.0f);
	TestEqual(TEXT("Dolly: the travel range is the track length"), Rig->GetSpec().GetAxis(EUEcineRigAxis::Travel).Max, 900.0, 0.1);
	Rig->SetTravel(450.0f);
	TestTrue(TEXT("Dolly: halfway down straight track"), Camera->GetActorLocation().Equals(FVector(1000.0, 2450.0, 50.0 + Dolly.DefaultPose.Lift), Tolerance));

	// Curved track: a quarter turn. The dolly ends up facing the way the track leads.
	Rig->GetTrack()->SetSplinePoints({ FVector::ZeroVector, FVector(300.0, 0.0, 0.0), FVector(600.0, 300.0, 0.0), FVector(600.0, 600.0, 0.0) }, ESplineCoordinateSpace::Local);
	Rig->SetTravel(Rig->GetTrackLength());
	const FVector EndOfTrack = RigTransform.TransformPosition(FVector(600.0, 600.0, Dolly.DefaultPose.Lift));
	TestTrue(TEXT("Dolly: the end of curved track"), Camera->GetActorLocation().Equals(EndOfTrack, 0.5));
	TestTrue(TEXT("Dolly: the camera faces along the track"), FVector::DotProduct(Camera->GetActorForwardVector(), RigTransform.TransformVector(FVector(0.0, 1.0, 0.0))) > 0.95);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
