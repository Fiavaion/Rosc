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
			"InputCore",
			"LevelEditor",
			"LevelSequence",
			"LevelSequenceEditor",
			"MovieScene",
			"MovieSceneTracks",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"UEcineRuntime"
		});
	}
}
