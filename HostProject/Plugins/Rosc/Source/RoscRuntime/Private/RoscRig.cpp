// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscRig.h"

#include "CineCameraActor.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr double DefaultTrackLengthCm = 600.0;

	/** The gap left between the top of a column or the end of an arm and the camera, so the shapes stay out of the lens. */
	constexpr double HeadClearanceCm = 20.0;

	/** Stretches a unit cube into a beam from Start to End. A beam too short to see is hidden. */
	void PlaceBeam(UStaticMeshComponent* Shape, const FVector& Start, const FVector& End, double WidthCm, double HeightCm)
	{
		const FVector Span = End - Start;
		const double Length = Span.Size();
		if (Length < 1.0)
		{
			Shape->SetVisibility(false);
			return;
		}
		Shape->SetVisibility(true);
		Shape->SetRelativeLocationAndRotation((Start + End) * 0.5, FRotationMatrix::MakeFromX(Span).Rotator());
		// The engine's basic cube is 100 cm on a side.
		Shape->SetRelativeScale3D(FVector(Length, WidthCm, HeightCm) / 100.0);
	}

	/** A flat plate on the floor, centred on a point. */
	void PlacePlate(UStaticMeshComponent* Shape, const FVector& Centre, double SizeCm, double ThicknessCm)
	{
		const FVector Half(SizeCm * 0.5, 0.0, 0.0);
		const FVector Raised = Centre + FVector(0.0, 0.0, ThicknessCm * 0.5);
		PlaceBeam(Shape, Raised - Half, Raised + Half, SizeCm, ThicknessCm);
	}
}

ARoscRig::ARoscRig()
{
	PrimaryActorTick.bCanEverTick = false;

	Base = CreateDefaultSubobject<USceneComponent>(TEXT("Base"));
	SetRootComponent(Base);

	CameraMount = CreateDefaultSubobject<USceneComponent>(TEXT("CameraMount"));
	CameraMount->SetupAttachment(Base);

	Track = CreateDefaultSubobject<USplineComponent>(TEXT("Track"));
	Track->SetupAttachment(Base);
	Track->SetSplinePoints({ FVector::ZeroVector, FVector(DefaultTrackLengthCm, 0.0, 0.0) }, ESplineCoordinateSpace::Local);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	const auto MakeShape = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* Shape = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Shape->SetupAttachment(Base);
		Shape->SetStaticMesh(Cube.Object);
		Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shape->SetGenerateOverlapEvents(false);
		Shape->SetCastShadow(false);
		Shape->SetHiddenInGame(true);
		return Shape;
	};
	BaseShape = MakeShape(TEXT("BaseShape"));
	ColumnShape = MakeShape(TEXT("ColumnShape"));
	ArmShape = MakeShape(TEXT("ArmShape"));

	const FRoscRigPreset& Preset = Rosc::Rig::GetDefaultPreset(ERoscRigType::Tripod);
	Spec = Preset.Spec;
	Lift = static_cast<float>(Preset.DefaultPose.Lift);
	Operator.Style = Rosc::Operator::GetDefaultStyle(Spec.Type);
}

FRoscRigSpec ARoscRig::GetSpec() const
{
	FRoscRigSpec Result = Spec;
	if (Spec.Type == ERoscRigType::Dolly)
	{
		FRoscAxisLimits& TravelLimits = Result.GetAxis(ERoscRigAxis::Travel);
		TravelLimits.Min = 0.0;
		TravelLimits.Max = GetTrackLength();
	}
	return Result;
}

void ARoscRig::SetSpec(const FRoscRigSpec& NewSpec, const FRoscRigPose& NewPose)
{
	Spec = NewSpec;
	SetPose(NewPose);
}

FRoscRigPose ARoscRig::GetPose() const
{
	FRoscRigPose Pose;
	Pose.Travel = Travel;
	Pose.Traverse = Traverse;
	Pose.Lift = Lift;
	Pose.Swing = Swing;
	Pose.Boom = Boom;
	Pose.Extension = Extension;
	Pose.Pan = Pan;
	Pose.Tilt = Tilt;
	Pose.Roll = Roll;
	return Pose;
}

