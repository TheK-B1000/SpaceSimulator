// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/EdenMissionModel.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenMissionModelTests
{
	FEdenMissionDefinitionConfig MakeValidConfig()
	{
		FEdenMissionDefinitionConfig Config;
		Config.MissionId = FName("TestMission");

		FEdenMissionObjectiveConfig Obj1;
		Obj1.ObjectiveId = FName("Obj1");
		Obj1.bRequired = true;
		Config.Objectives.Add(Obj1);

		FEdenMissionEventConfig Evt1;
		Evt1.EventId = FName("Evt1");
		Evt1.TriggerTimeSeconds = 10.0f;
		Config.Events.Add(Evt1);

		return Config;
	}
}

// ===========================================================================
// Validation tests
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionValidateDefinitionAcceptsValidConfigTest,
	"Eden.Unit.Mission.Validation.ValidateDefinitionAcceptsValidConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionValidateDefinitionAcceptsValidConfigTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FString> Errors;
	TestTrue(TEXT("Valid config passes"), FEdenMissionModel::ValidateDefinition(EdenMissionModelTests::MakeValidConfig(), &Errors));
	TestEqual(TEXT("No errors generated"), Errors.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionValidateDefinitionRejectsEmptyMissionIdTest,
	"Eden.Unit.Mission.Validation.ValidateDefinitionRejectsEmptyMissionId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionValidateDefinitionRejectsEmptyMissionIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.MissionId = NAME_None;
	TestFalse(TEXT("Empty MissionId is rejected"), FEdenMissionModel::ValidateDefinition(Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionValidateDefinitionRejectsDuplicateEventIdsTest,
	"Eden.Unit.Mission.Validation.ValidateDefinitionRejectsDuplicateEventIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionValidateDefinitionRejectsDuplicateEventIdsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.Events.Add(Config.Events[0]); // Duplicate event
	TestFalse(TEXT("Duplicate EventIds are rejected"), FEdenMissionModel::ValidateDefinition(Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionValidateDefinitionRejectsNegativeEventTimeTest,
	"Eden.Unit.Mission.Validation.ValidateDefinitionRejectsNegativeEventTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionValidateDefinitionRejectsNegativeEventTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.Events[0].TriggerTimeSeconds = -1.0f;
	TestFalse(TEXT("Negative trigger time is rejected"), FEdenMissionModel::ValidateDefinition(Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionValidateDefinitionRejectsNaNEventTimeTest,
	"Eden.Unit.Mission.Validation.ValidateDefinitionRejectsNaNEventTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionValidateDefinitionRejectsNaNEventTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.Events[0].TriggerTimeSeconds = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN trigger time is rejected"), FEdenMissionModel::ValidateDefinition(Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionValidateDefinitionRejectsInfEventTimeTest,
	"Eden.Unit.Mission.Validation.ValidateDefinitionRejectsInfEventTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionValidateDefinitionRejectsInfEventTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.Events[0].TriggerTimeSeconds = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Inf trigger time is rejected"), FEdenMissionModel::ValidateDefinition(Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionValidateDefinitionRejectsDuplicateObjectiveIdsTest,
	"Eden.Unit.Mission.Validation.ValidateDefinitionRejectsDuplicateObjectiveIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionValidateDefinitionRejectsDuplicateObjectiveIdsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.Objectives.Add(Config.Objectives[0]);
	TestFalse(TEXT("Duplicate ObjectiveIds are rejected"), FEdenMissionModel::ValidateDefinition(Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionValidateDefinitionAcceptsEmptyEventsArrayTest,
	"Eden.Unit.Mission.Validation.ValidateDefinitionAcceptsEmptyEventsArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionValidateDefinitionAcceptsEmptyEventsArrayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.Events.Empty();
	TestTrue(TEXT("Empty events array is accepted"), FEdenMissionModel::ValidateDefinition(Config));
	return true;
}

// ===========================================================================
// Lifecycle tests
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionCanTransitionInactiveToReadyTest,
	"Eden.Unit.Mission.Lifecycle.CanTransitionInactiveToReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionCanTransitionInactiveToReadyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Inactive->Ready"), FEdenMissionModel::CanTransition(EEdenMissionState::Inactive, EEdenMissionState::Ready));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionCanTransitionReadyToRunningTest,
	"Eden.Unit.Mission.Lifecycle.CanTransitionReadyToRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionCanTransitionReadyToRunningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Ready->Running"), FEdenMissionModel::CanTransition(EEdenMissionState::Ready, EEdenMissionState::Running));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionCanTransitionRunningToSucceededTest,
	"Eden.Unit.Mission.Lifecycle.CanTransitionRunningToSucceeded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionCanTransitionRunningToSucceededTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Running->Succeeded"), FEdenMissionModel::CanTransition(EEdenMissionState::Running, EEdenMissionState::Succeeded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionCanTransitionRunningToFailedTest,
	"Eden.Unit.Mission.Lifecycle.CanTransitionRunningToFailed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionCanTransitionRunningToFailedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Running->Failed"), FEdenMissionModel::CanTransition(EEdenMissionState::Running, EEdenMissionState::Failed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionCanTransitionRunningToInactiveTest,
	"Eden.Unit.Mission.Lifecycle.CanTransitionRunningToInactive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionCanTransitionRunningToInactiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Running->Inactive"), FEdenMissionModel::CanTransition(EEdenMissionState::Running, EEdenMissionState::Inactive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionCanTransitionSucceededToInactiveTest,
	"Eden.Unit.Mission.Lifecycle.CanTransitionSucceededToInactive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionCanTransitionSucceededToInactiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Succeeded->Inactive"), FEdenMissionModel::CanTransition(EEdenMissionState::Succeeded, EEdenMissionState::Inactive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionCanTransitionFailedToInactiveTest,
	"Eden.Unit.Mission.Lifecycle.CanTransitionFailedToInactive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionCanTransitionFailedToInactiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Failed->Inactive"), FEdenMissionModel::CanTransition(EEdenMissionState::Failed, EEdenMissionState::Inactive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionRejectsInactiveToRunningTest,
	"Eden.Unit.Mission.Lifecycle.RejectsInactiveToRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionRejectsInactiveToRunningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("Inactive->Running is rejected"), FEdenMissionModel::CanTransition(EEdenMissionState::Inactive, EEdenMissionState::Running));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionRejectsReadyToFailedTest,
	"Eden.Unit.Mission.Lifecycle.RejectsReadyToFailed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionRejectsReadyToFailedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("Ready->Failed is rejected"), FEdenMissionModel::CanTransition(EEdenMissionState::Ready, EEdenMissionState::Failed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionRejectsSucceededToRunningTest,
	"Eden.Unit.Mission.Lifecycle.RejectsSucceededToRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionRejectsSucceededToRunningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("Succeeded->Running is rejected"), FEdenMissionModel::CanTransition(EEdenMissionState::Succeeded, EEdenMissionState::Running));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionRejectsFailedToRunningTest,
	"Eden.Unit.Mission.Lifecycle.RejectsFailedToRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionRejectsFailedToRunningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("Failed->Running is rejected"), FEdenMissionModel::CanTransition(EEdenMissionState::Failed, EEdenMissionState::Running));
	return true;
}

// ===========================================================================
// Initialization tests
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionInitializeRuntimeStateSetsReadyTest,
	"Eden.Unit.Mission.Initialization.InitializeRuntimeStateSetsReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionInitializeRuntimeStateSetsReadyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(EdenMissionModelTests::MakeValidConfig());
	TestEqual(TEXT("Mission is ready"), State.MissionState, EEdenMissionState::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionInitializeRuntimeStateSortsEventsByTimeTest,
	"Eden.Unit.Mission.Initialization.InitializeRuntimeStateSortsEventsByTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionInitializeRuntimeStateSortsEventsByTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.Events[0].TriggerTimeSeconds = 20.0f;
	
	FEdenMissionEventConfig Evt2;
	Evt2.EventId = FName("Evt2");
	Evt2.TriggerTimeSeconds = 10.0f;
	Config.Events.Add(Evt2);

	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	TestEqual(TEXT("Events are sorted"), State.EventStates[0].EventId, FName("Evt2"));
	TestEqual(TEXT("Events are sorted"), State.EventStates[1].EventId, FName("Evt1"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionInitializeRuntimeStateActivatesOnStartObjectivesTest,
	"Eden.Unit.Mission.Initialization.InitializeRuntimeStateActivatesOnStartObjectives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionInitializeRuntimeStateActivatesOnStartObjectivesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	Config.Objectives[0].bActivateOnStart = true;
	
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	TestEqual(TEXT("Objective is active on start"), State.ObjectiveStates[0].State, EEdenObjectiveState::Active);
	return true;
}

// ===========================================================================
// Timeline tests
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionStepTimelineAdvancesElapsedTimeTest,
	"Eden.Unit.Mission.Timeline.StepTimelineAdvancesElapsedTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionStepTimelineAdvancesElapsedTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionRuntimeState State;
	State.MissionState = EEdenMissionState::Running;
	FEdenMissionStepResult Result = FEdenMissionModel::StepTimeline(State, EdenMissionModelTests::MakeValidConfig(), 5.0f);
	TestEqual(TEXT("Elapsed time increased"), Result.UpdatedState.MissionElapsedTimeSeconds, 5.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionStepTimelineTriggersEventAtExactTimeTest,
	"Eden.Unit.Mission.Timeline.StepTimelineTriggersEventAtExactTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionStepTimelineTriggersEventAtExactTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	FEdenMissionStepResult Result = FEdenMissionModel::StepTimeline(State, Config, 10.0f);
	TestEqual(TEXT("Event triggered"), Result.NewlyTriggeredEventIds.Num(), 1);
	TestEqual(TEXT("EventId is correct"), Result.NewlyTriggeredEventIds[0], FName("Evt1"));
	TestEqual(TEXT("Event is Executed"), Result.UpdatedState.EventStates[0].EventState, EEdenMissionEventState::Executed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionStepTimelineTriggersEventAfterTimeTest,
	"Eden.Unit.Mission.Timeline.StepTimelineTriggersEventAfterTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionStepTimelineTriggersEventAfterTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	FEdenMissionStepResult Result = FEdenMissionModel::StepTimeline(State, Config, 15.0f);
	TestEqual(TEXT("Event triggered"), Result.NewlyTriggeredEventIds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionStepTimelineDoesNotTriggerEventBeforeTimeTest,
	"Eden.Unit.Mission.Timeline.StepTimelineDoesNotTriggerEventBeforeTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionStepTimelineDoesNotTriggerEventBeforeTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	FEdenMissionStepResult Result = FEdenMissionModel::StepTimeline(State, Config, 9.9f);
	TestEqual(TEXT("No events triggered"), Result.NewlyTriggeredEventIds.Num(), 0);
	TestEqual(TEXT("Event is still Pending"), Result.UpdatedState.EventStates[0].EventState, EEdenMissionEventState::Pending);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionStepTimelineTriggersMultipleEventsTest,
	"Eden.Unit.Mission.Timeline.StepTimelineTriggersMultipleEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionStepTimelineTriggersMultipleEventsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionEventConfig Evt2;
	Evt2.EventId = FName("Evt2");
	Evt2.TriggerTimeSeconds = 5.0f;
	Config.Events.Add(Evt2);
	
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	FEdenMissionStepResult Result = FEdenMissionModel::StepTimeline(State, Config, 15.0f);
	TestEqual(TEXT("Multiple events triggered"), Result.NewlyTriggeredEventIds.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionStepTimelineSameTimeEventsPreserveOrderTest,
	"Eden.Unit.Mission.Timeline.StepTimelineSameTimeEventsPreserveOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionStepTimelineSameTimeEventsPreserveOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig(); // Evt1 at 10.0s
	FEdenMissionEventConfig Evt2;
	Evt2.EventId = FName("Evt2");
	Evt2.TriggerTimeSeconds = 10.0f;
	Config.Events.Add(Evt2);
	
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	FEdenMissionStepResult Result = FEdenMissionModel::StepTimeline(State, Config, 10.0f);
	TestEqual(TEXT("Two events triggered"), Result.NewlyTriggeredEventIds.Num(), 2);
	TestEqual(TEXT("Original order preserved (Evt1 first)"), Result.NewlyTriggeredEventIds[0], FName("Evt1"));
	TestEqual(TEXT("Original order preserved (Evt2 second)"), Result.NewlyTriggeredEventIds[1], FName("Evt2"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionStepTimelineDoesNotDoubleTriggerTest,
	"Eden.Unit.Mission.Timeline.StepTimelineDoesNotDoubleTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionStepTimelineDoesNotDoubleTriggerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	FEdenMissionStepResult Result1 = FEdenMissionModel::StepTimeline(State, Config, 15.0f);
	TestEqual(TEXT("Event triggered"), Result1.NewlyTriggeredEventIds.Num(), 1);
	
	FEdenMissionStepResult Result2 = FEdenMissionModel::StepTimeline(Result1.UpdatedState, Config, 5.0f);
	TestEqual(TEXT("No additional trigger"), Result2.NewlyTriggeredEventIds.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionStepTimelineNoAdvanceWhenNotRunningTest,
	"Eden.Unit.Mission.Timeline.StepTimelineNoAdvanceWhenNotRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionStepTimelineNoAdvanceWhenNotRunningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config); // Ready
	
	FEdenMissionStepResult Result = FEdenMissionModel::StepTimeline(State, Config, 15.0f);
	TestEqual(TEXT("Time did not advance"), Result.UpdatedState.MissionElapsedTimeSeconds, 0.0f);
	TestEqual(TEXT("No events triggered"), Result.NewlyTriggeredEventIds.Num(), 0);
	return true;
}

// ===========================================================================
// Objective tests
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionActivateObjectiveTransitionsPendingToActiveTest,
	"Eden.Unit.Mission.Objective.ActivateObjectiveTransitionsPendingToActive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionActivateObjectiveTransitionsPendingToActiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj1"));
	TestEqual(TEXT("Objective is Active"), State.ObjectiveStates[0].State, EEdenObjectiveState::Active);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionActivateObjectiveIgnoresNonPendingObjectiveTest,
	"Eden.Unit.Mission.Objective.ActivateObjectiveIgnoresNonPendingObjective",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionActivateObjectiveIgnoresNonPendingObjectiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj1"));
	State = FEdenMissionModel::CompleteObjective(State, FName("Obj1"));
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj1")); // Should be ignored
	TestEqual(TEXT("Objective remains Completed"), State.ObjectiveStates[0].State, EEdenObjectiveState::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionCompleteObjectiveTransitionsActiveToCompletedTest,
	"Eden.Unit.Mission.Objective.CompleteObjectiveTransitionsActiveToCompleted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionCompleteObjectiveTransitionsActiveToCompletedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj1"));
	State = FEdenMissionModel::CompleteObjective(State, FName("Obj1"));
	TestEqual(TEXT("Objective is Completed"), State.ObjectiveStates[0].State, EEdenObjectiveState::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionFailObjectiveTransitionsActiveToFailedTest,
	"Eden.Unit.Mission.Objective.FailObjectiveTransitionsActiveToFailed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionFailObjectiveTransitionsActiveToFailedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj1"));
	State = FEdenMissionModel::FailObjective(State, FName("Obj1"));
	TestEqual(TEXT("Objective is Failed"), State.ObjectiveStates[0].State, EEdenObjectiveState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionObjectiveOperationIgnoresUnknownIdTest,
	"Eden.Unit.Mission.Objective.ObjectiveOperationIgnoresUnknownId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionObjectiveOperationIgnoresUnknownIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	
	State = FEdenMissionModel::ActivateObjective(State, FName("UnknownObj"));
	TestEqual(TEXT("Known objective remains Pending"), State.ObjectiveStates[0].State, EEdenObjectiveState::Pending);
	return true;
}

// ===========================================================================
// Outcome tests
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionEvaluateOutcomeReturnsRunningWhenInProgressTest,
	"Eden.Unit.Mission.Outcome.EvaluateOutcomeReturnsRunningWhenInProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionEvaluateOutcomeReturnsRunningWhenInProgressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	EEdenMissionState Outcome = FEdenMissionModel::EvaluateOutcome(State, Config);
	TestEqual(TEXT("Outcome is Running"), Outcome, EEdenMissionState::Running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionEvaluateOutcomeReturnsSucceededWhenAllRequiredCompletedTest,
	"Eden.Unit.Mission.Outcome.EvaluateOutcomeReturnsSucceededWhenAllRequiredCompleted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionEvaluateOutcomeReturnsSucceededWhenAllRequiredCompletedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj1"));
	State = FEdenMissionModel::CompleteObjective(State, FName("Obj1"));
	
	EEdenMissionState Outcome = FEdenMissionModel::EvaluateOutcome(State, Config);
	TestEqual(TEXT("Outcome is Succeeded"), Outcome, EEdenMissionState::Succeeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionEvaluateOutcomeReturnsFailedWhenAnyRequiredFailedTest,
	"Eden.Unit.Mission.Outcome.EvaluateOutcomeReturnsFailedWhenAnyRequiredFailed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionEvaluateOutcomeReturnsFailedWhenAnyRequiredFailedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionObjectiveConfig Obj2;
	Obj2.ObjectiveId = FName("Obj2");
	Obj2.bRequired = true;
	Config.Objectives.Add(Obj2);

	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj1"));
	State = FEdenMissionModel::FailObjective(State, FName("Obj1")); // Fails one required
	
	EEdenMissionState Outcome = FEdenMissionModel::EvaluateOutcome(State, Config);
	TestEqual(TEXT("Outcome is Failed"), Outcome, EEdenMissionState::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionEvaluateOutcomeIgnoresOptionalObjectivesTest,
	"Eden.Unit.Mission.Outcome.EvaluateOutcomeIgnoresOptionalObjectives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionEvaluateOutcomeIgnoresOptionalObjectivesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionObjectiveConfig Obj2;
	Obj2.ObjectiveId = FName("Obj2");
	Obj2.bRequired = false; // Optional
	Config.Objectives.Add(Obj2);

	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Running;
	
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj1"));
	State = FEdenMissionModel::CompleteObjective(State, FName("Obj1")); // Required completed
	State = FEdenMissionModel::ActivateObjective(State, FName("Obj2"));
	State = FEdenMissionModel::FailObjective(State, FName("Obj2")); // Optional failed
	
	EEdenMissionState Outcome = FEdenMissionModel::EvaluateOutcome(State, Config);
	TestEqual(TEXT("Outcome is Succeeded despite optional failure"), Outcome, EEdenMissionState::Succeeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionEvaluateOutcomeSucceededIsTerminalTest,
	"Eden.Unit.Mission.Outcome.EvaluateOutcomeSucceededIsTerminal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionEvaluateOutcomeSucceededIsTerminalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Succeeded;
	
	EEdenMissionState Outcome = FEdenMissionModel::EvaluateOutcome(State, Config);
	TestEqual(TEXT("Succeeded remains Succeeded"), Outcome, EEdenMissionState::Succeeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionEvaluateOutcomeFailedIsTerminalTest,
	"Eden.Unit.Mission.Outcome.EvaluateOutcomeFailedIsTerminal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionEvaluateOutcomeFailedIsTerminalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionDefinitionConfig Config = EdenMissionModelTests::MakeValidConfig();
	FEdenMissionRuntimeState State = FEdenMissionModel::InitializeRuntimeState(Config);
	State.MissionState = EEdenMissionState::Failed;
	
	EEdenMissionState Outcome = FEdenMissionModel::EvaluateOutcome(State, Config);
	TestEqual(TEXT("Failed remains Failed"), Outcome, EEdenMissionState::Failed);
	return true;
}

// ===========================================================================
// Reset tests
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenMissionResetRuntimeStateClearsAllTest,
	"Eden.Unit.Mission.Reset.ResetRuntimeStateClearsAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenMissionResetRuntimeStateClearsAllTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenMissionRuntimeState State = FEdenMissionModel::ResetRuntimeState();
	TestEqual(TEXT("State is Inactive"), State.MissionState, EEdenMissionState::Inactive);
	TestEqual(TEXT("Phase is Nominal"), State.MissionPhase, EEdenMissionPhase::Nominal);
	TestEqual(TEXT("Elapsed time is zero"), State.MissionElapsedTimeSeconds, 0.0f);
	TestEqual(TEXT("Events cleared"), State.EventStates.Num(), 0);
	TestEqual(TEXT("Objectives cleared"), State.ObjectiveStates.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
