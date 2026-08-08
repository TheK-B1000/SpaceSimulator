// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorControlModel.h"
#include "Systems/EdenPowerModel.h"
#include "Systems/EdenThermalModel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenOperatorControlModelTests
{
FEdenOperatorControlConfig MakeValidConfig()
{
	FEdenOperatorControlConfig Config;
	Config.BoostDissipationDegreesCelsiusPerSecond = 1.0f;
	Config.EmergencyDissipationDegreesCelsiusPerSecond = 2.0f;
	Config.BoostCoolingDemandKilowatts = 1.5f;
	Config.EmergencyCoolingDemandKilowatts = 4.5f;
	Config.LoadShedDemandReductionKilowatts = 2.0f;
	Config.LoadShedDissipationReductionDegreesCelsiusPerSecond = 0.4f;
	Config.ReducedThrustAuthority = 0.5f;
	return Config;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOperatorControlConfigValidationAcceptsValidConfigTest,
	"Eden.Unit.Operations.OperatorControl.ConfigValidationAcceptsValidConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOperatorControlConfigValidationAcceptsValidConfigTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FString> Errors;
	TestTrue(TEXT("Valid config"), FEdenOperatorControlModel::ValidateConfig(EdenOperatorControlModelTests::MakeValidConfig(), &Errors));
	TestEqual(TEXT("No errors"), Errors.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOperatorControlResolveBoostAndShedTradeOffTest,
	"Eden.Unit.Operations.OperatorControl.ResolveBoostAndShedTradeOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOperatorControlResolveBoostAndShedTradeOffTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FEdenOperatorControlConfig Config = EdenOperatorControlModelTests::MakeValidConfig();
	FEdenOperatorIntent Intent;
	Intent.ThermalMode = EEdenThermalControlMode::Boost;
	Intent.LoadShedMode = EEdenLoadShedMode::Shed;
	Intent.PropulsionPriority = EEdenPropulsionPriorityMode::Reduced;

	const FEdenOperatorResolvedModifiers Modifiers = FEdenOperatorControlModel::ResolveIntent(Intent, Config);
	TestTrue(
		TEXT("Net demand = boost cooling - shed"),
		FMath::IsNearlyEqual(Modifiers.OperatorDemandKilowatts, Config.BoostCoolingDemandKilowatts - Config.LoadShedDemandReductionKilowatts));
	TestTrue(
		TEXT("Net dissipation = boost - shed loss"),
		FMath::IsNearlyEqual(
			Modifiers.OperatorDissipationDegreesCelsiusPerSecond,
			Config.BoostDissipationDegreesCelsiusPerSecond - Config.LoadShedDissipationReductionDegreesCelsiusPerSecond));
	TestTrue(TEXT("Thrust reduced"), FMath::IsNearlyEqual(Modifiers.ThrustAuthority, Config.ReducedThrustAuthority));
	TestFalse(TEXT("Stabilize assist unavailable when shed"), Modifiers.bStabilizationAssistAvailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOperatorModifierAdditivityWithMissionExternalTest,
	"Eden.Unit.Operations.OperatorControl.ModifierAdditivityWithMissionExternal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOperatorModifierAdditivityWithMissionExternalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenPowerConfig PowerConfig;
	PowerConfig.BatteryCapacityKilowattHours = 10.0f;
	PowerConfig.GenerationKilowatts = 5.0f;
	PowerConfig.BaselineDemandKilowatts = 1.0f;
	PowerConfig.InitialChargeFraction = 1.0f;
	PowerConfig.WarningThresholdFraction = 0.25f;
	PowerConfig.CriticalThresholdFraction = 0.1f;

	const FEdenPowerStateSnapshot PowerSnapshot =
		FEdenPowerModel::MakeSnapshot(PowerConfig, 10.0f, 5.0f, 1.0f, 3.0f, -0.5f);
	TestTrue(TEXT("Total demand clamps additively"), FMath::IsNearlyEqual(PowerSnapshot.TotalDemandKilowatts, 3.5f));

	FEdenThermalConfig ThermalConfig;
	ThermalConfig.AbsoluteMinTemperatureCelsius = -100.0f;
	ThermalConfig.AmbientTemperatureCelsius = 20.0f;
	ThermalConfig.WarningTemperatureCelsius = 70.0f;
	ThermalConfig.CriticalTemperatureCelsius = 100.0f;
	ThermalConfig.AbsoluteMaxTemperatureCelsius = 120.0f;
	ThermalConfig.InitialTemperatureCelsius = 20.0f;
	ThermalConfig.HeatGenerationDegreesCelsiusPerSecond = 1.0f;
	ThermalConfig.DissipationDegreesCelsiusPerSecond = 1.0f;

	const FEdenThermalStateSnapshot ThermalSnapshot =
		FEdenThermalModel::MakeSnapshot(ThermalConfig, 30.0f, 1.0f, 1.0f, 2.0f, -0.25f);
	TestTrue(
		TEXT("Effective dissipation = baseline + operator"),
		FMath::IsNearlyEqual(ThermalSnapshot.EffectiveDissipationDegreesCelsiusPerSecond, 0.75f));
	return true;
}

#endif
