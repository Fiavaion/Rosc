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
			"EditorSubsystem",
			"Engine",
			"ImageCore",
			"InputCore",
			"LevelEditor",
			"LevelSequence",
			"LevelSequenceEditor",
			"MovieScene",
			"MovieSceneTools",
			"MovieSceneTracks",
			"SceneDepthPickerMode",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"WorkspaceMenuStructure",
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
