// Copyright 2026 Fiavaion. All rights reserved.

#include "RoscOperator.h"

#include "Math/RandomStream.h"

#define LOCTEXT_NAMESPACE "RoscCore"

namespace
{
	/** How far and how fast one operator style moves the camera at intensity 1. */
	struct FStyleProfile
	{
		/** Greatest pan, tilt and roll offset in degrees. */
		FVector RotationDeg;
		double RotationHz;
		/** Greatest forward, sideways and vertical offset in centimetres. */
		FVector LocationCm;
		double LocationHz;
	};

	FStyleProfile GetProfile(ERoscOperatorStyle Style)
	{
		switch (Style)
		{
		case ERoscOperatorStyle::LockedOff: return { FVector::ZeroVector, 0.0, FVector::ZeroVector, 0.0 };
		case ERoscOperatorStyle::FluidHead: return { FVector(0.05, 0.05, 0.0), 0.3, FVector::ZeroVector, 0.0 };
		case ERoscOperatorStyle::RemoteHead: return { FVector(0.03, 0.03, 0.02), 0.5, FVector(0.2, 0.3, 0.3), 0.4 };
		case ERoscOperatorStyle::Steadicam: return { FVector(0.4, 0.3, 0.5), 0.25, FVector(1.0, 1.5, 1.5), 0.5 };
		case ERoscOperatorStyle::Handheld: return { FVector(0.8, 0.8, 0.5), 1.2, FVector(0.8, 1.2, 1.2), 1.0 };
		case ERoscOperatorStyle::Drone: return { FVector(0.1, 0.1, 0.05), 0.2, FVector(4.0, 4.0, 3.0), 0.3 };
		}
		checkNoEntry();
		return {};
	}

	/**
	 * Smooth wandering between -1 and 1: three sine waves at unrelated frequencies, with phases
	 * and frequency spread drawn from the seed. Each channel of each take gets its own stream.
	 */
	double Wander(int32 Seed, int32 Channel, double BaseHz, double TimeSeconds)
	{
		constexpr double FrequencyRatios[] = { 1.0, 2.31, 4.73 };
		constexpr double Weights[] = { 4.0 / 7.0, 2.0 / 7.0, 1.0 / 7.0 };

		FRandomStream Stream(Seed * 7919 + Channel * 104729);
		double Value = 0.0;
		for (int32 Wave = 0; Wave < 3; ++Wave)
		{
			const double Phase = Stream.GetFraction() * UE_DOUBLE_TWO_PI;
			const double Frequency = BaseHz * FrequencyRatios[Wave] * (0.85 + 0.3 * Stream.GetFraction());
			Value += Weights[Wave] * FMath::Sin(UE_DOUBLE_TWO_PI * Frequency * TimeSeconds + Phase);
		}
		return Value;
	}
}

FTransform Rosc::Operator::Offset(const FRoscOperator& Operator, double TimeSeconds)
{
	if (Operator.IsStill())
	{
		return FTransform::Identity;
	}

	const FStyleProfile Profile = GetProfile(Operator.Style);
	const double Intensity = Operator.Intensity;

	const FVector Location(
		Profile.LocationCm.X * Intensity * Wander(Operator.Seed, 0, Profile.LocationHz, TimeSeconds),
		Profile.LocationCm.Y * Intensity * Wander(Operator.Seed, 1, Profile.LocationHz, TimeSeconds),
		Profile.LocationCm.Z * Intensity * Wander(Operator.Seed, 2, Profile.LocationHz, TimeSeconds));

	const double Pan = Profile.RotationDeg.X * Intensity * Wander(Operator.Seed, 3, Profile.RotationHz, TimeSeconds);
	const double Tilt = Profile.RotationDeg.Y * Intensity * Wander(Operator.Seed, 4, Profile.RotationHz, TimeSeconds);
	const double Roll = Profile.RotationDeg.Z * Intensity * Wander(Operator.Seed, 5, Profile.RotationHz, TimeSeconds);

	return FTransform(FRotator(Tilt, Pan, Roll), Location);
}

void Rosc::Operator::GetBounds(const FRoscOperator& Operator, FVector& OutLocationCm, FVector& OutRotationDeg)
{
	if (Operator.IsStill())
	{
		OutLocationCm = FVector::ZeroVector;
		OutRotationDeg = FVector::ZeroVector;
		return;
	}
	const FStyleProfile Profile = GetProfile(Operator.Style);
	OutLocationCm = Profile.LocationCm * Operator.Intensity;
	OutRotationDeg = Profile.RotationDeg * Operator.Intensity;
}

ERoscOperatorStyle Rosc::Operator::GetDefaultStyle(ERoscRigType RigType)
{
	switch (RigType)
	{
	case ERoscRigType::Tripod:
	case ERoscRigType::Slider:
	case ERoscRigType::Dolly:
	case ERoscRigType::Jib:
		return ERoscOperatorStyle::FluidHead;
	case ERoscRigType::Crane:
		return ERoscOperatorStyle::RemoteHead;
	case ERoscRigType::Steadicam:
		return ERoscOperatorStyle::Steadicam;
	case ERoscRigType::Handheld:
		return ERoscOperatorStyle::Handheld;
	case ERoscRigType::Drone:
		return ERoscOperatorStyle::Drone;
	}
	checkNoEntry();
	return ERoscOperatorStyle::LockedOff;
}

FText Rosc::Operator::GetStyleName(ERoscOperatorStyle Style)
{
	switch (Style)
	{
	case ERoscOperatorStyle::LockedOff: return LOCTEXT("OperatorLockedOff", "Locked off");
	case ERoscOperatorStyle::FluidHead: return LOCTEXT("OperatorFluidHead", "Fluid head");
	case ERoscOperatorStyle::RemoteHead: return LOCTEXT("OperatorRemoteHead", "Remote head");
	case ERoscOperatorStyle::Steadicam: return LOCTEXT("OperatorSteadicam", "Steadicam");
	case ERoscOperatorStyle::Handheld: return LOCTEXT("OperatorHandheld", "Handheld");
	case ERoscOperatorStyle::Drone: return LOCTEXT("OperatorDrone", "Drone");
	}
	checkNoEntry();
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
