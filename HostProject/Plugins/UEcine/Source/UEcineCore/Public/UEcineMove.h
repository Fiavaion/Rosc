// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UEcineRigTypes.h"
#include "UEcineMove.generated.h"

/**
 * How long a move takes and how it starts and stops. The rig accelerates evenly for the ease-in
 * part, holds a steady speed, then decelerates evenly for the ease-out part, the way a grip
 * feathers a dolly or a crane operator feathers the arm.
 */
USTRUCT(BlueprintType)
struct UECINECORE_API FUEcineMoveTiming
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine", meta = (ClampMin = "0.1"))
	double DurationSeconds = 4.0;

	/** Share of the move spent getting up to speed, 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine", meta = (ClampMin = "0", ClampMax = "1"))
	double EaseInFraction = 0.25;

	/** Share of the move spent coming to rest, 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine", meta = (ClampMin = "0", ClampMax = "1"))
	double EaseOutFraction = 0.25;

	/** The same timing with a positive duration and ease fractions that fit inside the move. */
	FUEcineMoveTiming Normalized() const;
};

/** A position the rig is asked to hit, as a grip marks the floor with tape. */
USTRUCT(BlueprintType)
struct UECINECORE_API FUEcineRigMark
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	FUEcineRigPose Pose;

	/** How the rig arrives here from the mark before. Not used by the first mark. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	FUEcineMoveTiming Arrival;

	/**
	 * The move into this mark is a dolly zoom: the lens zooms as the camera travels, so that
	 * what it is focused on stays the same size in the frame. Not used by the first mark.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	bool bDollyZoom = false;
};

/** What a rig cannot do about a move. */
enum class EUEcineMoveProblem : uint8
{
	/** A mark is past the low end of the axis. */
	BelowRange,
	/** A mark is past the high end of the axis. */
	AboveRange,
	/** An axis that is set when the rig is built changes during the move. */
	FixedAxisMoved,
	/** The axis has to go faster than it can. */
	TooFast,
	/** The axis has to speed up or slow down harder than it can. */
	TooAbrupt
};

/** One way a move exceeds what the rig can do, with the numbers needed to explain it. */
struct FUEcineMoveFinding
{
	EUEcineRigAxis Axis = EUEcineRigAxis::Travel;
	EUEcineMoveProblem Problem = EUEcineMoveProblem::TooFast;
	/** What the move asks for: a position, a speed or an acceleration. Infinite for an instant start or stop. */
	double Value = 0.0;
	/** What the rig allows. For FixedAxisMoved, the value at the start of the move. */
	double Limit = 0.0;
	/** The shortest duration that would bring the move within the limit, or zero when time cannot fix it. */
	double MinimumDurationSeconds = 0.0;
};

/**
 * A point on an axis curve: the value at a time and the slope of the curve coming into and going
 * out of it. Cubic interpolation between these reproduces the move exactly. The two slopes
 * differ only where one move ends and the next begins.
 */
struct FUEcineMoveKey
{
	double TimeSeconds = 0.0;
	double Value = 0.0;
	double ArriveSlopePerSecond = 0.0;
	double LeaveSlopePerSecond = 0.0;
};

namespace UEcine::Move
{
	/** How far through the move the rig is, 0 to 1, at a time from the start of the move. */
	UECINECORE_API double Progress(const FUEcineMoveTiming& Timing, double TimeSeconds);

	/** The rig's pose at a time from the start of the move. */
	UECINECORE_API FUEcineRigPose Evaluate(const FUEcineRigPose& Start, const FUEcineRigPose& End, const FUEcineMoveTiming& Timing, double TimeSeconds);

	/** How long it takes to run through every mark in order, from the first to the last. */
	UECINECORE_API double GetTotalDuration(TConstArrayView<FUEcineRigMark> Marks);

	/**
	 * The rig's pose at a time from when it leaves the first mark, moving through the marks in
	 * order and coming to each one with that mark's arrival timing. Marks must not be empty.
	 */
	UECINECORE_API FUEcineRigPose EvaluateMarks(TConstArrayView<FUEcineRigMark> Marks, double TimeSeconds);

	/** Top speed reached by an axis that covers the given distance. */
	UECINECORE_API double PeakSpeed(const FUEcineMoveTiming& Timing, double Distance);

	/** Greatest acceleration of an axis that covers the given distance; infinite when it starts or stops with no ease. */
	UECINECORE_API double PeakAcceleration(const FUEcineMoveTiming& Timing, double Distance);

	/** Everything about the move that the rig cannot do, in axis order. Empty when the move is within the rig's limits. */
	UECINECORE_API TArray<FUEcineMoveFinding> Check(const FUEcineRigSpec& Spec, const FUEcineRigPose& Start, const FUEcineRigPose& End, const FUEcineMoveTiming& Timing);

	/**
	 * The findings worth showing for one move. A move that is too quick usually trips several
	 * axes at once and one answer, more time, clears them all; of those only the finding that
	 * needs the most time is kept, last. Findings that time cannot fix are all kept.
	 */
	UECINECORE_API TArray<FUEcineMoveFinding> MostLimiting(TConstArrayView<FUEcineMoveFinding> Findings);

	/** A finding as a sentence that says what is wrong and what would fix it. */
	UECINECORE_API FText Describe(const FUEcineMoveFinding& Finding, EUEcineRigType RigType, EUEcineDistanceUnits Units);

	/** The keys that draw one axis of the move as a curve, in time order. */
	UECINECORE_API TArray<FUEcineMoveKey> MakeKeys(double StartValue, double EndValue, const FUEcineMoveTiming& Timing);

	/** The keys that draw one axis through every mark in order, timed from leaving the first mark. Marks must not be empty. */
	UECINECORE_API TArray<FUEcineMoveKey> MakeKeysForMarks(TConstArrayView<FUEcineRigMark> Marks, EUEcineRigAxis Axis);
}