void ARoscRig::SetPose(const FRoscRigPose& NewPose)
{
	Travel = static_cast<float>(NewPose.Travel);
	Traverse = static_cast<float>(NewPose.Traverse);
	Lift = static_cast<float>(NewPose.Lift);
	Swing = static_cast<float>(NewPose.Swing);
	Boom = static_cast<float>(NewPose.Boom);
	Extension = static_cast<float>(NewPose.Extension);
	Pan = static_cast<float>(NewPose.Pan);
	Tilt = static_cast<float>(NewPose.Tilt);
	Roll = static_cast<float>(NewPose.Roll);
	UpdateRig();
}

FTransform ARoscRig::GetTrackPoint(double TravelCm) const
{
	if (Spec.Type != ERoscRigType::Dolly)
	{
		return Rosc::Rig::StraightTrackPoint(TravelCm);
	}
	const FTransform OnSpline = Track->GetTransformAtDistanceAlongSpline(static_cast<float>(TravelCm), ESplineCoordinateSpace::Local);
	// The track component sits at the rig's origin, so its local space is the rig's.
	return FTransform(OnSpline.GetRotation(), OnSpline.GetLocation());
}

FTransform ARoscRig::SolveMount(const FRoscRigPose& Pose) const
{
	return Rosc::Rig::SolveMount(Spec.Type, Pose, GetTrackPoint(Pose.Travel));
}

void ARoscRig::SetStraightTrack(float LengthCm)
{
	Track->SetSplinePoints({ FVector::ZeroVector, FVector(FMath::Max(LengthCm, 10.0f), 0.0, 0.0) }, ESplineCoordinateSpace::Local);
	UpdateRig();
}

float ARoscRig::GetTrackLength() const
{
	return Track->GetSplineLength();
}

bool ARoscRig::HasChangedSinceBake() const
{
	if (BakedMarks.IsEmpty())
	{
		return false;
	}
	if (BakedMarks.Num() != Marks.Num() || !FRoscOperator::StaticStruct()->CompareScriptStruct(&BakedOperator, &Operator, PPF_None))
	{
		return true;
	}
	for (int32 Index = 0; Index < Marks.Num(); ++Index)
	{
		if (!FRoscRigMark::StaticStruct()->CompareScriptStruct(&BakedMarks[Index], &Marks[Index], PPF_None))
		{
			return true;
		}
	}
	return false;
}

ACineCameraActor* ARoscRig::GetMountedCamera() const
{
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* Attached : AttachedActors)
	{
		ACineCameraActor* Camera = Cast<ACineCameraActor>(Attached);
		if (Camera != nullptr && Camera->GetRootComponent()->GetAttachParent() == CameraMount)
		{
			return Camera;
		}
	}
	return nullptr;
}

