#include "UEcineRig.h"

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

AUEcineRig::AUEcineRig()
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

	const FUEcineRigPreset& Preset = UEcine::Rig::GetDefaultPreset(EUEcineRigType::Tripod);
	Spec = Preset.Spec;
	Lift = static_cast<float>(Preset.DefaultPose.Lift);
	Operator.Style = UEcine::Operator::GetDefaultStyle(Spec.Type);
}

FUEcineRigSpec AUEcineRig::GetSpec() const
{
	FUEcineRigSpec Result = Spec;
	if (Spec.Type == EUEcineRigType::Dolly)
	{
		FUEcineAxisLimits& TravelLimits = Result.GetAxis(EUEcineRigAxis::Travel);
		TravelLimits.Min = 0.0;
		TravelLimits.Max = GetTrackLength();
	}
	return Result;
}

void AUEcineRig::SetSpec(const FUEcineRigSpec& NewSpec, const FUEcineRigPose& NewPose)
{
	Spec = NewSpec;
	SetPose(NewPose);
}

FUEcineRigPose AUEcineRig::GetPose() const
{
	FUEcineRigPose Pose;
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

void AUEcineRig::SetPose(const FUEcineRigPose& NewPose)
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

FTransform AUEcineRig::GetTrackPoint(double TravelCm) const
{
	if (Spec.Type != EUEcineRigType::Dolly)
	{
		return UEcine::Rig::StraightTrackPoint(TravelCm);
	}
	const FTransform OnSpline = Track->GetTransformAtDistanceAlongSpline(static_cast<float>(TravelCm), ESplineCoordinateSpace::Local);
	// The track component sits at the rig's origin, so its local space is the rig's.
	return FTransform(OnSpline.GetRotation(), OnSpline.GetLocation());
}

FTransform AUEcineRig::SolveMount(const FUEcineRigPose& Pose) const
{
	return UEcine::Rig::SolveMount(Spec.Type, Pose, GetTrackPoint(Pose.Travel));
}

void AUEcineRig::SetStraightTrack(float LengthCm)
{
	Track->SetSplinePoints({ FVector::ZeroVector, FVector(FMath::Max(LengthCm, 10.0f), 0.0, 0.0) }, ESplineCoordinateSpace::Local);
	UpdateRig();
}

float AUEcineRig::GetTrackLength() const
{
	return Track->GetSplineLength();
}

ACineCameraActor* AUEcineRig::GetMountedCamera() const
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

void AUEcineRig::MountCamera(ACineCameraActor* Camera)
{
	if (Camera != nullptr)
	{
		Camera->AttachToComponent(CameraMount, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

void AUEcineRig::SetTravel(float Value) { Travel = Value; UpdateRig(); }
void AUEcineRig::SetTraverse(float Value) { Traverse = Value; UpdateRig(); }
void AUEcineRig::SetLift(float Value) { Lift = Value; UpdateRig(); }
void AUEcineRig::SetSwing(float Value) { Swing = Value; UpdateRig(); }
void AUEcineRig::SetBoom(float Value) { Boom = Value; UpdateRig(); }
void AUEcineRig::SetExtension(float Value) { Extension = Value; UpdateRig(); }
void AUEcineRig::SetPan(float Value) { Pan = Value; UpdateRig(); }
void AUEcineRig::SetTilt(float Value) { Tilt = Value; UpdateRig(); }
void AUEcineRig::SetRoll(float Value) { Roll = Value; UpdateRig(); }

FName AUEcineRig::GetAxisPropertyName(EUEcineRigAxis Axis)
{
	switch (Axis)
	{
	case EUEcineRigAxis::Travel: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Travel);
	case EUEcineRigAxis::Traverse: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Traverse);
	case EUEcineRigAxis::Lift: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Lift);
	case EUEcineRigAxis::Swing: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Swing);
	case EUEcineRigAxis::Boom: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Boom);
	case EUEcineRigAxis::Extension: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Extension);
	case EUEcineRigAxis::Pan: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Pan);
	case EUEcineRigAxis::Tilt: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Tilt);
	case EUEcineRigAxis::Roll: return GET_MEMBER_NAME_CHECKED(AUEcineRig, Roll);
	}
	checkNoEntry();
	return NAME_None;
}

void AUEcineRig::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateRig();
}

#if WITH_EDITOR
void AUEcineRig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateRig();
}

void AUEcineRig::PostEditUndo()
{
	Super::PostEditUndo();
	UpdateRig();
}
#endif

void AUEcineRig::UpdateRig()
{
	const FUEcineRigPose Pose = GetPose();
	const FTransform Mount = SolveMount(Pose);
	CameraMount->SetRelativeTransform(Mount);

	Track->SetVisibility(Spec.Type == EUEcineRigType::Dolly);
	ArmShape->SetVisibility(false);
	ColumnShape->SetVisibility(false);

	const FVector Up = FVector::UpVector;
	switch (Spec.Type)
	{
	case EUEcineRigType::Tripod:
		PlacePlate(BaseShape, FVector::ZeroVector, 70.0, 2.0);
		PlaceBeam(ColumnShape, Up * 2.0, Up * (Pose.Lift - HeadClearanceCm), 8.0, 8.0);
		break;

	case EUEcineRigType::Slider:
	{
		// The rail, on a stand under its middle.
		const FUEcineAxisLimits& Rail = Spec.GetAxis(EUEcineRigAxis::Travel);
		const double RailHeight = Pose.Lift - HeadClearanceCm;
		PlaceBeam(BaseShape, FVector(Rail.Min - 10.0, 0.0, RailHeight), FVector(Rail.Max + 10.0, 0.0, RailHeight), 12.0, 6.0);
		const double Middle = (Rail.Min + Rail.Max) * 0.5;
		PlaceBeam(ColumnShape, FVector(Middle, 0.0, 0.0), FVector(Middle, 0.0, RailHeight - 3.0), 8.0, 8.0);
		break;
	}

	case EUEcineRigType::Dolly:
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

	case EUEcineRigType::Jib:
	case EUEcineRigType::Crane:
	{
		// A weighted base, a column up to the pivot, and the arm with its counterweight end behind.
		const FVector Pivot = Up * Pose.Lift;
		const FVector ArmDirection = FRotator(Pose.Boom, Pose.Swing, 0.0).Vector();
		const double TailLength = Spec.Type == EUEcineRigType::Crane ? 180.0 : Pose.Extension * 0.35;
		PlacePlate(BaseShape, FVector::ZeroVector, 120.0, 30.0);
		PlaceBeam(ColumnShape, Up * 30.0, Pivot, 20.0, 20.0);
		PlaceBeam(ArmShape, Pivot - ArmDirection * TailLength, Pivot + ArmDirection * FMath::Max(Pose.Extension - HeadClearanceCm - 15.0, 1.0), 14.0, 14.0);
		break;
	}

	case EUEcineRigType::Steadicam:
	case EUEcineRigType::Handheld:
	case EUEcineRigType::Drone:
		// Nothing stands on the floor; a plate marks where the operator or pilot starts from.
		PlacePlate(BaseShape, FVector::ZeroVector, 40.0, 1.0);
		break;
	}
}
