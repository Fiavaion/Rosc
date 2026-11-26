// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CineCameraSettings.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "RoscCameraPackage.h"
#include "RoscCameraPackageComponent.generated.h"

class UCineCameraComponent;


/**
 * A focus mark: a distance from the camera, or a point in the level. A point is kept as a place
 * on a subject, so it stays on the subject when the subject or the camera moves.
 */
USTRUCT(BlueprintType)
struct FRoscFocusMark
{
	GENERATED_BODY()

	/** How far from the camera the focus was when it was marked, in centimetres along the lens axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc|Focus", meta = (Units = "cm"))
	float DistanceCm = 0.0f;

	/** True for a mark on a point in the level: the distance is then measured to the point each time it is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc|Focus")
	bool bPoint = false;

	/** The subject the point is on. With none, Offset is a place in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc|Focus")
	TSoftObjectPtr<AActor> Subject;

	/** The point, in the subject's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc|Focus")
	FVector Offset = FVector::ZeroVector;
};

/**
 * Lives on a standard Cine Camera Actor and remembers which real camera body, recording format
 * and lens it represents, and the focus puller's marks for it. Applying the package writes
 * ordinary Cine Camera properties, so the camera keeps working if the component is removed.
 */
UCLASS(ClassGroup = (Rosc), meta = (BlueprintSpawnableComponent, DisplayName = "Rosc Camera Package"))
class ROSCRUNTIME_API URoscCameraPackageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc")
	FRoscCameraPackage Package;

	/** What the focus puller has marked for this camera, in the order it was marked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc|Focus")
	TArray<FRoscFocusMark> FocusMarks;

	/** How long a rack from one focus distance to another takes, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rosc|Focus", meta = (ClampMin = "0.1", Units = "s"))
	float RackSeconds = 1.0f;

	/**
	 * Writes the package to the owning actor's Cine Camera: filmback, lens limits, focal length,
	 * aperture, motion blur and, with physical exposure on, the exposure settings.
	 * @param OutWarnings Things worth telling the user that did not stop the apply, such as vignetting.
	 * @return false, with the reason in OutError and the camera untouched, when the owner has no
	 *         Cine Camera or the package names equipment that is not in the database.
	 */
	bool ApplyToCamera(FText& OutError, TArray<FText>* OutWarnings = nullptr);

	UCineCameraComponent* GetCineCamera() const;

	/** The package component on an actor, or null when it has none. */
	static URoscCameraPackageComponent* Find(const AActor* Actor);

	/**
	 * In a running level, crops the camera to its delivery frame while a render asks for that
	 * (see Rosc::Deliver::DeliveryFrameOnlyVariable), and puts its own crop back afterwards.
	 * The camera in the editor is never touched.
	 */
	void UpdateDeliveryFrameCrop();

	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** The camera's own crop, kept while it is cropped to the delivery frame. */
	TOptional<FPlateCropSettings> CropBeforeRender;
};
