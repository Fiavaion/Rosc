// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcineEditorSettings.h"

EUEcineDistanceUnits UUEcineEditorSettings::GetDistanceUnits()
{
	return GetDefault<UUEcineEditorSettings>()->DistanceUnits;
}

FName UUEcineEditorSettings::GetContainerName() const
{
	return TEXT("Editor");
}

FName UUEcineEditorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
