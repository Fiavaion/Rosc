// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

using UnrealBuildTool;

public class RoscMonitor : ModuleRules
{
	public RoscMonitor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RoscCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"RenderCore",
			"Renderer",
			"RHI"
		});
	}
}
