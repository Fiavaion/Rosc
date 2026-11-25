// Copyright 2026 Fiavaion. All rights reserved.

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
