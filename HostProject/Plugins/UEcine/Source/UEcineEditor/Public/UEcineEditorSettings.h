#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UEcineUnits.h"
#include "UEcineEditorSettings.generated.h"

/** Per-user preferences for UEcine, under Editor Preferences > Plugins > UEcine. */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "UEcine"))
class UECINEEDITOR_API UUEcineEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Whether distances are shown in metres or in feet and inches. */
	UPROPERTY(config, EditAnywhere, Category = "Units")
	EUEcineDistanceUnits DistanceUnits = EUEcineDistanceUnits::Metric;

	static EUEcineDistanceUnits GetDistanceUnits();

	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
};
