// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

using UnrealBuildTool;

public class RoscRuntime : ModuleRules
{
	public RoscRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "CinematicCamera", "RoscCore" });
	}
}
