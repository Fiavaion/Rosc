#include "Misc/AutomationTest.h"
#include "UEcineMove.h"
#include "UEcineOperator.h"
#include "UEcineRigTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const EAutomationTestFlags RigTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** Cubic interpolation between two keys from their values and slopes, as a curve editor draws it. */
	double EvaluateKeys(const TArray<FUEcineMoveKey>& Keys, double TimeSeconds)
	{
		for (int32 Index = 0; Index + 1 < Keys.Num(); ++Index)
		{
			const FUEcineMoveKey& A = Keys[Index];
			const FUEcineMoveKey& B = Keys[Index + 1];
			if (TimeSeconds <= B.TimeSeconds)
			{
				const double Span = B.TimeSeconds - A.TimeSeconds;
				const double S = (TimeSeconds - A.TimeSeconds) / Span;
				const double S2 = S * S;
				const double S3 = S2 * S;
				return (2.0 * S3 - 3.0 * S2 + 1.0) * A.Value
					+ (S3 - 2.0 * S2 + S) * Span * A.LeaveSlopePerSecond
					+ (-2.0 * S3 + 3.0 * S2) * B.Value
					+ (S3 - S2) * Span * B.ArriveSlopePerSecond;
			}
		}
		return Keys.Last().Value;
	}

	FUEcineMoveTiming MakeTiming(double Duration, double EaseIn, double EaseOut)
	{
		FUEcineMoveTiming Timing;
		Timing.DurationSeconds = Duration;
		Timing.EaseInFraction = EaseIn;
		Timing.EaseOutFraction = EaseOut;
		return Timing;
	}

	const FUEcineMoveFinding* FindFinding(const TArray<FUEcineMoveFinding>& Findings, EUEcineRigAxis Axis, EUEcineMoveProblem Problem)
	{
		return Findings.FindByPredicate([Axis, Problem](const FUEcineMoveFinding& Finding) { return Finding.Axis == Axis && Finding.Problem == Problem; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineRigPresetsTest, "UEcine.Core.Rig.Presets", RigTestFlags)

bool FUEcineRigPresetsTest::RunTest(const FString& Parameters)
{
	TSet<FString> Ids;
	for (const FUEcineRigPreset& Preset : UEcine::Rig::GetPresets())
	{
		TestFalse(*FString::Printf(TEXT("%s: the id is used once"), *Preset.Id), Ids.Contains(Preset.Id));
		Ids.Add(Preset.Id);
		TestFalse(*FString::Printf(TEXT("%s: has a name"), *Preset.Id), Preset.Name.IsEmpty());
		TestEqual(*FString::Printf(TEXT("%s: the spec remembers its preset"), *Preset.Id), *Preset.Spec.PresetId, *Preset.Id);

		bool bHasMovingAxis = false;
		for (int32 AxisIndex = 0; AxisIndex < UEcineRigAxisCount; ++AxisIndex)
		{
			const EUEcineRigAxis Axis = static_cast<EUEcineRigAxis>(AxisIndex);
			const FUEcineAxisLimits& Limits = Preset.Spec.Axes[AxisIndex];
			if (!Limits.bUsed)
			{
				TestEqual(*FString::Printf(TEXT("%s: unused axis %d rests at zero"), *Preset.Id, AxisIndex), Preset.DefaultPose.Get(Axis), 0.0);
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s: axis %d range is ordered"), *Preset.Id, AxisIndex), Limits.Min <= Limits.Max);
			TestTrue(*FString::Printf(TEXT("%s: axis %d starts inside its range"), *Preset.Id, AxisIndex),
				Preset.DefaultPose.Get(Axis) >= Limits.Min && Preset.DefaultPose.Get(Axis) <= Limits.Max);
			TestFalse(*FString::Printf(TEXT("%s: axis %d has a name"), *Preset.Id, AxisIndex), UEcine::Rig::GetAxisName(Preset.Spec.Type, Axis).IsEmpty());
			bHasMovingAxis |= !Limits.IsFixedDuringShot();
		}
		TestTrue(*FString::Printf(TEXT("%s: something can move during a shot"), *Preset.Id), bHasMovingAxis);
	}

	for (const EUEcineRigType Type : { EUEcineRigType::Tripod, EUEcineRigType::Slider, EUEcineRigType::Dolly, EUEcineRigType::Jib,
		EUEcineRigType::Crane, EUEcineRigType::Steadicam, EUEcineRigType::Handheld, EUEcineRigType::Drone })
	{
		TestTrue(TEXT("Every rig type has a default preset of its own type"), UEcine::Rig::GetDefaultPreset(Type).Spec.Type == Type);
	}

	TestNotNull(TEXT("A preset is found by id"), UEcine::Rig::FindPreset(TEXT("crane-30ft")));
	TestNull(TEXT("An unknown id finds nothing"), UEcine::Rig::FindPreset(TEXT("crane-9000ft")));

	// Clamping brings a pose back inside the rig and clears axes the rig does not have.
	const FUEcineRigSpec& Crane = UEcine::Rig::FindPreset(TEXT("crane-22ft"))->Spec;
	FUEcineRigPose Wild;
	Wild.Extension = 5000.0;
	Wild.Boom = -80.0;
	Wild.Travel = 123.0;
	const FUEcineRigPose Clamped = Crane.ClampPose(Wild);
	TestEqual(TEXT("Telescope is clamped to full reach"), Clamped.Extension, 670.0);
	TestEqual(TEXT("Boom is clamped to its lowest angle"), Clamped.Boom, -30.0);
	TestEqual(TEXT("A crane has no travel axis"), Clamped.Travel, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineRigKinematicsTest, "UEcine.Core.Rig.Kinematics", RigTestFlags)

bool FUEcineRigKinematicsTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Rig;
	const double Tolerance = 0.01;

	// Tripod: the head turns in place at the height of the sticks.
	{
		FUEcineRigPose Pose;
		Pose.Lift = 150.0;
		Pose.Pan = 30.0;
		Pose.Tilt = -10.0;
		const FTransform Mount = SolveMount(EUEcineRigType::Tripod, Pose);
		TestTrue(TEXT("Tripod: camera sits on top of the sticks"), Mount.GetLocation().Equals(FVector(0.0, 0.0, 150.0), Tolerance));
		TestTrue(TEXT("Tripod: pan and tilt turn the camera"), Mount.Rotator().Equals(FRotator(-10.0, 30.0, 0.0), Tolerance));
	}

	// Crane: a 5 m arm swung 90 degrees and boomed up 30 from a pivot 2 m up.
	{
		FUEcineRigPose Pose;
		Pose.Lift = 200.0;
		Pose.Extension = 500.0;
		Pose.Swing = 90.0;
		Pose.Boom = 30.0;
		const FTransform Mount = SolveMount(EUEcineRigType::Crane, Pose);
		TestTrue(TEXT("Crane: head position follows the arm"), Mount.GetLocation().Equals(FVector(0.0, 433.0127, 450.0), Tolerance));
		TestTrue(TEXT("Crane: head looks along the arm"), Mount.GetRotation().GetForwardVector().Equals(FVector(0.0, 1.0, 0.0), 1.e-4));
		TestTrue(TEXT("Crane: head stays level when the arm booms"), Mount.GetRotation().GetUpVector().Equals(FVector(0.0, 0.0, 1.0), 1.e-4));

		Pose.Pan = 20.0;
		Pose.Tilt = -15.0;
		const FTransform Panned = SolveMount(EUEcineRigType::Crane, Pose);
		TestTrue(TEXT("Crane: the head pans relative to the arm"), Panned.Rotator().Equals(FRotator(-15.0, 110.0, 0.0), Tolerance));
		TestTrue(TEXT("Crane: working the head does not move it"), Panned.GetLocation().Equals(Mount.GetLocation(), Tolerance));

		// Telescoping in and out moves the head along the arm, which is what tells a crane from a jib arc.
		Pose.Extension = 300.0;
		const FTransform Retracted = SolveMount(EUEcineRigType::Crane, Pose);
		TestEqual(TEXT("Crane: telescoping changes the reach by the same amount"), FVector::Dist(Retracted.GetLocation(), FVector(0.0, 0.0, 200.0)), 300.0, Tolerance);
	}

	// Jib: swinging the arm carries the camera round an arc of constant radius.
	{
		FUEcineRigPose Pose;
		Pose.Lift = 150.0;
		Pose.Extension = 400.0;
		for (const double Swing : { -60.0, 0.0, 45.0, 170.0 })
		{
			Pose.Swing = Swing;
			const FVector Location = SolveMount(EUEcineRigType::Jib, Pose).GetLocation();
			TestEqual(TEXT("Jib: the arc keeps its radius"), FVector::Dist2D(Location, FVector::ZeroVector), 400.0, Tolerance);
			TestEqual(TEXT("Jib: a level arm keeps its height"), Location.Z, 150.0, Tolerance);
		}
	}

	// Dolly: along the track, with the column rising from it and the head turning with the chassis.
	{
		FUEcineRigPose Pose;
		Pose.Travel = 300.0;
		Pose.Lift = 130.0;
		const FTransform Straight = SolveMount(EUEcineRigType::Dolly, Pose, StraightTrackPoint(Pose.Travel));
		TestTrue(TEXT("Dolly: straight track runs forward"), Straight.GetLocation().Equals(FVector(300.0, 0.0, 130.0), Tolerance));

		Pose.Pan = 10.0;
		const FTransform Curve(FRotator(0.0, 90.0, 0.0), FVector(100.0, 200.0, 0.0));
		const FTransform OnCurve = SolveMount(EUEcineRigType::Dolly, Pose, Curve);
		TestTrue(TEXT("Dolly: the camera is above the track point"), OnCurve.GetLocation().Equals(FVector(100.0, 200.0, 130.0), Tolerance));
		TestTrue(TEXT("Dolly: the head pans from the direction of the track"), OnCurve.Rotator().Equals(FRotator(0.0, 100.0, 0.0), Tolerance));
	}

	// Free-moving rigs go where they are taken.
	{
		FUEcineRigPose Pose;
		Pose.Travel = 500.0;
		Pose.Traverse = -200.0;
		Pose.Lift = 3000.0;
		Pose.Pan = 45.0;
		const FTransform Mount = SolveMount(EUEcineRigType::Drone, Pose);
		TestTrue(TEXT("Drone: position is forward, sideways and altitude"), Mount.GetLocation().Equals(FVector(500.0, -200.0, 3000.0), Tolerance));
		TestTrue(TEXT("Drone: heading turns the camera"), Mount.Rotator().Equals(FRotator(0.0, 45.0, 0.0), Tolerance));
	}

	TestEqual(TEXT("Distances are shown in the chosen units"), *FormatAxisValue(EUEcineRigAxis::Extension, 670.0, EUEcineDistanceUnits::Metric).ToString(), TEXT("6.7 m"));
	TestEqual(TEXT("Angles are shown in degrees"), *FormatAxisValue(EUEcineRigAxis::Boom, 22.5, EUEcineDistanceUnits::Metric).ToString(), TEXT("22.5°"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMoveTimingTest, "UEcine.Core.Move.Timing", RigTestFlags)

bool FUEcineMoveTimingTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Move;

	// A 4 second move with a quarter of it easing in and a quarter easing out.
	const FUEcineMoveTiming Feathered = MakeTiming(4.0, 0.25, 0.25);
	TestEqual(TEXT("A move starts at its start"), Progress(Feathered, 0.0), 0.0, 1.e-9);
	TestEqual(TEXT("A move ends at its end"), Progress(Feathered, 4.0), 1.0, 1.e-9);
	TestEqual(TEXT("Even easing is halfway at half time"), Progress(Feathered, 2.0), 0.5, 1.e-9);
	TestEqual(TEXT("Progress at the end of the ease in"), Progress(Feathered, 1.0), 1.0 / 6.0, 1.e-9);
	TestEqual(TEXT("Time before the move holds the start"), Progress(Feathered, -1.0), 0.0, 1.e-9);
	TestEqual(TEXT("Time after the move holds the end"), Progress(Feathered, 9.0), 1.0, 1.e-9);

	// 3 m in those 4 seconds: the steady part runs at 1 m/s, reached in 1 s.
	TestEqual(TEXT("Peak speed"), PeakSpeed(Feathered, 300.0), 100.0, 1.e-6);
	TestEqual(TEXT("Peak acceleration"), PeakAcceleration(Feathered, 300.0), 100.0, 1.e-6);
	TestEqual(TEXT("Direction does not change the peak speed"), PeakSpeed(Feathered, -300.0), 100.0, 1.e-6);

	const FUEcineMoveTiming Linear = MakeTiming(5.0, 0.0, 0.0);
	TestEqual(TEXT("With no ease the move is linear"), Progress(Linear, 1.25), 0.25, 1.e-9);
	TestEqual(TEXT("A linear move runs at its average speed"), PeakSpeed(Linear, 300.0), 60.0, 1.e-6);
	TestTrue(TEXT("A linear move starts instantly"), PeakAcceleration(Linear, 300.0) > 1.e12);
	TestEqual(TEXT("An axis that does not move does not accelerate"), PeakAcceleration(Linear, 0.0), 0.0);

	// Progress never runs backwards, whatever the easing.
	for (const FUEcineMoveTiming& Timing : { Feathered, Linear, MakeTiming(3.0, 1.0, 0.0), MakeTiming(3.0, 0.0, 1.0), MakeTiming(3.0, 0.5, 0.5), MakeTiming(3.0, 0.9, 0.6), MakeTiming(2.0, 0.1, 0.4) })
	{
		double Previous = 0.0;
		const TArray<FUEcineMoveKey> Keys = MakeKeys(10.0, 250.0, Timing);
		for (int32 Step = 0; Step <= 100; ++Step)
		{
			const double Time = Timing.DurationSeconds * Step / 100.0;
			const double Value = Progress(Timing, Time);
			TestTrue(TEXT("Progress never runs backwards"), Value >= Previous - 1.e-12);
			Previous = Value;

			// The keys handed to a curve editor draw exactly the same move.
			TestEqual(TEXT("Keys reproduce the move"), EvaluateKeys(Keys, Time), 10.0 + 240.0 * Value, 1.e-6);
		}
		TestEqual(TEXT("Every easing arrives"), Previous, 1.0, 1.e-9);
		TestTrue(TEXT("A move needs between two and four keys"), Keys.Num() >= 2 && Keys.Num() <= 4);
	}

	FUEcineRigPose Start;
	Start.Pan = -20.0;
	Start.Extension = 300.0;
	FUEcineRigPose End;
	End.Pan = 40.0;
	End.Extension = 600.0;
	const FUEcineRigPose Middle = Evaluate(Start, End, Feathered, 2.0);
	TestEqual(TEXT("Every axis moves together: pan"), Middle.Pan, 10.0, 1.e-9);
	TestEqual(TEXT("Every axis moves together: telescope"), Middle.Extension, 450.0, 1.e-9);

	// Three marks: 4 seconds to the second, then 2 seconds back to where it started.
	TArray<FUEcineRigMark> Marks;
	Marks.AddDefaulted(3);
	Marks[0].Pose = Start;
	Marks[1].Pose = End;
	Marks[1].Arrival = Feathered;
	Marks[2].Pose = Start;
	Marks[2].Arrival = MakeTiming(2.0, 0.5, 0.5);
	TestEqual(TEXT("Marks: total time is the sum of the moves"), GetTotalDuration(Marks), 6.0, 1.e-9);
	TestEqual(TEXT("Marks: starts on the first mark"), EvaluateMarks(Marks, 0.0).Pan, -20.0, 1.e-9);
	TestEqual(TEXT("Marks: halfway through the first move"), EvaluateMarks(Marks, 2.0).Pan, 10.0, 1.e-9);
	TestEqual(TEXT("Marks: on the second mark when the first move ends"), EvaluateMarks(Marks, 4.0).Pan, 40.0, 1.e-9);
	TestEqual(TEXT("Marks: halfway through the second move"), EvaluateMarks(Marks, 5.0).Pan, 10.0, 1.e-9);
	TestEqual(TEXT("Marks: holds the last mark afterwards"), EvaluateMarks(Marks, 60.0).Pan, -20.0, 1.e-9);
	TestEqual(TEXT("Marks: one mark is a held position"), EvaluateMarks(MakeArrayView(Marks.GetData(), 1), 3.0).Extension, 300.0, 1.e-9);

	// One curve per axis runs through all the marks and matches the solver everywhere.
	const TArray<FUEcineMoveKey> PanKeys = MakeKeysForMarks(Marks, EUEcineRigAxis::Pan);
	TestEqual(TEXT("Marks: four keys for the first move and two more for the second"), PanKeys.Num(), 6);
	for (int32 Step = 0; Step <= 120; ++Step)
	{
		const double Time = 6.0 * Step / 120.0;
		TestEqual(TEXT("Marks: the keyed curve matches the solver"), EvaluateKeys(PanKeys, Time), EvaluateMarks(Marks, Time).Pan, 1.e-6);
	}
	TestEqual(TEXT("Marks: one mark is one key"), MakeKeysForMarks(MakeArrayView(Marks.GetData(), 1), EUEcineRigAxis::Pan).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineMoveLimitsTest, "UEcine.Core.Move.Limits", RigTestFlags)

bool FUEcineMoveLimitsTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Move;

	const FUEcineRigPreset& Crane = *UEcine::Rig::FindPreset(TEXT("crane-22ft"));
	FUEcineRigPose Start = Crane.DefaultPose;
	Start.Extension = 200.0;
	FUEcineRigPose End = Start;
	End.Extension = 670.0;

	// Full telescope travel (4.7 m) in 2 seconds: faster and more abrupt than the arm can go.
	{
		const TArray<FUEcineMoveFinding> Findings = Check(Crane.Spec, Start, End, MakeTiming(2.0, 0.25, 0.25));
		const FUEcineMoveFinding* TooFast = FindFinding(Findings, EUEcineRigAxis::Extension, EUEcineMoveProblem::TooFast);
		const FUEcineMoveFinding* TooAbrupt = FindFinding(Findings, EUEcineRigAxis::Extension, EUEcineMoveProblem::TooAbrupt);
		TestEqual(TEXT("Only the telescope is flagged"), Findings.Num(), 2);
		if (TestNotNull(TEXT("The telescope is too fast"), TooFast))
		{
			TestEqual(TEXT("Speed the move asks for"), TooFast->Value, 313.333, 0.01);
			TestEqual(TEXT("Speed the crane manages"), TooFast->Limit, 180.0);
			TestEqual(TEXT("Shortest duration that fixes the speed"), TooFast->MinimumDurationSeconds, 3.4815, 0.001);

			const FString Sentence = Describe(*TooFast, EUEcineRigType::Crane, EUEcineDistanceUnits::Metric).ToString();
			TestTrue(TEXT("The sentence names the axis as the crew does"), Sentence.Contains(TEXT("Telescope")));
			TestTrue(TEXT("The sentence gives both speeds"), Sentence.Contains(TEXT("3.13 m/s")) && Sentence.Contains(TEXT("1.8 m/s")));
			TestTrue(TEXT("The sentence says how long the move needs, rounded up"), Sentence.Contains(TEXT("3.5 s")));
		}
		if (TestNotNull(TEXT("The telescope is too abrupt"), TooAbrupt))
		{
			TestEqual(TEXT("Acceleration the move asks for"), TooAbrupt->Value, 626.667, 0.01);
			TestEqual(TEXT("Shortest duration that fixes the acceleration"), TooAbrupt->MinimumDurationSeconds, 3.5402, 0.001);
		}
	}

	// Taking the suggested time clears the findings.
	TestEqual(TEXT("The suggested duration is enough"), Check(Crane.Spec, Start, End, MakeTiming(3.6, 0.25, 0.25)).Num(), 0);

	// No ease at all: reported as an instant start, which more time cannot fix.
	{
		const TArray<FUEcineMoveFinding> Findings = Check(Crane.Spec, Start, End, MakeTiming(10.0, 0.0, 0.25));
		const FUEcineMoveFinding* Instant = FindFinding(Findings, EUEcineRigAxis::Extension, EUEcineMoveProblem::TooAbrupt);
		if (TestNotNull(TEXT("An instant start is flagged"), Instant))
		{
			TestEqual(TEXT("Time cannot fix an instant start"), Instant->MinimumDurationSeconds, 0.0);
			TestTrue(TEXT("The sentence asks for an ease"), Describe(*Instant, EUEcineRigType::Crane, EUEcineDistanceUnits::Metric).ToString().Contains(TEXT("ease in")));
		}
	}

	// A mark beyond the arm's reach.
	{
		FUEcineRigPose TooFar = End;
		TooFar.Extension = 700.0;
		const TArray<FUEcineMoveFinding> Findings = Check(Crane.Spec, Start, TooFar, MakeTiming(20.0, 0.25, 0.25));
		const FUEcineMoveFinding* Above = FindFinding(Findings, EUEcineRigAxis::Extension, EUEcineMoveProblem::AboveRange);
		TestEqual(TEXT("Only the reach is flagged"), Findings.Num(), 1);
		if (TestNotNull(TEXT("Past full reach is flagged"), Above))
		{
			TestEqual(TEXT("The reach limit is reported"), Above->Limit, 670.0);
			const FString Sentence = Describe(*Above, EUEcineRigType::Crane, EUEcineDistanceUnits::Imperial).ToString();
			TestTrue(TEXT("Imperial units are honoured"), Sentence.Contains(TEXT("23' 0\"")) && Sentence.Contains(TEXT("22' 0\"")));
		}
	}

	// Tripod height is set before the shot.
	{
		const FUEcineRigPreset& Tripod = UEcine::Rig::GetDefaultPreset(EUEcineRigType::Tripod);
		FUEcineRigPose Higher = Tripod.DefaultPose;
		Higher.Lift += 10.0;
		const TArray<FUEcineMoveFinding> Findings = Check(Tripod.Spec, Tripod.DefaultPose, Higher, MakeTiming(4.0, 0.25, 0.25));
		TestEqual(TEXT("Only the height is flagged"), Findings.Num(), 1);
		if (TestNotNull(TEXT("Changing tripod height mid-shot is flagged"), FindFinding(Findings, EUEcineRigAxis::Lift, EUEcineMoveProblem::FixedAxisMoved)))
		{
			const FString Sentence = Describe(Findings[0], EUEcineRigType::Tripod, EUEcineDistanceUnits::Metric).ToString();
			TestTrue(TEXT("The sentence gives both heights"), Sentence.Contains(TEXT("1.5 m")) && Sentence.Contains(TEXT("1.6 m")));
		}

		// A gentle pan and tilt is what a tripod is for.
		FUEcineRigPose Panned = Tripod.DefaultPose;
		Panned.Pan = 60.0;
		Panned.Tilt = -10.0;
		TestEqual(TEXT("A gentle pan is within limits"), Check(Tripod.Spec, Tripod.DefaultPose, Panned, MakeTiming(4.0, 0.25, 0.25)).Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineOperatorTest, "UEcine.Core.Operator", RigTestFlags)

bool FUEcineOperatorTest::RunTest(const FString& Parameters)
{
	using namespace UEcine::Operator;

	FUEcineOperator Handheld;
	Handheld.Style = EUEcineOperatorStyle::Handheld;
	Handheld.Seed = 7;

	FUEcineOperator OtherTake = Handheld;
	OtherTake.Seed = 8;

	FVector LocationBound;
	FVector RotationBound;
	GetBounds(Handheld, LocationBound, RotationBound);
	TestTrue(TEXT("Handheld moves the camera"), LocationBound.GetMax() > 0.0 && RotationBound.GetMax() > 0.0);

	bool bTakesDiffer = false;
	bool bMoves = false;
	FTransform Previous = Offset(Handheld, 0.0);
	for (int32 Frame = 0; Frame <= 240; ++Frame)
	{
		const double Time = Frame / 24.0;
		const FTransform Sample = Offset(Handheld, Time);

		TestTrue(TEXT("The same take repeats exactly"), Sample.Equals(Offset(Handheld, Time), 0.0));
		bTakesDiffer |= !Sample.Equals(Offset(OtherTake, Time), 1.e-4);
		bMoves |= !Sample.Equals(Previous, 1.e-4);

		const FVector Location = Sample.GetLocation();
		const FRotator Rotation = Sample.Rotator();
		TestTrue(TEXT("Location stays within the stated bounds"),
			FMath::Abs(Location.X) <= LocationBound.X + 1.e-6 && FMath::Abs(Location.Y) <= LocationBound.Y + 1.e-6 && FMath::Abs(Location.Z) <= LocationBound.Z + 1.e-6);
		TestTrue(TEXT("Rotation stays within the stated bounds"),
			FMath::Abs(Rotation.Yaw) <= RotationBound.X + 1.e-4 && FMath::Abs(Rotation.Pitch) <= RotationBound.Y + 1.e-4 && FMath::Abs(Rotation.Roll) <= RotationBound.Z + 1.e-4);

		// The motion is smooth: a tenth of a millisecond later the camera has barely moved.
		TestTrue(TEXT("Motion is continuous"), FVector::Dist(Location, Offset(Handheld, Time + 1.e-4).GetLocation()) < 0.01);
		Previous = Sample;
	}
	TestTrue(TEXT("The offset changes over time"), bMoves);
	TestTrue(TEXT("A different seed is a different take"), bTakesDiffer);

	FUEcineOperator LockedOff;
	TestTrue(TEXT("Locked off does not move"), Offset(LockedOff, 3.7).Equals(FTransform::Identity, 0.0));

	FUEcineOperator ZeroIntensity = Handheld;
	ZeroIntensity.Intensity = 0.0f;
	TestTrue(TEXT("Zero intensity does not move"), Offset(ZeroIntensity, 3.7).Equals(FTransform::Identity, 0.0));

	FUEcineOperator Rough = Handheld;
	Rough.Intensity = 2.0f;
	TestTrue(TEXT("Intensity scales the motion"), Offset(Rough, 1.3).GetLocation().Equals(Offset(Handheld, 1.3).GetLocation() * 2.0, 1.e-6));

	TestTrue(TEXT("A crane comes with a remote head"), GetDefaultStyle(EUEcineRigType::Crane) == EUEcineOperatorStyle::RemoteHead);
	TestTrue(TEXT("Handheld comes with a handheld operator"), GetDefaultStyle(EUEcineRigType::Handheld) == EUEcineOperatorStyle::Handheld);
	return true;
}

#endif
