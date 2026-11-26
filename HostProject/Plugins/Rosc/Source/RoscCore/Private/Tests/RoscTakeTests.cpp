// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"
#include "RoscRigTypes.h"
#include "RoscTake.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const EAutomationTestFlags TakeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	constexpr double Fps = 24.0;
	constexpr int32 TenSeconds = 241;
	/** The middle 6 s of a 10 s take: frames for 2 s to 8 s. */
	constexpr int32 MiddleFirst = 48;
	constexpr int32 MiddleLast = 192;

	FRoscTake MakeTake(ERoscRigAxis Axis, TFunctionRef<double(double)> OfSeconds, int32 Frames = TenSeconds)
	{
		FRoscTake Take;
		Take.FramesPerSecond = Fps;
		Take.Poses.SetNum(Frames);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Take.Poses[Frame].Set(Axis, OfSeconds(Frame / Fps));
		}
		return Take;
	}

	/** Half the swing of an axis over the middle of a take. */
	double MiddleAmplitude(const TArray<FRoscRigPose>& Poses, ERoscRigAxis Axis)
	{
		double Low = Poses[MiddleFirst].Get(Axis);
		double High = Low;
		for (int32 Frame = MiddleFirst; Frame <= MiddleLast; ++Frame)
		{
			Low = FMath::Min(Low, Poses[Frame].Get(Axis));
			High = FMath::Max(High, Poses[Frame].Get(Axis));
		}
		return 0.5 * (High - Low);
	}

	int32 PeakFrame(const TArray<FRoscRigPose>& Poses, ERoscRigAxis Axis)
	{
		int32 Peak = 0;
		for (int32 Frame = 1; Frame < Poses.Num(); ++Frame)
		{
			if (Poses[Frame].Get(Axis) > Poses[Peak].Get(Axis))
			{
				Peak = Frame;
			}
		}
		return Peak;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscTakeProcessingTest, "Rosc.Core.Take.Processing", TakeTestFlags)

