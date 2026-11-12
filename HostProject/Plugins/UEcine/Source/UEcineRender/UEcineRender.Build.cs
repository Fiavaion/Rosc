// Copyright 2026 Fiavaion. All rights reserved.

using UnrealBuildTool;

public class UEcineRender : ModuleRules
{
	public UEcineRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"EditorSubsystem",
			"MovieRenderPipelineCore",
			"UMG",
			"UEcineCore"
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
