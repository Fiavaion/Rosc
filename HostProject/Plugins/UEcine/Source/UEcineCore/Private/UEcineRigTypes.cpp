#include "UEcineRigTypes.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

namespace
{
	/** Far enough that a free-moving rig never meets the edge of its range in practice. */
	constexpr double Unbounded = 1000000.0;

	void SetAxis(FUEcineRigSpec& Spec, EUEcineRigAxis Axis, double Min, double Max, double MaxSpeed, double MaxAcceleration)
	{
		FUEcineAxisLimits& Limits = Spec.GetAxis(Axis);
		Limits.bUsed = true;
		Limits.Min = Min;
		Limits.Max = Max;
		Limits.MaxSpeed = MaxSpeed;
		Limits.MaxAcceleration = MaxAcceleration;
	}

	/** An axis that is set when the rig is built and stays put during a shot. */
	void SetFixedAxis(FUEcineRigSpec& Spec, EUEcineRigAxis Axis, double Min, double Max)
	{
		SetAxis(Spec, Axis, Min, Max, 0.0, 0.0);
	}

	/** Pan and tilt worked by hand on a fluid head. */
	void SetFluidHead(FUEcineRigSpec& Spec)
	{
		SetAxis(Spec, EUEcineRigAxis::Pan, -720.0, 720.0, 120.0, 400.0);
		SetAxis(Spec, EUEcineRigAxis::Tilt, -90.0, 90.0, 90.0, 300.0);
	}

	FUEcineRigPreset MakePreset(const TCHAR* Id, const FText& Name, EUEcineRigType Type)
	{
		FUEcineRigPreset Preset;
		Preset.Id = Id;
		Preset.Name = Name;
		Preset.Spec.PresetId = Id;
		Preset.Spec.Type = Type;
		return Preset;
	}

	FUEcineRigPreset MakeSlider(const TCHAR* Id, const FText& Name, double RailLengthCm)
	{
		FUEcineRigPreset Preset = MakePreset(Id, Name, EUEcineRigType::Slider);
		SetAxis(Preset.Spec, EUEcineRigAxis::Travel, 0.0, RailLengthCm, 50.0, 100.0);
		SetFixedAxis(Preset.Spec, EUEcineRigAxis::Lift, 30.0, 170.0);
		SetFluidHead(Preset.Spec);
		Preset.DefaultPose.Lift = 120.0;
		Preset.DefaultPose.Travel = RailLengthCm * 0.5;
		return Preset;
	}

	FUEcineRigPreset MakeJib(const TCHAR* Id, const FText& Name, double ArmLengthCm)
	{
		FUEcineRigPreset Preset = MakePreset(Id, Name, EUEcineRigType::Jib);
		SetFixedAxis(Preset.Spec, EUEcineRigAxis::Lift, 100.0, 180.0);
		SetFixedAxis(Preset.Spec, EUEcineRigAxis::Extension, ArmLengthCm, ArmLengthCm);
		SetAxis(Preset.Spec, EUEcineRigAxis::Swing, -360.0, 360.0, 45.0, 60.0);
		SetAxis(Preset.Spec, EUEcineRigAxis::Boom, -40.0, 50.0, 30.0, 45.0);
		SetFluidHead(Preset.Spec);
		Preset.DefaultPose.Lift = 150.0;
		Preset.DefaultPose.Extension = ArmLengthCm;
		return Preset;
	}

	FUEcineRigPreset MakeCrane(const TCHAR* Id, const FText& Name, double MinReachCm, double MaxReachCm)
	{
		FUEcineRigPreset Preset = MakePreset(Id, Name, EUEcineRigType::Crane);
		SetFixedAxis(Preset.Spec, EUEcineRigAxis::Lift, 150.0, 250.0);
		SetAxis(Preset.Spec, EUEcineRigAxis::Extension, MinReachCm, MaxReachCm, 180.0, 200.0);
		SetAxis(Preset.Spec, EUEcineRigAxis::Swing, -360.0, 360.0, 40.0, 40.0);
		SetAxis(Preset.Spec, EUEcineRigAxis::Boom, -30.0, 50.0, 25.0, 30.0);
		// Remote head: three motorised axes.
		SetAxis(Preset.Spec, EUEcineRigAxis::Pan, -720.0, 720.0, 120.0, 300.0);
		SetAxis(Preset.Spec, EUEcineRigAxis::Tilt, -180.0, 180.0, 120.0, 300.0);
		SetAxis(Preset.Spec, EUEcineRigAxis::Roll, -180.0, 180.0, 90.0, 200.0);
		Preset.DefaultPose.Lift = 200.0;
		Preset.DefaultPose.Extension = (MinReachCm + MaxReachCm) * 0.5;
		return Preset;
	}

