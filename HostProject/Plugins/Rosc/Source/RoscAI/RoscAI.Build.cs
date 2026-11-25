// Copyright 2026 Fiavaion. All rights reserved.

using UnrealBuildTool;

public class RoscAI : ModuleRules
{
	public RoscAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"DeveloperSettings",
			"EditorSubsystem",
			"RoscCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Engine",
			"HTTP",
			"ImageCore",
			"Json",
			"NNE",
			"UnrealEd"
		});

		// SHA-256, to check a downloaded model file against the list.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

		// The tests stand a server up inside the editor for the helper's protocol and the assistant.
		PrivateDependencyModuleNames.Add("HTTPServer");
	}
}
