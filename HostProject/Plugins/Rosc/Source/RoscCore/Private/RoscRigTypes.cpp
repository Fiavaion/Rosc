// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscRigTypes.h"

#define LOCTEXT_NAMESPACE "RoscCore"

namespace
{
	/** Far enough that a free-moving rig never meets the edge of its range in practice. */
	constexpr double Unbounded = 1000000.0;

	void SetAxis(FRoscRigSpec& Spec, ERoscRigAxis Axis, double Min, double Max, double MaxSpeed, double MaxAcceleration)
	{
		FRoscAxisLimits& Limits = Spec.GetAxis(Axis);
		Limits.bUsed = true;
		Limits.Min = Min;
		Limits.Max = Max;
		Limits.MaxSpeed = MaxSpeed;
		Limits.MaxAcceleration = MaxAcceleration;
	}

	/** An axis that is set when the rig is built and stays put during a shot. */
	void SetFixedAxis(FRoscRigSpec& Spec, ERoscRigAxis Axis, double Min, double Max)
	{
		SetAxis(Spec, Axis, Min, Max, 0.0, 0.0);
	}

	/** Pan and tilt worked by hand on a fluid head. */
	void SetFluidHead(FRoscRigSpec& Spec)
	{
		SetAxis(Spec, ERoscRigAxis::Pan, -720.0, 720.0, 120.0, 400.0);
		SetAxis(Spec, ERoscRigAxis::Tilt, -90.0, 90.0, 90.0, 300.0);
	}

	FRoscRigPreset MakePreset(const TCHAR* Id, const FText& Name, ERoscRigType Type)
	{
		FRoscRigPreset Preset;
		Preset.Id = Id;
		Preset.Name = Name;
		Preset.Spec.PresetId = Id;
		Preset.Spec.Type = Type;
		return Preset;
	}

	FRoscRigPreset MakeSlider(const TCHAR* Id, const FText& Name, double RailLengthCm)
	{
		FRoscRigPreset Preset = MakePreset(Id, Name, ERoscRigType::Slider);
		SetAxis(Preset.Spec, ERoscRigAxis::Travel, 0.0, RailLengthCm, 50.0, 100.0);
		SetFixedAxis(Preset.Spec, ERoscRigAxis::Lift, 30.0, 170.0);
		SetFluidHead(Preset.Spec);
		Preset.DefaultPose.Lift = 120.0;
		Preset.DefaultPose.Travel = RailLengthCm * 0.5;
		return Preset;
	}

	FRoscRigPreset MakeJib(const TCHAR* Id, const FText& Name, double ArmLengthCm)
	{
		FRoscRigPreset Preset = MakePreset(Id, Name, ERoscRigType::Jib);
		SetFixedAxis(Preset.Spec, ERoscRigAxis::Lift, 100.0, 180.0);
		SetFixedAxis(Preset.Spec, ERoscRigAxis::Extension, ArmLengthCm, ArmLengthCm);
		SetAxis(Preset.Spec, ERoscRigAxis::Swing, -360.0, 360.0, 45.0, 60.0);
		SetAxis(Preset.Spec, ERoscRigAxis::Boom, -40.0, 50.0, 30.0, 45.0);
		SetFluidHead(Preset.Spec);
		Preset.DefaultPose.Lift = 150.0;
		Preset.DefaultPose.Extension = ArmLengthCm;
		return Preset;
	}

