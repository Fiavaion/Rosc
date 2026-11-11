#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "UEcineCameraPackage.h"
#include "UEcineCameraPackageComponent.generated.h"

class UCineCameraComponent;

/**
 * Lives on a standard Cine Camera Actor and remembers which real camera body, recording format
 * and lens it represents. Applying the package writes ordinary Cine Camera properties, so the
 * camera keeps working if the component is removed.
 */
UCLASS(ClassGroup = (UEcine), meta = (BlueprintSpawnableComponent, DisplayName = "UEcine Camera Package"))
class UECINERUNTIME_API UUEcineCameraPackageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEcine")
	FUEcineCameraPackage Package;

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
	static UUEcineCameraPackageComponent* Find(const AActor* Actor);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
