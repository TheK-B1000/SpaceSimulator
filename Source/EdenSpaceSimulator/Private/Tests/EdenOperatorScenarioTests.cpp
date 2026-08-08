// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorControlModel.h"
#include "Systems/EdenPowerModel.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalModel.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Flight/EdenFlightMovementComponent.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenOperatorScenarioTests
{
FEdenOperatorControlConfig MakeOperatorConfig()
{
	FEdenOperatorControlConfig Config;
	Config.BoostDissipationDegreesCelsiusPerSecond = 2.5f;
	Config.EmergencyDissipationDegreesCelsiusPerSecond = 5.0f;
	Config.BoostCoolingDemandKilowatts = 2.0f;
	Config.EmergencyCoolingDemandKilowatts = 6.0f;
	Config.LoadShedDemandReductionKilowatts = 3.0f;
	Config.LoadShedDissipationReductionDegreesCelsiusPerSecond = 0.5f;
	Config.ReducedThrustAuthority = 0.5f;
	return Config;
}

FEdenPowerConfig MakePowerConfig()
{
	FEdenPowerConfig PowerConfig;
	PowerConfig.BatteryCapacityKilowattHours = 20.0f;
	PowerConfig.GenerationKilowatts = 2.0f;
	PowerConfig.BaselineDemandKilowatts = 1.0f;
	PowerConfig.InitialChargeFraction = 1.0f;
	PowerConfig.WarningThresholdFraction = 0.25f;
	PowerConfig.CriticalThresholdFraction = 0.1f;
	return PowerConfig;
}

FEdenThermalConfig MakeThermalConfig()
{
	FEdenThermalConfig ThermalConfig;
	ThermalConfig.AbsoluteMinTemperatureCelsius = -100.0f;
	ThermalConfig.AmbientTemperatureCelsius = 20.0f;
	ThermalConfig.WarningTemperatureCelsius = 70.0f;
	ThermalConfig.CriticalTemperatureCelsius = 100.0f;
	ThermalConfig.AbsoluteMaxTemperatureCelsius = 120.0f;
	ThermalConfig.InitialTemperatureCelsius = 20.0f;
	ThermalConfig.HeatGenerationDegreesCelsiusPerSecond = 1.0f;
	ThermalConfig.DissipationDegreesCelsiusPerSecond = 1.0f;
	return ThermalConfig;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOperatorModifiersCoexistWithMissionExternalTest,
	"Eden.Integration.Operations.OperatorControl.ModifiersCoexistWithMissionExternal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOperatorModifiersCoexistWithMissionExternalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>();
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>();
	UEdenFlightMovementComponent* Movement = NewObject<UEdenFlightMovementComponent>();

	TestTrue(TEXT("Init power"), Power->InitializePowerSimulation(EdenOperatorScenarioTests::MakePowerConfig()));
	TestTrue(TEXT("Init thermal"), Thermal->InitializeThermalSimulation(EdenOperatorScenarioTests::MakeThermalConfig()));

	const FEdenOperatorControlConfig Config = EdenOperatorScenarioTests::MakeOperatorConfig();
	FEdenOperatorIntent Intent;
	Intent.ThermalMode = EEdenThermalControlMode::Emergency;
	const FEdenOperatorResolvedModifiers Modifiers = FEdenOperatorControlModel::ResolveIntent(Intent, Config);

	TestTrue(TEXT("Set mission external demand"), Power->SetExternalDemandKilowatts(5.0f));
	TestTrue(TEXT("Set operator demand"), Power->SetOperatorDemandKilowatts(Modifiers.OperatorDemandKilowatts));
	TestTrue(TEXT("Set mission external heating"), Thermal->SetExternalHeatingRateDegreesCelsiusPerSecond(2.5f));
	TestTrue(
		TEXT("Set operator dissipation"),
		Thermal->SetOperatorDissipationDegreesCelsiusPerSecond(Modifiers.OperatorDissipationDegreesCelsiusPerSecond));
	TestTrue(TEXT("Set thrust authority"), Movement->SetThrustAuthority(0.5f));
	Movement->SetStabilizationAssistAvailable(false);

	const FEdenPowerStateSnapshot PowerSnapshot = Power->GetPowerStateSnapshot();
	TestTrue(TEXT("External demand preserved"), FMath::IsNearlyEqual(PowerSnapshot.ExternalDemandKilowatts, 5.0f));
	TestTrue(TEXT("Operator demand applied"), FMath::IsNearlyEqual(PowerSnapshot.OperatorDemandKilowatts, 6.0f));
	TestTrue(TEXT("Total demand includes both"), FMath::IsNearlyEqual(PowerSnapshot.TotalDemandKilowatts, 12.0f));

	const FEdenThermalStateSnapshot ThermalSnapshot = Thermal->GetThermalStateSnapshot();
	TestTrue(TEXT("External heating preserved"), FMath::IsNearlyEqual(ThermalSnapshot.ExternalHeatingRateDegreesCelsiusPerSecond, 2.5f));
	TestTrue(TEXT("Operator dissipation applied"), FMath::IsNearlyEqual(ThermalSnapshot.OperatorDissipationDegreesCelsiusPerSecond, 5.0f));

	TestTrue(TEXT("Clear external leaves operator"), Power->ClearExternalDemand());
	TestTrue(TEXT("Operator still present"), FMath::IsNearlyEqual(Power->GetPowerStateSnapshot().OperatorDemandKilowatts, 6.0f));

	TestTrue(TEXT("Clear operator leaves external zero"), Power->ClearOperatorDemand());
	TestTrue(TEXT("Operator cleared"), FMath::IsNearlyEqual(Power->GetPowerStateSnapshot().OperatorDemandKilowatts, 0.0f));
	TestTrue(TEXT("Thrust authority reduced"), FMath::IsNearlyEqual(Movement->GetThrustAuthority(), 0.5f));
	TestFalse(TEXT("Stabilize assist unavailable"), Movement->IsStabilizationAssistAvailable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOperatorPassiveVsEmergencyDemandDiffersTest,
	"Eden.Scenario.Operations.OperatorControl.PassiveVsEmergencyDemandDiffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOperatorPassiveVsEmergencyDemandDiffersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenOperatorControlConfig Config = EdenOperatorScenarioTests::MakeOperatorConfig();
	FEdenOperatorIntent Passive;
	FEdenOperatorIntent Emergency;
	Emergency.ThermalMode = EEdenThermalControlMode::Emergency;

	const FEdenOperatorResolvedModifiers PassiveModifiers = FEdenOperatorControlModel::ResolveIntent(Passive, Config);
	const FEdenOperatorResolvedModifiers EmergencyModifiers = FEdenOperatorControlModel::ResolveIntent(Emergency, Config);

	TestTrue(TEXT("Passive demand near zero"), FMath::IsNearlyZero(PassiveModifiers.OperatorDemandKilowatts));
	TestTrue(
		TEXT("Emergency demand higher"),
		EmergencyModifiers.OperatorDemandKilowatts > PassiveModifiers.OperatorDemandKilowatts + KINDA_SMALL_NUMBER);
	TestTrue(
		TEXT("Emergency dissipation higher"),
		EmergencyModifiers.OperatorDissipationDegreesCelsiusPerSecond
			> PassiveModifiers.OperatorDissipationDegreesCelsiusPerSecond + KINDA_SMALL_NUMBER);
	return true;
}

#endif
