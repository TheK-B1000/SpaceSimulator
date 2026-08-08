// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenMissionTestListener.h"
#include "EdenSimClockTestSubscriber.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Missions/EdenMissionModel.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "UObject/Package.h"

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
	Config.HeatGenerationDegreesCelsiusPerSecond = 0.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.0f;
	return Config;
}

FEdenPowerConfig MakePowerConfig()
{
	FEdenPowerConfig Config;
	Config.BatteryCapacityKilowattHours = 20.0f;
	Config.GenerationKilowatts = 0.0f;
	Config.BaselineDemandKilowatts = 0.0f;
	Config.InitialChargeFraction = 1.0f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	return Config;
}

FEdenMissionDefinitionConfig MakeRequiredObjectiveDefinition(FName MissionId)
{
	FEdenMissionDefinitionConfig Config;
	Config.MissionId = MissionId;

	FEdenMissionObjectiveConfig Obj;
	Obj.ObjectiveId = FName("Survive");
	Obj.bRequired = true;
	Obj.bActivateOnStart = true;
	Config.Objectives.Add(Obj);
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

	TestTrue(TEXT("Mission registers with clock"), Clock->RegisterSimulationTickable(MissionSubsystem, EdenSimulationClockPriority::Mission));

	UEdenMissionTestListener* Listener = NewObject<UEdenMissionTestListener>();
	MissionSubsystem->OnMissionEventTriggered.AddDynamic(Listener, &UEdenMissionTestListener::HandleMissionEventTriggered);

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
	MissionSubsystem->SetMissionResourceTargets(ThermalComponent, nullptr);

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("ThermalDisturbanceMission"));

	FEdenMissionEventConfig SetHeatingEvent;
	SetHeatingEvent.EventId = FName("SolarFlareImpact");
	SetHeatingEvent.TriggerTimeSeconds = 0.1f;
	SetHeatingEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	SetHeatingEvent.FloatParameter = 25.0f;
	Config.Events.Add(SetHeatingEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());

	const float TemperatureBeforeDispatch = ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius;

	// Resource step N runs with old modifiers, then mission dispatches.
	Clock->Tick(0.15f);
	TestEqual(
		TEXT("Thermal external rate is set by mission event"),
		ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond,
		25.0f);
	TestEqual(
		TEXT("Temperature does not retroactively change in the dispatch step"),
		ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius,
		TemperatureBeforeDispatch);

	const float TemperatureAfterDispatchStep = ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius;
	Clock->Tick(0.1f);
	TestTrue(
		TEXT("Modifier affects the following resource step"),
		ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius > TemperatureAfterDispatchStep);

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
	MissionSubsystem->SetMissionResourceTargets(nullptr, PowerComponent);

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("PowerDisturbanceMission"));

	FEdenMissionEventConfig SetDemandEvent;
	SetDemandEvent.EventId = FName("PowerSpike");
	SetDemandEvent.TriggerTimeSeconds = 0.1f;
	SetDemandEvent.CommandType = EEdenMissionCommandType::SetExternalPowerDemand;
	SetDemandEvent.FloatParameter = 5.0f;
	Config.Events.Add(SetDemandEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());

	const float BatteryBeforeDispatch = PowerComponent->GetPowerStateSnapshot().BatteryChargeKilowattHours;

	Clock->Tick(0.15f);
	TestEqual(
		TEXT("Power external demand is set by mission event"),
		PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts,
		5.0f);
	TestEqual(
		TEXT("Battery does not retroactively change in the dispatch step"),
		PowerComponent->GetPowerStateSnapshot().BatteryChargeKilowattHours,
		BatteryBeforeDispatch);

	const float BatteryAfterDispatchStep = PowerComponent->GetPowerStateSnapshot().BatteryChargeKilowattHours;
	Clock->Tick(0.1f);
	TestTrue(
		TEXT("Battery effect begins on the next resource step"),
		PowerComponent->GetPowerStateSnapshot().BatteryChargeKilowattHours < BatteryAfterDispatchStep);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionSameTimeEventsDispatchDeterministicallyTest,
	"Eden.Integration.Mission.SameTimeMissionEventsDispatchDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionSameTimeEventsDispatchDeterministicallyTest::RunTest(const FString& Parameters)
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

	UEdenPowerSystemComponent* PowerComponent = NewObject<UEdenPowerSystemComponent>(TestActor);
	PowerComponent->RegisterComponent();
	PowerComponent->InitializePowerSimulation(EdenMissionSubsystemTests::MakePowerConfig());

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	MissionSubsystem->SetMissionResourceTargets(ThermalComponent, PowerComponent);

	UEdenMissionTestListener* Listener = NewObject<UEdenMissionTestListener>();
	MissionSubsystem->OnMissionEventTriggered.AddDynamic(Listener, &UEdenMissionTestListener::HandleMissionEventTriggered);

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("SameTimeMission"));

	FEdenMissionEventConfig FirstEvent;
	FirstEvent.EventId = FName("AlphaHeat");
	FirstEvent.TriggerTimeSeconds = 0.1f;
	FirstEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	FirstEvent.FloatParameter = 11.0f;
	Config.Events.Add(FirstEvent);

	FEdenMissionEventConfig SecondEvent;
	SecondEvent.EventId = FName("BetaDemand");
	SecondEvent.TriggerTimeSeconds = 0.1f;
	SecondEvent.CommandType = EEdenMissionCommandType::SetExternalPowerDemand;
	SecondEvent.FloatParameter = 7.0f;
	Config.Events.Add(SecondEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());

	Clock->Tick(0.15f);

	TestEqual(TEXT("Both same-time events fired"), Listener->TriggeredEvents.Num(), 2);
	if (Listener->TriggeredEvents.Num() == 2)
	{
		TestEqual(TEXT("Configured order preserved: Alpha first"), Listener->TriggeredEvents[0], FName("AlphaHeat"));
		TestEqual(TEXT("Configured order preserved: Beta second"), Listener->TriggeredEvents[1], FName("BetaDemand"));
	}

	TestEqual(TEXT("Thermal command applied"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 11.0f);
	TestEqual(TEXT("Power command applied"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 7.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionEventDoesNotDispatchTwiceTest,
	"Eden.Integration.Mission.MissionEventDoesNotDispatchTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionEventDoesNotDispatchTwiceTest::RunTest(const FString& Parameters)
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

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	MissionSubsystem->SetMissionResourceTargets(ThermalComponent, nullptr);

	UEdenMissionTestListener* Listener = NewObject<UEdenMissionTestListener>();
	MissionSubsystem->OnMissionEventTriggered.AddDynamic(Listener, &UEdenMissionTestListener::HandleMissionEventTriggered);

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("OnceOnlyMission"));

	FEdenMissionEventConfig SetHeatingEvent;
	SetHeatingEvent.EventId = FName("OnceHeat");
	SetHeatingEvent.TriggerTimeSeconds = 0.1f;
	SetHeatingEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	SetHeatingEvent.FloatParameter = 9.0f;
	Config.Events.Add(SetHeatingEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());

	Clock->Tick(0.15f);
	TestEqual(TEXT("Event fires once"), Listener->TriggeredEvents.Num(), 1);
	TestEqual(TEXT("External heating applied"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 9.0f);

	ThermalComponent->ClearExternalHeatingRate();
	Clock->Tick(0.2f);
	TestEqual(TEXT("Event does not re-dispatch"), Listener->TriggeredEvents.Num(), 1);
	TestEqual(TEXT("External heating remains cleared"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionAbortClearsAppliedExternalModifiersTest,
	"Eden.Integration.Mission.MissionAbortClearsAppliedExternalModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionAbortClearsAppliedExternalModifiersTest::RunTest(const FString& Parameters)
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

	UEdenPowerSystemComponent* PowerComponent = NewObject<UEdenPowerSystemComponent>(TestActor);
	PowerComponent->RegisterComponent();
	PowerComponent->InitializePowerSimulation(EdenMissionSubsystemTests::MakePowerConfig());

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	MissionSubsystem->SetMissionResourceTargets(ThermalComponent, PowerComponent);

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("AbortCleanupMission"));

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

	Clock->Tick(0.15f);
	TestEqual(TEXT("External heating active"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 30.0f);
	TestEqual(TEXT("External demand active"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 12.0f);

	const float TemperatureBeforeAbort = ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius;
	const float BatteryBeforeAbort = PowerComponent->GetPowerStateSnapshot().BatteryChargeKilowattHours;

	TestTrue(TEXT("Mission aborts"), MissionSubsystem->AbortMission());
	TestEqual(TEXT("Thermal external rate cleared on abort"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 0.0f);
	TestEqual(TEXT("Power external demand cleared on abort"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 0.0f);
	TestEqual(TEXT("Abort does not reset temperature"), ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius, TemperatureBeforeAbort);
	TestEqual(TEXT("Abort does not reset battery"), PowerComponent->GetPowerStateSnapshot().BatteryChargeKilowattHours, BatteryBeforeAbort);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionResetClearsAppliedExternalModifiersTest,
	"Eden.Integration.Mission.MissionResetClearsAppliedExternalModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionResetClearsAppliedExternalModifiersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenMissionSubsystemTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	AActor* TestActor = World->SpawnActor<AActor>();
	UEdenThermalSystemComponent* ThermalComponent = NewObject<UEdenThermalSystemComponent>(TestActor);
	ThermalComponent->RegisterComponent();
	ThermalComponent->InitializeThermalSimulation(EdenMissionSubsystemTests::MakeThermalConfig());

	UEdenPowerSystemComponent* PowerComponent = NewObject<UEdenPowerSystemComponent>(TestActor);
	PowerComponent->RegisterComponent();
	PowerComponent->InitializePowerSimulation(EdenMissionSubsystemTests::MakePowerConfig());

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	MissionSubsystem->SetMissionResourceTargets(ThermalComponent, PowerComponent);

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("ResetCleanupMission"));
	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));

	ThermalComponent->SetExternalHeatingRateDegreesCelsiusPerSecond(18.0f);
	PowerComponent->SetExternalDemandKilowatts(4.0f);

	TestTrue(TEXT("Mission resets"), MissionSubsystem->ResetMission());
	TestEqual(TEXT("Thermal external rate cleared on reset"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 0.0f);
	TestEqual(TEXT("Power external demand cleared on reset"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 0.0f);
	TestEqual(TEXT("State is Inactive after reset"), MissionSubsystem->GetMissionState(), EEdenMissionState::Inactive);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionMissingThermalTargetFailsSafelyTest,
	"Eden.Integration.Mission.MissingThermalTargetFailsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionMissingThermalTargetFailsSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenMissionSubsystemTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	MissionSubsystem->ClearMissionResourceTargets();

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("MissingThermalMission"));

	FEdenMissionEventConfig SetHeatingEvent;
	SetHeatingEvent.EventId = FName("HeatWithoutTarget");
	SetHeatingEvent.TriggerTimeSeconds = 0.1f;
	SetHeatingEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	SetHeatingEvent.FloatParameter = 20.0f;
	Config.Events.Add(SetHeatingEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts without thermal target"), MissionSubsystem->StartMission());
	Clock->Tick(0.15f);

	TestEqual(TEXT("Mission remains Running after missing-target dispatch"), MissionSubsystem->GetMissionState(), EEdenMissionState::Running);
	TestEqual(TEXT("Event is marked Executed after single attempt"), MissionSubsystem->GetMissionRuntimeState().EventStates[0].EventState, EEdenMissionEventState::Executed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionMissingPowerTargetFailsSafelyTest,
	"Eden.Integration.Mission.MissingPowerTargetFailsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionMissingPowerTargetFailsSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenMissionSubsystemTests::FScopedMissionWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	MissionSubsystem->ClearMissionResourceTargets();

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("MissingPowerMission"));

	FEdenMissionEventConfig SetDemandEvent;
	SetDemandEvent.EventId = FName("DemandWithoutTarget");
	SetDemandEvent.TriggerTimeSeconds = 0.1f;
	SetDemandEvent.CommandType = EEdenMissionCommandType::SetExternalPowerDemand;
	SetDemandEvent.FloatParameter = 8.0f;
	Config.Events.Add(SetDemandEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts without power target"), MissionSubsystem->StartMission());
	Clock->Tick(0.15f);

	TestEqual(TEXT("Mission remains Running after missing-target dispatch"), MissionSubsystem->GetMissionState(), EEdenMissionState::Running);
	TestEqual(TEXT("Event is marked Executed after single attempt"), MissionSubsystem->GetMissionRuntimeState().EventStates[0].EventState, EEdenMissionEventState::Executed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionInvalidCommandPayloadFailsSafelyTest,
	"Eden.Integration.Mission.InvalidCommandPayloadFailsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionInvalidCommandPayloadFailsSafelyTest::RunTest(const FString& Parameters)
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

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	MissionSubsystem->SetMissionResourceTargets(ThermalComponent, nullptr);

	FEdenMissionDefinitionConfig InvalidConfig = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("InvalidPayloadMission"));
	FEdenMissionEventConfig InvalidEvent;
	InvalidEvent.EventId = FName("NaNHeat");
	InvalidEvent.TriggerTimeSeconds = 0.1f;
	InvalidEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	InvalidEvent.FloatParameter = std::numeric_limits<float>::quiet_NaN();
	InvalidConfig.Events.Add(InvalidEvent);

	TestFalse(TEXT("Definition with non-finite payload is rejected at load"), MissionSubsystem->LoadMission(InvalidConfig));

	FEdenMissionDefinitionConfig RuntimeConfig = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("RuntimeSanitizedMission"));
	FEdenMissionEventConfig NegativeEvent;
	NegativeEvent.EventId = FName("NegativeHeat");
	NegativeEvent.TriggerTimeSeconds = 0.1f;
	NegativeEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	NegativeEvent.FloatParameter = -12.0f;
	RuntimeConfig.Events.Add(NegativeEvent);

	TestTrue(TEXT("Finite negative payload loads"), MissionSubsystem->LoadMission(RuntimeConfig));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());
	Clock->Tick(0.15f);

	TestEqual(
		TEXT("Resource sanitization clamps negative external heating"),
		ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond,
		0.0f);
	TestEqual(TEXT("Mission remains Running"), MissionSubsystem->GetMissionState(), EEdenMissionState::Running);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionUnsupportedCommandTypeFailsSafelyTest,
	"Eden.Integration.Mission.UnsupportedCommandTypeFailsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionUnsupportedCommandTypeFailsSafelyTest::RunTest(const FString& Parameters)
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

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	MissionSubsystem->SetMissionResourceTargets(nullptr, PowerComponent);

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("UnsupportedCommandMission"));

	FEdenMissionEventConfig UnsupportedEvent;
	UnsupportedEvent.EventId = FName("GenerationAttempt");
	UnsupportedEvent.TriggerTimeSeconds = 0.1f;
	UnsupportedEvent.CommandType = EEdenMissionCommandType::SetPowerGeneration;
	UnsupportedEvent.FloatParameter = 40.0f;
	Config.Events.Add(UnsupportedEvent);

	const float GenerationBefore = PowerComponent->GetPowerStateSnapshot().GenerationKilowatts;

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());
	Clock->Tick(0.15f);

	TestEqual(TEXT("Unsupported generation command does not mutate power"), PowerComponent->GetPowerStateSnapshot().GenerationKilowatts, GenerationBefore);
	TestEqual(TEXT("Mission remains Running"), MissionSubsystem->GetMissionState(), EEdenMissionState::Running);
	TestEqual(TEXT("Event still advances to Executed once"), MissionSubsystem->GetMissionRuntimeState().EventStates[0].EventState, EEdenMissionEventState::Executed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionClockOrderingCommandLatencyRegressionTest,
	"Eden.Integration.Mission.ClockOrderingCommandAffectsNextStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenMissionClockOrderingCommandLatencyRegressionTest::RunTest(const FString& Parameters)
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
	MissionSubsystem->SetMissionResourceTargets(ThermalComponent, nullptr);

	FEdenMissionDefinitionConfig Config = EdenMissionSubsystemTests::MakeRequiredObjectiveDefinition(FName("LatencyMission"));

	FEdenMissionEventConfig SetHeatingEvent;
	SetHeatingEvent.EventId = FName("Impact");
	SetHeatingEvent.TriggerTimeSeconds = 0.1f;
	SetHeatingEvent.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	SetHeatingEvent.FloatParameter = 50.0f;
	Config.Events.Add(SetHeatingEvent);

	TestTrue(TEXT("Mission loads"), MissionSubsystem->LoadMission(Config));
	TestTrue(TEXT("Mission starts"), MissionSubsystem->StartMission());

	const float TemperatureAtStart = ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius;
	Clock->Tick(0.15f);

	TestEqual(TEXT("Command applied after resource step"), ThermalComponent->GetThermalStateSnapshot().ExternalHeatingRateDegreesCelsiusPerSecond, 50.0f);
	TestEqual(TEXT("Same-step temperature unchanged by new modifier"), ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius, TemperatureAtStart);

	Clock->Tick(0.1f);
	const float ExpectedTemperature = TemperatureAtStart + (50.0f * 0.1f);
	TestTrue(
		TEXT("Next fixed step applies the dispatched modifier"),
		FMath::IsNearlyEqual(ThermalComponent->GetThermalStateSnapshot().TemperatureCelsius, ExpectedTemperature, 0.001f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
