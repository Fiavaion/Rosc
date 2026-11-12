using UnrealBuildTool;

public class UEcineEditor : ModuleRules
{
	public UEcineEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"DeveloperSettings",
			"UEcineCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetTools",
			"CinematicCamera",
			"DesktopPlatform",
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
			"SceneDepthPickerMode",
			"SequencerScriptingEditor",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"UEcineAI",
			"UEcineMonitor",
			"UEcineRender",
			"UEcineRuntime"
		});

		// The panel picture tests draw the panel off screen.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"UMG"
		});
	}
}
