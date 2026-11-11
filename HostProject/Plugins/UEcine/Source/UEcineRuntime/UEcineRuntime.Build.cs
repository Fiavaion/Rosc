using UnrealBuildTool;

public class UEcineRuntime : ModuleRules
{
	public UEcineRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "CinematicCamera", "UEcineCore" });
	}
}