	/** A rig that goes where its operator or pilot takes it. */
	FUEcineRigPreset MakeFreeRig(const TCHAR* Id, const FText& Name, EUEcineRigType Type, double MaxSpeed, double MaxAcceleration)
	{
		FUEcineRigPreset Preset = MakePreset(Id, Name, Type);
		SetAxis(Preset.Spec, EUEcineRigAxis::Travel, -Unbounded, Unbounded, MaxSpeed, MaxAcceleration);
		SetAxis(Preset.Spec, EUEcineRigAxis::Traverse, -Unbounded, Unbounded, MaxSpeed, MaxAcceleration);
		return Preset;
	}

	TArray<FUEcineRigPreset> BuildPresets()
	{
		TArray<FUEcineRigPreset> Presets;

		{
			FUEcineRigPreset Tripod = MakePreset(TEXT("tripod-fluid-head"), LOCTEXT("PresetTripod", "Tripod, fluid head"), EUEcineRigType::Tripod);
			SetFixedAxis(Tripod.Spec, EUEcineRigAxis::Lift, 60.0, 170.0);
			SetFluidHead(Tripod.Spec);
			Tripod.DefaultPose.Lift = 150.0;
			Presets.Add(MoveTemp(Tripod));
		}

		Presets.Add(MakeSlider(TEXT("slider-100"), LOCTEXT("PresetSlider100", "Slider, 1 m"), 100.0));
		Presets.Add(MakeSlider(TEXT("slider-150"), LOCTEXT("PresetSlider150", "Slider, 1.5 m"), 150.0));

		{
			FUEcineRigPreset Dolly = MakePreset(TEXT("dolly"), LOCTEXT("PresetDolly", "Dolly on track"), EUEcineRigType::Dolly);
			// The travel range is the length of the track the user lays; this is the default length.
			SetAxis(Dolly.Spec, EUEcineRigAxis::Travel, 0.0, 600.0, 250.0, 150.0);
			SetAxis(Dolly.Spec, EUEcineRigAxis::Lift, 40.0, 190.0, 30.0, 60.0);
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
			FUEcineRigPreset Steadicam = MakeFreeRig(TEXT("steadicam"), LOCTEXT("PresetSteadicam", "Steadicam"), EUEcineRigType::Steadicam, 300.0, 200.0);
			SetAxis(Steadicam.Spec, EUEcineRigAxis::Lift, 60.0, 200.0, 50.0, 100.0);
			SetAxis(Steadicam.Spec, EUEcineRigAxis::Pan, -720.0, 720.0, 120.0, 300.0);
			SetAxis(Steadicam.Spec, EUEcineRigAxis::Tilt, -60.0, 60.0, 60.0, 200.0);
			SetAxis(Steadicam.Spec, EUEcineRigAxis::Roll, -15.0, 15.0, 30.0, 100.0);
			Steadicam.DefaultPose.Lift = 150.0;
			Presets.Add(MoveTemp(Steadicam));
		}
		{
			FUEcineRigPreset Handheld = MakeFreeRig(TEXT("handheld"), LOCTEXT("PresetHandheld", "Handheld"), EUEcineRigType::Handheld, 400.0, 300.0);
			SetAxis(Handheld.Spec, EUEcineRigAxis::Lift, 20.0, 210.0, 100.0, 300.0);
			SetAxis(Handheld.Spec, EUEcineRigAxis::Pan, -720.0, 720.0, 180.0, 600.0);
			SetAxis(Handheld.Spec, EUEcineRigAxis::Tilt, -90.0, 90.0, 120.0, 400.0);
			SetAxis(Handheld.Spec, EUEcineRigAxis::Roll, -45.0, 45.0, 60.0, 200.0);
			Handheld.DefaultPose.Lift = 160.0;
			Presets.Add(MoveTemp(Handheld));
		}
		{
			FUEcineRigPreset Drone = MakeFreeRig(TEXT("drone"), LOCTEXT("PresetDrone", "Drone"), EUEcineRigType::Drone, 1500.0, 600.0);
			SetAxis(Drone.Spec, EUEcineRigAxis::Lift, 0.0, 12000.0, 500.0, 300.0);
			SetAxis(Drone.Spec, EUEcineRigAxis::Pan, -720.0, 720.0, 100.0, 200.0);
			SetAxis(Drone.Spec, EUEcineRigAxis::Tilt, -90.0, 30.0, 60.0, 200.0);
			SetAxis(Drone.Spec, EUEcineRigAxis::Roll, -30.0, 30.0, 30.0, 100.0);
			Drone.DefaultPose.Lift = 300.0;
			Presets.Add(MoveTemp(Drone));
		}

		return Presets;
	}
}

