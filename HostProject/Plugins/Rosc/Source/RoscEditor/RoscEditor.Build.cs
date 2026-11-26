// Copyright 2026 Fiavaion
// SPDX-License-Identifier: Apache-2.0

using UnrealBuildTool;

public class RoscEditor : ModuleRules
{
	public RoscEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"DeveloperSettings",
			"RoscCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools",
			"CinematicCamera",
			"DesktopPlatform",
			"EditorWidgets",
			"EditorSubsystem",
			"Engine",
			"ImageCore",
			"InputCore",
			"Json",
			"LevelEditor",
			"LevelSequence",
			"LevelSequenceEditor",
			"MovieScene",
			"MovieSceneTools",
			"MovieSceneTracks",
			"Projects",
			"SceneDepthPickerMode",
			"SequencerScriptingEditor",
			"Settings",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"RoscAI",
			"RoscMonitor",
			"RoscRender",
			"RoscRuntime"
		});

		// The panel picture tests draw the panel off screen.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"UMG"
		});
	}
}
