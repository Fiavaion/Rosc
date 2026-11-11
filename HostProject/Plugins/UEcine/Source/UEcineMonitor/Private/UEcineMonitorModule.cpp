#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

/** Loads before the engine compiles its global shaders, so that the monitor shaders are among them. */
class FUEcineMonitorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString ShaderDirectory = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("UEcine"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/UEcine"), ShaderDirectory);
	}
};

IMPLEMENT_MODULE(FUEcineMonitorModule, UEcineMonitor);
