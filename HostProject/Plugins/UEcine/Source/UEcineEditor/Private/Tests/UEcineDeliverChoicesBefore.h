// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UEcineDeliverEditing.h"
#include "UEcineEditorSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

/** The user's own choices for deliveries as they were when this was made, to put back after a test that changes them. */
struct FUEcineDeliverChoicesBefore
{
	FUEcineDeliverChoicesBefore()
	{
		const UUEcineEditorSettings* Choices = GetDefault<UUEcineEditorSettings>();
		Intent = Choices->DeliverIntent;
		Folder = Choices->DeliverFolder;
		Pattern = Choices->DeliverNamePattern;
		bSlate = Choices->bDeliverSlate;
		bBurnIn = Choices->bDeliverBurnIn;
	}

	void PutBack() const
	{
		UEcine::DeliverEditing::EditChoices([this](UUEcineEditorSettings& Choices)
		{
			Choices.DeliverIntent = Intent;
			Choices.DeliverFolder = Folder;
			Choices.DeliverNamePattern = Pattern;
			Choices.bDeliverSlate = bSlate;
			Choices.bDeliverBurnIn = bBurnIn;
		});
	}

	EUEcineDeliverIntent Intent;
	FDirectoryPath Folder;
	FString Pattern;
	bool bSlate;
	bool bBurnIn;
};

#endif
