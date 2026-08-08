// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenAfterActionModel.h"
#include "Telemetry/EdenTelemetryTypes.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenAfterActionModelReportsAggregatesAndTruncationTest,
	"Eden.Unit.Telemetry.AfterAction.ReportsAggregatesAndTruncation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenAfterActionModelReportsAggregatesAndTruncationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenTelemetrySessionMetadata Metadata;
	Metadata.PeakTemperatureCelsius = 88.5f;
	Metadata.MinimumBatteryChargeFraction = 0.22f;
	Metadata.MinimumFuelFraction = 0.41f;
	Metadata.SnapshotIntervalSeconds = 0.5f;
	Metadata.bHistoryTruncated = true;
	Metadata.DroppedSnapshotCount = 3;

	TArray<FEdenTelemetryEvent> Events;
	FEdenTelemetryEvent OperatorEvent;
	OperatorEvent.EventType = EEdenTelemetryEventType::OperatorCommandIssued;
	OperatorEvent.MissionElapsedTimeSeconds = 14.2f;
	Events.Add(OperatorEvent);

	FEdenTelemetryEvent SuccessEvent;
	SuccessEvent.EventType = EEdenTelemetryEventType::MissionSucceeded;
	SuccessEvent.MissionElapsedTimeSeconds = 50.0f;
	Events.Add(SuccessEvent);

	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySnapshot First;
	First.MissionElapsedTimeSeconds = 0.0f;
	FEdenTelemetrySnapshot Last;
	Last.MissionElapsedTimeSeconds = 50.0f;
	Last.Mission.MissionState = EEdenMissionState::Succeeded;
	Snapshots.Add(First);
	Snapshots.Add(Last);

	const FEdenAfterActionResult Result = FEdenAfterActionModel::Build(Events, Snapshots, Metadata);
	TestTrue(TEXT("Duration"), FMath::IsNearlyEqual(Result.DurationSeconds, 50.0f));
	TestTrue(TEXT("Peak temp"), FMath::IsNearlyEqual(Result.PeakRecordedSimulationTemperatureCelsius, 88.5f));
	TestTrue(TEXT("Min battery"), FMath::IsNearlyEqual(Result.LowestRecordedBatteryChargeFraction, 0.22f));
	TestTrue(TEXT("Truncated"), Result.bHistoryTruncated);
	TestEqual(TEXT("Operator commands"), Result.OperatorCommandCount, 1);
	TestEqual(TEXT("Final state"), Result.FinalMissionState, EEdenMissionState::Succeeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetrySessionMetadataDefaultsTest,
	"Eden.Unit.Telemetry.Types.SessionMetadataDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetrySessionMetadataDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenTelemetrySessionMetadata Metadata;
	TestEqual(TEXT("Dropped snapshots default 0"), Metadata.DroppedSnapshotCount, 0);
	TestEqual(TEXT("Dropped events default 0"), Metadata.DroppedEventCount, 0);
	TestFalse(TEXT("Not truncated by default"), Metadata.bHistoryTruncated);
	TestFalse(TEXT("Integrity ok by default"), Metadata.bEventIntegrityCompromised);
	return true;
}

#endif
