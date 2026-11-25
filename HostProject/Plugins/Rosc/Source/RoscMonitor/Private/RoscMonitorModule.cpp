// Copyright 2026 Fiavaion. All rights reserved.

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

/** Loads before the engine compiles its global shaders, so that the monitor shaders are among them. */
class FRoscMonitorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString ShaderDirectory = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Rosc"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/Rosc"), ShaderDirectory);
	}
};

IMPLEMENT_MODULE(FRoscMonitorModule, RoscMonitor);
