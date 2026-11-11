using UnrealBuildTool;

public class UEcineHostTarget : TargetRules
{
	public UEcineHostTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("UEcineHost");
	}
}