	FRoscRigPreset MakeCrane(const TCHAR* Id, const FText& Name, double MinReachCm, double MaxReachCm)
	{
		FRoscRigPreset Preset = MakePreset(Id, Name, ERoscRigType::Crane);
		SetFixedAxis(Preset.Spec, ERoscRigAxis::Lift, 150.0, 250.0);
		SetAxis(Preset.Spec, ERoscRigAxis::Extension, MinReachCm, MaxReachCm, 180.0, 200.0);
		SetAxis(Preset.Spec, ERoscRigAxis::Swing, -360.0, 360.0, 40.0, 40.0);
		SetAxis(Preset.Spec, ERoscRigAxis::Boom, -30.0, 50.0, 25.0, 30.0);
		// Remote head: three motorised axes.
		SetAxis(Preset.Spec, ERoscRigAxis::Pan, -720.0, 720.0, 120.0, 300.0);
		SetAxis(Preset.Spec, ERoscRigAxis::Tilt, -180.0, 180.0, 120.0, 300.0);
		SetAxis(Preset.Spec, ERoscRigAxis::Roll, -180.0, 180.0, 90.0, 200.0);
		Preset.DefaultPose.Lift = 200.0;
		Preset.DefaultPose.Extension = (MinReachCm + MaxReachCm) * 0.5;
		return Preset;
	}

	/** A rig that goes where its operator or pilot takes it. */
	FRoscRigPreset MakeFreeRig(const TCHAR* Id, const FText& Name, ERoscRigType Type, double MaxSpeed, double MaxAcceleration)
	{
		FRoscRigPreset Preset = MakePreset(Id, Name, Type);
		SetAxis(Preset.Spec, ERoscRigAxis::Travel, -Unbounded, Unbounded, MaxSpeed, MaxAcceleration);
		SetAxis(Preset.Spec, ERoscRigAxis::Traverse, -Unbounded, Unbounded, MaxSpeed, MaxAcceleration);
		return Preset;
	}

