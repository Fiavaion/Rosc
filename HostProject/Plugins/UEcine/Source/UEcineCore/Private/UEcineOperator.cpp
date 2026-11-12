// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineOperator.h"

#include "Math/RandomStream.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

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

	FStyleProfile GetProfile(EUEcineOperatorStyle Style)
	{
		switch (Style)
		{
		case EUEcineOperatorStyle::LockedOff: return { FVector::ZeroVector, 0.0, FVector::ZeroVector, 0.0 };
		case EUEcineOperatorStyle::FluidHead: return { FVector(0.05, 0.05, 0.0), 0.3, FVector::ZeroVector, 0.0 };
		case EUEcineOperatorStyle::RemoteHead: return { FVector(0.03, 0.03, 0.02), 0.5, FVector(0.2, 0.3, 0.3), 0.4 };
		case EUEcineOperatorStyle::Steadicam: return { FVector(0.4, 0.3, 0.5), 0.25, FVector(1.0, 1.5, 1.5), 0.5 };
		case EUEcineOperatorStyle::Handheld: return { FVector(0.8, 0.8, 0.5), 1.2, FVector(0.8, 1.2, 1.2), 1.0 };
		case EUEcineOperatorStyle::Drone: return { FVector(0.1, 0.1, 0.05), 0.2, FVector(4.0, 4.0, 3.0), 0.3 };
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

FTransform UEcine::Operator::Offset(const FUEcineOperator& Operator, double TimeSeconds)
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

void UEcine::Operator::GetBounds(const FUEcineOperator& Operator, FVector& OutLocationCm, FVector& OutRotationDeg)
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

EUEcineOperatorStyle UEcine::Operator::GetDefaultStyle(EUEcineRigType RigType)
{
	switch (RigType)
	{
	case EUEcineRigType::Tripod:
	case EUEcineRigType::Slider:
	case EUEcineRigType::Dolly:
	case EUEcineRigType::Jib:
		return EUEcineOperatorStyle::FluidHead;
	case EUEcineRigType::Crane:
		return EUEcineOperatorStyle::RemoteHead;
	case EUEcineRigType::Steadicam:
		return EUEcineOperatorStyle::Steadicam;
	case EUEcineRigType::Handheld:
		return EUEcineOperatorStyle::Handheld;
	case EUEcineRigType::Drone:
		return EUEcineOperatorStyle::Drone;
	}
	checkNoEntry();
	return EUEcineOperatorStyle::LockedOff;
}

FText UEcine::Operator::GetStyleName(EUEcineOperatorStyle Style)
{
	switch (Style)
	{
	case EUEcineOperatorStyle::LockedOff: return LOCTEXT("OperatorLockedOff", "Locked off");
	case EUEcineOperatorStyle::FluidHead: return LOCTEXT("OperatorFluidHead", "Fluid head");
	case EUEcineOperatorStyle::RemoteHead: return LOCTEXT("OperatorRemoteHead", "Remote head");
	case EUEcineOperatorStyle::Steadicam: return LOCTEXT("OperatorSteadicam", "Steadicam");
	case EUEcineOperatorStyle::Handheld: return LOCTEXT("OperatorHandheld", "Handheld");
	case EUEcineOperatorStyle::Drone: return LOCTEXT("OperatorDrone", "Drone");
	}
	checkNoEntry();
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
