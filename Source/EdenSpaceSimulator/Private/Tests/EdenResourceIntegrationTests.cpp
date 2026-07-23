// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenResourceIntegrationTestTypes.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"

#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenResourceIntegrationTests
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
	Config.CapacityKilograms = 100.0f;
	Config.ConsumptionRateKilogramsPerSecond = 10.0f;
	Config.InitialFuelFraction = 1.0f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	return Config;
}

FEdenPowerConfig MakePowerConfig()
{
	FEdenPowerConfig Config;
	Config.BatteryCapacityKilowattHours = 1.0f;
	Config.GenerationKilowatts = 0.0f;
	Config.BaselineDemandKilowatts = 36.0f;
	Config.InitialChargeFraction = 1.0f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	return Config;
}

FEdenThermalConfig MakeThermalConfig()
{
	FEdenThermalConfig Config;
	Config.AbsoluteMinTemperatureCelsius = -100.0f;
	Config.AmbientTemperatureCelsius = 20.0f;
	Config.WarningTemperatureCelsius = 70.0f;
	Config.CriticalTemperatureCelsius = 100.0f;
	Config.AbsoluteMaxTemperatureCelsius = 120.0f;
	Config.InitialTemperatureCelsius = 20.0f;
	Config.HeatGenerationDegreesCelsiusPerSecond = 10.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.0f;
	return Config;
}