	TArray<FRoscRigPreset> BuildPresets()
	{
		TArray<FRoscRigPreset> Presets;

		{
			FRoscRigPreset Tripod = MakePreset(TEXT("tripod-fluid-head"), LOCTEXT("PresetTripod", "Tripod, fluid head"), ERoscRigType::Tripod);
			SetFixedAxis(Tripod.Spec, ERoscRigAxis::Lift, 60.0, 170.0);
			SetFluidHead(Tripod.Spec);
			Tripod.DefaultPose.Lift = 150.0;
			Presets.Add(MoveTemp(Tripod));
		}

		Presets.Add(MakeSlider(TEXT("slider-100"), LOCTEXT("PresetSlider100", "Slider, 1 m"), 100.0));
		Presets.Add(MakeSlider(TEXT("slider-150"), LOCTEXT("PresetSlider150", "Slider, 1.5 m"), 150.0));

		{
			FRoscRigPreset Dolly = MakePreset(TEXT("dolly"), LOCTEXT("PresetDolly", "Dolly on track"), ERoscRigType::Dolly);
			// The travel range is the length of the track the user lays; this is the default length.
			SetAxis(Dolly.Spec, ERoscRigAxis::Travel, 0.0, 600.0, 250.0, 150.0);
			SetAxis(Dolly.Spec, ERoscRigAxis::Lift, 40.0, 190.0, 30.0, 60.0);
			SetFluidHead(Dolly.Spec);
			Dolly.DefaultPose.Lift = 130.0;
			Presets.Add(MoveTemp(Dolly));
		}

		Presets.Add(MakeJib(TEXT("jib-2m"), LOCTEXT("PresetJib2", "Jib, 2 m arm"), 200.0));
		Presets.Add(MakeJib(TEXT("jib-4m"), LOCTEXT("PresetJib4", "Jib, 4 m arm"), 400.0));

		Presets.Add(MakeCrane(TEXT("crane-22ft"), LOCTEXT("PresetCrane22", "Telescoping crane, 22 ft"), 200.0, 670.0));
		Presets.Add(MakeCrane(TEXT("crane-30ft"), LOCTEXT("PresetCrane30", "Telescoping crane, 30 ft"), 280.0, 910.0));
		Presets.Add(MakeCrane(TEXT("crane-50ft"), LOCTEXT("PresetCrane50", "Telescoping crane, 50 ft"), 460.0, 1520.0));

		{
			FRoscRigPreset Steadicam = MakeFreeRig(TEXT("steadicam"), LOCTEXT("PresetSteadicam", "Steadicam"), ERoscRigType::Steadicam, 300.0, 200.0);
			SetAxis(Steadicam.Spec, ERoscRigAxis::Lift, 60.0, 200.0, 50.0, 100.0);
			SetAxis(Steadicam.Spec, ERoscRigAxis::Pan, -720.0, 720.0, 120.0, 300.0);
			SetAxis(Steadicam.Spec, ERoscRigAxis::Tilt, -60.0, 60.0, 60.0, 200.0);
			SetAxis(Steadicam.Spec, ERoscRigAxis::Roll, -15.0, 15.0, 30.0, 100.0);
			Steadicam.DefaultPose.Lift = 150.0;
			Presets.Add(MoveTemp(Steadicam));
		}
		{
			FRoscRigPreset Handheld = MakeFreeRig(TEXT("handheld"), LOCTEXT("PresetHandheld", "Handheld"), ERoscRigType::Handheld, 400.0, 300.0);
			SetAxis(Handheld.Spec, ERoscRigAxis::Lift, 20.0, 210.0, 100.0, 300.0);
			SetAxis(Handheld.Spec, ERoscRigAxis::Pan, -720.0, 720.0, 180.0, 600.0);
			SetAxis(Handheld.Spec, ERoscRigAxis::Tilt, -90.0, 90.0, 120.0, 400.0);
			SetAxis(Handheld.Spec, ERoscRigAxis::Roll, -45.0, 45.0, 60.0, 200.0);
			Handheld.DefaultPose.Lift = 160.0;
			Presets.Add(MoveTemp(Handheld));
		}
		{
			FRoscRigPreset Drone = MakeFreeRig(TEXT("drone"), LOCTEXT("PresetDrone", "Drone"), ERoscRigType::Drone, 1500.0, 600.0);
			SetAxis(Drone.Spec, ERoscRigAxis::Lift, 0.0, 12000.0, 500.0, 300.0);
			SetAxis(Drone.Spec, ERoscRigAxis::Pan, -720.0, 720.0, 100.0, 200.0);
			SetAxis(Drone.Spec, ERoscRigAxis::Tilt, -90.0, 30.0, 60.0, 200.0);
			SetAxis(Drone.Spec, ERoscRigAxis::Roll, -30.0, 30.0, 30.0, 100.0);
			Drone.DefaultPose.Lift = 300.0;
			Presets.Add(MoveTemp(Drone));
		}

		return Presets;
	}
}

double FRoscRigPose::Get(ERoscRigAxis Axis) const
{
	switch (Axis)
	{
	case ERoscRigAxis::Travel: return Travel;
	case ERoscRigAxis::Traverse: return Traverse;
	case ERoscRigAxis::Lift: return Lift;
	case ERoscRigAxis::Swing: return Swing;
	case ERoscRigAxis::Boom: return Boom;
	case ERoscRigAxis::Extension: return Extension;
	case ERoscRigAxis::Pan: return Pan;
	case ERoscRigAxis::Tilt: return Tilt;
	case ERoscRigAxis::Roll: return Roll;
	}
	checkNoEntry();
	return 0.0;
}

void FRoscRigPose::Set(ERoscRigAxis Axis, double Value)
{
	switch (Axis)
	{
	case ERoscRigAxis::Travel: Travel = Value; return;
	case ERoscRigAxis::Traverse: Traverse = Value; return;
	case ERoscRigAxis::Lift: Lift = Value; return;
	case ERoscRigAxis::Swing: Swing = Value; return;
	case ERoscRigAxis::Boom: Boom = Value; return;
	case ERoscRigAxis::Extension: Extension = Value; return;
	case ERoscRigAxis::Pan: Pan = Value; return;
	case ERoscRigAxis::Tilt: Tilt = Value; return;
	case ERoscRigAxis::Roll: Roll = Value; return;
	}
	checkNoEntry();
}