bool FRoscTakeProcessingTest::RunTest(const FString& Parameters)
{
	const FRoscTakeProcessing Defaults;

	TestNearlyEqual(TEXT("the default smoothing width"), Rosc::Take::GetSmoothingSeconds(Defaults), FMath::Sqrt(0.04 * 0.04 + 0.0625 * 0.0625), 1e-12);

	{
		const FRoscTake Shake = MakeTake(ERoscRigAxis::Pan, [](double T) { return 2.0 * FMath::Sin(2.0 * UE_DOUBLE_PI * 10.0 * T); });
		const TArray<FRoscRigPose> Out = Rosc::Take::Process(Shake, Defaults);
		TestTrue(*FString::Printf(TEXT("a 10 Hz shake of 2 degrees is down at least 90 percent (%.4f)"), MiddleAmplitude(Out, ERoscRigAxis::Pan)), MiddleAmplitude(Out, ERoscRigAxis::Pan) <= 0.2);
	}

	{
		const FRoscTake Sweep = MakeTake(ERoscRigAxis::Tilt, [](double T) { return 30.0 * FMath::Sin(2.0 * UE_DOUBLE_PI * 0.5 * T); });
		const TArray<FRoscRigPose> Out = Rosc::Take::Process(Sweep, Defaults);
		// The Gaussian at 0.5 Hz keeps 97.3 percent; the width is set by the rules, so the bound is 97.
		TestTrue(*FString::Printf(TEXT("a slow 30 degree tilt keeps its swing (%.4f)"), MiddleAmplitude(Out, ERoscRigAxis::Tilt)), MiddleAmplitude(Out, ERoscRigAxis::Tilt) >= 0.97 * 30.0);
		TestNearlyEqual(TEXT("the first tilt holds"), Out[0].Tilt, Sweep.Poses[0].Tilt, 1e-9);
		TestNearlyEqual(TEXT("the last tilt holds"), Out.Last().Tilt, Sweep.Poses.Last().Tilt, 1e-9);
	}

	{
		const FRoscTake Bump = MakeTake(ERoscRigAxis::Pan, [](double T) { return 30.0 * FMath::Exp(-FMath::Square((T - 5.0) / 0.5)); });
		const TArray<FRoscRigPose> Out = Rosc::Take::Process(Bump, Defaults);
		TestTrue(TEXT("a bump keeps its peak on the same frame"), FMath::Abs(PeakFrame(Out, ERoscRigAxis::Pan) - PeakFrame(Bump.Poses, ERoscRigAxis::Pan)) <= 1);
	}

	{
		const FRoscTake Ramp = MakeTake(ERoscRigAxis::Travel, [](double T) { return 40.0 * T - 20.0; });
		const TArray<FRoscRigPose> Out = Rosc::Take::Process(Ramp, Defaults);
		double Worst = 0.0;
		for (int32 Frame = 0; Frame < Out.Num(); ++Frame)
		{
			Worst = FMath::Max(Worst, FMath::Abs(Out[Frame].Travel - Ramp.Poses[Frame].Travel));
		}
		TestTrue(*FString::Printf(TEXT("a straight ramp is unchanged (%g)"), Worst), Worst < 1e-9);
	}

	{
		FRoscTakeProcessing Off;
		Off.Smoothing = ERoscTakeSmoothing::Off;
		Off.Stabilise = 0.0;
		FRoscTake Take = MakeTake(ERoscRigAxis::Pan, [](double T) { return 20.0 * FMath::Sin(7.0 * T); });
		for (int32 Frame = 0; Frame < Take.Poses.Num(); ++Frame)
		{
			Take.Poses[Frame].Travel = 3.0 * FMath::Cos(2.0 * Frame);
		}
		const TArray<FRoscRigPose> Out = Rosc::Take::Process(Take, Off);
		bool bSame = Out.Num() == Take.Poses.Num();
		for (int32 Frame = 0; bSame && Frame < Out.Num(); ++Frame)
		{
			for (int32 Axis = 0; Axis < RoscRigAxisCount; ++Axis)
			{
				bSame &= Out[Frame].Get(static_cast<ERoscRigAxis>(Axis)) == Take.Poses[Frame].Get(static_cast<ERoscRigAxis>(Axis));
			}
		}
		TestTrue(TEXT("Off with no stabilising returns the take as it was"), bSame);
	}

	{
		FRoscTake Take = MakeTake(ERoscRigAxis::Tilt, [](double T) { return 10.0 * FMath::Sin(3.0 * T) + 5.0; });
		for (int32 Frame = 0; Frame < Take.Poses.Num(); ++Frame)
		{
			const double T = Frame / Fps;
			Take.Poses[Frame].Pan = 20.0 * FMath::Sin(T);
			Take.Poses[Frame].Travel = 10.0 * T;
			Take.Poses[Frame].Traverse = 5.0 * FMath::Sin(T);
			Take.Poses[Frame].Lift = 100.0 + 2.0 * T;
		}

		FRoscTakeProcessing LockTilt;
		LockTilt.bLockTilt = true;
		const TArray<FRoscRigPose> Tilted = Rosc::Take::Process(Take, LockTilt);
		bool bTiltHeld = true;
		double PanSwing = 0.0;
		for (const FRoscRigPose& Pose : Tilted)
		{
			bTiltHeld &= Pose.Tilt == Take.Poses[0].Tilt;
			PanSwing = FMath::Max(PanSwing, FMath::Abs(Pose.Pan - Tilted[0].Pan));
		}
		TestTrue(TEXT("a locked tilt takes the first frame's value"), bTiltHeld);
		TestTrue(TEXT("a pan that is not locked still moves"), PanSwing > 1.0);

		FRoscTakeProcessing LockPosition;
		LockPosition.bLockPosition = true;
		const TArray<FRoscRigPose> Placed = Rosc::Take::Process(Take, LockPosition);
		bool bPositionHeld = true;
		double TiltSwing = 0.0;
		for (const FRoscRigPose& Pose : Placed)
		{
			bPositionHeld &= Pose.Travel == Take.Poses[0].Travel && Pose.Traverse == Take.Poses[0].Traverse && Pose.Lift == Take.Poses[0].Lift;
			TiltSwing = FMath::Max(TiltSwing, FMath::Abs(Pose.Tilt - Placed[0].Tilt));
		}
		TestTrue(TEXT("a locked position holds travel, traverse and lift"), bPositionHeld);
		TestTrue(TEXT("a tilt that is not locked still moves"), TiltSwing > 1.0);
	}

	{
		// The pan truly rises 5 degrees a frame but the device reports it wrapped.
		const FRoscTake Whip = MakeTake(ERoscRigAxis::Pan, [](double T) { return FMath::UnwindDegrees(170.0 + 120.0 * T); }, 40);
		const TArray<FRoscRigPose> Out = Rosc::Take::Process(Whip, Defaults);
		double LargestStep = 0.0;
		for (int32 Frame = 1; Frame < Out.Num(); ++Frame)
		{
			LargestStep = FMath::Max(LargestStep, FMath::Abs(Out[Frame].Pan - Out[Frame - 1].Pan));
		}
		TestTrue(*FString::Printf(TEXT("a pan across 180 degrees has no jump (%g)"), LargestStep), LargestStep <= 10.0);
	}

	TestTrue(TEXT("an empty take comes back empty"), Rosc::Take::Process(FRoscTake(), Defaults).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscTakeFindingsTest, "Rosc.Core.Take.Findings", TakeTestFlags)

bool FRoscTakeFindingsTest::RunTest(const FString& Parameters)
{
	const FRoscRigSpec& Tripod = Rosc::Rig::GetDefaultPreset(ERoscRigType::Tripod).Spec;
	const double PanLimit = Tripod.GetAxis(ERoscRigAxis::Pan).MaxSpeed;

	// The tripod has to stand at a height it allows.
	const auto OnTripod = [](FRoscTake Take)
	{
		for (FRoscRigPose& Pose : Take.Poses)
		{
			Pose.Lift = FMath::Max(Pose.Lift, 100.0);
		}
		return Take;
	};

	const auto CountFor = [](const TArray<FRoscMoveFinding>& Findings, ERoscRigAxis Axis, ERoscMoveProblem Problem)
	{
		return Findings.FilterByPredicate([Axis, Problem](const FRoscMoveFinding& Finding) { return Finding.Axis == Axis && Finding.Problem == Problem; }).Num();
	};

	{
		const double Speed = 1.5 * PanLimit;
		const FRoscTake Fast = OnTripod(MakeTake(ERoscRigAxis::Pan, [Speed](double T) { return Speed * T; }, 49));
		const TArray<FRoscMoveFinding> Findings = Rosc::Take::Check(Tripod, Fast.Poses, Fps);
		TestEqual(TEXT("a pan that is too fast gives one finding"), Findings.Num(), 1);
		TestEqual(TEXT("it is a TooFast on the pan"), CountFor(Findings, ERoscRigAxis::Pan, ERoscMoveProblem::TooFast), 1);
		if (!Findings.IsEmpty())
		{
			TestTrue(*FString::Printf(TEXT("it reports the true speed (%g)"), Findings[0].Value), FMath::Abs(Findings[0].Value - Speed) <= 0.02 * Speed);
			TestEqual(TEXT("it names the limit"), Findings[0].Limit, PanLimit);
		}
	}

	{
		const double Speed = 0.5 * PanLimit;
		const FRoscTake Slow = OnTripod(MakeTake(ERoscRigAxis::Pan, [Speed](double T) { return Speed * T; }, 49));
		TestTrue(TEXT("a pan within limits gives no findings"), Rosc::Take::Check(Tripod, Slow.Poses, Fps).IsEmpty());
	}

	{
		// The tripod's height is set before the shot.
		TestTrue(TEXT("the tripod's lift is fixed during a shot"), Tripod.GetAxis(ERoscRigAxis::Lift).bUsed && Tripod.GetAxis(ERoscRigAxis::Lift).IsFixedDuringShot());
		const FRoscTake Rising = MakeTake(ERoscRigAxis::Lift, [](double T) { return 100.0 + 5.0 * T; }, 49);
		const TArray<FRoscMoveFinding> Findings = Rosc::Take::Check(Tripod, Rising.Poses, Fps);
		TestEqual(TEXT("a lift that changes on a tripod is a FixedAxisMoved"), CountFor(Findings, ERoscRigAxis::Lift, ERoscMoveProblem::FixedAxisMoved), 1);
	}

	TestTrue(TEXT("one pose gives no findings"), Rosc::Take::Check(Tripod, MakeTake(ERoscRigAxis::Pan, [](double) { return 1000.0; }, 1).Poses, Fps).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscTakeDriveTest, "Rosc.Core.Take.Drive", TakeTestFlags)
bool FRoscTakeDriveTest::RunTest(const FString& Parameters)
{
	// A device on a tripod turns the head and leaves the legs where they are.
	const FRoscRigSpec Tripod = Rosc::Rig::GetDefaultPreset(ERoscRigType::Tripod).Spec;
	FRoscRigPose AtZero;
	AtZero.Pan = 10.0;
	AtZero.Lift = 120.0;
	const FTransform DeviceAtZero(FRotator(0.0, 90.0, 0.0), FVector(100.0, 0.0, 0.0));
	const FTransform DeviceTurned(FRotator(5.0, 120.0, 2.0), FVector(300.0, 50.0, 20.0));
	const FRoscRigPose Driven = Rosc::Take::DrivePose(Tripod, AtZero, DeviceAtZero, DeviceTurned, AtZero);
	TestEqual(TEXT("the device's pan is added to the head's"), Driven.Pan, 40.0, 1.e-9);
	TestEqual(TEXT("the device's tilt is added to the head's"), Driven.Tilt, Tripod.GetAxis(ERoscRigAxis::Tilt).bUsed ? 5.0 : 0.0, 1.e-9);
	TestEqual(TEXT("the device's roll is added when the head rolls"), Driven.Roll, Tripod.GetAxis(ERoscRigAxis::Roll).bUsed ? 2.0 : 0.0, 1.e-9);
	TestEqual(TEXT("a tripod does not walk with the device"), Driven.Lift, 120.0, 1.e-9);
	TestEqual(TEXT("nor travel"), Driven.Travel, 0.0, 1.e-9);

	// A turn past half a circle carries on from the previous pose instead of jumping back.
	FRoscRigPose Previous = AtZero;
	double LastPan = AtZero.Pan;
	for (int32 Step = 1; Step <= 40; ++Step)
	{
		const FTransform Device(FRotator(0.0, FRotator::NormalizeAxis(90.0 + Step * 10.0), 0.0), FVector::ZeroVector);
		Previous = Rosc::Take::DrivePose(Tripod, AtZero, DeviceAtZero, Device, Previous);
		TestEqual(FString::Printf(TEXT("step %d of a slow pan goes on turning"), Step), Previous.Pan - LastPan, 10.0, 1.e-6);
		LastPan = Previous.Pan;
	}

	// A handheld rig walks where the device walks, read along the way the camera looked when zeroed.
	const FRoscRigSpec Handheld = Rosc::Rig::GetDefaultPreset(ERoscRigType::Handheld).Spec;
	FRoscRigPose HandAtZero;
	HandAtZero.Lift = 150.0;
	const FTransform Walked(FRotator(0.0, 90.0, 0.0), FVector(100.0, 200.0, -10.0));
	const FRoscRigPose Walking = Rosc::Take::DrivePose(Handheld, HandAtZero, DeviceAtZero, Walked, HandAtZero);
	TestEqual(TEXT("walking the way the device faced is travel"), Walking.Travel, 200.0, 1.e-3);
	TestEqual(TEXT("with no sideways step"), Walking.Traverse, 0.0, 1.e-3);
	TestEqual(TEXT("and crouching is lift"), Walking.Lift, 140.0, 1.e-3);
	TestTrue(TEXT("handheld is free-moving"), Rosc::Take::IsFreeMoving(ERoscRigType::Handheld));
	TestFalse(TEXT("a crane is not"), Rosc::Take::IsFreeMoving(ERoscRigType::Crane));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoscTakeTurnHeadTest, "Rosc.Core.Take.TurnHead", TakeTestFlags)
bool FRoscTakeTurnHeadTest::RunTest(const FString& Parameters)
{
	// Turning the view turns the head so the camera, as the rig places it, looks where the view does.
	for (const ERoscRigType Type : { ERoscRigType::Tripod, ERoscRigType::Crane, ERoscRigType::Steadicam })
	{
		const FRoscRigPreset& Preset = Rosc::Rig::GetDefaultPreset(Type);
		FRoscRigPose Pose;
		Pose.Lift = 120.0;
		Pose.Swing = 40.0;
		Pose.Extension = Preset.Spec.GetAxis(ERoscRigAxis::Extension).Min;
		const FRotator Camera = Rosc::Rig::SolveMount(Type, Pose).Rotator();
		const FRotator View = Camera + FRotator(10.0, 30.0, 0.0);
		const FRoscRigPose Turned = Rosc::Take::TurnHead(Preset.Spec, Pose, Camera, View);
		const FRotator Looks = Rosc::Rig::SolveMount(Type, Turned).Rotator();
		const FString Name = Rosc::Rig::GetTypeName(Type).ToString();
		TestEqual(Name + TEXT(": the camera turns to the view's direction"), FRotator::NormalizeAxis(Looks.Yaw - View.Yaw), 0.0, 1.e-4);
		TestEqual(Name + TEXT(": and to its height"), FRotator::NormalizeAxis(Looks.Pitch - View.Pitch), 0.0, 1.e-4);
		TestEqual(Name + TEXT(": nothing but the head moves"), Turned.Swing, Pose.Swing, 1.e-9);

		// A head that tilts a full turn (a crane's remote head) has no stop to run into.
		const FRoscAxisLimits& Tilt = Preset.Spec.GetAxis(ERoscRigAxis::Tilt);
		if (Tilt.Max + 20.0 < 180.0)
		{
			const FRoscRigPose TooFar = Rosc::Take::TurnHead(Preset.Spec, Pose, Camera, Camera + FRotator(Tilt.Max + 20.0, 0.0, 0.0));
			TestEqual(Name + TEXT(": the head stops at its tilt limit"), TooFar.Tilt, Tilt.Max, 1.e-6);
		}
	}
	return true;
}

#endif