UEdenSimulationClockSubsystem* MakeClock(float FixedStepSeconds = 0.1f, int32 MaxCatchUpSteps = 20)
{
	UEdenSimulationClockSubsystem* Clock = NewObject<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(FixedStepSeconds);
	Clock->SetMaxCatchUpSteps(MaxCatchUpSteps);
	Clock->ResetSimulationClock();
	return Clock;
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
	FEdenResourceIntegrationPawnCreatesResourceDefaultSubobjectsTest,
	"Eden.Integration.Systems.PawnCreatesResourceDefaultSubobjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationPawnCreatesResourceDefaultSubobjectsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const AEdenSpacecraftPawn* Pawn = NewObject<AEdenSpacecraftPawn>();

	TestNotNull(TEXT("Pawn creates fuel system default subobject"), Pawn->GetFuelSystemComponent());
	TestNotNull(TEXT("Pawn creates power system default subobject"), Pawn->GetPowerSystemComponent());
	TestNotNull(TEXT("Pawn creates thermal system default subobject"), Pawn->GetThermalSystemComponent());
	TestNotNull(TEXT("Pawn keeps flight movement component"), Pawn->GetFlightMovementComponent());
	TestFalse(TEXT("Pawn does not use actor tick for resource simulation"), Pawn->PrimaryActorTick.bCanEverTick);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationClockAdvancesAllResourcesTest,
	"Eden.Integration.Systems.ClockAdvancesAllResources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationClockAdvancesAllResourcesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	AActor* Actor = NewObject<AActor>();
	UEdenFuelSystemComponent* Fuel = EdenResourceIntegrationTests::AddComponent<UEdenFuelSystemComponent>(Actor);
	UEdenPowerSystemComponent* Power = EdenResourceIntegrationTests::AddComponent<UEdenPowerSystemComponent>(Actor);
	UEdenThermalSystemComponent* Thermal = EdenResourceIntegrationTests::AddComponent<UEdenThermalSystemComponent>(Actor);
	UEdenResourceIntegrationTestDemandComponent* Demand =
		EdenResourceIntegrationTests::AddComponent<UEdenResourceIntegrationTestDemandComponent>(Actor);
	Demand->DemandNormalized = 1.0f;

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceIntegrationTests::MakeFuelConfig()));
	TestTrue(TEXT("Fuel discovers one demand source"), Fuel->RefreshPropulsionDemandSource());
	TestTrue(TEXT("Power initializes"), Power->InitializePowerSimulation(EdenResourceIntegrationTests::MakePowerConfig()));
	TestTrue(TEXT("Thermal initializes"), Thermal->InitializeThermalSimulation(EdenResourceIntegrationTests::MakeThermalConfig()));

	UEdenSimulationClockSubsystem* Clock = EdenResourceIntegrationTests::MakeClock();
	TestTrue(TEXT("Fuel registers with clock"), Clock->RegisterSimulationTickable(Fuel));
	TestTrue(TEXT("Power registers with clock"), Clock->RegisterSimulationTickable(Power));
	TestTrue(TEXT("Thermal registers with clock"), Clock->RegisterSimulationTickable(Thermal));

	Clock->Tick(0.1f);

	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Clock-driven demand consumes fuel"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		99.0f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Clock-driven power converts kW over fixed seconds to kWh"),
		Power->GetPowerStateSnapshot().BatteryChargeKilowattHours,
		0.999f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Clock-driven thermal heat raises temperature"),
		Thermal->GetThermalStateSnapshot().TemperatureCelsius,
		21.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationSustainedDemandConsumesFuelTest,
	"Eden.Integration.Systems.SustainedPropulsionDemandConsumesFuel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationSustainedDemandConsumesFuelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	AActor* Actor = NewObject<AActor>();
	UEdenFuelSystemComponent* Fuel = EdenResourceIntegrationTests::AddComponent<UEdenFuelSystemComponent>(Actor);
	UEdenResourceIntegrationTestDemandComponent* Demand =
		EdenResourceIntegrationTests::AddComponent<UEdenResourceIntegrationTestDemandComponent>(Actor);
	Demand->DemandNormalized = 0.5f;

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceIntegrationTests::MakeFuelConfig()));
	TestTrue(TEXT("Fuel discovers one demand source"), Fuel->RefreshPropulsionDemandSource());

	UEdenSimulationClockSubsystem* Clock = EdenResourceIntegrationTests::MakeClock();
	TestTrue(TEXT("Fuel registers with clock"), Clock->RegisterSimulationTickable(Fuel));
	Clock->Tick(0.5f);

	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Sustained half demand consumes rate * demand * fixed simulation time"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		97.5f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Fuel caches normalized demand from source"),
		Fuel->GetConsumptionDemandNormalized(),
		0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationZeroDemandDoesNotConsumeFuelTest,
	"Eden.Integration.Systems.ZeroPropulsionDemandDoesNotConsumeFuel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationZeroDemandDoesNotConsumeFuelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	AActor* Actor = NewObject<AActor>();
	UEdenFuelSystemComponent* Fuel = EdenResourceIntegrationTests::AddComponent<UEdenFuelSystemComponent>(Actor);
	UEdenResourceIntegrationTestDemandComponent* Demand =
		EdenResourceIntegrationTests::AddComponent<UEdenResourceIntegrationTestDemandComponent>(Actor);
	Demand->DemandNormalized = 0.0f;

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceIntegrationTests::MakeFuelConfig()));
	TestTrue(TEXT("Fuel discovers one demand source"), Fuel->RefreshPropulsionDemandSource());

	UEdenSimulationClockSubsystem* Clock = EdenResourceIntegrationTests::MakeClock();
	TestTrue(TEXT("Fuel registers with clock"), Clock->RegisterSimulationTickable(Fuel));
	Clock->Tick(1.0f);

	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Zero propulsion demand consumes no fuel"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		100.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationMissingDemandSourceUsesZeroTest,
	"Eden.Integration.Systems.MissingPropulsionDemandSourceUsesZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationMissingDemandSourceUsesZeroTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	AActor* Actor = NewObject<AActor>();
	UEdenFuelSystemComponent* Fuel = EdenResourceIntegrationTests::AddComponent<UEdenFuelSystemComponent>(Actor);

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceIntegrationTests::MakeFuelConfig()));
	TestFalse(TEXT("Missing demand source is reported"), Fuel->RefreshPropulsionDemandSource());
	TestTrue(TEXT("Explicit demand is accepted before runtime source resolution"), Fuel->SetConsumptionDemandNormalized(1.0f));

	Fuel->AdvanceSimulation(1.0f);

	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Missing source forces zero runtime demand"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		100.0f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Missing source clears cached demand"),
		Fuel->GetConsumptionDemandNormalized(),
		0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationMultipleDemandSourcesAreAmbiguousTest,
	"Eden.Integration.Systems.MultiplePropulsionDemandSourcesAreAmbiguous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationMultipleDemandSourcesAreAmbiguousTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	AActor* Actor = NewObject<AActor>();
	UEdenFuelSystemComponent* Fuel = EdenResourceIntegrationTests::AddComponent<UEdenFuelSystemComponent>(Actor);
	UEdenResourceIntegrationTestDemandComponent* DemandA =
		EdenResourceIntegrationTests::AddComponent<UEdenResourceIntegrationTestDemandComponent>(Actor);
	UEdenResourceIntegrationTestDemandComponent* DemandB =
		EdenResourceIntegrationTests::AddComponent<UEdenResourceIntegrationTestDemandComponent>(Actor);
	DemandA->DemandNormalized = 1.0f;
	DemandB->DemandNormalized = 1.0f;

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceIntegrationTests::MakeFuelConfig()));
	AddExpectedError(TEXT("multiple propulsion demand sources"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Multiple demand sources are ambiguous"), Fuel->RefreshPropulsionDemandSource());
	TestTrue(TEXT("Explicit demand does not override runtime ambiguity"), Fuel->SetConsumptionDemandNormalized(1.0f));

	Fuel->AdvanceSimulation(1.0f);

	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Ambiguous sources force zero runtime demand"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		100.0f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Ambiguous sources clear cached demand"),
		Fuel->GetConsumptionDemandNormalized(),
		0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationResourceResetRestoresInitialValuesTest,
	"Eden.Integration.Systems.ResourceResetRestoresInitialValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationResourceResetRestoresInitialValuesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>();
	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>();
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>();

	FEdenFuelConfig FuelConfig = EdenResourceIntegrationTests::MakeFuelConfig();
	FuelConfig.InitialFuelFraction = 0.6f;
	FEdenPowerConfig PowerConfig = EdenResourceIntegrationTests::MakePowerConfig();
	PowerConfig.InitialChargeFraction = 0.5f;
	FEdenThermalConfig ThermalConfig = EdenResourceIntegrationTests::MakeThermalConfig();
	ThermalConfig.InitialTemperatureCelsius = 30.0f;

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(FuelConfig));
	TestTrue(TEXT("Power initializes"), Power->InitializePowerSimulation(PowerConfig));
	TestTrue(TEXT("Thermal initializes"), Thermal->InitializeThermalSimulation(ThermalConfig));

	Fuel->SetFuelQuantityKilograms(10.0f);
	Power->SetBatteryChargeKilowattHours(0.1f);
	Thermal->SetTemperatureCelsius(90.0f);

	TestTrue(TEXT("Fuel reset succeeds"), Fuel->ResetFuelState());
	TestTrue(TEXT("Power reset succeeds"), Power->ResetPowerState());
	TestTrue(TEXT("Thermal reset succeeds"), Thermal->ResetThermalState());

	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Fuel reset restores configured initial quantity"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		60.0f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Power reset restores configured initial charge"),
		Power->GetPowerStateSnapshot().BatteryChargeKilowattHours,
		0.5f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Thermal reset restores configured initial temperature"),
		Thermal->GetThermalStateSnapshot().TemperatureCelsius,
		30.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationRegistrationAndUnregistrationAreSafeTest,
	"Eden.Integration.Systems.RegistrationAndUnregistrationAreSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationRegistrationAndUnregistrationAreSafeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>();
	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>();
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>();
	UEdenSimulationClockSubsystem* Clock = EdenResourceIntegrationTests::MakeClock();

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceIntegrationTests::MakeFuelConfig()));
	TestTrue(TEXT("Power initializes"), Power->InitializePowerSimulation(EdenResourceIntegrationTests::MakePowerConfig()));
	TestTrue(TEXT("Thermal initializes"), Thermal->InitializeThermalSimulation(EdenResourceIntegrationTests::MakeThermalConfig()));

	TestTrue(TEXT("Fuel registers"), Clock->RegisterSimulationTickable(Fuel));
	TestTrue(TEXT("Power registers"), Clock->RegisterSimulationTickable(Power));
	TestTrue(TEXT("Thermal registers"), Clock->RegisterSimulationTickable(Thermal));
	TestFalse(TEXT("Duplicate fuel registration is rejected"), Clock->RegisterSimulationTickable(Fuel));
	TestEqual(TEXT("Three unique subscribers retained"), Clock->GetSubscriberCount(), 3);

	TestTrue(TEXT("Fuel unregisters"), Clock->UnregisterSimulationTickable(Fuel));
	TestTrue(TEXT("Power unregisters"), Clock->UnregisterSimulationTickable(Power));
	TestTrue(TEXT("Thermal unregisters"), Clock->UnregisterSimulationTickable(Thermal));
	TestFalse(TEXT("Duplicate fuel unregistration is safe"), Clock->UnregisterSimulationTickable(Fuel));
	TestEqual(TEXT("No subscribers retained after unregistration"), Clock->GetSubscriberCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationMissingConfigAndClockDoNotCrashTest,
	"Eden.Integration.Systems.MissingConfigOrClockDoesNotCrash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationMissingConfigAndClockDoNotCrashTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenFuelSystemComponent* MissingFuelConfig = NewObject<UEdenFuelSystemComponent>();
	UEdenPowerSystemComponent* MissingPowerConfig = NewObject<UEdenPowerSystemComponent>();
	UEdenThermalSystemComponent* MissingThermalConfig = NewObject<UEdenThermalSystemComponent>();

	TestFalse(TEXT("Missing fuel config reset fails safely"), MissingFuelConfig->ResetFuelState());
	TestFalse(TEXT("Missing power config reset fails safely"), MissingPowerConfig->ResetPowerState());
	TestFalse(TEXT("Missing thermal config reset fails safely"), MissingThermalConfig->ResetThermalState());
	MissingFuelConfig->AdvanceSimulation(0.1f);
	MissingPowerConfig->AdvanceSimulation(0.1f);
	MissingThermalConfig->AdvanceSimulation(0.1f);
	TestFalse(TEXT("Fuel remains inert without config"), MissingFuelConfig->IsFuelSimulationEnabled());
	TestFalse(TEXT("Power remains inert without config"), MissingPowerConfig->IsPowerSimulationEnabled());
	TestFalse(TEXT("Thermal remains inert without config"), MissingThermalConfig->IsThermalSimulationEnabled());

	UEdenFuelSystemComponent* FuelWithoutClock = NewObject<UEdenFuelSystemComponent>();
	UEdenPowerSystemComponent* PowerWithoutClock = NewObject<UEdenPowerSystemComponent>();
	UEdenThermalSystemComponent* ThermalWithoutClock = NewObject<UEdenThermalSystemComponent>();

	TestTrue(TEXT("Fuel initializes without world"), FuelWithoutClock->InitializeFuelSimulation(EdenResourceIntegrationTests::MakeFuelConfig()));
	TestTrue(TEXT("Power initializes without world"), PowerWithoutClock->InitializePowerSimulation(EdenResourceIntegrationTests::MakePowerConfig()));
	TestTrue(TEXT("Thermal initializes without world"), ThermalWithoutClock->InitializeThermalSimulation(EdenResourceIntegrationTests::MakeThermalConfig()));

	TestFalse(TEXT("Fuel missing clock registration fails safely"), FuelWithoutClock->RegisterWithSimulationClock());
	TestFalse(TEXT("Power missing clock registration fails safely"), PowerWithoutClock->RegisterWithSimulationClock());
	TestFalse(TEXT("Thermal missing clock registration fails safely"), ThermalWithoutClock->RegisterWithSimulationClock());
	TestFalse(TEXT("Fuel disables when clock is missing"), FuelWithoutClock->IsFuelSimulationEnabled());
	TestFalse(TEXT("Power disables when clock is missing"), PowerWithoutClock->IsPowerSimulationEnabled());
	TestFalse(TEXT("Thermal disables when clock is missing"), ThermalWithoutClock->IsThermalSimulationEnabled());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenResourceIntegrationSimulatedRestartResetsClockAndResourcesTest,
	"Eden.Integration.Systems.SimulatedRestartResetsClockAndResources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenResourceIntegrationSimulatedRestartResetsClockAndResourcesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	AActor* Actor = NewObject<AActor>();
	UEdenFuelSystemComponent* Fuel = EdenResourceIntegrationTests::AddComponent<UEdenFuelSystemComponent>(Actor);
	UEdenPowerSystemComponent* Power = EdenResourceIntegrationTests::AddComponent<UEdenPowerSystemComponent>(Actor);
	UEdenThermalSystemComponent* Thermal = EdenResourceIntegrationTests::AddComponent<UEdenThermalSystemComponent>(Actor);
	UEdenResourceIntegrationTestDemandComponent* Demand =
		EdenResourceIntegrationTests::AddComponent<UEdenResourceIntegrationTestDemandComponent>(Actor);
	Demand->DemandNormalized = 1.0f;

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(EdenResourceIntegrationTests::MakeFuelConfig()));
	TestTrue(TEXT("Fuel discovers one demand source"), Fuel->RefreshPropulsionDemandSource());
	TestTrue(TEXT("Power initializes"), Power->InitializePowerSimulation(EdenResourceIntegrationTests::MakePowerConfig()));
	TestTrue(TEXT("Thermal initializes"), Thermal->InitializeThermalSimulation(EdenResourceIntegrationTests::MakeThermalConfig()));

	UEdenSimulationClockSubsystem* Clock = EdenResourceIntegrationTests::MakeClock();
	Clock->RegisterSimulationTickable(Fuel);
	Clock->RegisterSimulationTickable(Power);
	Clock->RegisterSimulationTickable(Thermal);
	Clock->Tick(0.2f);

	TestTrue(TEXT("Fuel changed before restart"), Fuel->GetFuelStateSnapshot().FuelQuantityKilograms < 100.0f);
	TestTrue(TEXT("Power changed before restart"), Power->GetPowerStateSnapshot().BatteryChargeKilowattHours < 1.0f);
	TestTrue(TEXT("Thermal changed before restart"), Thermal->GetThermalStateSnapshot().TemperatureCelsius > 20.0f);

	TestTrue(TEXT("Fuel unregisters before simulated restart"), Clock->UnregisterSimulationTickable(Fuel));
	TestTrue(TEXT("Power unregisters before simulated restart"), Clock->UnregisterSimulationTickable(Power));
	TestTrue(TEXT("Thermal unregisters before simulated restart"), Clock->UnregisterSimulationTickable(Thermal));
	TestEqual(TEXT("Old clock has no subscribers before simulated restart"), Clock->GetSubscriberCount(), 0);

	UEdenSimulationClockSubsystem* RestartedClock = EdenResourceIntegrationTests::MakeClock();
	TestTrue(TEXT("Fuel reset succeeds"), Fuel->ResetFuelState());
	TestTrue(TEXT("Power reset succeeds"), Power->ResetPowerState());
	TestTrue(TEXT("Thermal reset succeeds"), Thermal->ResetThermalState());
	TestTrue(TEXT("Fuel re-registers after restart"), RestartedClock->RegisterSimulationTickable(Fuel));
	TestTrue(TEXT("Power re-registers after restart"), RestartedClock->RegisterSimulationTickable(Power));
	TestTrue(TEXT("Thermal re-registers after restart"), RestartedClock->RegisterSimulationTickable(Thermal));

	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Restarted clock elapsed time starts clean"),
		RestartedClock->GetElapsedSimulationTimeSeconds(),
		0.0f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Fuel reset after restart restores initial fuel"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		100.0f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Power reset after restart restores initial charge"),
		Power->GetPowerStateSnapshot().BatteryChargeKilowattHours,
		1.0f);
	EdenResourceIntegrationTests::TestFloatNearlyEqual(
		*this,
		TEXT("Thermal reset after restart restores initial temperature"),
		Thermal->GetThermalStateSnapshot().TemperatureCelsius,
		20.0f);

	return true;
}

#endif
