// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoscMove.h"
#include "RoscOperator.h"
#include "RoscRigTypes.h"
#include "RoscRig.generated.h"

class ACineCameraActor;
class USplineComponent;
class UStaticMeshComponent;

/**
 * A camera support standing in the level: tripod, slider, dolly, jib, crane, Steadicam,
 * handheld or drone. A standard Cine Camera Actor is attached to its camera mount, and the
 * rig's axes (track position, boom, telescope, pan, tilt and so on) place the mount the way
 * the real equipment would. The axes are what Sequencer keys, so a blocked move plays back
 * with the rig moving as a crew would move it.
 *
 * The grey shapes that show the rig in the editor are hidden in game and in renders.
 */
UCLASS(ClassGroup = (Rosc), meta = (DisplayName = "Rosc Rig"))
class ROSCRUNTIME_API ARoscRig : public AActor
{
	GENERATED_BODY()

public:
	ARoscRig();

	/** The rig's type and limits, with the travel range of a dolly taken from its track. */
	UFUNCTION(BlueprintPure, Category = "Rosc")
	FRoscRigSpec GetSpec() const;

	/** Changes what kind of rig this is and stands it up in the given pose. */
	UFUNCTION(BlueprintCallable, Category = "Rosc")
	void SetSpec(const FRoscRigSpec& NewSpec, const FRoscRigPose& NewPose);

	UFUNCTION(BlueprintPure, Category = "Rosc")
	FRoscRigPose GetPose() const;

	UFUNCTION(BlueprintCallable, Category = "Rosc")
	void SetPose(const FRoscRigPose& NewPose);

	/** Where the camera mount would be for a pose, relative to this actor. */
	FTransform SolveMount(const FRoscRigPose& Pose) const;

	/** Lays straight track of the given length along the rig's forward axis. Dolly only. */
	UFUNCTION(BlueprintCallable, Category = "Rosc")
	void SetStraightTrack(float LengthCm);

	UFUNCTION(BlueprintPure, Category = "Rosc")
	float GetTrackLength() const;

	/** The Cine Camera Actor attached to the camera mount, if there is one. */
	UFUNCTION(BlueprintPure, Category = "Rosc")
	ACineCameraActor* GetMountedCamera() const;

	/** Attaches a camera to the mount, at the mount's position. */
	UFUNCTION(BlueprintCallable, Category = "Rosc")
	void MountCamera(ACineCameraActor* Camera);

	USceneComponent* GetCameraMount() const { return CameraMount; }
	USplineComponent* GetTrack() const { return Track; }

	// Sequencer sets an animated property through a function named Set<Property> when one
	// exists; these move the rig as each axis changes. CallInEditor is what lets Sequencer
	// reach them while scrubbing in the editor: without it the engine drops function calls on
	// actors in an editor world (LESSON-API-001).
	UFUNCTION(BlueprintSetter, CallInEditor) void SetTravel(float Value);
	UFUNCTION(BlueprintSetter, CallInEditor) void SetTraverse(float Value);
	UFUNCTION(BlueprintSetter, CallInEditor) void SetLift(float Value);
	UFUNCTION(BlueprintSetter, CallInEditor) void SetSwing(float Value);
	UFUNCTION(BlueprintSetter, CallInEditor) void SetBoom(float Value);
	UFUNCTION(BlueprintSetter, CallInEditor) void SetExtension(float Value);
	UFUNCTION(BlueprintSetter, CallInEditor) void SetPan(float Value);
	UFUNCTION(BlueprintSetter, CallInEditor) void SetTilt(float Value);
	UFUNCTION(BlueprintSetter, CallInEditor) void SetRoll(float Value);

	/** The name of the property behind an axis, as Sequencer tracks refer to it. */
	static FName GetAxisPropertyName(ERoscRigAxis Axis);

	/** The positions this rig has been asked to hit, in shooting order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	TArray<FRoscRigMark> Marks;

	/** The operator imperfection baked on top of the move. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	FRoscOperator Operator;

	/**
	 * The marks and the operator as they were when the move was last baked to Sequencer, kept
	 * so that the Grip tab can say when the move has been changed since. No marks before the
	 * first bake.
	 */
	UPROPERTY()
	TArray<FRoscRigMark> BakedMarks;

	UPROPERTY()
	FRoscOperator BakedOperator;

	/** True when a move has been baked and the marks or the operator have changed since. */
	bool HasChangedSinceBake() const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

private:
	/** Moves the camera mount and the editor shapes to match the axes. */
	void UpdateRig();

	FTransform GetTrackPoint(double TravelCm) const;

	UPROPERTY(EditAnywhere, Category = "Rosc")
	FRoscRigSpec Spec;

	/** Along the slider rail or dolly track; forward and back for Steadicam, handheld and drone. Centimetres. */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetTravel, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Travel = 0.0f;

	/** Sideways, for Steadicam, handheld and drone. Centimetres. */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetTraverse, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Traverse = 0.0f;

	/** Tripod height, dolly column, crane pivot height, lens height or altitude. Centimetres. */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetLift, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Lift = 0.0f;

	/** The arm swinging left and right. Degrees. */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetSwing, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Swing = 0.0f;

	/** The arm rising and falling. Degrees. */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetBoom, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Boom = 0.0f;

	/** Arm length from pivot to head. Centimetres. */
	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetExtension, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Extension = 0.0f;

	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetPan, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Pan = 0.0f;

	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetTilt, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Tilt = 0.0f;

	UPROPERTY(EditAnywhere, Interp, BlueprintReadWrite, BlueprintSetter = SetRoll, Category = "Rosc|Axes", meta = (AllowPrivateAccess = "true"))
	float Roll = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Rosc")
	TObjectPtr<USceneComponent> Base;

	UPROPERTY(VisibleAnywhere, Category = "Rosc")
	TObjectPtr<USceneComponent> CameraMount;

	/** The dolly's track. Edit its points in the viewport to curve it. */
	UPROPERTY(VisibleAnywhere, Category = "Rosc")
	TObjectPtr<USplineComponent> Track;

	// Editor shapes: what stands on the floor, what holds the height, and the arm.
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BaseShape;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ColumnShape;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ArmShape;
};