double FUEcineRigPose::Get(EUEcineRigAxis Axis) const
{
	switch (Axis)
	{
	case EUEcineRigAxis::Travel: return Travel;
	case EUEcineRigAxis::Traverse: return Traverse;
	case EUEcineRigAxis::Lift: return Lift;
	case EUEcineRigAxis::Swing: return Swing;
	case EUEcineRigAxis::Boom: return Boom;
	case EUEcineRigAxis::Extension: return Extension;
	case EUEcineRigAxis::Pan: return Pan;
	case EUEcineRigAxis::Tilt: return Tilt;
	case EUEcineRigAxis::Roll: return Roll;
	}
	checkNoEntry();
	return 0.0;
}

void FUEcineRigPose::Set(EUEcineRigAxis Axis, double Value)
{
	switch (Axis)
	{
	case EUEcineRigAxis::Travel: Travel = Value; return;
	case EUEcineRigAxis::Traverse: Traverse = Value; return;
	case EUEcineRigAxis::Lift: Lift = Value; return;
	case EUEcineRigAxis::Swing: Swing = Value; return;
	case EUEcineRigAxis::Boom: Boom = Value; return;
	case EUEcineRigAxis::Extension: Extension = Value; return;
	case EUEcineRigAxis::Pan: Pan = Value; return;
	case EUEcineRigAxis::Tilt: Tilt = Value; return;
	case EUEcineRigAxis::Roll: Roll = Value; return;
	}
	checkNoEntry();
}

FUEcineRigPose FUEcineRigSpec::ClampPose(const FUEcineRigPose& Pose) const
{
	FUEcineRigPose Clamped;
	for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
	{
		const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
		const FUEcineAxisLimits& Limits = Axes[AxisIndex];
		if (Limits.bUsed)
		{
			Clamped.Set(Axis, FMath::Clamp(Pose.Get(Axis), Limits.Min, Limits.Max));
		}
	}
	return Clamped;
}

TConstArrayView<FUEcineRigPreset> UEcine::Rig::GetPresets()
{
	static const TArray<FUEcineRigPreset> Presets = BuildPresets();
	return Presets;
}

const FUEcineRigPreset* UEcine::Rig::FindPreset(const FString& Id)
{
	return GetPresets().FindByPredicate([&Id](const FUEcineRigPreset& Preset) { return Preset.Id == Id; });
}

const FUEcineRigPreset& UEcine::Rig::GetDefaultPreset(EUEcineRigType Type)
{
	const FUEcineRigPreset* Preset = GetPresets().FindByPredicate([Type](const FUEcineRigPreset& Candidate) { return Candidate.Spec.Type == Type; });
	check(Preset != nullptr);
	return *Preset;
}

FText UEcine::Rig::GetTypeName(EUEcineRigType Type)
{
	switch (Type)
	{
	case EUEcineRigType::Tripod: return LOCTEXT("TypeTripod", "Tripod");
	case EUEcineRigType::Slider: return LOCTEXT("TypeSlider", "Slider");
	case EUEcineRigType::Dolly: return LOCTEXT("TypeDolly", "Dolly");
	case EUEcineRigType::Jib: return LOCTEXT("TypeJib", "Jib");
	case EUEcineRigType::Crane: return LOCTEXT("TypeCrane", "Crane");
	case EUEcineRigType::Steadicam: return LOCTEXT("TypeSteadicam", "Steadicam");
	case EUEcineRigType::Handheld: return LOCTEXT("TypeHandheld", "Handheld");
	case EUEcineRigType::Drone: return LOCTEXT("TypeDrone", "Drone");
	}
	checkNoEntry();
	return FText::GetEmpty();
}

