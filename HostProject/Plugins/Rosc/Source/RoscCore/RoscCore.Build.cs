// Copyright 2026 Fiavaion. All rights reserved.

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
