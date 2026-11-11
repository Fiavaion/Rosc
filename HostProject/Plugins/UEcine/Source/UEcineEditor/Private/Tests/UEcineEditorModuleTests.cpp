#include "Framework/Docking/TabManager.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "UEcineEditorModule.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEcineEditorPanelRegisteredTest, "UEcine.Editor.PanelRegistered", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUEcineEditorPanelRegisteredTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("UEcineCore module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("UEcineCore")));
	TestTrue(TEXT("UEcineEditor module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("UEcineEditor")));
	TestTrue(TEXT("The UEcine panel has a registered tab spawner"), FGlobalTabmanager::Get()->HasTabSpawner(FUEcineEditorModule::PanelTabId));
	return true;
}

#endif
