// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenMissionTestListener.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Missions/EdenMissionDefinitionDataAsset.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenSolarEventMissionTests
{
static const TCHAR* SolarEventAssetPath = TEXT("/Game/Eden/Data/Missions/DA_SolarEventEmergency.DA_SolarEventEmergency");

UEdenMissionDefinitionDataAsset* LoadSolarEventAsset()
{
	return Cast<UEdenMissionDefinitionDataAsset>(FSoftObjectPath(SolarEventAssetPath).TryLoad());
}

FEdenThermalConfig MakeThermalConfig()
{
	FEdenThermalConfig Config;
	Config.AbsoluteMinTemperatureCelsius = -50.0f;
	Config.AmbientTemperatureCelsius = 20.0f;
	Config.WarningTemperatureCelsius = 70.0f;
	Config.CriticalTemperatureCelsius = 100.0f;
	Config.AbsoluteMaxTemperatureCelsius = 120.0f;
	Config.InitialTemperatureCelsius = 20.0f;
	Config.HeatGenerationDegreesCelsiusPerSecond = 1.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.5f;
	return Config;
}

FEdenPowerConfig MakePowerConfig()
{
	FEdenPowerConfig Config;
	Config.BatteryCapacityKilowattHours = 20.0f;
	Config.GenerationKilowatts = 2.0f;
	Config.BaselineDemandKilowatts = 1.0f;
	Config.InitialChargeFraction = 1.0f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	return Config;
}

FEdenFuelConfig MakeFuelConfig()
{
	FEdenFuelConfig Config;
	Config.CapacityKilograms = 100.0f;
	Config.ConsumptionRateKilogramsPerSecond = 0.0f;
	Config.InitialFuelFraction = 1.0f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	return Config;
}

struct FScopedMissionWorld
{
	FWorldContext* WorldContext = nullptr;
	UWorld* World = nullptr;

	FScopedMissionWorld()
	{
		const FName WorldName = MakeUniqueObjectName(
			nullptr,
			UWorld::StaticClass(),
			TEXT("EdenSolarEventMissionTestWorld"),
			EUniqueObjectNameOptions::GloballyUnique);

		WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		check(World);
		World->AddToRoot();
		WorldContext->SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
	}

	~FScopedMissionWorld()
	{
		if (!World)
		{
			return;
		}

		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->RemoveFromRoot();
		GEngine->DestroyWorldContext(World);
		World = nullptr;
		WorldContext = nullptr;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarEventDefinitionIsValidTest,
	"Eden.Integration.Mission.SolarEventDefinitionIsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarEventDefinitionIsValidTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenMissionDefinitionDataAsset* Asset = EdenSolarEventMissionTests::LoadSolarEventAsset();
	TestNotNull(TEXT("DA_SolarEventEmergency loads"), Asset);
	if (!Asset)
	{
		return false;
	}

	TArray<FString> Errors;
	TestTrue(TEXT("Asset definition validates"), FEdenMissionModel::ValidateDefinition(Asset->GetMissionDefinition(), &Errors));
	TestEqual(TEXT("MissionId is SolarCrisis"), Asset->GetMissionId(), FName("SolarCrisis"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarEventTimelineUsesExpectedAbsoluteTimesTest,
	"Eden.Integration.Mission.SolarEventTimelineUsesExpectedAbsoluteTimes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarEventTimelineUsesExpectedAbsoluteTimesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenMissionDefinitionDataAsset* Asset = EdenSolarEventMissionTests::LoadSolarEventAsset();
	TestNotNull(TEXT("Asset loads"), Asset);
	if (!Asset)
	{
		return false;
	}

	const FEdenMissionDefinitionConfig& Config = Asset->GetMissionDefinition();
	auto FindEvent = [&Config](FName EventId) -> const FEdenMissionEventConfig*
	{
		return Config.Events.FindByPredicate([&EventId](const FEdenMissionEventConfig& Event)
		{
			return Event.EventId == EventId;
		});
	};

	TestEqual(TEXT("Warning at 5s"), FindEvent(FName("WarningPhaseBegin"))->TriggerTimeSeconds, 5.0f);
	TestEqual(TEXT("Impact phase at 10s"), FindEvent(FName("ImpactPhaseBegin"))->TriggerTimeSeconds, 10.0f);
	TestEqual(TEXT("Heating at 10s"), FindEvent(FName("SolarFlareHeating"))->TriggerTimeSeconds, 10.0f);
	TestEqual(TEXT("Demand at 10s"), FindEvent(FName("AuxiliaryLoadDemand"))->TriggerTimeSeconds, 10.0f);
	TestEqual(TEXT("Recovery at 30s"), FindEvent(FName("RecoveryPhaseBegin"))->TriggerTimeSeconds, 30.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarEventContainsNoUnsupportedCommandsTest,
	"Eden.Integration.Mission.SolarEventContainsNoUnsupportedCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarEventContainsNoUnsupportedCommandsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenMissionDefinitionDataAsset* Asset = EdenSolarEventMissionTests::LoadSolarEventAsset();
	TestNotNull(TEXT("Asset loads"), Asset);
	if (!Asset)
	{
		return false;
	}

	for (const FEdenMissionEventConfig& Event : Asset->GetMissionDefinition().Events)
	{
		TestNotEqual(TEXT("No SetPowerGeneration"), Event.CommandType, EEdenMissionCommandType::SetPowerGeneration);
		if (Event.CommandType == EEdenMissionCommandType::SetMissionPhase)
		{
			TestTrue(
				TEXT("Phase events use typed PhaseParameter"),
				Event.PhaseParameter == EEdenMissionPhase::Warning
					|| Event.PhaseParameter == EEdenMissionPhase::Impact
					|| Event.PhaseParameter == EEdenMissionPhase::Recovery
					|| Event.PhaseParameter == EEdenMissionPhase::Resolved);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarEventSuccessPathTest,
	"Eden.Integration.Mission.SolarEventSuccessPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarEventSuccessPathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenMissionDefinitionDataAsset* Asset = EdenSolarEventMissionTests::LoadSolarEventAsset();
	TestNotNull(TEXT("Asset loads"), Asset);
	if (!Asset)
	{
		return false;
	}

	EdenSolarEventMissionTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.5f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	AActor* TestActor = World->SpawnActor<AActor>();
	FEdenThermalConfig ThermalConfig = EdenSolarEventMissionTests::MakeThermalConfig();
	ThermalConfig.HeatGenerationDegreesCelsiusPerSecond = 0.0f;
	ThermalConfig.DissipationDegreesCelsiusPerSecond = 0.0f;
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>(TestActor);
	Thermal->RegisterComponent();
	Thermal->InitializeThermalSimulation(ThermalConfig);
	Thermal->RegisterWithSimulationClock();

	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>(TestActor);
	Power->RegisterComponent();
	Power->InitializePowerSimulation(EdenSolarEventMissionTests::MakePowerConfig());
	Power->RegisterWithSimulationClock();

	UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>(TestActor);
	Fuel->RegisterComponent();
	Fuel->InitializeFuelSimulation(EdenSolarEventMissionTests::MakeFuelConfig());
	Fuel->RegisterWithSimulationClock();

	UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
	Mission->SetMissionResourceTargets(Thermal, Power, Fuel);

	UEdenMissionTestListener* Listener = NewObject<UEdenMissionTestListener>();
	Mission->OnMissionStateChanged.AddDynamic(Listener, &UEdenMissionTestListener::HandleMissionStateChanged);

	TestTrue(TEXT("Loads from Data Asset"), Mission->LoadMissionFromDefinitionAsset(Asset));
	TestTrue(TEXT("Starts"), Mission->StartMission());
	TestEqual(TEXT("Nominal at start"), Mission->GetMissionPhase(), EEdenMissionPhase::Nominal);

	for (int32 Step = 0; Step < 9; ++Step)
	{
		Clock->Tick(0.5f);
	}
	TestEqual(TEXT("Still Nominal before 5s"), Mission->GetMissionPhase(), EEdenMissionPhase::Nominal);

	Clock->Tick(0.5f); // t=5
	TestEqual(TEXT("Warning at 5s"), Mission->GetMissionPhase(), EEdenMissionPhase::Warning);

	for (int32 Step = 0; Step < 9; ++Step)
	{
		Clock->Tick(0.5f);
	}
	TestEqual(TEXT("Still Warning before 10s"), Mission->GetMissionPhase(), EEdenMissionPhase::Warning);

	const float TempBeforeImpact = Thermal->GetThermalStateSnapshot().TemperatureCelsius;
	Clock->Tick(0.5f); // t=10 dispatch
	TestEqual(TEXT("Impact at 10s"), Mission->GetMissionPhase(), EEdenMissionPhase::Impact);
	TestTrue(TEXT("Heating applied"), Thermal->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond > 0.0f);
	TestTrue(TEXT("Demand applied"), Power->GetPowerStateSnapshot().ExternalDemandKilowatts > 0.0f);
	TestEqual(TEXT("One-step latency: temp unchanged at dispatch"), Thermal->GetThermalStateSnapshot().TemperatureCelsius, TempBeforeImpact);

	Clock->Tick(0.5f); // next resource step
	TestTrue(TEXT("Heating affects next resource step"), Thermal->GetThermalStateSnapshot().TemperatureCelsius > TempBeforeImpact);

	for (int32 Step = 0; Step < 39; ++Step)
	{
		Clock->Tick(0.5f);
	}
	TestEqual(TEXT("Recovery at 30s"), Mission->GetMissionPhase(), EEdenMissionPhase::Recovery);
	TestEqual(TEXT("Heating cleared"), Thermal->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 0.0f);
	TestEqual(TEXT("Demand cleared"), Power->GetPowerStateSnapshot().ExternalDemandKilowatts, 0.0f);

	for (int32 Step = 0; Step < 40; ++Step)
	{
		Clock->Tick(0.5f);
	}

	TestEqual(TEXT("Succeeded at resolution"), Mission->GetMissionState(), EEdenMissionState::Succeeded);
	int32 SucceededCount = 0;
	for (EEdenMissionState State : Listener->NewStates)
	{
		if (State == EEdenMissionState::Succeeded)
		{
			++SucceededCount;
		}
	}
	TestEqual(TEXT("Succeeded exactly once"), SucceededCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarEventFailurePathTest,
	"Eden.Integration.Mission.SolarEventFailurePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarEventFailurePathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenMissionDefinitionDataAsset* Asset = EdenSolarEventMissionTests::LoadSolarEventAsset();
	TestNotNull(TEXT("Asset loads"), Asset);
	if (!Asset)
	{
		return false;
	}

	EdenSolarEventMissionTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.5f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	AActor* TestActor = World->SpawnActor<AActor>();
	FEdenThermalConfig ThermalConfig = EdenSolarEventMissionTests::MakeThermalConfig();
	ThermalConfig.DissipationDegreesCelsiusPerSecond = 0.0f;
	ThermalConfig.HeatGenerationDegreesCelsiusPerSecond = 0.0f;
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>(TestActor);
	Thermal->RegisterComponent();
	Thermal->InitializeThermalSimulation(ThermalConfig);
	Thermal->RegisterWithSimulationClock();

	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>(TestActor);
	Power->RegisterComponent();
	Power->InitializePowerSimulation(EdenSolarEventMissionTests::MakePowerConfig());

	UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>(TestActor);
	Fuel->RegisterComponent();
	Fuel->InitializeFuelSimulation(EdenSolarEventMissionTests::MakeFuelConfig());

	UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
	Mission->SetMissionResourceTargets(Thermal, Power, Fuel);
	TestTrue(TEXT("Loads asset"), Mission->LoadMissionFromDefinitionAsset(Asset));
	TestTrue(TEXT("Starts"), Mission->StartMission());

	// Advance to impact and then force an authoritative overheating condition.
	for (int32 Step = 0; Step < 21; ++Step)
	{
		Clock->Tick(0.5f);
	}
	TestEqual(TEXT("Reached Impact"), Mission->GetMissionPhase(), EEdenMissionPhase::Impact);

	FEdenThermalConfig OverheatConfig = ThermalConfig;
	OverheatConfig.InitialTemperatureCelsius = 110.0f;
	Thermal->InitializeThermalSimulation(OverheatConfig);
	Clock->Tick(0.5f);

	TestEqual(TEXT("Mission failed via resource snapshot evaluation"), Mission->GetMissionState(), EEdenMissionState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarEventAbortClearsMissionEffectsTest,
	"Eden.Integration.Mission.SolarEventAbortClearsMissionEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarEventAbortClearsMissionEffectsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenMissionDefinitionDataAsset* Asset = EdenSolarEventMissionTests::LoadSolarEventAsset();
	TestNotNull(TEXT("Asset loads"), Asset);
	if (!Asset)
	{
		return false;
	}

	EdenSolarEventMissionTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;
	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.5f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	AActor* TestActor = World->SpawnActor<AActor>();
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>(TestActor);
	Thermal->RegisterComponent();
	Thermal->InitializeThermalSimulation(EdenSolarEventMissionTests::MakeThermalConfig());
	Thermal->RegisterWithSimulationClock();

	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>(TestActor);
	Power->RegisterComponent();
	Power->InitializePowerSimulation(EdenSolarEventMissionTests::MakePowerConfig());
	Power->RegisterWithSimulationClock();

	UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>(TestActor);
	Fuel->RegisterComponent();
	Fuel->InitializeFuelSimulation(EdenSolarEventMissionTests::MakeFuelConfig());
	Fuel->RegisterWithSimulationClock();

	UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
	Mission->SetMissionResourceTargets(Thermal, Power, Fuel);
	TestTrue(TEXT("Loads"), Mission->LoadMissionFromDefinitionAsset(Asset));
	TestTrue(TEXT("Starts"), Mission->StartMission());

	for (int32 Step = 0; Step < 21; ++Step)
	{
		Clock->Tick(0.5f);
	}
	TestTrue(TEXT("Modifiers active in Impact"), Thermal->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond > 0.0f);

	const float TempBeforeAbort = Thermal->GetThermalStateSnapshot().TemperatureCelsius;
	const float BatteryBeforeAbort = Power->GetPowerStateSnapshot().BatteryChargeKilowattHours;
	TestTrue(TEXT("Abort succeeds"), Mission->AbortMission());
	TestEqual(TEXT("Heating cleared"), Thermal->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 0.0f);
	TestEqual(TEXT("Demand cleared"), Power->GetPowerStateSnapshot().ExternalDemandKilowatts, 0.0f);
	TestEqual(TEXT("Temperature not reset by abort"), Thermal->GetThermalStateSnapshot().TemperatureCelsius, TempBeforeAbort);
	TestEqual(TEXT("Battery not reset by abort"), Power->GetPowerStateSnapshot().BatteryChargeKilowattHours, BatteryBeforeAbort);

	TestTrue(TEXT("Reset after abort"), Mission->ResetMission());
	TestEqual(TEXT("Inactive after reset"), Mission->GetMissionState(), EEdenMissionState::Inactive);
	TestEqual(TEXT("Elapsed cleared"), Mission->GetMissionElapsedTimeSeconds(), 0.0f);
	TestTrue(TEXT("Can load again"), Mission->LoadMissionFromDefinitionAsset(Asset));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
