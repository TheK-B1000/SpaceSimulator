// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdvisoryModel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenOsAdvisoryTests
{
	FEdenTelemetrySnapshot MakeSnapshot(
		int64 SequenceNumber,
		float SimulationTimeSeconds,
		float TemperatureCelsius,
		float ChargeFraction,
		float FuelFraction,
		EEdenMissionState MissionState = EEdenMissionState::Running)
	{
		FEdenTelemetrySnapshot Snapshot;
		Snapshot.SequenceNumber = SequenceNumber;
		Snapshot.SimulationTimeSeconds = SimulationTimeSeconds;
		Snapshot.MissionElapsedTimeSeconds = SimulationTimeSeconds;
		Snapshot.Thermal.TemperatureCelsius = TemperatureCelsius;
		Snapshot.Power.ChargeFraction = ChargeFraction;
		Snapshot.Fuel.FuelFraction = FuelFraction;
		Snapshot.Mission.MissionState = MissionState;
		Snapshot.Mission.MissionPhase = EEdenMissionPhase::Impact;
		Snapshot.Mission.ActiveMissionId = FName("SolarEventEmergency");
		return Snapshot;
	}

	FEdenTelemetryEvent MakeEvent(int64 SequenceNumber, EEdenTelemetryEventType EventType)
	{
		FEdenTelemetryEvent Event;
		Event.SequenceNumber = SequenceNumber;
		Event.EventType = EventType;
		Event.SourceSystem = FName("Test");
		return Event;
	}

	/**
	 * Baseline input: mission Running, Advisory mode, one settled snapshot, and cursors positioned
	 * so the heartbeat is NOT due. Event triggers are therefore isolated in trigger tests.
	 */
	struct FInputFixture
	{
		TArray<FEdenTelemetryEvent> Events;
		TArray<FEdenTelemetrySnapshot> Snapshots;

		FInputFixture()
		{
			Snapshots.Add(MakeSnapshot(100, 10.0f, 70.0f, 0.5f, 0.8f));
		}

		FEdenOsAdvisoryEvaluationInput Build()
		{
			FEdenOsAdvisoryEvaluationInput Input;
			Input.Events = Events;
			Input.Snapshots = Snapshots;
			Input.SessionId = TEXT("advisory-session");
			Input.SimulationTimeSeconds = Snapshots.Num() > 0 ? Snapshots.Last().SimulationTimeSeconds : 10.0f;
			Input.MissionState = Snapshots.Num() > 0
				? Snapshots.Last().Mission.MissionState
				: EEdenMissionState::Running;
			Input.AuthorityMode = EEdenOsAuthorityMode::Advisory;
			Input.HeartbeatIntervalSimulationSeconds = 5.0f;
			Input.LastEvaluatedSequence = 99;
			Input.LastEvaluationSimulationSeconds = 9.0f;
			Input.bHasEvaluatedBefore = true;
			return Input;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryHeartbeatUsesSimulationTimeTest,
	"Eden.Unit.EdenOs.Advisory.HeartbeatUsesSimulationTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryHeartbeatUsesSimulationTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// Heartbeat is a pure function of simulation time deltas. Just below the interval is not due.
	TestFalse(
		TEXT("4.9s of simulation time does not reach a 5.0s heartbeat"),
		FEdenOsAdvisoryModel::IsHeartbeatDue(14.9f, 10.0f, true, 5.0f));

	TestTrue(
		TEXT("Exactly 5.0s of simulation time is due"),
		FEdenOsAdvisoryModel::IsHeartbeatDue(15.0f, 10.0f, true, 5.0f));

	TestTrue(
		TEXT("Beyond the interval is due"),
		FEdenOsAdvisoryModel::IsHeartbeatDue(21.0f, 10.0f, true, 5.0f));

	// A paused simulation does not advance simulation time, so no heartbeat can become due.
	TestFalse(
		TEXT("Unchanged simulation time never becomes due"),
		FEdenOsAdvisoryModel::IsHeartbeatDue(10.0f, 10.0f, true, 5.0f));

	// The first evaluation of a mission is always due regardless of elapsed time.
	TestTrue(
		TEXT("First evaluation is always due"),
		FEdenOsAdvisoryModel::IsHeartbeatDue(0.0f, 0.0f, false, 5.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryHeartbeatOnlyWhileMissionRunningTest,
	"Eden.Unit.EdenOs.Advisory.HeartbeatOnlyWhileMissionRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryHeartbeatOnlyWhileMissionRunningTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const EEdenMissionState NonRunningStates[] = {
		EEdenMissionState::Inactive,
		EEdenMissionState::Ready,
		EEdenMissionState::Succeeded,
		EEdenMissionState::Failed
	};

	for (const EEdenMissionState State : NonRunningStates)
	{
		EdenOsAdvisoryTests::FInputFixture Fixture;
		Fixture.Snapshots.Reset();
		Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(100, 100.0f, 70.0f, 0.5f, 0.8f, State));

		FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
		// Heartbeat would be long overdue if the mission were still running.
		Input.LastEvaluationSimulationSeconds = 0.0f;

		const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);
		TestFalse(
			FString::Printf(TEXT("Terminal/inactive state %d does not evaluate"), static_cast<int32>(State)),
			Result.bShouldEvaluate);
	}

	EdenOsAdvisoryTests::FInputFixture RunningFixture;
	FEdenOsAdvisoryEvaluationInput RunningInput = RunningFixture.Build();
	RunningInput.LastEvaluationSimulationSeconds = 0.0f;
	TestTrue(
		TEXT("Running mission with overdue heartbeat evaluates"),
		FEdenOsAdvisoryModel::Evaluate(RunningInput).bShouldEvaluate);

	return true;
}

namespace EdenOsAdvisoryTests
{
	/** Drives one event type through Evaluate and asserts the single expected reason. */
	bool RunSingleTriggerCase(
		FAutomationTestBase& Test,
		EEdenTelemetryEventType EventType,
		EEdenOsAdvisoryTriggerReason ExpectedReason,
		const TCHAR* CaseName)
	{
		FInputFixture Fixture;
		Fixture.Events.Add(MakeEvent(100, EventType));

		const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Fixture.Build());

		Test.TestTrue(FString::Printf(TEXT("%s triggers evaluation"), CaseName), Result.bShouldEvaluate);
		Test.TestEqual(
			FString::Printf(TEXT("%s produces exactly one reason"), CaseName),
			Result.Context.TriggerReasons.Num(),
			1);

		if (Result.Context.TriggerReasons.Num() == 1)
		{
			Test.TestEqual(
				FString::Printf(TEXT("%s reason matches"), CaseName),
				Result.Context.TriggerReasons[0],
				ExpectedReason);
		}

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryPhaseTransitionTriggersEvaluationTest,
	"Eden.Unit.EdenOs.Advisory.PhaseTransitionTriggersEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryPhaseTransitionTriggersEvaluationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	return EdenOsAdvisoryTests::RunSingleTriggerCase(
		*this,
		EEdenTelemetryEventType::PhaseChanged,
		EEdenOsAdvisoryTriggerReason::MissionPhaseTransition,
		TEXT("PhaseChanged"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryAlertTransitionTriggersEvaluationTest,
	"Eden.Unit.EdenOs.Advisory.AlertTransitionTriggersEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryAlertTransitionTriggersEvaluationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::RunSingleTriggerCase(
		*this,
		EEdenTelemetryEventType::AlertRaised,
		EEdenOsAdvisoryTriggerReason::AlertTransition,
		TEXT("AlertRaised"));

	return EdenOsAdvisoryTests::RunSingleTriggerCase(
		*this,
		EEdenTelemetryEventType::AlertCleared,
		EEdenOsAdvisoryTriggerReason::AlertTransition,
		TEXT("AlertCleared"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryObjectiveTransitionTriggersEvaluationTest,
	"Eden.Unit.EdenOs.Advisory.ObjectiveTransitionTriggersEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryObjectiveTransitionTriggersEvaluationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	return EdenOsAdvisoryTests::RunSingleTriggerCase(
		*this,
		EEdenTelemetryEventType::ObjectiveStateChanged,
		EEdenOsAdvisoryTriggerReason::ObjectiveTransition,
		TEXT("ObjectiveStateChanged"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryOperatorActionTriggersEvaluationTest,
	"Eden.Unit.EdenOs.Advisory.OperatorActionTriggersEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryOperatorActionTriggersEvaluationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::RunSingleTriggerCase(
		*this,
		EEdenTelemetryEventType::OperatorCommandIssued,
		EEdenOsAdvisoryTriggerReason::OperatorAction,
		TEXT("OperatorCommandIssued"));

	// Resource transitions are recorded by telemetry but are not locked advisory triggers.
	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(100, EEdenTelemetryEventType::ResourceStateTransition));
	TestFalse(
		TEXT("ResourceStateTransition is not a locked trigger"),
		FEdenOsAdvisoryModel::Evaluate(Fixture.Build()).bShouldEvaluate);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisorySameStepReasonsCoalesceTest,
	"Eden.Unit.EdenOs.Advisory.SameStepReasonsCoalesce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisorySameStepReasonsCoalesceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	// Deliberately out of canonical order, with a duplicate objective transition.
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(100, EEdenTelemetryEventType::ObjectiveStateChanged));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(101, EEdenTelemetryEventType::AlertRaised));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(102, EEdenTelemetryEventType::PhaseChanged));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(103, EEdenTelemetryEventType::ObjectiveStateChanged));

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Fixture.Build());

	TestTrue(TEXT("Evaluation occurs"), Result.bShouldEvaluate);
	TestEqual(TEXT("Three distinct reasons after coalescing"), Result.Context.TriggerReasons.Num(), 3);

	if (Result.Context.TriggerReasons.Num() == 3)
	{
		// Canonical ascending-enum order, independent of arrival order.
		TestEqual(
			TEXT("Reason 0 is MissionPhaseTransition"),
			Result.Context.TriggerReasons[0],
			EEdenOsAdvisoryTriggerReason::MissionPhaseTransition);
		TestEqual(
			TEXT("Reason 1 is AlertTransition"),
			Result.Context.TriggerReasons[1],
			EEdenOsAdvisoryTriggerReason::AlertTransition);
		TestEqual(
			TEXT("Reason 2 is ObjectiveTransition"),
			Result.Context.TriggerReasons[2],
			EEdenOsAdvisoryTriggerReason::ObjectiveTransition);
	}

	// Cursor advances past every event folded into this single evaluation.
	TestEqual(TEXT("Cursor advances to highest observed sequence"), Result.NewLastEvaluatedSequence, (int64)103);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryHeartbeatCoalescesWithEventTriggersTest,
	"Eden.Unit.EdenOs.Advisory.HeartbeatCoalescesWithEventTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryHeartbeatCoalescesWithEventTriggersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(100, EEdenTelemetryEventType::PhaseChanged));

	FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
	// Make the heartbeat due on the very same step as the phase transition.
	Input.LastEvaluationSimulationSeconds = 5.0f;
	Input.SimulationTimeSeconds = 10.0f;

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);

	TestTrue(TEXT("Evaluation occurs"), Result.bShouldEvaluate);
	TestEqual(TEXT("One evaluation carrying both reasons"), Result.Context.TriggerReasons.Num(), 2);
	TestTrue(
		TEXT("Phase reason preserved"),
		Result.Context.TriggerReasons.Contains(EEdenOsAdvisoryTriggerReason::MissionPhaseTransition));
	TestTrue(
		TEXT("Heartbeat reason preserved"),
		Result.Context.TriggerReasons.Contains(EEdenOsAdvisoryTriggerReason::Heartbeat));

	if (Result.Context.TriggerReasons.Num() == 2)
	{
		// Heartbeat sorts last under canonical ordering.
		TestEqual(
			TEXT("Heartbeat is ordered last"),
			Result.Context.TriggerReasons[1],
			EEdenOsAdvisoryTriggerReason::Heartbeat);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryObserveModeDoesNotEvaluateTest,
	"Eden.Unit.EdenOs.Advisory.ObserveModeDoesNotEvaluate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryObserveModeDoesNotEvaluateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestFalse(
		TEXT("Observe is not permitted to evaluate"),
		FEdenOsAdvisoryModel::IsAdvisoryEvaluationPermitted(EEdenOsAuthorityMode::Observe));
	TestTrue(
		TEXT("Advisory is permitted"),
		FEdenOsAdvisoryModel::IsAdvisoryEvaluationPermitted(EEdenOsAuthorityMode::Advisory));
	TestTrue(
		TEXT("AuthorizedControl is permitted for L advisory+automation chain"),
		FEdenOsAdvisoryModel::IsAdvisoryEvaluationPermitted(EEdenOsAuthorityMode::AuthorizedControl));

	// Observe must not evaluate even when every trigger would otherwise fire.
	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(100, EEdenTelemetryEventType::PhaseChanged));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(101, EEdenTelemetryEventType::AlertRaised));

	FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
	Input.AuthorityMode = EEdenOsAuthorityMode::Observe;
	Input.LastEvaluationSimulationSeconds = 0.0f;
	Input.bHasEvaluatedBefore = false;

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);

	TestFalse(TEXT("Observe performs no evaluation"), Result.bShouldEvaluate);
	TestFalse(TEXT("Observe builds no context"), Result.Context.bIsValid);
	TestEqual(TEXT("Observe leaves the sequence cursor untouched"), Result.NewLastEvaluatedSequence, (int64)99);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryContextReadsTelemetryHistoryTest,
	"Eden.Unit.EdenOs.Advisory.ContextReadsTelemetryHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryContextReadsTelemetryHistoryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Snapshots.Reset();
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(100, 8.0f, 60.0f, 0.80f, 0.90f));
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(101, 9.0f, 65.0f, 0.70f, 0.87f));
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(102, 10.0f, 71.0f, 0.62f, 0.85f));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(103, EEdenTelemetryEventType::PhaseChanged));

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Fixture.Build());
	TestTrue(TEXT("Evaluation occurs"), Result.bShouldEvaluate);

	const FEdenOsAdvisoryContext& Context = Result.Context;

	// Settled facts come from the newest telemetry snapshot.
	TestEqual(TEXT("Temperature from newest snapshot"), Context.Thermal.TemperatureCelsius, 71.0f);
	TestEqual(TEXT("Charge fraction from newest snapshot"), Context.Power.ChargeFraction, 0.62f);
	TestEqual(TEXT("Fuel fraction from newest snapshot"), Context.Fuel.FuelFraction, 0.85f);
	TestEqual(TEXT("Mission phase from newest snapshot"), Context.MissionPhase, EEdenMissionPhase::Impact);
	TestEqual(TEXT("Mission id from newest snapshot"), Context.ActiveMissionId, FName("SolarEventEmergency"));
	TestEqual(TEXT("Session id carried"), Context.SessionId, FString(TEXT("advisory-session")));

	// Trends are derived from the recorded window, oldest to newest.
	TestTrue(TEXT("Temperature trend has data"), Context.TemperatureCelsiusTrend.bHasData);
	TestEqual(TEXT("Temperature earliest"), Context.TemperatureCelsiusTrend.EarliestValue, 60.0f);
	TestEqual(TEXT("Temperature latest"), Context.TemperatureCelsiusTrend.LatestValue, 71.0f);
	TestEqual(TEXT("Temperature rising by 11C"), Context.TemperatureCelsiusTrend.Delta, 11.0f);
	TestEqual(TEXT("Temperature sample count"), Context.TemperatureCelsiusTrend.SampleCount, 3);

	TestEqual(TEXT("Battery falling"), Context.BatteryChargeFractionTrend.Delta, 0.62f - 0.80f);
	TestEqual(TEXT("Fuel falling"), Context.FuelFractionTrend.Delta, 0.85f - 0.90f);

	TestEqual(TEXT("Recent snapshots carried"), Context.RecentSnapshots.Num(), 3);
	TestEqual(TEXT("Recent events carried"), Context.RecentEvents.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryContextIsBoundedTest,
	"Eden.Unit.EdenOs.Advisory.ContextIsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryContextIsBoundedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Snapshots.Reset();
	for (int32 Index = 0; Index < 50; ++Index)
	{
		Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(
			100 + Index,
			static_cast<float>(Index),
			50.0f + static_cast<float>(Index),
			0.9f,
			0.9f));
	}
	for (int32 Index = 0; Index < 40; ++Index)
	{
		Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(
			200 + Index,
			EEdenTelemetryEventType::PhaseChanged));
	}

	FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
	Input.Bounds.MaxSnapshots = 5;
	Input.Bounds.MaxEvents = 3;

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);
	const FEdenOsAdvisoryContext& Context = Result.Context;

	TestEqual(TEXT("Snapshot window respects bound"), Context.RecentSnapshots.Num(), 5);
	TestEqual(TEXT("Event window respects bound"), Context.RecentEvents.Num(), 3);
	TestTrue(TEXT("Truncation is observable"), Context.bContextTruncated);

	// Newest entries are retained; oldest are dropped first.
	TestEqual(
		TEXT("Newest snapshot retained"),
		Context.RecentSnapshots.Last().SequenceNumber,
		(int64)149);
	TestEqual(
		TEXT("Oldest retained snapshot is newest-5"),
		Context.RecentSnapshots[0].SequenceNumber,
		(int64)145);
	TestEqual(
		TEXT("Newest event retained"),
		Context.RecentEvents.Last().SequenceNumber,
		(int64)239);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryContextIsImmutableTest,
	"Eden.Unit.EdenOs.Advisory.ContextIsImmutable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryContextIsImmutableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(100, EEdenTelemetryEventType::PhaseChanged));

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Fixture.Build());
	TestTrue(TEXT("Evaluation occurs"), Result.bShouldEvaluate);

	const FEdenOsAdvisoryContext ContextA = Result.Context;
	const float CapturedTemperature = ContextA.Thermal.TemperatureCelsius;
	const int32 CapturedSnapshotCount = ContextA.RecentSnapshots.Num();
	const int32 CapturedEventCount = ContextA.RecentEvents.Num();

	// Advance the underlying history the way a later simulation step would.
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(200, 20.0f, 999.0f, 0.1f, 0.1f));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(201, EEdenTelemetryEventType::AlertRaised));
	Fixture.Snapshots.Reset();
	Fixture.Events.Reset();

	TestEqual(TEXT("Temperature unchanged after history advanced"), ContextA.Thermal.TemperatureCelsius, CapturedTemperature);
	TestEqual(TEXT("Snapshot window unchanged"), ContextA.RecentSnapshots.Num(), CapturedSnapshotCount);
	TestEqual(TEXT("Event window unchanged"), ContextA.RecentEvents.Num(), CapturedEventCount);
	TestTrue(TEXT("Context remains valid"), ContextA.bIsValid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisorySameHistoryBuildsDeterministicContextTest,
	"Eden.Unit.EdenOs.Advisory.SameHistoryBuildsDeterministicContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisorySameHistoryBuildsDeterministicContextTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Snapshots.Reset();
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(100, 8.0f, 60.0f, 0.8f, 0.9f));
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(101, 10.0f, 72.0f, 0.6f, 0.85f));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(102, EEdenTelemetryEventType::AlertRaised));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(103, EEdenTelemetryEventType::PhaseChanged));

	const FEdenOsAdvisoryEvaluationResult First = FEdenOsAdvisoryModel::Evaluate(Fixture.Build());
	const FEdenOsAdvisoryEvaluationResult Second = FEdenOsAdvisoryModel::Evaluate(Fixture.Build());

	TestTrue(TEXT("Both evaluations occur"), First.bShouldEvaluate && Second.bShouldEvaluate);
	TestEqual(TEXT("Same reason count"), Second.Context.TriggerReasons.Num(), First.Context.TriggerReasons.Num());

	for (int32 Index = 0; Index < First.Context.TriggerReasons.Num(); ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("Reason %d matches"), Index),
			Second.Context.TriggerReasons[Index],
			First.Context.TriggerReasons[Index]);
	}

	TestEqual(TEXT("Same evaluated sequence"), Second.Context.EvaluatedThroughSequence, First.Context.EvaluatedThroughSequence);
	TestEqual(TEXT("Same temperature"), Second.Context.Thermal.TemperatureCelsius, First.Context.Thermal.TemperatureCelsius);
	TestEqual(TEXT("Same temperature delta"), Second.Context.TemperatureCelsiusTrend.Delta, First.Context.TemperatureCelsiusTrend.Delta);
	TestEqual(TEXT("Same snapshot window size"), Second.Context.RecentSnapshots.Num(), First.Context.RecentSnapshots.Num());
	TestEqual(TEXT("Same event window size"), Second.Context.RecentEvents.Num(), First.Context.RecentEvents.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryEvaluationAndContextTimesCanDifferTest,
	"Eden.Unit.EdenOs.Advisory.EvaluationAndContextTimesCanDiffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryEvaluationAndContextTimesCanDifferTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// Newest telemetry snapshot is from 0.5s; the triggering event lands at 0.7s. The evaluation is
	// due now, but the freshest settled observation available is older.
	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Snapshots.Reset();
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(100, 0.5f, 70.0f, 0.5f, 0.8f));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(101, EEdenTelemetryEventType::ObjectiveStateChanged));

	FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
	Input.SimulationTimeSeconds = 0.7f;

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);
	TestTrue(TEXT("Evaluation occurs"), Result.bShouldEvaluate);

	TestEqual(TEXT("Evaluation time is the true due time"), Result.Context.SimulationTimeSeconds, 0.7f);
	TestEqual(
		TEXT("Context snapshot time is the selected snapshot's own timestamp"),
		Result.Context.ContextSnapshotSimulationTimeSeconds,
		0.5f);
	TestTrue(
		TEXT("The two timestamps genuinely diverge"),
		Result.Context.SimulationTimeSeconds > Result.Context.ContextSnapshotSimulationTimeSeconds);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryContextSnapshotTimestampMatchesSelectedSnapshotTest,
	"Eden.Unit.EdenOs.Advisory.ContextSnapshotTimestampMatchesSelectedSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryContextSnapshotTimestampMatchesSelectedSnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Snapshots.Reset();
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(100, 1.0f, 60.0f, 0.9f, 0.9f));
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(101, 1.5f, 65.0f, 0.8f, 0.88f));
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(102, 2.0f, 70.0f, 0.7f, 0.86f));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(103, EEdenTelemetryEventType::AlertRaised));

	FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
	Input.SimulationTimeSeconds = 2.3f;
	// Bound the window so the newest snapshot is still the selected one after truncation.
	Input.Bounds.MaxSnapshots = 2;

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);

	TestEqual(
		TEXT("Snapshot timestamp matches the newest retained snapshot"),
		Result.Context.ContextSnapshotSimulationTimeSeconds,
		2.0f);
	TestEqual(TEXT("Evaluation time is unaffected by truncation"), Result.Context.SimulationTimeSeconds, 2.3f);
	TestEqual(
		TEXT("Selected snapshot is the newest one"),
		Result.Context.RecentSnapshots.Last().SimulationTimeSeconds,
		2.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryHeartbeatUsesTrueSimulationTimeNotSnapshotCadenceTest,
	"Eden.Unit.EdenOs.Advisory.HeartbeatUsesTrueSimulationTimeNotSnapshotCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryHeartbeatUsesTrueSimulationTimeNotSnapshotCadenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// Snapshots are sparse (every 2.0s) while the heartbeat interval is 5.0s. If cadence were driven
	// by snapshot timestamps, the heartbeat would only ever become due at snapshot boundaries.
	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Snapshots.Reset();
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(100, 4.0f, 70.0f, 0.5f, 0.8f));

	FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
	Input.HeartbeatIntervalSimulationSeconds = 5.0f;
	Input.LastEvaluationSimulationSeconds = 0.0f;
	Input.bHasEvaluatedBefore = true;

	// Stale snapshot at 4.0s, true simulation time 5.0s: the heartbeat is due on true time.
	Input.SimulationTimeSeconds = 5.0f;
	const FEdenOsAdvisoryEvaluationResult DueResult = FEdenOsAdvisoryModel::Evaluate(Input);

	TestTrue(TEXT("Heartbeat becomes due on true simulation time"), DueResult.bShouldEvaluate);
	TestEqual(TEXT("Evaluation stamped with true time"), DueResult.Context.SimulationTimeSeconds, 5.0f);
	TestEqual(TEXT("Context still reports the stale snapshot time"), DueResult.Context.ContextSnapshotSimulationTimeSeconds, 4.0f);
	TestTrue(
		TEXT("Heartbeat is the reason"),
		DueResult.Context.TriggerReasons.Contains(EEdenOsAdvisoryTriggerReason::Heartbeat));

	// Just under the interval it must not be due, even though the same stale snapshot is present.
	Input.SimulationTimeSeconds = 4.9f;
	TestFalse(
		TEXT("Not yet due just below the interval"),
		FEdenOsAdvisoryModel::Evaluate(Input).bShouldEvaluate);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisorySnapshotDecimationDoesNotDelayHeartbeatTest,
	"Eden.Unit.EdenOs.Advisory.SnapshotDecimationDoesNotDelayHeartbeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisorySnapshotDecimationDoesNotDelayHeartbeatTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// No new snapshot arrives at or near the due time; the newest is far older.
	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Snapshots.Reset();
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(100, 2.0f, 70.0f, 0.5f, 0.8f));

	FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
	Input.HeartbeatIntervalSimulationSeconds = 5.0f;
	Input.LastEvaluationSimulationSeconds = 2.0f;
	Input.bHasEvaluatedBefore = true;
	Input.SimulationTimeSeconds = 7.0f;

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);

	TestTrue(TEXT("Heartbeat fires without a fresh snapshot"), Result.bShouldEvaluate);
	TestEqual(TEXT("Due exactly one interval after the last evaluation"), Result.Context.SimulationTimeSeconds, 7.0f);
	TestEqual(TEXT("Context carries the older snapshot"), Result.Context.ContextSnapshotSimulationTimeSeconds, 2.0f);
	TestEqual(TEXT("Next evaluation cursor uses true time"), Result.NewLastEvaluationSimulationSeconds, 7.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryHeartbeatStillCoalescesWithSameStepTriggerTest,
	"Eden.Unit.EdenOs.Advisory.HeartbeatStillCoalescesWithSameStepTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryHeartbeatStillCoalescesWithSameStepTriggerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Snapshots.Reset();
	Fixture.Snapshots.Add(EdenOsAdvisoryTests::MakeSnapshot(100, 5.5f, 70.0f, 0.5f, 0.8f));
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(101, EEdenTelemetryEventType::ObjectiveStateChanged));

	FEdenOsAdvisoryEvaluationInput Input = Fixture.Build();
	Input.HeartbeatIntervalSimulationSeconds = 5.0f;
	Input.LastEvaluationSimulationSeconds = 1.0f;
	Input.bHasEvaluatedBefore = true;
	Input.SimulationTimeSeconds = 6.0f;

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);

	TestTrue(TEXT("Evaluation occurs"), Result.bShouldEvaluate);
	TestEqual(TEXT("Exactly one evaluation carrying both reasons"), Result.Context.TriggerReasons.Num(), 2);
	TestTrue(
		TEXT("Objective reason preserved"),
		Result.Context.TriggerReasons.Contains(EEdenOsAdvisoryTriggerReason::ObjectiveTransition));
	TestTrue(
		TEXT("Heartbeat reason preserved"),
		Result.Context.TriggerReasons.Contains(EEdenOsAdvisoryTriggerReason::Heartbeat));
	TestEqual(TEXT("Stamped with true evaluation time"), Result.Context.SimulationTimeSeconds, 6.0f);
	TestEqual(TEXT("Snapshot time remains the snapshot's own"), Result.Context.ContextSnapshotSimulationTimeSeconds, 5.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryCursorSuppressesRepeatTriggersTest,
	"Eden.Unit.EdenOs.Advisory.CursorSuppressesAlreadyProcessedEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryCursorSuppressesRepeatTriggersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenOsAdvisoryTests::FInputFixture Fixture;
	Fixture.Events.Add(EdenOsAdvisoryTests::MakeEvent(100, EEdenTelemetryEventType::PhaseChanged));

	const FEdenOsAdvisoryEvaluationResult First = FEdenOsAdvisoryModel::Evaluate(Fixture.Build());
	TestTrue(TEXT("First evaluation occurs"), First.bShouldEvaluate);

	// Same history, cursor advanced: the already-folded event must not retrigger, and the heartbeat
	// is not yet due, so this settled step produces no evaluation at all.
	FEdenOsAdvisoryEvaluationInput SecondInput = Fixture.Build();
	SecondInput.LastEvaluatedSequence = First.NewLastEvaluatedSequence;
	SecondInput.LastEvaluationSimulationSeconds = First.NewLastEvaluationSimulationSeconds;

	const FEdenOsAdvisoryEvaluationResult Second = FEdenOsAdvisoryModel::Evaluate(SecondInput);
	TestFalse(TEXT("Processed event does not retrigger"), Second.bShouldEvaluate);

	return true;
}

#endif
