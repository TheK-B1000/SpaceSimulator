// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenAfterActionModel.h"
#include "Telemetry/EdenTelemetryExportModel.h"
#include "Telemetry/EdenTelemetrySink.h"
#include "Telemetry/EdenTelemetryTypes.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

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

	FEdenTelemetryEvent CriticalAlert;
	CriticalAlert.EventType = EEdenTelemetryEventType::AlertRaised;
	CriticalAlert.Detail = TEXT("[EEdenAlertSeverity::Critical] Battery critical");
	Events.Add(CriticalAlert);

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
	Last.Mission.ActiveMissionId = TEXT("SolarCrisis");
	Last.Fuel.FuelFraction = 0.87f;
	FEdenMissionObjectiveRuntime Objective;
	Objective.ObjectiveId = TEXT("SurviveSolarEvent");
	Objective.State = EEdenObjectiveState::Completed;
	Last.Mission.ObjectiveSnapshots.Add(Objective);
	Snapshots.Add(First);
	Snapshots.Add(Last);

	const FEdenAfterActionResult Result = FEdenAfterActionModel::Build(Events, Snapshots, Metadata);
	TestTrue(TEXT("Duration"), FMath::IsNearlyEqual(Result.DurationSeconds, 50.0f));
	TestTrue(TEXT("Peak temp"), FMath::IsNearlyEqual(Result.PeakRecordedSimulationTemperatureCelsius, 88.5f));
	TestTrue(TEXT("Min battery"), FMath::IsNearlyEqual(Result.LowestRecordedBatteryChargeFraction, 0.22f));
	TestTrue(TEXT("Final fuel"), FMath::IsNearlyEqual(Result.FinalFuelFraction, 0.87f));
	TestTrue(TEXT("Truncated"), Result.bHistoryTruncated);
	TestEqual(TEXT("Operator commands"), Result.OperatorCommandCount, 1);
	TestEqual(TEXT("Critical alerts"), Result.CriticalAlertCount, 1);
	TestEqual(TEXT("Final state"), Result.FinalMissionState, EEdenMissionState::Succeeded);
	TestEqual(TEXT("Mission id"), Result.MissionId, FName(TEXT("SolarCrisis")));
	TestEqual(TEXT("Objective count"), Result.Objectives.Num(), 1);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetryExportSchemaV1ContainsContractFieldsTest,
	"Eden.Unit.Telemetry.Export.SchemaV1ContainsContractFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetryExportSchemaV1ContainsContractFieldsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenTelemetrySessionMetadata Metadata;
	Metadata.DroppedSnapshotCount = 1;
	Metadata.DroppedEventCount = 0;
	Metadata.bHistoryTruncated = true;
	Metadata.PeakTemperatureCelsius = 73.4f;
	Metadata.MinimumBatteryChargeFraction = 0.44f;
	Metadata.MinimumFuelFraction = 0.87f;
	Metadata.SnapshotIntervalSeconds = 0.5f;

	TArray<FEdenTelemetryEvent> Events;
	FEdenTelemetryEvent Started;
	Started.SequenceNumber = 1;
	Started.SimulationTimeSeconds = 0.0f;
	Started.MissionElapsedTimeSeconds = 0.0f;
	Started.EventType = EEdenTelemetryEventType::MissionStarted;
	Started.SourceSystem = TEXT("Mission");
	Started.EventId = TEXT("Running");
	Started.Detail = TEXT("quote \" and \\ slash");
	Events.Add(Started);

	FEdenTelemetryEvent Succeeded;
	Succeeded.SequenceNumber = 2;
	Succeeded.SimulationTimeSeconds = 50.0f;
	Succeeded.MissionElapsedTimeSeconds = 50.0f;
	Succeeded.EventType = EEdenTelemetryEventType::MissionSucceeded;
	Succeeded.SourceSystem = TEXT("Mission");
	Succeeded.EventId = TEXT("Succeeded");
	Events.Add(Succeeded);

	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySnapshot Snapshot;
	Snapshot.SequenceNumber = 3;
	Snapshot.SimulationTimeSeconds = 50.0f;
	Snapshot.MissionElapsedTimeSeconds = 50.0f;
	Snapshot.Fuel.FuelFraction = 0.87f;
	Snapshot.Power.ChargeFraction = 0.44f;
	Snapshot.Power.GenerationKilowatts = 2.0f;
	Snapshot.Power.TotalDemandKilowatts = 5.0f;
	Snapshot.Thermal.TemperatureCelsius = 73.4f;
	Snapshot.Mission.MissionState = EEdenMissionState::Succeeded;
	Snapshot.Mission.MissionPhase = EEdenMissionPhase::Recovery;
	Snapshot.Mission.ActiveMissionId = TEXT("SolarCrisis");
	Snapshots.Add(Snapshot);

	const FString Json = FEdenTelemetryExportModel::BuildSessionJsonV1(
		Events,
		Snapshots,
		Metadata,
		TEXT("test-session-001"),
		TEXT("SolarCrisis"));

	TestTrue(TEXT("schemaVersion"), Json.Contains(TEXT("\"schemaVersion\": 1")));
	TestTrue(TEXT("session block"), Json.Contains(TEXT("\"session\"")));
	TestTrue(TEXT("sessionId"), Json.Contains(TEXT("\"sessionId\": \"test-session-001\"")));
	TestTrue(TEXT("missionId"), Json.Contains(TEXT("\"missionId\": \"SolarCrisis\"")));
	TestTrue(TEXT("outcome"), Json.Contains(TEXT("\"outcome\": \"Succeeded\"")));
	TestTrue(TEXT("integrity"), Json.Contains(TEXT("\"historyTruncated\": true")));
	TestTrue(TEXT("droppedSnapshots"), Json.Contains(TEXT("\"droppedSnapshots\": 1")));
	TestTrue(TEXT("aggregates"), Json.Contains(TEXT("\"aggregates\"")));
	TestTrue(TEXT("peakTemperature field"), Json.Contains(TEXT("\"peakTemperatureCelsius\"")));
	TestTrue(TEXT("events array"), Json.Contains(TEXT("\"events\"")));
	TestTrue(TEXT("snapshots array"), Json.Contains(TEXT("\"snapshots\"")));
	TestTrue(TEXT("escaped quote"), Json.Contains(TEXT("quote \\\" and \\\\ slash")));
	TestTrue(TEXT("event type"), Json.Contains(TEXT("\"type\": \"MissionSucceeded\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetrySinkPayloadBuildsSameSchemaAsDirectExportTest,
	"Eden.Unit.Telemetry.Sink.PayloadBuildsSameSchemaAsDirectExport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetrySinkPayloadBuildsSameSchemaAsDirectExportTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenTelemetrySessionMetadata Metadata;
	Metadata.PeakTemperatureCelsius = 91.0f;
	Metadata.MinimumBatteryChargeFraction = 0.31f;
	Metadata.MinimumFuelFraction = 0.62f;
	Metadata.SnapshotIntervalSeconds = 0.5f;

	TArray<FEdenTelemetryEvent> Events;
	FEdenTelemetryEvent Started;
	Started.SequenceNumber = 1;
	Started.EventType = EEdenTelemetryEventType::MissionStarted;
	Started.SourceSystem = TEXT("Mission");
	Started.EventId = TEXT("Running");
	Events.Add(Started);

	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySnapshot Snapshot;
	Snapshot.SequenceNumber = 2;
	Snapshot.SimulationTimeSeconds = 5.0f;
	Snapshot.Fuel.FuelFraction = 0.62f;
	Snapshot.Power.ChargeFraction = 0.31f;
	Snapshot.Thermal.TemperatureCelsius = 91.0f;
	Snapshot.Mission.ActiveMissionId = TEXT("SolarCrisis");
	Snapshot.Mission.MissionState = EEdenMissionState::Running;
	Snapshots.Add(Snapshot);

	const FString DirectJson = FEdenTelemetryExportModel::BuildSessionJsonV1(
		Events,
		Snapshots,
		Metadata,
		TEXT("payload-session"),
		TEXT("SolarCrisis"));

	const FEdenTelemetrySessionPayload Payload(
		Events,
		Snapshots,
		Metadata,
		TEXT("payload-session"),
		TEXT("SolarCrisis"));
	const FString PayloadJson = FEdenTelemetryExportModel::BuildSessionJsonV1(Payload);

	TestEqual(TEXT("Payload overload preserves schema output"), PayloadJson, DirectJson);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetryExportFileSmokeTest,
	"Eden.Unit.Telemetry.Export.FileSmokeWritesSavedTelemetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetryExportFileSmokeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	const FEdenTelemetrySessionPayload Payload(
		Events,
		Snapshots,
		FEdenTelemetrySessionMetadata(),
		TEXT("smoke-session"),
		TEXT("SolarCrisis"));

	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry"), TEXT("SinkSmoke"));
	FEdenLocalJsonTelemetrySink Sink(Directory);
	const FEdenTelemetrySinkResult Result = Sink.DeliverTelemetrySession(Payload);

	TestTrue(TEXT("Write smoke file through sink"), Result.IsSuccess());
	TestFalse(TEXT("Sink returns destination"), Result.Destination.IsEmpty());
	TestTrue(TEXT("Smoke file exists"), IFileManager::Get().FileExists(*Result.Destination));

	FString RoundTrip;
	TestTrue(TEXT("Read smoke file"), FFileHelper::LoadFileToString(RoundTrip, *Result.Destination));
	TestTrue(TEXT("Round-trip schema"), RoundTrip.Contains(TEXT("\"schemaVersion\": 1")));
	TestTrue(TEXT("Round-trip session"), RoundTrip.Contains(TEXT("\"sessionId\": \"smoke-session\"")));
	return true;
}

#endif
