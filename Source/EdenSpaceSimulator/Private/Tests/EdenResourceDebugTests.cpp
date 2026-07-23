// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenResourceIntegrationTestTypes.h"
#include "EdenSimClockTestSubscriber.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"

#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenResourceDebugTests
{
constexpr double Tolerance = 0.001;

bool TestFloatNearlyEqual(FAutomationTestBase& Test, const TCHAR* What, float Actual, float Expected)
{
	return Test.TestTrue(
		FString::Printf(TEXT("%s. Actual=%f Expected=%f"), What, Actual, Expected),
		FMath::IsNearlyEqual(Actual, Expected, Tolerance));
}

FEdenFuelConfig MakeFuelConfig()
{
	FEdenFuelConfig Config;
	Config.CapacityKilograms = 200.0f;
	Config.ConsumptionRateKilogramsPerSecond = 20.0f;
	Config.InitialFuelFraction = 0.5f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	return Config;
}

FEdenPowerConfig MakePowerConfig()
{
	FEdenPowerConfig Config;
	Config.BatteryCapacityKilowattHours = 4.0f;
	Config.GenerationKilowatts = 3.0f;
	Config.BaselineDemandKilowatts = 1.5f;
	Config.InitialChargeFraction = 0.5f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	return Config;
}

FEdenThermalConfig MakeThermalConfig()
{
	FEdenThermalConfig Config;
	Config.AbsoluteMinTemperatureCelsius = -100.0f;
	Config.AmbientTemperatureCelsius = 22.0f;
	Config.WarningTemperatureCelsius = 70.0f;
	Config.CriticalTemperatureCelsius = 100.0f;
	Config.AbsoluteMaxTemperatureCelsius = 120.0f;
	Config.InitialTemperatureCelsius = 30.0f;
	Config.HeatGenerationDegreesCelsiusPerSecond = 2.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.25f;
	return Config;
}

template <typename TComponent>
TComponent* AddComponent(AActor* Actor)
{
	TComponent* Component = NewObject<TComponent>(Actor);
	Actor->AddInstanceComponent(Component);
	return Component;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceDebugClockSnapshotReportsStateTest,
	"Eden.Unit.Systems.Debug.ClockSnapshotReportsState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceDebugClockSnapshotReportsStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = NewObject<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.2f);
	Clock->SetMaxCatchUpSteps(2);
	Clock->ResetSimulationClock();

	UEdenSimClockTestSubscriber* Subscriber = NewObject<UEdenSimClockTestSubscriber>();
	TestTrue(TEXT("Subscriber registers"), Clock->RegisterSimulationTickable(Subscriber));

	Clock->Tick(0.6f);
	Clock->PauseSimulation();

	const FEdenSimulationClockDebugSnapshot Snapshot = Clock->GetSimulationClockDebugSnapshot();
	TestTrue(TEXT("Clock snapshot reports availability"), Snapshot.bClockAvailable);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Clock snapshot reports elapsed fixed simulation time"),
		Snapshot.ElapsedSimulationTimeSeconds,
		0.4f);
	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Clock snapshot reports fixed step"), Snapshot.FixedStepSeconds, 0.2f);
	TestTrue(TEXT("Clock snapshot reports paused state"), Snapshot.bPaused);
	TestEqual(TEXT("Clock snapshot reports subscriber count"), Snapshot.SubscriberCount, 1);
	TestEqual(TEXT("Clock snapshot reports dropped overrun steps"), Snapshot.LastDroppedSteps, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceDebugResourceSnapshotsReportValuesTest,
	"Eden.Unit.Systems.Debug.ResourceSnapshotsReportValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceDebugResourceSnapshotsReportValuesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	AActor* Actor = NewObject<AActor>();
	UEdenFuelSystemComponent* Fuel = EdenResourceDebugTests::AddComponent<UEdenFuelSystemComponent>(Actor);
	UEdenPowerSystemComponent* Power = EdenResourceDebugTests::AddComponent<UEdenPowerSystemComponent>(Actor);
	UEdenThermalSystemComponent* Thermal = EdenResourceDebugTests::AddComponent<UEdenThermalSystemComponent>(Actor);
	UEdenResourceIntegrationTestDemandComponent* Demand =
		EdenResourceDebugTests::AddComponent<UEdenResourceIntegrationTestDemandComponent>(Actor);
	Demand->DemandNormalized = 0.75f;

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceDebugTests::MakeFuelConfig()));
	TestTrue(TEXT("Fuel discovers demand source"), Fuel->RefreshPropulsionDemandSource());
	TestTrue(TEXT("Power initializes"), Power->InitializePowerSimulation(EdenResourceDebugTests::MakePowerConfig()));
	TestTrue(TEXT("Thermal initializes"), Thermal->InitializeThermalSimulation(EdenResourceDebugTests::MakeThermalConfig()));

	const FEdenFuelDebugSnapshot FuelDebug = Fuel->GetFuelDebugSnapshot();
	TestTrue(TEXT("Fuel debug reports component availability"), FuelDebug.bComponentAvailable);
	TestTrue(TEXT("Fuel debug reports valid configuration"), FuelDebug.bConfigurationValid);
	TestFalse(TEXT("Fuel debug reports unregistered clock"), FuelDebug.bRegisteredWithClock);
	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Fuel debug reports current fuel"), FuelDebug.FuelQuantityKilograms, 100.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Fuel debug reports capacity"), FuelDebug.CapacityKilograms, 200.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Fuel debug reports percent"), FuelDebug.FuelPercent, 50.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Fuel debug reports current demand"), FuelDebug.PropulsionDemandNormalized, 0.75f);
	TestEqual(TEXT("Fuel debug reports state"), FuelDebug.FuelState, EEdenFuelState::Normal);

	const FEdenPowerDebugSnapshot PowerDebug = Power->GetPowerDebugSnapshot();
	TestTrue(TEXT("Power debug reports component availability"), PowerDebug.bComponentAvailable);
	TestTrue(TEXT("Power debug reports valid configuration"), PowerDebug.bConfigurationValid);
	TestFalse(TEXT("Power debug reports unregistered clock"), PowerDebug.bRegisteredWithClock);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Power debug reports battery charge"),
		PowerDebug.BatteryChargeKilowattHours,
		2.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Power debug reports battery capacity"),
		PowerDebug.BatteryCapacityKilowattHours,
		4.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Power debug reports generation"), PowerDebug.GenerationKilowatts, 3.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Power debug reports demand"), PowerDebug.DemandKilowatts, 1.5f);
	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Power debug reports net"), PowerDebug.NetPowerKilowatts, 1.5f);
	TestEqual(TEXT("Power debug reports state"), PowerDebug.PowerState, EEdenPowerState::Normal);

	const FEdenThermalDebugSnapshot ThermalDebug = Thermal->GetThermalDebugSnapshot();
	TestTrue(TEXT("Thermal debug reports component availability"), ThermalDebug.bComponentAvailable);
	TestTrue(TEXT("Thermal debug reports valid configuration"), ThermalDebug.bConfigurationValid);
	TestFalse(TEXT("Thermal debug reports unregistered clock"), ThermalDebug.bRegisteredWithClock);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Thermal debug reports current temperature"),
		ThermalDebug.TemperatureCelsius,
		30.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Thermal debug reports ambient temperature"),
		ThermalDebug.AmbientTemperatureCelsius,
		22.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Thermal debug reports heat generation"),
		ThermalDebug.HeatGenerationDegreesCelsiusPerSecond,
		2.0f);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Thermal debug reports dissipation"),
		ThermalDebug.DissipationDegreesCelsiusPerSecond,
		0.25f);
	TestEqual(TEXT("Thermal debug reports state"), ThermalDebug.ThermalState, EEdenThermalState::Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceDebugSnapshotsAreReadOnlyTest,
	"Eden.Unit.Systems.Debug.SnapshotsAreReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceDebugSnapshotsAreReadOnlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	AActor* Actor = NewObject<AActor>();
	UEdenFuelSystemComponent* Fuel = EdenResourceDebugTests::AddComponent<UEdenFuelSystemComponent>(Actor);
	UEdenResourceIntegrationTestDemandComponent* Demand =
		EdenResourceDebugTests::AddComponent<UEdenResourceIntegrationTestDemandComponent>(Actor);
	Demand->DemandNormalized = 0.8f;

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceDebugTests::MakeFuelConfig()));
	TestTrue(TEXT("Fuel discovers demand source"), Fuel->RefreshPropulsionDemandSource());

	const FEdenFuelStateSnapshot BeforeFuel = Fuel->GetFuelStateSnapshot();
	const float BeforeCachedDemand = Fuel->GetConsumptionDemandNormalized();
	const FEdenFuelDebugSnapshot FuelDebug = Fuel->GetFuelDebugSnapshot();
	const FEdenFuelStateSnapshot AfterFuel = Fuel->GetFuelStateSnapshot();

	EdenResourceDebugTests::TestFloatNearlyEqual(*this, TEXT("Debug reports live demand"), FuelDebug.PropulsionDemandNormalized, 0.8f);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Debug query does not mutate cached demand"),
		Fuel->GetConsumptionDemandNormalized(),
		BeforeCachedDemand);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Debug query does not mutate fuel quantity"),
		AfterFuel.FuelQuantityKilograms,
		BeforeFuel.FuelQuantityKilograms);
	TestEqual(TEXT("Debug query does not mutate fuel state"), AfterFuel.FuelState, BeforeFuel.FuelState);

	AEdenSpacecraftPawn* Pawn = NewObject<AEdenSpacecraftPawn>();
	TestTrue(
		TEXT("Pawn fuel initializes"),
		Pawn->GetFuelSystemComponent()->InitializeFuelSimulation(EdenResourceDebugTests::MakeFuelConfig()));
	TestTrue(
		TEXT("Pawn power initializes"),
		Pawn->GetPowerSystemComponent()->InitializePowerSimulation(EdenResourceDebugTests::MakePowerConfig()));
	TestTrue(
		TEXT("Pawn thermal initializes"),
		Pawn->GetThermalSystemComponent()->InitializeThermalSimulation(EdenResourceDebugTests::MakeThermalConfig()));

	const FEdenSpacecraftSystemsDebugSnapshot PawnDebug = Pawn->GetEdenSystemsDebugSnapshot();
	TestFalse(TEXT("Pawn debug snapshot reports no clock outside a world"), PawnDebug.Clock.bClockAvailable);
	TestTrue(TEXT("Pawn debug snapshot includes fuel component"), PawnDebug.Fuel.bComponentAvailable);
	TestTrue(TEXT("Pawn debug snapshot includes power component"), PawnDebug.Power.bComponentAvailable);
	TestTrue(TEXT("Pawn debug snapshot includes thermal component"), PawnDebug.Thermal.bComponentAvailable);
	EdenResourceDebugTests::TestFloatNearlyEqual(
		*this,
		TEXT("Pawn debug snapshot reports configured fuel"),
		PawnDebug.Fuel.FuelQuantityKilograms,
		100.0f);

	return true;
}

#endif
