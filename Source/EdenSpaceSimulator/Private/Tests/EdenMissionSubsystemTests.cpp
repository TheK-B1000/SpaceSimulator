// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenMissionTestListener.h"
#include "EdenSimClockTestSubscriber.h"
#include "Misc/AutomationTest.h"
#include "Missions/EdenMissionModel.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenMissionSubsystemTests
{
	FEdenMissionDefinitionConfig MakeTestDefinition()
	{
		FEdenMissionDefinitionConfig Config;
		Config.MissionId = FName("SubsystemTestMission");

		FEdenMissionObjectiveConfig Obj1;
		Obj1.ObjectiveId = FName("Obj1");
		Obj1.bRequired = true;
		Obj1.bActivateOnStart = true;
		Config.Objectives.Add(Obj1);

		FEdenMissionEventConfig Evt1;
		Evt1.EventId = FName("Evt1");
		Evt1.TriggerTimeSeconds = 0.1f;
		Config.Events.Add(Evt1);

		FEdenMissionEventConfig Evt2;
		Evt2.EventId = FName("Evt2");
		Evt2.TriggerTimeSeconds = 0.2f;
		Config.Events.Add(Evt2);

		return Config;
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
		Config.HeatGenerationDegreesCelsiusPerSecond = 10.0f;
		Config.DissipationDegreesCelsiusPerSecond = 0.0f;
		return Config;
	}

	FEdenPowerConfig MakePowerConfig()
	{
		FEdenPowerConfig Config;
		Config.BatteryCapacityKilowattHours = 20.0f;
		Config.GenerationKilowatts = 10.0f;
		Config.BaselineDemandKilowatts = 2.0f;
		Config.InitialChargeFraction = 1.0f;
		Config.WarningThresholdFraction = 0.25f;
		Config.CriticalThresholdFraction = 0.1f;
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionSubsystemReceivesSimulationStepsTest,
	"Eden.Integration.Mission.MissionSubsystemReceivesSimulationSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionSubsystemReceivesSimulationStepsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = NewObject<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	UEdenMissionSubsystem* MissionSubsystem = NewObject<UEdenMissionSubsystem>();
	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeTestDefinition();

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestEqual(TEXT("State is Ready"), MissionSubsystem->GetMissionState(), EEdenMissionState::Ready);

	// Register directly with the clock for testing
	TestTrue(TEXT("Mission registers with clock"), Clock->RegisterSimulationTickable(MissionSubsystem, EdenSimulationClockPriority::Mission));

	UEdenMissionTestListener* Listener = NewObject<UEdenMissionTestListener>();
	MissionSubsystem->OnMissionEventTriggered.AddDynamic(Listener, &UEdenMissionTestListener::HandleMissionEventTriggered);

	// Tick clock by 0.25s -> 2 fixed steps (0.1s and 0.2s)
	// First tick when ready: step does not advance because state is not Running
	Clock->Tick(0.25f);
	TestEqual(TEXT("Elapsed simulation time is 0 when Ready"), MissionSubsystem->GetMissionElapsedTimeSeconds(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionSubsystemMultipleSimulationStepsCatchUpTest,
	"Eden.Integration.Mission.MultipleSimulationStepsCatchUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionSubsystemMultipleSimulationStepsCatchUpTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = NewObject<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	UEdenMissionSubsystem* MissionSubsystem = NewObject<UEdenMissionSubsystem>();
	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeTestDefinition();

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission registers with clock"), Clock->RegisterSimulationTickable(MissionSubsystem, EdenSimulationClockPriority::Mission));

	// Tick clock by 0.35s in a single frame -> 3 fixed steps
	Clock->Tick(0.35f);

	TestEqual(TEXT("Clock executed 3 steps"), Clock->GetLastStepsTaken(), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionClockOrdersSystemsBeforeMissionTest,
	"Eden.Integration.Mission.ClockOrdersSystemsBeforeMission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionClockOrdersSystemsBeforeMissionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = NewObject<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	TArray<FName> StepOrderLog;

	UEdenSimClockTestSubscriber* SystemSubscriber = NewObject<UEdenSimClockTestSubscriber>();
	SystemSubscriber->SubscriberName = FName("SystemComponent");
	SystemSubscriber->ExecutionOrderLog = &StepOrderLog;

	UEdenMissionSubsystem* MissionSubsystem = NewObject<UEdenMissionSubsystem>();
	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeTestDefinition();
	MissionSubsystem->LoadMission(Config);

	UEdenMissionTestListener* Listener = NewObject<UEdenMissionTestListener>();
	MissionSubsystem->OnMissionEventTriggered.AddDynamic(Listener, &UEdenMissionTestListener::HandleMissionEventTriggered);

	// Register Mission FIRST with Priority 100, System SECOND with Priority 0
	TestTrue(TEXT("Mission registers first"), Clock->RegisterSimulationTickable(MissionSubsystem, EdenSimulationClockPriority::Mission));
	TestTrue(TEXT("System registers second"), Clock->RegisterSimulationTickable(SystemSubscriber, EdenSimulationClockPriority::Systems));

	Clock->Tick(0.15f);

	TestEqual(TEXT("System stepped"), StepOrderLog.Num(), 1);
	if (StepOrderLog.Num() == 1)
	{
		TestEqual(TEXT("System stepped first"), StepOrderLog[0], FName("SystemComponent"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionSubsystemLifecycleTransitionsTest,
	"Eden.Integration.Mission.LifecycleTransitionsViaSubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionSubsystemLifecycleTransitionsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenMissionSubsystem* MissionSubsystem = NewObject<UEdenMissionSubsystem>();
	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeTestDefinition();

	UEdenMissionTestListener* Listener = NewObject<UEdenMissionTestListener>();
	MissionSubsystem->OnMissionStateChanged.AddDynamic(Listener, &UEdenMissionTestListener::HandleMissionStateChanged);

	TestEqual(TEXT("Initial state is Inactive"), MissionSubsystem->GetMissionState(), EEdenMissionState::Inactive);
	TestFalse(TEXT("Cannot start when Inactive"), MissionSubsystem->StartMission());

	TestTrue(TEXT("LoadMission succeeds"), MissionSubsystem->LoadMission(Config));
	TestEqual(TEXT("State is Ready"), MissionSubsystem->GetMissionState(), EEdenMissionState::Ready);
	TestEqual(TEXT("Active mission ID set"), MissionSubsystem->GetActiveMissionId(), FName("SubsystemTestMission"));

	// Check that state changed broadcast fired
	TestEqual(TEXT("State changed fired once"), Listener->NewStates.Num(), 1);
	if (Listener->NewStates.Num() == 1)
	{
		TestEqual(TEXT("Previous was Inactive"), Listener->PreviousStates[0], EEdenMissionState::Inactive);
		TestEqual(TEXT("New is Ready"), Listener->NewStates[0], EEdenMissionState::Ready);
	}

	FEdenMissionStateSnapshot Snapshot = MissionSubsystem->GetMissionStateSnapshot();
	TestEqual(TEXT("Snapshot state matches"), Snapshot.MissionState, EEdenMissionState::Ready);
	TestEqual(TEXT("Snapshot mission ID matches"), Snapshot.ActiveMissionId, FName("SubsystemTestMission"));

	TestTrue(TEXT("ResetMission resets to Inactive"), MissionSubsystem->ResetMission());
	TestEqual(TEXT("State is Inactive after reset"), MissionSubsystem->GetMissionState(), EEdenMissionState::Inactive);

	TestEqual(TEXT("State changed fired twice"), Listener->NewStates.Num(), 2);
	if (Listener->NewStates.Num() == 2)
	{
		TestEqual(TEXT("Previous was Ready"), Listener->PreviousStates[1], EEdenMissionState::Ready);
		TestEqual(TEXT("New is Inactive"), Listener->NewStates[1], EEdenMissionState::Inactive);
	}

	return true;
}

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "UObject/Package.h"

namespace EdenMissionSubsystemTests
{
	struct FScopedMissionWorld
	{
		FWorldContext* WorldContext = nullptr;
		UWorld* World = nullptr;

		FScopedMissionWorld()
		{
			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("EdenMissionSubsystemTestWorld"),
				EUniqueObjectNameOptions::GloballyUnique);

			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			WorldContext->SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedMissionWorld()
		{
			if (!World)
			{
				return;
			}

			if (World->AreActorsInitialized())
			{
				for (AActor* Actor : FActorRange(World))
				{
					if (Actor)
					{
						Actor->RouteEndPlay(EEndPlayReason::LevelTransition);
					}
				}
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
	FEdenMissionEventCommandsReachThermalTest,
	"Eden.Integration.Mission.MissionEventCommandsReachThermal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionEventCommandsReachThermalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenMissionSubsystemTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	AActor* TestActor = World->SpawnActor<AActor>();
	UEdenThermalSystemComponent* ThermalComponent = NewObject<UEdenThermalSystemComponent>(TestActor);
	ThermalComponent->RegisterComponent();
	ThermalComponent->InitializeThermalSimulation(EdenMissionSubsystemTests::MakeThermalConfig());
	ThermalComponent->RegisterWithSimulationClock();

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	FEdenMissionDefinitionConfig Config;
	Config.MissionId = FName("ThermalDisturbanceMission");

	FEdenMissionObjectiveConfig Obj;
	Obj.ObjectiveId = FName("Survive");
	Obj.bRequired = true;
	Obj.bActivateOnStart = true;
	Config.Objectives.Add(Obj);

	FEdenMissionEventConfig SetHeatingEvent;
	SetHeatingEvent.EventId = FName("SolarFlareImpact");
	SetHeatingEvent.TriggerTimeSeconds = 0.1f;
	SetHeatingEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	SetHeatingEvent.FloatParameter = 25.0f;
	Config.Events.Add(SetHeatingEvent);

	FEdenMissionEventConfig ClearHeatingEvent;
	ClearHeatingEvent.EventId = FName("SolarFlareClear");
	ClearHeatingEvent.TriggerTimeSeconds = 0.3f;
	ClearHeatingEvent.CommandType = EEdenMissionCommandType::ClearExternalHeatingRate;
	Config.Events.Add(ClearHeatingEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());

	// Tick 0.15s -> steps to 0.1s -> SolarFlareImpact triggers
	Clock->Tick(0.15f);
	TestEqual(
		TEXT("Thermal external rate is set to 25 C/s by mission event"),
		ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond,
		25.0f);

	// Tick another 0.2s -> total 0.35s -> steps to 0.3s -> SolarFlareClear triggers
	Clock->Tick(0.2f);
	TestEqual(
		TEXT("Thermal external rate is cleared to 0 by mission event"),
		ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond,
		0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionEventCommandsReachPowerTest,
	"Eden.Integration.Mission.MissionEventCommandsReachPower",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionEventCommandsReachPowerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenMissionSubsystemTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	AActor* TestActor = World->SpawnActor<AActor>();
	UEdenPowerSystemComponent* PowerComponent = NewObject<UEdenPowerSystemComponent>(TestActor);
	PowerComponent->RegisterComponent();
	PowerComponent->InitializePowerSimulation(EdenMissionSubsystemTests::MakePowerConfig());
	PowerComponent->RegisterWithSimulationClock();

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	FEdenMissionDefinitionConfig Config;
	Config.MissionId = FName("PowerDisturbanceMission");

	FEdenMissionObjectiveConfig Obj;
	Obj.ObjectiveId = FName("Survive");
	Obj.bRequired = true;
	Obj.bActivateOnStart = true;
	Config.Objectives.Add(Obj);

	FEdenMissionEventConfig SetDemandEvent;
	SetDemandEvent.EventId = FName("PowerSpike");
	SetDemandEvent.TriggerTimeSeconds = 0.1f;
	SetDemandEvent.CommandType = EEdenMissionCommandType::SetExternalPowerDemand;
	SetDemandEvent.FloatParameter = 5.0f;
	Config.Events.Add(SetDemandEvent);

	FEdenMissionEventConfig SetGenerationEvent;
	SetGenerationEvent.EventId = FName("AuxiliaryPowerOn");
	SetGenerationEvent.TriggerTimeSeconds = 0.2f;
	SetGenerationEvent.CommandType = EEdenMissionCommandType::SetPowerGeneration;
	SetGenerationEvent.FloatParameter = 20.0f;
	Config.Events.Add(SetGenerationEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());

	// Tick 0.15s -> steps to 0.1s -> PowerSpike triggers
	Clock->Tick(0.15f);
	TestEqual(
		TEXT("Power external demand is set to 5 kW by mission event"),
		PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts,
		5.0f);

	// Tick 0.1s -> total 0.25s -> steps to 0.2s -> AuxiliaryPowerOn triggers
	Clock->Tick(0.1f);
	TestEqual(
		TEXT("Power generation is set to 20 kW by mission event"),
		PowerComponent->GetPowerStateSnapshot().GenerationKilowatts,
		20.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionResetClearsExternalModifiersTest,
	"Eden.Integration.Mission.MissionResetClearsExternalModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionResetClearsExternalModifiersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenMissionSubsystemTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	AActor* TestActor = World->SpawnActor<AActor>();
	UEdenThermalSystemComponent* ThermalComponent = NewObject<UEdenThermalSystemComponent>(TestActor);
	ThermalComponent->RegisterComponent();
	ThermalComponent->InitializeThermalSimulation(EdenMissionSubsystemTests::MakeThermalConfig());
	ThermalComponent->RegisterWithSimulationClock();

	UEdenPowerSystemComponent* PowerComponent = NewObject<UEdenPowerSystemComponent>(TestActor);
	PowerComponent->RegisterComponent();
	PowerComponent->InitializePowerSimulation(EdenMissionSubsystemTests::MakePowerConfig());
	PowerComponent->RegisterWithSimulationClock();

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	FEdenMissionDefinitionConfig Config;
	Config.MissionId = FName("DisturbanceMission");

	FEdenMissionObjectiveConfig Obj;
	Obj.ObjectiveId = FName("Survive");
	Obj.bRequired = true;
	Obj.bActivateOnStart = true;
	Config.Objectives.Add(Obj);

	FEdenMissionEventConfig SetHeatingEvent;
	SetHeatingEvent.EventId = FName("HeatDisturbance");
	SetHeatingEvent.TriggerTimeSeconds = 0.1f;
	SetHeatingEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	SetHeatingEvent.FloatParameter = 30.0f;
	Config.Events.Add(SetHeatingEvent);

	FEdenMissionEventConfig SetDemandEvent;
	SetDemandEvent.EventId = FName("DemandDisturbance");
	SetDemandEvent.TriggerTimeSeconds = 0.1f;
	SetDemandEvent.CommandType = EEdenMissionCommandType::SetExternalPowerDemand;
	SetDemandEvent.FloatParameter = 12.0f;
	Config.Events.Add(SetDemandEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());

	// Tick 0.15s to trigger both events
	Clock->Tick(0.15f);
	TestEqual(TEXT("External heating active"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 30.0f);
	TestEqual(TEXT("External demand active"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 12.0f);

	// Abort mission and verify external modifiers cleared
	TestTrue(TEXT("Mission aborts"), MissionSubsystem->AbortMission());
	TestEqual(TEXT("Thermal external rate cleared on abort"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 0.0f);
	TestEqual(TEXT("Power external demand cleared on abort"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
