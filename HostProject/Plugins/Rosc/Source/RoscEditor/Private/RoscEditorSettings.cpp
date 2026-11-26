// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

#include "RoscEditorSettings.h"

ERoscDistanceUnits URoscEditorSettings::GetDistanceUnits()
{
	return GetDefault<URoscEditorSettings>()->DistanceUnits;
}

FName URoscEditorSettings::GetContainerName() const
{
	return TEXT("Editor");
}

FName URoscEditorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