void ARoscRig::MountCamera(ACineCameraActor* Camera)
{
	if (Camera != nullptr)
	{
		Camera->AttachToComponent(CameraMount, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

void ARoscRig::SetTravel(float Value) { Travel = Value; UpdateRig(); }
void ARoscRig::SetTraverse(float Value) { Traverse = Value; UpdateRig(); }
void ARoscRig::SetLift(float Value) { Lift = Value; UpdateRig(); }
void ARoscRig::SetSwing(float Value) { Swing = Value; UpdateRig(); }
void ARoscRig::SetBoom(float Value) { Boom = Value; UpdateRig(); }
void ARoscRig::SetExtension(float Value) { Extension = Value; UpdateRig(); }
void ARoscRig::SetPan(float Value) { Pan = Value; UpdateRig(); }
void ARoscRig::SetTilt(float Value) { Tilt = Value; UpdateRig(); }
void ARoscRig::SetRoll(float Value) { Roll = Value; UpdateRig(); }

FName ARoscRig::GetAxisPropertyName(ERoscRigAxis Axis)
{
	switch (Axis)
	{
	case ERoscRigAxis::Travel: return GET_MEMBER_NAME_CHECKED(ARoscRig, Travel);
	case ERoscRigAxis::Traverse: return GET_MEMBER_NAME_CHECKED(ARoscRig, Traverse);
	case ERoscRigAxis::Lift: return GET_MEMBER_NAME_CHECKED(ARoscRig, Lift);
	case ERoscRigAxis::Swing: return GET_MEMBER_NAME_CHECKED(ARoscRig, Swing);
	case ERoscRigAxis::Boom: return GET_MEMBER_NAME_CHECKED(ARoscRig, Boom);
	case ERoscRigAxis::Extension: return GET_MEMBER_NAME_CHECKED(ARoscRig, Extension);
	case ERoscRigAxis::Pan: return GET_MEMBER_NAME_CHECKED(ARoscRig, Pan);
	case ERoscRigAxis::Tilt: return GET_MEMBER_NAME_CHECKED(ARoscRig, Tilt);
	case ERoscRigAxis::Roll: return GET_MEMBER_NAME_CHECKED(ARoscRig, Roll);
	}
	checkNoEntry();
	return NAME_None;
}

void ARoscRig::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateRig();
}

#if WITH_EDITOR
void ARoscRig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateRig();
}

void ARoscRig::PostEditUndo()
{
	Super::PostEditUndo();
	UpdateRig();
}
#endif

void ARoscRig::Destroyed()
{
#if WITH_EDITOR
	// A camera is held in place while it is on a support (Rosc::GripEditing::PutCameraOnRig).
	// A support deleted in the level lets go of it, or nothing could move the camera again.
	if (ACineCameraActor* Camera = GetMountedCamera())
	{
		Camera->Modify();
		Camera->SetLockLocation(false);
	}
#endif
	Super::Destroyed();
}

void ARoscRig::UpdateRig()
{
	const FRoscRigPose Pose = GetPose();
	const FTransform Mount = SolveMount(Pose);
	CameraMount->SetRelativeTransform(Mount);

	Track->SetVisibility(Spec.Type == ERoscRigType::Dolly);
	ArmShape->SetVisibility(false);
	ColumnShape->SetVisibility(false);

	const FVector Up = FVector::UpVector;
	switch (Spec.Type)
	{
	case ERoscRigType::Tripod:
		PlacePlate(BaseShape, FVector::ZeroVector, 70.0, 2.0);
		PlaceBeam(ColumnShape, Up * 2.0, Up * (Pose.Lift - HeadClearanceCm), 8.0, 8.0);
		break;

	case ERoscRigType::Slider:
	{
		// The rail, on a stand under its middle.
		const FRoscAxisLimits& Rail = Spec.GetAxis(ERoscRigAxis::Travel);
		const double RailHeight = Pose.Lift - HeadClearanceCm;
		PlaceBeam(BaseShape, FVector(Rail.Min - 10.0, 0.0, RailHeight), FVector(Rail.Max + 10.0, 0.0, RailHeight), 12.0, 6.0);
		const double Middle = (Rail.Min + Rail.Max) * 0.5;
		PlaceBeam(ColumnShape, FVector(Middle, 0.0, 0.0), FVector(Middle, 0.0, RailHeight - 3.0), 8.0, 8.0);
		break;
	}

	case ERoscRigType::Dolly:
	{
		// The chassis on the track, and the column up to the head.
		const FTransform TrackPoint = GetTrackPoint(Pose.Travel);
		const FVector Forward = TrackPoint.GetRotation().GetForwardVector();
		const FVector TrackUp = TrackPoint.GetRotation().GetUpVector();
		const FVector Chassis = TrackPoint.GetLocation() + TrackUp * 12.0;
		PlaceBeam(BaseShape, Chassis - Forward * 55.0, Chassis + Forward * 55.0, 70.0, 20.0);
		PlaceBeam(ColumnShape, Chassis + TrackUp * 10.0, Mount.GetLocation() - TrackUp * HeadClearanceCm, 12.0, 12.0);
		break;
	}

	case ERoscRigType::Jib:
	case ERoscRigType::Crane:
	{
		// A weighted base, a column up to the pivot, and the arm with its counterweight end behind.
		const FVector Pivot = Up * Pose.Lift;
		const FVector ArmDirection = FRotator(Pose.Boom, Pose.Swing, 0.0).Vector();
		const double TailLength = Spec.Type == ERoscRigType::Crane ? 180.0 : Pose.Extension * 0.35;
		PlacePlate(BaseShape, FVector::ZeroVector, 120.0, 30.0);
		PlaceBeam(ColumnShape, Up * 30.0, Pivot, 20.0, 20.0);
		PlaceBeam(ArmShape, Pivot - ArmDirection * TailLength, Pivot + ArmDirection * FMath::Max(Pose.Extension - HeadClearanceCm - 15.0, 1.0), 14.0, 14.0);
		break;
	}

	case ERoscRigType::Steadicam:
	case ERoscRigType::Handheld:
	case ERoscRigType::Drone:
		// Nothing stands on the floor; a plate marks where the operator or pilot starts from.
		PlacePlate(BaseShape, FVector::ZeroVector, 40.0, 1.0);
		break;
	}
}
