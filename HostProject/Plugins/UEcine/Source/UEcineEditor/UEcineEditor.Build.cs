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
			"CinematicCamera",
			"EditorSubsystem",
			"Engine",
			"InputCore",
			"LevelEditor",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"UEcineRuntime"
		});
	}
}
