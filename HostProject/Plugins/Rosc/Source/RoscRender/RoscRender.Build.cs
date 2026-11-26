// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

using UnrealBuildTool;

public class RoscRender : ModuleRules
{
	public RoscRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"EditorSubsystem",
			"MovieRenderPipelineCore",
			"UMG",
			"RoscCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Engine",
			"LevelSequence",
			"MovieRenderPipelineEditor",
			"MovieRenderPipelineRenderPasses",
			"MovieScene",
			"Slate",
			"SlateCore",
			"UnrealEd"
		});
	}
}
