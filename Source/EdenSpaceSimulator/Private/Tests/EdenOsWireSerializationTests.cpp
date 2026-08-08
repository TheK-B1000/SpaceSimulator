// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsConnectionSettings.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Telemetry/EdenTelemetryExportModel.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenOsWireTests
{
	void PopulateSampleTelemetry(
		TArray<FEdenTelemetryEvent>& OutEvents,
		TArray<FEdenTelemetrySnapshot>& OutSnapshots,
		FEdenTelemetrySessionMetadata& OutMetadata)
	{
		FEdenTelemetryEvent Started;
		Started.SequenceNumber = 1;
		Started.SimulationTimeSeconds = 0.0f;
		Started.MissionElapsedTimeSeconds = 0.0f;
		Started.EventType = EEdenTelemetryEventType::MissionStarted;
		Started.SourceSystem = TEXT("Mission");
		Started.EventId = TEXT("Running");
		Started.Detail = TEXT("Mission started");
		OutEvents.Add(Started);

		FEdenTelemetryEvent PhaseChanged;
		PhaseChanged.SequenceNumber = 2;
		PhaseChanged.SimulationTimeSeconds = 10.0f;
		PhaseChanged.MissionElapsedTimeSeconds = 10.0f;
		PhaseChanged.EventType = EEdenTelemetryEventType::PhaseChanged;
		PhaseChanged.SourceSystem = TEXT("Mission");
		PhaseChanged.EventId = TEXT("Impact");
		PhaseChanged.Detail = TEXT("Nominal -> Impact");
		OutEvents.Add(PhaseChanged);

		FEdenTelemetrySnapshot Snapshot;
		Snapshot.SequenceNumber = 3;
		Snapshot.SimulationTimeSeconds = 10.0f;
		Snapshot.MissionElapsedTimeSeconds = 10.0f;
		Snapshot.Fuel.FuelFraction = 0.87f;
		Snapshot.Power.ChargeFraction = 0.44f;
		Snapshot.Power.GenerationKilowatts = 2.0f;
		Snapshot.Power.TotalDemandKilowatts = 5.0f;
		Snapshot.Thermal.TemperatureCelsius = 73.5f;
		Snapshot.Mission.MissionState = EEdenMissionState::Running;
		Snapshot.Mission.MissionPhase = EEdenMissionPhase::Impact;
		Snapshot.Mission.ActiveMissionId = TEXT("SolarCrisis");
		Snapshot.Operator.ThermalMode = EEdenThermalControlMode::Boost;
		Snapshot.Operator.LoadShedMode = EEdenLoadShedMode::Shed;
		Snapshot.Operator.PropulsionPriority = EEdenPropulsionPriorityMode::Reduced;
		Snapshot.Flight.ThrustAuthority = 0.75f;
		Snapshot.Flight.PropulsionDemandNormalized = 0.33f;
		OutSnapshots.Add(Snapshot);

		OutMetadata.DroppedSnapshotCount = 1;
		OutMetadata.DroppedEventCount = 0;
		OutMetadata.bHistoryTruncated = true;
		OutMetadata.bEventIntegrityCompromised = false;
		OutMetadata.FirstAvailableSequence = 1;
		OutMetadata.LastAvailableSequence = 3;
		OutMetadata.SnapshotIntervalSeconds = 0.5f;
		OutMetadata.PeakTemperatureCelsius = 73.5f;
		OutMetadata.MinimumBatteryChargeFraction = 0.44f;
		OutMetadata.MinimumFuelFraction = 0.87f;
	}

	FEdenTelemetrySessionPayload MakePayload(
		const TArray<FEdenTelemetryEvent>& Events,
		const TArray<FEdenTelemetrySnapshot>& Snapshots,
		const FEdenTelemetrySessionMetadata& Metadata)
	{
		return FEdenTelemetrySessionPayload(Events, Snapshots, Metadata, TEXT("session-001"), TEXT("SolarCrisis"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireRoutesAndSchemaVersionContractTest,
	"Eden.Unit.EdenOs.Wire.RoutesAndSchemaVersionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireRoutesAndSchemaVersionContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(TEXT("Current schema version"), EdenOsWireContract::CurrentSchemaVersion, 1);
	TestEqual(TEXT("Session create route"), FString(EdenOsWireContract::CreateSessionRoute), TEXT("/api/missions/sessions"));
	TestEqual(
		TEXT("Telemetry route"),
		FString(EdenOsWireContract::TelemetryRouteTemplate),
		TEXT("/api/missions/sessions/{id}/telemetry"));
	TestEqual(
		TEXT("Events route"),
		FString(EdenOsWireContract::EventsRouteTemplate),
		TEXT("/api/missions/sessions/{id}/events"));
	TestEqual(
		TEXT("Complete route"),
		FString(EdenOsWireContract::CompleteRouteTemplate),
		TEXT("/api/missions/sessions/{id}/complete"));
	TestFalse(TEXT("No route-level /api/v1"), FString(EdenOsWireContract::CreateSessionRoute).Contains(TEXT("/api/v1/")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireSessionCreateSerializesRequiredFieldsOnlyTest,
	"Eden.Unit.EdenOs.Wire.SessionCreateSerializesRequiredFieldsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireSessionCreateSerializesRequiredFieldsOnlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenOsMissionSessionCreateRequestV1 Request;
	Request.SessionId = TEXT("session-001");
	Request.MissionId = TEXT("SolarCrisis");
	Request.StartSimulationTimeSeconds = 0.0f;

	const FEdenOsWireSerializationResult Result = FEdenOsWireSerializationModel::BuildSessionCreateJsonV1(Request);
	TestTrue(TEXT("Session create succeeds"), Result.IsSuccess());
	TestTrue(TEXT("schemaVersion"), Result.Json.Contains(TEXT("\"schemaVersion\": 1")));
	TestTrue(TEXT("sessionId"), Result.Json.Contains(TEXT("\"sessionId\": \"session-001\"")));
	TestTrue(TEXT("missionId"), Result.Json.Contains(TEXT("\"missionId\": \"SolarCrisis\"")));
	TestTrue(TEXT("origin"), Result.Json.Contains(TEXT("\"origin\": \"mission_environment\"")));
	TestTrue(TEXT("start simulation time"), Result.Json.Contains(TEXT("\"startSimulationTimeSeconds\": 0.000000")));
	TestTrue(TEXT("optional timestamp deterministic"), Result.Json.Contains(TEXT("\"startedAtUtc\": \"\"")));
	TestFalse(TEXT("No seed fabrication"), Result.Json.Contains(TEXT("\"seed\"")));
	TestFalse(TEXT("No fake endedAt"), Result.Json.Contains(TEXT("\"endedAt\"")));
	TestFalse(TEXT("No fake alertsCount"), Result.Json.Contains(TEXT("\"alertsCount\"")));
	TestFalse(TEXT("No fake ticks"), Result.Json.Contains(TEXT("\"ticks\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireTelemetryWrapsCanonicalExportSchemaV1Test,
	"Eden.Unit.EdenOs.Wire.TelemetryWrapsCanonicalExportSchemaV1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireTelemetryWrapsCanonicalExportSchemaV1Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsWireTests;

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	PopulateSampleTelemetry(Events, Snapshots, Metadata);
	const FEdenTelemetrySessionPayload Payload = MakePayload(Events, Snapshots, Metadata);
	const FString CanonicalJson = FEdenTelemetryExportModel::BuildSessionJsonV1(Payload).TrimEnd();

	FEdenOsTelemetryIngestionRequestV1 Request;
	Request.Payload = Payload;
	const FEdenOsWireSerializationResult Result = FEdenOsWireSerializationModel::BuildTelemetryJsonV1(Request);

	TestTrue(TEXT("Telemetry serialization succeeds"), Result.IsSuccess());
	TestTrue(TEXT("Outer schemaVersion"), Result.Json.Contains(TEXT("\"schemaVersion\": 1")));
	TestTrue(TEXT("Outer sessionId"), Result.Json.Contains(TEXT("\"sessionId\": \"session-001\"")));
	TestTrue(TEXT("Outer sequence"), Result.Json.Contains(TEXT("\"sequence\": 3")));
	TestTrue(TEXT("Outer simulation time units"), Result.Json.Contains(TEXT("\"simulationTimeSeconds\": 10.000000")));
	TestTrue(TEXT("Canonical telemetry is embedded"), Result.Json.Contains(CanonicalJson));
	TestTrue(TEXT("Explicit temperature units preserved"), Result.Json.Contains(TEXT("\"temperatureCelsius\": 73.500000")));
	TestTrue(TEXT("Explicit power units preserved"), Result.Json.Contains(TEXT("\"generationKilowatts\": 2.000000")));
	TestTrue(TEXT("Integrity metadata preserved"), Result.Json.Contains(TEXT("\"historyTruncated\": true")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireEventSerializesFactIdentityTest,
	"Eden.Unit.EdenOs.Wire.EventSerializesFactIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireEventSerializesFactIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenOsEventIngestionRequestV1 Request;
	Request.SessionId = TEXT("session-001");
	Request.MissionId = TEXT("SolarCrisis");
	Request.Event.SequenceNumber = 9;
	Request.Event.SimulationTimeSeconds = 12.5f;
	Request.Event.MissionElapsedTimeSeconds = 12.5f;
	Request.Event.EventType = EEdenTelemetryEventType::AlertRaised;
	Request.Event.SourceSystem = TEXT("Alert");
	Request.Event.EventId = TEXT("ThermalWarning");
	Request.Event.Detail = TEXT("[Warning] Thermal warning");

	const FEdenOsWireSerializationResult Result = FEdenOsWireSerializationModel::BuildEventJsonV1(Request);
	TestTrue(TEXT("Event serialization succeeds"), Result.IsSuccess());
	TestTrue(TEXT("Event sequence"), Result.Json.Contains(TEXT("\"sequence\": 9")));
	TestTrue(TEXT("Event simulation time"), Result.Json.Contains(TEXT("\"simulationTimeSeconds\": 12.500000")));
	TestTrue(TEXT("Event type"), Result.Json.Contains(TEXT("\"type\": \"AlertRaised\"")));
	TestTrue(TEXT("Event source"), Result.Json.Contains(TEXT("\"source\": \"Alert\"")));
	TestTrue(TEXT("Event id"), Result.Json.Contains(TEXT("\"id\": \"ThermalWarning\"")));
	TestTrue(TEXT("Events are facts"), Result.Json.Contains(TEXT("\"detail\": \"[Warning] Thermal warning\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireCompletionSerializesTerminalFactsTest,
	"Eden.Unit.EdenOs.Wire.CompletionSerializesTerminalFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireCompletionSerializesTerminalFactsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsWireTests;

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	PopulateSampleTelemetry(Events, Snapshots, Metadata);
	FEdenTelemetryEvent Succeeded;
	Succeeded.SequenceNumber = 4;
	Succeeded.SimulationTimeSeconds = 50.0f;
	Succeeded.MissionElapsedTimeSeconds = 50.0f;
	Succeeded.EventType = EEdenTelemetryEventType::MissionSucceeded;
	Succeeded.SourceSystem = TEXT("Mission");
	Succeeded.EventId = TEXT("Succeeded");
	Events.Add(Succeeded);
	Metadata.LastAvailableSequence = 4;

	FEdenOsSessionCompleteRequestV1 Request;
	Request.Payload = MakePayload(Events, Snapshots, Metadata);
	const FEdenOsWireSerializationResult Result = FEdenOsWireSerializationModel::BuildSessionCompleteJsonV1(Request);

	TestTrue(TEXT("Completion serialization succeeds"), Result.IsSuccess());
	TestTrue(TEXT("Terminal outcome"), Result.Json.Contains(TEXT("\"terminalOutcome\": \"Succeeded\"")));
	TestTrue(TEXT("End time"), Result.Json.Contains(TEXT("\"endSimulationTimeSeconds\": 50.000000")));
	TestTrue(TEXT("Dropped snapshots"), Result.Json.Contains(TEXT("\"droppedSnapshots\": 1")));
	TestTrue(TEXT("Peak temperature"), Result.Json.Contains(TEXT("\"peakTemperatureCelsius\": 73.500000")));
	TestTrue(TEXT("Canonical telemetry attached"), Result.Json.Contains(TEXT("\"telemetry\": {")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireRejectsUnsupportedSchemaVersionTest,
	"Eden.Unit.EdenOs.Wire.RejectsUnsupportedSchemaVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireRejectsUnsupportedSchemaVersionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(
		TEXT("Current version accepted"),
		FEdenOsWireSerializationModel::ValidateSchemaVersionFromJson(TEXT("{ \"schemaVersion\": 1 }")).IsSuccess());
	TestFalse(
		TEXT("Unsupported version rejected"),
		FEdenOsWireSerializationModel::ValidateSchemaVersionFromJson(TEXT("{ \"schemaVersion\": 2 }")).IsSuccess());
	TestFalse(
		TEXT("Missing version rejected"),
		FEdenOsWireSerializationModel::ValidateSchemaVersionFromJson(TEXT("{ \"sessionId\": \"session-001\" }")).IsSuccess());
	TestFalse(
		TEXT("Malformed version rejected"),
		FEdenOsWireSerializationModel::ValidateSchemaVersionFromJson(TEXT("{ \"schemaVersion\": \"1\" }")).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireRejectsMissingIdentifiersTest,
	"Eden.Unit.EdenOs.Wire.RejectsMissingIdentifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireRejectsMissingIdentifiersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsWireTests;

	FEdenOsMissionSessionCreateRequestV1 CreateRequest;
	CreateRequest.MissionId = TEXT("SolarCrisis");
	TestFalse(TEXT("Create missing session rejected"), FEdenOsWireSerializationModel::BuildSessionCreateJsonV1(CreateRequest).IsSuccess());

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	PopulateSampleTelemetry(Events, Snapshots, Metadata);
	FEdenOsTelemetryIngestionRequestV1 TelemetryRequest;
	TelemetryRequest.Payload = FEdenTelemetrySessionPayload(Events, Snapshots, Metadata, FString(), TEXT("SolarCrisis"));
	TestFalse(TEXT("Telemetry missing session rejected"), FEdenOsWireSerializationModel::BuildTelemetryJsonV1(TelemetryRequest).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireRejectsNonFiniteTelemetryNumbersTest,
	"Eden.Unit.EdenOs.Wire.RejectsNonFiniteTelemetryNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireRejectsNonFiniteTelemetryNumbersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsWireTests;

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	PopulateSampleTelemetry(Events, Snapshots, Metadata);
	Snapshots[0].Thermal.TemperatureCelsius = std::numeric_limits<float>::infinity();

	FEdenOsTelemetryIngestionRequestV1 Request;
	Request.Payload = MakePayload(Events, Snapshots, Metadata);
	TestFalse(TEXT("Non-finite telemetry rejected"), FEdenOsWireSerializationModel::BuildTelemetryJsonV1(Request).IsSuccess());

	PopulateSampleTelemetry(Events, Snapshots, Metadata);
	Metadata.PeakTemperatureCelsius = std::numeric_limits<float>::quiet_NaN();
	Request.Payload = MakePayload(Events, Snapshots, Metadata);
	TestFalse(TEXT("Non-finite metadata rejected"), FEdenOsWireSerializationModel::BuildTelemetryJsonV1(Request).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireRejectsInvalidEventSequenceTest,
	"Eden.Unit.EdenOs.Wire.RejectsInvalidEventSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireRejectsInvalidEventSequenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenOsEventIngestionRequestV1 Request;
	Request.SessionId = TEXT("session-001");
	Request.MissionId = TEXT("SolarCrisis");
	Request.Event.SequenceNumber = -1;
	Request.Event.SimulationTimeSeconds = 1.0f;
	Request.Event.MissionElapsedTimeSeconds = 1.0f;
	TestFalse(TEXT("Negative event sequence rejected"), FEdenOsWireSerializationModel::BuildEventJsonV1(Request).IsSuccess());

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	EdenOsWireTests::PopulateSampleTelemetry(Events, Snapshots, Metadata);
	Metadata.LastAvailableSequence = -1;
	FEdenOsTelemetryIngestionRequestV1 TelemetryRequest;
	TelemetryRequest.Payload = EdenOsWireTests::MakePayload(Events, Snapshots, Metadata);
	TestFalse(TEXT("Negative payload sequence metadata rejected"), FEdenOsWireSerializationModel::BuildTelemetryJsonV1(TelemetryRequest).IsSuccess());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireDoesNotSerializeJwtFromConnectionConfigTest,
	"Eden.Unit.EdenOs.Wire.DoesNotSerializeJwtFromConnectionConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireDoesNotSerializeJwtFromConnectionConfigTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsWireTests;

	FEdenOsConnectionConfig Config;
	Config.RuntimeBearerJwt = TEXT("test-token");

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	PopulateSampleTelemetry(Events, Snapshots, Metadata);
	FEdenOsTelemetryIngestionRequestV1 Request;
	Request.Payload = MakePayload(Events, Snapshots, Metadata);

	const FEdenOsWireSerializationResult Result = FEdenOsWireSerializationModel::BuildTelemetryJsonV1(Request);
	TestTrue(TEXT("Telemetry serialization succeeds"), Result.IsSuccess());
	TestFalse(TEXT("No token value"), Result.Json.Contains(Config.RuntimeBearerJwt));
	TestFalse(TEXT("No token key"), Result.Json.Contains(TEXT("token")));
	TestFalse(TEXT("No JWT key"), Result.Json.Contains(TEXT("jwt"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("No authorization header data"), Result.Json.Contains(TEXT("Authorization")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsWireSerializesDeterministicallyTest,
	"Eden.Unit.EdenOs.Wire.SerializesDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsWireSerializesDeterministicallyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsWireTests;

	TArray<FEdenTelemetryEvent> Events;
	TArray<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	PopulateSampleTelemetry(Events, Snapshots, Metadata);
	FEdenOsTelemetryIngestionRequestV1 Request;
	Request.Payload = MakePayload(Events, Snapshots, Metadata);

	const FEdenOsWireSerializationResult First = FEdenOsWireSerializationModel::BuildTelemetryJsonV1(Request);
	const FEdenOsWireSerializationResult Second = FEdenOsWireSerializationModel::BuildTelemetryJsonV1(Request);
	TestTrue(TEXT("First succeeds"), First.IsSuccess());
	TestTrue(TEXT("Second succeeds"), Second.IsSuccess());
	TestEqual(TEXT("Same logical input serializes identically"), First.Json, Second.Json);
	return true;
}

#endif