FRoscRigPose FRoscRigSpec::ClampPose(const FRoscRigPose& Pose) const
{
	FRoscRigPose Clamped;
	for (int32 AxisIndex = 0; AxisIndex < RoscRigAxisCount; ++AxisIndex)
	{
		const ERoscRigAxis Axis = static_cast<ERoscRigAxis>(AxisIndex);
		const FRoscAxisLimits& Limits = Axes[AxisIndex];
		if (Limits.bUsed)
		{
			Clamped.Set(Axis, FMath::Clamp(Pose.Get(Axis), Limits.Min, Limits.Max));
		}
	}
	return Clamped;
}

TConstArrayView<FRoscRigPreset> Rosc::Rig::GetPresets()
{
	static const TArray<FRoscRigPreset> Presets = BuildPresets();
	return Presets;
}

const FRoscRigPreset* Rosc::Rig::FindPreset(const FString& Id)
{
	return GetPresets().FindByPredicate([&Id](const FRoscRigPreset& Preset) { return Preset.Id == Id; });
}

const FRoscRigPreset& Rosc::Rig::GetDefaultPreset(ERoscRigType Type)
{
	const FRoscRigPreset* Preset = GetPresets().FindByPredicate([Type](const FRoscRigPreset& Candidate) { return Candidate.Spec.Type == Type; });
	check(Preset != nullptr);
	return *Preset;
}

FText Rosc::Rig::GetTypeName(ERoscRigType Type)
{
	switch (Type)
	{
	case ERoscRigType::Tripod: return LOCTEXT("TypeTripod", "Tripod");
	case ERoscRigType::Slider: return LOCTEXT("TypeSlider", "Slider");
	case ERoscRigType::Dolly: return LOCTEXT("TypeDolly", "Dolly");
	case ERoscRigType::Jib: return LOCTEXT("TypeJib", "Jib");
	case ERoscRigType::Crane: return LOCTEXT("TypeCrane", "Crane");
	case ERoscRigType::Steadicam: return LOCTEXT("TypeSteadicam", "Steadicam");
	case ERoscRigType::Handheld: return LOCTEXT("TypeHandheld", "Handheld");
	case ERoscRigType::Drone: return LOCTEXT("TypeDrone", "Drone");
	}
	checkNoEntry();
	return FText::GetEmpty();
}

FText Rosc::Rig::GetAxisName(ERoscRigType Type, ERoscRigAxis Axis)
{
	switch (Axis)
	{
	case ERoscRigAxis::Travel:
		switch (Type)
		{
		case ERoscRigType::Slider: return LOCTEXT("AxisSlide", "Slide");
		case ERoscRigType::Dolly: return LOCTEXT("AxisTrack", "Track");
		default: return LOCTEXT("AxisForward", "Forward");
		}
	case ERoscRigAxis::Traverse:
		return LOCTEXT("AxisSideways", "Sideways");
	case ERoscRigAxis::Lift:
		switch (Type)
		{
		case ERoscRigType::Dolly: return LOCTEXT("AxisColumn", "Column");
		case ERoscRigType::Jib:
		case ERoscRigType::Crane: return LOCTEXT("AxisPivotHeight", "Pivot height");
		case ERoscRigType::Steadicam:
		case ERoscRigType::Handheld: return LOCTEXT("AxisLensHeight", "Lens height");
		case ERoscRigType::Drone: return LOCTEXT("AxisAltitude", "Altitude");
		default: return LOCTEXT("AxisHeight", "Height");
		}
	case ERoscRigAxis::Swing:
		return LOCTEXT("AxisSwing", "Swing");
	case ERoscRigAxis::Boom:
		return LOCTEXT("AxisBoom", "Boom");
	case ERoscRigAxis::Extension:
		return Type == ERoscRigType::Crane ? LOCTEXT("AxisTelescope", "Telescope") : LOCTEXT("AxisArmLength", "Arm length");
	case ERoscRigAxis::Pan:
		return Type == ERoscRigType::Drone ? LOCTEXT("AxisHeading", "Heading") : LOCTEXT("AxisPan", "Pan");
	case ERoscRigAxis::Tilt:
		return LOCTEXT("AxisTilt", "Tilt");
	case ERoscRigAxis::Roll:
		return LOCTEXT("AxisRoll", "Roll");
	}
	checkNoEntry();
	return FText::GetEmpty();
}

