using UnrealBuildTool;

public class RoscHostEditorTarget : TargetRules
{
	public RoscHostEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("RoscHost");
	}
}