FText UEcine::Rig::GetAxisName(EUEcineRigType Type, EUEcineRigAxis Axis)
{
	switch (Axis)
	{
	case EUEcineRigAxis::Travel:
		switch (Type)
		{
		case EUEcineRigType::Slider: return LOCTEXT("AxisSlide", "Slide");
		case EUEcineRigType::Dolly: return LOCTEXT("AxisTrack", "Track");
		default: return LOCTEXT("AxisForward", "Forward");
		}
	case EUEcineRigAxis::Traverse:
		return LOCTEXT("AxisSideways", "Sideways");
	case EUEcineRigAxis::Lift:
		switch (Type)
		{
		case EUEcineRigType::Dolly: return LOCTEXT("AxisColumn", "Column");
		case EUEcineRigType::Jib:
		case EUEcineRigType::Crane: return LOCTEXT("AxisPivotHeight", "Pivot height");
		case EUEcineRigType::Steadicam:
		case EUEcineRigType::Handheld: return LOCTEXT("AxisLensHeight", "Lens height");
		case EUEcineRigType::Drone: return LOCTEXT("AxisAltitude", "Altitude");
		default: return LOCTEXT("AxisHeight", "Height");
		}
	case EUEcineRigAxis::Swing:
		return LOCTEXT("AxisSwing", "Swing");
	case EUEcineRigAxis::Boom:
		return LOCTEXT("AxisBoom", "Boom");
	case EUEcineRigAxis::Extension:
		return Type == EUEcineRigType::Crane ? LOCTEXT("AxisTelescope", "Telescope") : LOCTEXT("AxisArmLength", "Arm length");
	case EUEcineRigAxis::Pan:
		return Type == EUEcineRigType::Drone ? LOCTEXT("AxisHeading", "Heading") : LOCTEXT("AxisPan", "Pan");
	case EUEcineRigAxis::Tilt:
		return LOCTEXT("AxisTilt", "Tilt");
	case EUEcineRigAxis::Roll:
		return LOCTEXT("AxisRoll", "Roll");
	}
	checkNoEntry();
	return FText::GetEmpty();
}

bool UEcine::Rig::IsDistanceAxis(EUEcineRigAxis Axis)
{
	return Axis == EUEcineRigAxis::Travel || Axis == EUEcineRigAxis::Traverse || Axis == EUEcineRigAxis::Lift || Axis == EUEcineRigAxis::Extension;
}

FTransform UEcine::Rig::StraightTrackPoint(double TravelCm)
{
	return FTransform(FVector(TravelCm, 0.0, 0.0));
}

FTransform UEcine::Rig::SolveMount(EUEcineRigType Type, const FUEcineRigPose& Pose, const FTransform& TrackPoint)
{
	const FRotator Head(Pose.Tilt, Pose.Pan, Pose.Roll);

	switch (Type)
	{
	case EUEcineRigType::Tripod:
		return FTransform(Head, FVector(0.0, 0.0, Pose.Lift));

	case EUEcineRigType::Slider:
	case EUEcineRigType::Dolly:
	{
		// The chassis follows the track; the column rises along the track's own up direction
		// and the head turns relative to the chassis.
		const FQuat TrackRotation = TrackPoint.GetRotation();
		const FVector Location = TrackPoint.GetLocation() + TrackRotation.GetUpVector() * Pose.Lift;
		return FTransform(TrackRotation * Head.Quaternion(), Location);
	}

	case EUEcineRigType::Jib:
	case EUEcineRigType::Crane:
	{
		// The arm swings and booms about a pivot on top of the column. The head stays level
		// whatever the boom angle, and pans relative to the arm.
		const FVector Pivot(0.0, 0.0, Pose.Lift);
		const FVector Arm = FRotator(Pose.Boom, Pose.Swing, 0.0).RotateVector(FVector(Pose.Extension, 0.0, 0.0));
		return FTransform(FRotator(0.0, Pose.Swing, 0.0).Quaternion() * Head.Quaternion(), Pivot + Arm);
	}

	case EUEcineRigType::Steadicam:
	case EUEcineRigType::Handheld:
	case EUEcineRigType::Drone:
		return FTransform(Head, FVector(Pose.Travel, Pose.Traverse, Pose.Lift));
	}
	checkNoEntry();
	return FTransform::Identity;
}

FText UEcine::Rig::FormatAxisValue(EUEcineRigAxis Axis, double Value, EUEcineDistanceUnits Units)
{
	if (IsDistanceAxis(Axis))
	{
		return UEcine::Units::FormatDistance(Value * 10.0, Units);
	}
	return FText::Format(LOCTEXT("AngleValue", "{0}°"), UEcine::Units::FormatNumber(Value, 1));
}

TOptional<double> UEcine::Rig::ParseAxisValue(EUEcineRigAxis Axis, const FString& Text, EUEcineDistanceUnits Units)
{
	if (IsDistanceAxis(Axis))
	{
		const TOptional<double> Millimetres = UEcine::Units::ParseDistance(Text, Units);
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
