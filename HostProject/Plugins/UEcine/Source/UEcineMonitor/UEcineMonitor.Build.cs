using UnrealBuildTool;

public class UEcineMonitor : ModuleRules
{
	public UEcineMonitor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UEcineCore"
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
