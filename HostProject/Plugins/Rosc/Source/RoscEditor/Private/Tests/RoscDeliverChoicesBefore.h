// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "RoscDeliverEditing.h"
#include "RoscEditorSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

/** The user's own choices for deliveries as they were when this was made, to put back after a test that changes them. */
struct FRoscDeliverChoicesBefore
{
	FRoscDeliverChoicesBefore()
	{
		const URoscEditorSettings* Choices = GetDefault<URoscEditorSettings>();
		Intent = Choices->DeliverIntent;
		Folder = Choices->DeliverFolder;
		Pattern = Choices->DeliverNamePattern;
		bSlate = Choices->bDeliverSlate;
		bBurnIn = Choices->bDeliverBurnIn;
	}

	void PutBack() const
	{
		Rosc::DeliverEditing::EditChoices([this](URoscEditorSettings& Choices)
		{
			Choices.DeliverIntent = Intent;
			Choices.DeliverFolder = Folder;
			Choices.DeliverNamePattern = Pattern;
			Choices.bDeliverSlate = bSlate;
			Choices.bDeliverBurnIn = bBurnIn;
		});
	}

	ERoscDeliverIntent Intent;
	FDirectoryPath Folder;
	FString Pattern;
	bool bSlate;
	bool bBurnIn;
};

#endif
