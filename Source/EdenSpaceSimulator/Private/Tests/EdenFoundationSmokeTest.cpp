// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenLogCategories.h"

#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFoundationSmokeTest,
	"Eden.Unit.Foundation.Smoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFoundationSmokeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const bool bModuleLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("EdenSpaceSimulator"));

	TestTrue(TEXT("EdenSpaceSimulator runtime module is loaded"), bModuleLoaded);
	UE_LOG(
		LogEden,
		Verbose,
		TEXT("Eden foundation smoke test completed. ModuleLoaded=%s"),
		bModuleLoaded ? TEXT("true") : TEXT("false"));

	return bModuleLoaded;
}

#endif
