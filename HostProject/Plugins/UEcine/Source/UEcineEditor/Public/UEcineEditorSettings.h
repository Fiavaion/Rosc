#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "UEcineDeliver.h"
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

	/** What the Deliver tab is set to deliver. */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "Deliver For"))
	EUEcineDeliverIntent DeliverIntent = EUEcineDeliverIntent::Review;

	/**
	 * The folder deliveries are written under; each sequence has a folder of its own in it.
	 * Left empty, they go under UEcine/Deliveries in the project's Saved folder.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "Delivery Folder"))
	FDirectoryPath DeliverFolder;

	/**
	 * How delivered files are named. Words in braces stand for something of the shot:
	 * {sequence} {scene} {shot} {size} {camera} {intent} {date} {version}.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "File Name Pattern"))
	FString DeliverNamePattern = UEcine::Deliver::DefaultNamePattern;

	/** Review movies open with a slate. */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "Slate on Review Movies"))
	bool bDeliverSlate = true;

	/** Review movies carry camera and shot data over the picture. */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "Burn-ins on Review Movies"))
	bool bDeliverBurnIn = true;

	/**
	 * The folder shot lists, camera reports and printable pages are written under; each
	 * sequence has a folder of its own in it. Left empty, they go under UEcine/Paperwork in
	 * the project's Saved folder.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Shots", meta = (DisplayName = "Paperwork Folder"))
	FDirectoryPath PaperworkFolder;

	static EUEcineDistanceUnits GetDistanceUnits();

	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
};
