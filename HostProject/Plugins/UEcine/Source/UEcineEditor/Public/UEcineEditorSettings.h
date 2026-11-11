#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UEcineMonitorTypes.h"
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

	/** Which monitor tools are on. Set from the Monitor tab (see UUEcineMonitorSubsystem), so it is not shown here. */
	UPROPERTY(config)
	FUEcineMonitorSettings MonitorSettings;

	static EUEcineDistanceUnits GetDistanceUnits();

	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
};
