// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "RoscDeliver.h"
#include "RoscMonitorTypes.h"
#include "RoscUnits.h"
#include "RoscEditorSettings.generated.h"

/** Per-user preferences for Rosc, under Editor Preferences > Plugins > Rosc. */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Rosc"))
class ROSCEDITOR_API URoscEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Whether distances are shown in metres or in feet and inches. */
	UPROPERTY(config, EditAnywhere, Category = "Units")
	ERoscDistanceUnits DistanceUnits = ERoscDistanceUnits::Metric;

	/** Which monitor tools are on. Set from the Monitor tab (see URoscMonitorSubsystem), so it is not shown here. */
	UPROPERTY(config)
	FRoscMonitorSettings MonitorSettings;

	/** What the Deliver tab is set to deliver. */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "Deliver For"))
	ERoscDeliverIntent DeliverIntent = ERoscDeliverIntent::Review;

	/**
	 * The folder deliveries are written under; each sequence has a folder of its own in it.
	 * Left empty, they go under Rosc/Deliveries in the project's Saved folder.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "Delivery Folder"))
	FDirectoryPath DeliverFolder;

	/**
	 * How delivered files are named. Words in braces stand for something of the shot:
	 * {sequence} {scene} {shot} {size} {camera} {intent} {date} {version}.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "File Name Pattern"))
	FString DeliverNamePattern = Rosc::Deliver::DefaultNamePattern;

	/** Review movies open with a slate. */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "Slate on Review Movies"))
	bool bDeliverSlate = true;

	/** Review movies carry camera and shot data over the picture. */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "Burn-ins on Review Movies"))
	bool bDeliverBurnIn = true;

	/** YouTube files and DCP sources are made at 4K, not at 1080p and 2K. */
	UPROPERTY(config, EditAnywhere, Category = "Deliver", meta = (DisplayName = "YouTube and DCP Source at 4K"))
	bool bDeliverFourK = false;

	/**
	 * The folder shot lists, camera reports and printable pages are written under; each
	 * sequence has a folder of its own in it. Left empty, they go under Rosc/Paperwork in
	 * the project's Saved folder.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Shots", meta = (DisplayName = "Paperwork Folder"))
	FDirectoryPath PaperworkFolder;

	static ERoscDistanceUnits GetDistanceUnits();

	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
};
