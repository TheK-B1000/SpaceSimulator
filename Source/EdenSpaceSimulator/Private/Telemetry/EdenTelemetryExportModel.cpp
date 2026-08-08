// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenTelemetryExportModel.h"

namespace EdenTelemetryExportModelPrivate
{
	FString BoolJson(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString ResolveOutcome(
		TConstArrayView<FEdenTelemetryEvent> Events,
		TConstArrayView<FEdenTelemetrySnapshot> Snapshots)
	{
		for (int32 Index = Events.Num() - 1; Index >= 0; --Index)
		{
			switch (Events[Index].EventType)
			{
			case EEdenTelemetryEventType::MissionSucceeded:
				return TEXT("Succeeded");
			case EEdenTelemetryEventType::MissionFailed:
				return TEXT("Failed");
			case EEdenTelemetryEventType::MissionAborted:
				return TEXT("Aborted");
			default:
				break;
			}
		}

		if (Snapshots.Num() > 0)
		{
			return FEdenTelemetryExportModel::EnumToken(Snapshots.Last().Mission.MissionState);
		}

		return TEXT("Unknown");
	}

	void AppendEvent(FString& Json, const FEdenTelemetryEvent& Event, bool bTrailingComma)
	{
		Json += TEXT("    {\n");
		Json += FString::Printf(TEXT("      \"sequence\": %lld,\n"), Event.SequenceNumber);
		Json += FString::Printf(TEXT("      \"simulationTime\": %.6f,\n"), Event.SimulationTimeSeconds);
		Json += FString::Printf(TEXT("      \"missionElapsed\": %.6f,\n"), Event.MissionElapsedTimeSeconds);
		Json += FString::Printf(
			TEXT("      \"type\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(FEdenTelemetryExportModel::EnumToken(Event.EventType)));
		Json += FString::Printf(
			TEXT("      \"source\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(Event.SourceSystem.ToString()));
		Json += FString::Printf(
			TEXT("      \"id\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(Event.EventId.ToString()));
		Json += FString::Printf(
			TEXT("      \"detail\": \"%s\"\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(Event.Detail));
		Json += bTrailingComma ? TEXT("    },\n") : TEXT("    }\n");
	}

	void AppendSnapshot(FString& Json, const FEdenTelemetrySnapshot& Snapshot, bool bTrailingComma)
	{
		Json += TEXT("    {\n");
		Json += FString::Printf(TEXT("      \"sequence\": %lld,\n"), Snapshot.SequenceNumber);
		Json += FString::Printf(TEXT("      \"simulationTime\": %.6f,\n"), Snapshot.SimulationTimeSeconds);
		Json += FString::Printf(TEXT("      \"missionElapsed\": %.6f,\n"), Snapshot.MissionElapsedTimeSeconds);
		Json += FString::Printf(TEXT("      \"fuelFraction\": %.6f,\n"), Snapshot.Fuel.FuelFraction);
		Json += FString::Printf(TEXT("      \"batteryFraction\": %.6f,\n"), Snapshot.Power.ChargeFraction);
		Json += FString::Printf(TEXT("      \"generationKilowatts\": %.6f,\n"), Snapshot.Power.GenerationKilowatts);
		Json += FString::Printf(TEXT("      \"demandKilowatts\": %.6f,\n"), Snapshot.Power.TotalDemandKilowatts);
		Json += FString::Printf(TEXT("      \"temperatureCelsius\": %.6f,\n"), Snapshot.Thermal.TemperatureCelsius);
		Json += FString::Printf(
			TEXT("      \"missionState\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(FEdenTelemetryExportModel::EnumToken(Snapshot.Mission.MissionState)));
		Json += FString::Printf(
			TEXT("      \"missionPhase\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(FEdenTelemetryExportModel::EnumToken(Snapshot.Mission.MissionPhase)));
		Json += FString::Printf(
			TEXT("      \"missionId\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(Snapshot.Mission.ActiveMissionId.ToString()));
		Json += FString::Printf(
			TEXT("      \"thermalMode\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(FEdenTelemetryExportModel::EnumToken(Snapshot.Operator.ThermalMode)));
		Json += FString::Printf(
			TEXT("      \"loadShed\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(FEdenTelemetryExportModel::EnumToken(Snapshot.Operator.LoadShedMode)));
		Json += FString::Printf(
			TEXT("      \"propulsionPriority\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(FEdenTelemetryExportModel::EnumToken(Snapshot.Operator.PropulsionPriority)));
		Json += FString::Printf(TEXT("      \"thrustAuthority\": %.6f\n"), Snapshot.Flight.ThrustAuthority);
		Json += bTrailingComma ? TEXT("    },\n") : TEXT("    }\n");
	}
}

FString FEdenTelemetryExportModel::EscapeJsonString(const FString& Value)
{
	FString Escaped;
	Escaped.Reserve(Value.Len() + 8);
	for (TCHAR Character : Value)
	{
		switch (Character)
		{
		case TCHAR('\\'):
			Escaped += TEXT("\\\\");
			break;
		case TCHAR('"'):
			Escaped += TEXT("\\\"");
			break;
		case TCHAR('\n'):
			Escaped += TEXT("\\n");
			break;
		case TCHAR('\r'):
			Escaped += TEXT("\\r");
			break;
		case TCHAR('\t'):
			Escaped += TEXT("\\t");
			break;
		default:
			Escaped.AppendChar(Character);
			break;
		}
	}
	return Escaped;
}

FString FEdenTelemetryExportModel::EnumToken(const UEnum* Enum, int64 Value)
{
	if (!Enum)
	{
		return TEXT("Unknown");
	}

	FString Name = Enum->GetNameStringByValue(Value);
	const int32 DotIndex = Name.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (DotIndex != INDEX_NONE)
	{
		Name.RightChopInline(DotIndex + 2, EAllowShrinking::No);
	}
	return Name;
}

FString FEdenTelemetryExportModel::BuildSessionJsonV1(
	const TArray<FEdenTelemetryEvent>& Events,
	const TArray<FEdenTelemetrySnapshot>& Snapshots,
	const FEdenTelemetrySessionMetadata& Metadata,
	const FString& SessionId,
	const FName MissionId)
{
	const FEdenTelemetrySessionPayload Payload(Events, Snapshots, Metadata, SessionId, MissionId);
	return BuildSessionJsonV1(Payload);
}

FString FEdenTelemetryExportModel::BuildSessionJsonV1(const FEdenTelemetrySessionPayload& Payload)
{
	using namespace EdenTelemetryExportModelPrivate;

	float StartSimulationTime = 0.0f;
	float EndSimulationTime = 0.0f;
	if (Payload.Snapshots.Num() > 0)
	{
		StartSimulationTime = Payload.Snapshots[0].SimulationTimeSeconds;
		EndSimulationTime = Payload.Snapshots.Last().SimulationTimeSeconds;
	}
	else if (Payload.Events.Num() > 0)
	{
		StartSimulationTime = Payload.Events[0].SimulationTimeSeconds;
		EndSimulationTime = Payload.Events.Last().SimulationTimeSeconds;
	}

	FName ResolvedMissionId = Payload.MissionId;
	if (ResolvedMissionId.IsNone() && Payload.Snapshots.Num() > 0)
	{
		ResolvedMissionId = Payload.Snapshots.Last().Mission.ActiveMissionId;
	}

	const FString Outcome = ResolveOutcome(Payload.Events, Payload.Snapshots);

	FString Json;
	Json += TEXT("{\n");
	Json += TEXT("  \"schemaVersion\": 1,\n");
	Json += TEXT("  \"session\": {\n");
	Json += FString::Printf(TEXT("    \"sessionId\": \"%s\",\n"), *EscapeJsonString(Payload.GetSafeSessionId()));
	Json += FString::Printf(
		TEXT("    \"missionId\": \"%s\",\n"),
		*EscapeJsonString(ResolvedMissionId.ToString()));
	Json += FString::Printf(TEXT("    \"startSimulationTime\": %.6f,\n"), StartSimulationTime);
	Json += FString::Printf(TEXT("    \"endSimulationTime\": %.6f,\n"), EndSimulationTime);
	Json += FString::Printf(TEXT("    \"outcome\": \"%s\"\n"), *EscapeJsonString(Outcome));
	Json += TEXT("  },\n");
	Json += TEXT("  \"integrity\": {\n");
	Json += FString::Printf(
		TEXT("    \"historyTruncated\": %s,\n"),
		*BoolJson(Payload.Metadata.bHistoryTruncated));
	Json += FString::Printf(TEXT("    \"droppedSnapshots\": %d,\n"), Payload.Metadata.DroppedSnapshotCount);
	Json += FString::Printf(TEXT("    \"droppedEvents\": %d,\n"), Payload.Metadata.DroppedEventCount);
	Json += FString::Printf(
		TEXT("    \"eventIntegrityCompromised\": %s\n"),
		*BoolJson(Payload.Metadata.bEventIntegrityCompromised));
	Json += TEXT("  },\n");
	Json += TEXT("  \"aggregates\": {\n");
	Json += FString::Printf(TEXT("    \"peakTemperatureCelsius\": %.6f,\n"), Payload.Metadata.PeakTemperatureCelsius);
	Json += FString::Printf(
		TEXT("    \"minimumBatteryChargeFraction\": %.6f,\n"),
		Payload.Metadata.MinimumBatteryChargeFraction);
	Json += FString::Printf(TEXT("    \"minimumFuelFraction\": %.6f,\n"), Payload.Metadata.MinimumFuelFraction);
	Json += FString::Printf(TEXT("    \"snapshotIntervalSeconds\": %.6f\n"), Payload.Metadata.SnapshotIntervalSeconds);
	Json += TEXT("  },\n");

	Json += TEXT("  \"events\": [\n");
	for (int32 Index = 0; Index < Payload.Events.Num(); ++Index)
	{
		AppendEvent(Json, Payload.Events[Index], Index + 1 < Payload.Events.Num());
	}
	Json += TEXT("  ],\n");

	Json += TEXT("  \"snapshots\": [\n");
	for (int32 Index = 0; Index < Payload.Snapshots.Num(); ++Index)
	{
		AppendSnapshot(Json, Payload.Snapshots[Index], Index + 1 < Payload.Snapshots.Num());
	}
	Json += TEXT("  ]\n");
	Json += TEXT("}\n");
	return Json;
}
