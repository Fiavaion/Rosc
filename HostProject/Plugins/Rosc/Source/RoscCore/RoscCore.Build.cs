// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

using UnrealBuildTool;

public class RoscCore : ModuleRules
{
	public RoscCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "Projects" });
	}
}
