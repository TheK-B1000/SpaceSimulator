// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenAfterActionModel.h"
#include "Telemetry/EdenTelemetryExportModel.h"
#include "Telemetry/EdenTelemetrySink.h"
#include "Telemetry/EdenTelemetrySubsystem.h"
#include "Telemetry/EdenTelemetryTypes.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenTelemetrySinkTests
{
	class FRecordingTelemetrySink final : public IEdenTelemetrySink
	{
	public:
		FRecordingTelemetrySink(FName InSinkName, TArray<FName>* InOrderLog = nullptr)
			: SinkName(InSinkName)
			, OrderLog(InOrderLog)
		{
		}

		virtual FName GetTelemetrySinkName() const override
		{
			return SinkName;
		}

		virtual FEdenTelemetrySinkResult DeliverTelemetrySession(const FEdenTelemetrySessionPayload& Payload) override
		{
			++DeliveryCount;
			LastPayloadAddress = &Payload;
			LastSessionId = Payload.SessionId;
			LastEventCount = Payload.Events.Num();
			LastSnapshotCount = Payload.Snapshots.Num();
			if (OrderLog)
			{
				OrderLog->Add(SinkName);
			}

			if (bShouldFail)
			{
				return FEdenTelemetrySinkResult::Failed(FString::Printf(TEXT("%s failed"), *SinkName.ToString()));
			}

			return FEdenTelemetrySinkResult::Succeeded(SinkName.ToString());
		}

		FName SinkName;
		TArray<FName>* OrderLog = nullptr;
		int32 DeliveryCount = 0;
		const FEdenTelemetrySessionPayload* LastPayloadAddress = nullptr;
		FString LastSessionId;
		int32 LastEventCount = INDEX_NONE;
		int32 LastSnapshotCount = INDEX_NONE;
		bool bShouldFail = false;
	};

	UEdenTelemetrySubsystem* NewTelemetrySubsystem()
	{
		UEdenTelemetrySubsystem* Telemetry = NewObject<UEdenTelemetrySubsystem>();
		Telemetry->ClearHistory();
		return Telemetry;
	}
}

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
	FEdenTelemetrySinkRegistersAndRejectsDuplicateIdentityTest,
	"Eden.Unit.Telemetry.Sink.RegistersAndRejectsDuplicateIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetrySinkRegistersAndRejectsDuplicateIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenTelemetrySinkTests;

	UEdenTelemetrySubsystem* Telemetry = NewTelemetrySubsystem();
	FRecordingTelemetrySink Primary(TEXT("Primary"));
	FRecordingTelemetrySink DuplicateName(TEXT("Primary"));
	FRecordingTelemetrySink Secondary(TEXT("Secondary"));

	TestFalse(TEXT("Null sink rejected"), Telemetry->RegisterTelemetrySink(nullptr));
	TestTrue(TEXT("Primary registers"), Telemetry->RegisterTelemetrySink(&Primary));
	TestFalse(TEXT("Duplicate pointer rejected"), Telemetry->RegisterTelemetrySink(&Primary));
	TestFalse(TEXT("Duplicate name rejected"), Telemetry->RegisterTelemetrySink(&DuplicateName));
	TestTrue(TEXT("Distinct sink registers"), Telemetry->RegisterTelemetrySink(&Secondary));
	TestEqual(TEXT("Only distinct sinks registered"), Telemetry->GetRegisteredTelemetrySinkCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetrySinkFanOutUsesRegistrationOrderTest,
	"Eden.Unit.Telemetry.Sink.FanOutUsesRegistrationOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetrySinkFanOutUsesRegistrationOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenTelemetrySinkTests;

	UEdenTelemetrySubsystem* Telemetry = NewTelemetrySubsystem();
	TArray<FName> OrderLog;
	FRecordingTelemetrySink First(TEXT("First"), &OrderLog);
	FRecordingTelemetrySink Second(TEXT("Second"), &OrderLog);
	FRecordingTelemetrySink Third(TEXT("Third"), &OrderLog);

	Telemetry->RegisterTelemetrySink(&First);
	Telemetry->RegisterTelemetrySink(&Second);
	Telemetry->RegisterTelemetrySink(&Third);

	const FEdenTelemetrySinkDeliverySummary Summary = Telemetry->DeliverSessionToRegisteredSinks();
	TestEqual(TEXT("Attempted count"), Summary.AttemptedCount, 3);
	TestEqual(TEXT("Succeeded count"), Summary.SucceededCount, 3);
	TestEqual(TEXT("Failed count"), Summary.FailedCount, 0);
	TestEqual(TEXT("Order count"), OrderLog.Num(), 3);
	TestEqual(TEXT("First delivered first"), OrderLog[0], FName(TEXT("First")));
	TestEqual(TEXT("Second delivered second"), OrderLog[1], FName(TEXT("Second")));
	TestEqual(TEXT("Third delivered third"), OrderLog[2], FName(TEXT("Third")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetrySinkUnregisterAndScopedRegistrationStopDeliveryTest,
	"Eden.Unit.Telemetry.Sink.UnregisterAndScopedRegistrationStopDelivery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetrySinkUnregisterAndScopedRegistrationStopDeliveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenTelemetrySinkTests;

	UEdenTelemetrySubsystem* Telemetry = NewTelemetrySubsystem();
	FRecordingTelemetrySink Manual(TEXT("Manual"));
	FRecordingTelemetrySink Scoped(TEXT("Scoped"));

	TestTrue(TEXT("Manual registers"), Telemetry->RegisterTelemetrySink(&Manual));
	TestTrue(TEXT("Manual unregisters"), Telemetry->UnregisterTelemetrySink(&Manual));
	TestFalse(TEXT("Manual unregister is idempotent false"), Telemetry->UnregisterTelemetrySink(&Manual));
	TestEqual(TEXT("No registered sinks after manual unregister"), Telemetry->GetRegisteredTelemetrySinkCount(), 0);

	{
		FEdenScopedTelemetrySinkRegistration ScopedRegistration(*Telemetry, Scoped);
		TestTrue(TEXT("Scoped registration succeeds"), ScopedRegistration.IsRegistered());
		TestEqual(TEXT("Scoped sink registered"), Telemetry->GetRegisteredTelemetrySinkCount(), 1);
	}

	const FEdenTelemetrySinkDeliverySummary Summary = Telemetry->DeliverSessionToRegisteredSinks();
	TestEqual(TEXT("Scoped sink unregistered on scope exit"), Telemetry->GetRegisteredTelemetrySinkCount(), 0);
	TestEqual(TEXT("No delivery after scoped unregister"), Scoped.DeliveryCount, 0);
	TestEqual(TEXT("No attempts after scoped unregister"), Summary.AttemptedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetrySinkPartialFailureContinuesAndAggregatesTest,
	"Eden.Unit.Telemetry.Sink.PartialFailureContinuesAndAggregates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetrySinkPartialFailureContinuesAndAggregatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenTelemetrySinkTests;

	UEdenTelemetrySubsystem* Telemetry = NewTelemetrySubsystem();
	FRecordingTelemetrySink SuccessA(TEXT("SuccessA"));
	FRecordingTelemetrySink Failure(TEXT("Failure"));
	FRecordingTelemetrySink SuccessB(TEXT("SuccessB"));
	Failure.bShouldFail = true;

	Telemetry->RegisterTelemetrySink(&SuccessA);
	Telemetry->RegisterTelemetrySink(&Failure);
	Telemetry->RegisterTelemetrySink(&SuccessB);

	const TArray<FEdenTelemetryEvent> EventsBefore = Telemetry->GetEventHistory();
	const TArray<FEdenTelemetrySnapshot> SnapshotsBefore = Telemetry->GetSnapshotHistory();
	const FString SessionBefore = Telemetry->GetSessionId();

	const FEdenTelemetrySinkDeliverySummary Summary = Telemetry->DeliverSessionToRegisteredSinks();
	TestEqual(TEXT("Attempted all sinks"), Summary.AttemptedCount, 3);
	TestEqual(TEXT("Two sinks succeeded"), Summary.SucceededCount, 2);
	TestEqual(TEXT("One sink failed"), Summary.FailedCount, 1);
	TestFalse(TEXT("Summary not fully successful"), Summary.WasFullySuccessful());
	TestEqual(TEXT("First success delivered"), SuccessA.DeliveryCount, 1);
	TestEqual(TEXT("Failure delivered"), Failure.DeliveryCount, 1);
	TestEqual(TEXT("Second success still delivered"), SuccessB.DeliveryCount, 1);
	TestEqual(TEXT("Record count"), Summary.Records.Num(), 3);
	TestTrue(TEXT("Failure record captured"), !Summary.Records[1].Result.IsSuccess());

	TestEqual(TEXT("Events unchanged after failing sink"), Telemetry->GetEventHistory().Num(), EventsBefore.Num());
	TestEqual(TEXT("Snapshots unchanged after failing sink"), Telemetry->GetSnapshotHistory().Num(), SnapshotsBefore.Num());
	TestEqual(TEXT("Session unchanged after failing sink"), Telemetry->GetSessionId(), SessionBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetrySinkDeliversOneImmutablePayloadToAllSinksTest,
	"Eden.Unit.Telemetry.Sink.DeliversOneImmutablePayloadToAllSinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetrySinkDeliversOneImmutablePayloadToAllSinksTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenTelemetrySinkTests;

	UEdenTelemetrySubsystem* Telemetry = NewTelemetrySubsystem();
	FRecordingTelemetrySink First(TEXT("First"));
	FRecordingTelemetrySink Second(TEXT("Second"));
	Telemetry->RegisterTelemetrySink(&First);
	Telemetry->RegisterTelemetrySink(&Second);

	const FEdenTelemetrySinkDeliverySummary Summary = Telemetry->DeliverSessionToRegisteredSinks();
	TestEqual(TEXT("Both sinks attempted"), Summary.AttemptedCount, 2);
	TestNotNull(TEXT("First saw payload"), First.LastPayloadAddress);
	TestNotNull(TEXT("Second saw payload"), Second.LastPayloadAddress);
	TestEqual(TEXT("Both sinks receive same payload object"), First.LastPayloadAddress, Second.LastPayloadAddress);
	TestEqual(TEXT("Payload session is consistent"), First.LastSessionId, Second.LastSessionId);
	TestEqual(TEXT("Payload event count is consistent"), First.LastEventCount, Second.LastEventCount);
	TestEqual(TEXT("Payload snapshot count is consistent"), First.LastSnapshotCount, Second.LastSnapshotCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetryExportEmptySessionRegressionTest,
	"Eden.Unit.Telemetry.Export.EmptySessionRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetryExportEmptySessionRegressionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	const FString DirectJson = FEdenTelemetryExportModel::BuildSessionJsonV1(
		Events,
		Snapshots,
		FEdenTelemetrySessionMetadata(),
		FString(),
		TEXT("SolarCrisis"));

	TestTrue(TEXT("Direct empty session remains empty"), DirectJson.Contains(TEXT("\"sessionId\": \"\"")));

	const FEdenTelemetrySessionPayload Payload(
		Events,
		Snapshots,
		FEdenTelemetrySessionMetadata(),
		FString(),
		TEXT("SolarCrisis"));
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry"), TEXT("EmptySession"));
	FEdenLocalJsonTelemetrySink Sink(Directory);
	const FEdenTelemetrySinkResult Result = Sink.DeliverTelemetrySession(Payload);

	TestTrue(TEXT("Empty session local sink succeeds"), Result.IsSuccess());
	TestTrue(
		TEXT("Empty session local sink uses safe filename"),
		Result.Destination.EndsWith(TEXT("telemetry_unknown-session.json")));
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
	TestTrue(
		TEXT("Filename convention preserved"),
		Result.Destination.EndsWith(TEXT("telemetry_smoke-session.json")));
	TestTrue(TEXT("Smoke file exists"), IFileManager::Get().FileExists(*Result.Destination));

	FString RoundTrip;
	TestTrue(TEXT("Read smoke file"), FFileHelper::LoadFileToString(RoundTrip, *Result.Destination));
	TestTrue(TEXT("Round-trip schema"), RoundTrip.Contains(TEXT("\"schemaVersion\": 1")));
	TestTrue(TEXT("Round-trip session"), RoundTrip.Contains(TEXT("\"sessionId\": \"smoke-session\"")));
	return true;
}

#endif