bool Rosc::Rig::IsDistanceAxis(ERoscRigAxis Axis)
{
	return Axis == ERoscRigAxis::Travel || Axis == ERoscRigAxis::Traverse || Axis == ERoscRigAxis::Lift || Axis == ERoscRigAxis::Extension;
}

FTransform Rosc::Rig::StraightTrackPoint(double TravelCm)
{
	return FTransform(FVector(TravelCm, 0.0, 0.0));
}

FTransform Rosc::Rig::SolveMount(ERoscRigType Type, const FRoscRigPose& Pose, const FTransform& TrackPoint)
{
	const FRotator Head(Pose.Tilt, Pose.Pan, Pose.Roll);

	switch (Type)
	{
	case ERoscRigType::Tripod:
		return FTransform(Head, FVector(0.0, 0.0, Pose.Lift));

	case ERoscRigType::Slider:
	case ERoscRigType::Dolly:
	{
		// The chassis follows the track; the column rises along the track's own up direction
		// and the head turns relative to the chassis.
		const FQuat TrackRotation = TrackPoint.GetRotation();
		const FVector Location = TrackPoint.GetLocation() + TrackRotation.GetUpVector() * Pose.Lift;
		return FTransform(TrackRotation * Head.Quaternion(), Location);
	}

	case ERoscRigType::Jib:
	case ERoscRigType::Crane:
	{
		// The arm swings and booms about a pivot on top of the column. The head stays level
		// whatever the boom angle, and pans relative to the arm.
		const FVector Pivot(0.0, 0.0, Pose.Lift);
		const FVector Arm = FRotator(Pose.Boom, Pose.Swing, 0.0).RotateVector(FVector(Pose.Extension, 0.0, 0.0));
		return FTransform(FRotator(0.0, Pose.Swing, 0.0).Quaternion() * Head.Quaternion(), Pivot + Arm);
	}

	case ERoscRigType::Steadicam:
	case ERoscRigType::Handheld:
	case ERoscRigType::Drone:
		return FTransform(Head, FVector(Pose.Travel, Pose.Traverse, Pose.Lift));
	}
	checkNoEntry();
	return FTransform::Identity;
}

FText Rosc::Rig::FormatAxisValue(ERoscRigAxis Axis, double Value, ERoscDistanceUnits Units)
{
	if (IsDistanceAxis(Axis))
	{
		return Rosc::Units::FormatDistance(Value * 10.0, Units);
	}
	return FText::Format(LOCTEXT("AngleValue", "{0}°"), Rosc::Units::FormatNumber(Value, 1));
}

TOptional<double> Rosc::Rig::ParseAxisValue(ERoscRigAxis Axis, const FString& Text, ERoscDistanceUnits Units)
{
	if (IsDistanceAxis(Axis))
	{
		const TOptional<double> Millimetres = Rosc::Units::ParseDistance(Text, Units);
		return Millimetres.IsSet() ? TOptional<double>(Millimetres.GetValue() / 10.0) : TOptional<double>();
	}

	FString Number = Text.TrimStartAndEnd();
	Number.RemoveFromEnd(TEXT("°"));
	Number.TrimEndInline();
	if (Number.IsEmpty() || !Number.IsNumeric())
	{
		return {};
	}
	return FCString::Atod(*Number);
}

#undef LOCTEXT_NAMESPACE
