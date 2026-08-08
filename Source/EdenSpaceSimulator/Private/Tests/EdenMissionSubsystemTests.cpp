// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenMissionTestListener.h"
#include "EdenSimClockTestSubscriber.h"
#include "Misc/AutomationTest.h"
#include "Missions/EdenMissionModel.h"
#include "Missions/EdenMissionSubsystem.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
