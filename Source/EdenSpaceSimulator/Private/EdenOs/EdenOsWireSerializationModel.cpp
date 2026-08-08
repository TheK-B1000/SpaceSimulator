// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsWireTypes.h"

#include "Telemetry/EdenTelemetryExportModel.h"

namespace EdenOsWireSerializationPrivate
{
	bool HasIdentifier(const FString& Identifier)
	{
		return !Identifier.TrimStartAndEnd().IsEmpty();
	}

	FString FinalStatusToken(EEdenOsMissionFinalStatus Status)
	{
		switch (Status)
		{
		case EEdenOsMissionFinalStatus::Succeeded:
			return TEXT("succeeded");
		case EEdenOsMissionFinalStatus::Failed:
			return TEXT("failed");
		case EEdenOsMissionFinalStatus::Aborted:
			return TEXT("aborted");
		default:
			return TEXT("failed");
		}
	}

	FName ResolveMissionId(const FEdenTelemetrySessionPayload& Payload)
	{
		if (!Payload.MissionId.IsNone())
		{
			return Payload.MissionId;
		}

		if (Payload.Snapshots.Num() > 0)
		{
			return Payload.Snapshots.Last().Mission.ActiveMissionId;
		}

		return NAME_None;
	}

	int64 ResolveLatestSequence(const FEdenTelemetrySessionPayload& Payload)
	{
		if (Payload.Metadata.LastAvailableSequence > 0)
		{
			return Payload.Metadata.LastAvailableSequence;
		}

		if (Payload.Snapshots.Num() > 0)
		{
			return Payload.Snapshots.Last().SequenceNumber;
		}

		if (Payload.Events.Num() > 0)
		{
			return Payload.Events.Last().SequenceNumber;
		}

		return 0;
	}

	float ResolveLatestSimulationTimeSeconds(const FEdenTelemetrySessionPayload& Payload)
	{
		if (Payload.Snapshots.Num() > 0)
		{
			return Payload.Snapshots.Last().SimulationTimeSeconds;
		}

		if (Payload.Events.Num() > 0)
		{
			return Payload.Events.Last().SimulationTimeSeconds;
		}

		return 0.0f;
	}

	bool ValidatePayload(const FEdenTelemetrySessionPayload& Payload, FString& OutErrorMessage)
	{
		if (!HasIdentifier(Payload.SessionId))
		{
			OutErrorMessage = TEXT("EDEN OS wire payload requires a non-empty sessionId.");
			return false;
		}

		if (ResolveMissionId(Payload).IsNone())
		{
			OutErrorMessage = TEXT("EDEN OS wire payload requires a missionId.");
			return false;
		}

		if (ResolveLatestSequence(Payload) < 0)
		{
			OutErrorMessage = TEXT("EDEN OS wire payload sequence metadata cannot be negative.");
			return false;
		}
		if (Payload.Metadata.FirstAvailableSequence < 0 || Payload.Metadata.LastAvailableSequence < 0)
		{
			OutErrorMessage = TEXT("EDEN OS wire payload sequence metadata cannot be negative.");
			return false;
		}
		if (!FMath::IsFinite(Payload.Metadata.SnapshotIntervalSeconds)
			|| !FMath::IsFinite(Payload.Metadata.PeakTemperatureCelsius)
			|| !FMath::IsFinite(Payload.Metadata.MinimumBatteryChargeFraction)
			|| !FMath::IsFinite(Payload.Metadata.MinimumFuelFraction))
		{
			OutErrorMessage = TEXT("EDEN OS wire payload metadata numeric values must be finite.");
			return false;
		}

		for (const FEdenTelemetryEvent& Event : Payload.Events)
		{
			if (Event.SequenceNumber < 0)
			{
				OutErrorMessage = TEXT("EDEN OS wire event sequence cannot be negative.");
				return false;
			}
			if (!FMath::IsFinite(Event.SimulationTimeSeconds) || !FMath::IsFinite(Event.MissionElapsedTimeSeconds))
			{
				OutErrorMessage = TEXT("EDEN OS wire event times must be finite.");
				return false;
			}
		}

		for (const FEdenTelemetrySnapshot& Snapshot : Payload.Snapshots)
		{
			if (Snapshot.SequenceNumber < 0)
			{
				OutErrorMessage = TEXT("EDEN OS wire telemetry sequence cannot be negative.");
				return false;
			}
			if (!FMath::IsFinite(Snapshot.SimulationTimeSeconds) || !FMath::IsFinite(Snapshot.MissionElapsedTimeSeconds))
			{
				OutErrorMessage = TEXT("EDEN OS wire telemetry times must be finite.");
				return false;
			}
			if (!FMath::IsFinite(Snapshot.Fuel.FuelFraction)
				|| !FMath::IsFinite(Snapshot.Power.ChargeFraction)
				|| !FMath::IsFinite(Snapshot.Power.GenerationKilowatts)
				|| !FMath::IsFinite(Snapshot.Power.TotalDemandKilowatts)
				|| !FMath::IsFinite(Snapshot.Thermal.TemperatureCelsius)
				|| !FMath::IsFinite(Snapshot.Flight.ThrustAuthority)
				|| !FMath::IsFinite(Snapshot.Flight.PropulsionDemandNormalized))
			{
				OutErrorMessage = TEXT("EDEN OS wire telemetry numeric values must be finite.");
				return false;
			}
		}

		return true;
	}

	FString TrimmedCanonicalTelemetryJson(const FEdenTelemetrySessionPayload& Payload)
	{
		FString CanonicalJson = FEdenTelemetryExportModel::BuildSessionJsonV1(Payload);
		CanonicalJson.TrimEndInline();
		return CanonicalJson;
	}
}

FEdenOsWireSerializationResult FEdenOsWireSerializationResult::Succeeded(FString InJson)
{
	FEdenOsWireSerializationResult Result;
	Result.Json = MoveTemp(InJson);
	return Result;
}

FEdenOsWireSerializationResult FEdenOsWireSerializationResult::Failed(FString InErrorMessage)
{
	FEdenOsWireSerializationResult Result;
	Result.ErrorMessage = MoveTemp(InErrorMessage);
	return Result;
}

bool FEdenOsWireSerializationResult::IsSuccess() const
{
	return ErrorMessage.IsEmpty();
}

FEdenOsWireSerializationResult FEdenOsWireSerializationModel::BuildSessionCreateJsonV1(
	const FEdenOsMissionSessionCreateRequestV1& Request)
{
	using namespace EdenOsWireSerializationPrivate;

	if (!HasIdentifier(Request.SessionId))
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS session create requires a non-empty sessionId."));
	}
	if (!HasIdentifier(Request.ScenarioId))
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS session create requires a scenarioId."));
	}
	if (!HasIdentifier(Request.StartedAtIso8601))
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS session create requires startedAt."));
	}

	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"schemaVersion\": %d,\n"), EdenOsWireContract::CurrentSchemaVersion);
	Json += FString::Printf(TEXT("  \"sessionId\": \"%s\",\n"), *FEdenTelemetryExportModel::EscapeJsonString(Request.SessionId));
	Json += FString::Printf(TEXT("  \"scenarioId\": \"%s\",\n"), *FEdenTelemetryExportModel::EscapeJsonString(Request.ScenarioId));
	Json += FString::Printf(
		TEXT("  \"startedAt\": \"%s\"\n"),
		*FEdenTelemetryExportModel::EscapeJsonString(Request.StartedAtIso8601));
	Json += TEXT("}\n");
	return FEdenOsWireSerializationResult::Succeeded(Json);
}

FEdenOsWireSerializationResult FEdenOsWireSerializationModel::BuildTelemetryJsonV1(
	const FEdenOsTelemetryIngestionRequestV1& Request)
{
	using namespace EdenOsWireSerializationPrivate;

	FString ErrorMessage;
	if (!ValidatePayload(Request.Payload, ErrorMessage))
	{
		return FEdenOsWireSerializationResult::Failed(ErrorMessage);
	}

	const int64 Sequence = ResolveLatestSequence(Request.Payload);
	const float SimulationTimeSeconds = ResolveLatestSimulationTimeSeconds(Request.Payload);
	const FString CanonicalTelemetryJson = TrimmedCanonicalTelemetryJson(Request.Payload);

	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"schemaVersion\": %d,\n"), EdenOsWireContract::CurrentSchemaVersion);
	Json += FString::Printf(TEXT("  \"sessionId\": \"%s\",\n"), *FEdenTelemetryExportModel::EscapeJsonString(Request.Payload.SessionId));
	Json += FString::Printf(TEXT("  \"sequence\": %lld,\n"), Sequence);
	Json += FString::Printf(TEXT("  \"simulationTimeSeconds\": %.6f,\n"), SimulationTimeSeconds);
	Json += TEXT("  \"telemetry\": ");
	Json += CanonicalTelemetryJson;
	Json += TEXT("\n}\n");
	return FEdenOsWireSerializationResult::Succeeded(Json);
}

FEdenOsWireSerializationResult FEdenOsWireSerializationModel::BuildEventJsonV1(
	const FEdenOsEventIngestionRequestV1& Request)
{
	using namespace EdenOsWireSerializationPrivate;

	if (!HasIdentifier(Request.SessionId))
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS event requires a non-empty sessionId."));
	}
	if (Request.Event.SequenceNumber < 0)
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS event sequence cannot be negative."));
	}
	if (!FMath::IsFinite(Request.Event.SimulationTimeSeconds) || !FMath::IsFinite(Request.Event.MissionElapsedTimeSeconds))
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS event times must be finite."));
	}

	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"schemaVersion\": %d,\n"), EdenOsWireContract::CurrentSchemaVersion);
	Json += FString::Printf(TEXT("  \"sessionId\": \"%s\",\n"), *FEdenTelemetryExportModel::EscapeJsonString(Request.SessionId));
	Json += FString::Printf(
		TEXT("  \"eventId\": \"%s\",\n"),
		*FEdenTelemetryExportModel::EscapeJsonString(Request.Event.EventId.ToString()));
	Json += FString::Printf(
		TEXT("  \"eventType\": \"%s\",\n"),
		*FEdenTelemetryExportModel::EscapeJsonString(FEdenTelemetryExportModel::EnumToken(Request.Event.EventType)));
	Json += FString::Printf(TEXT("  \"sequence\": %lld,\n"), Request.Event.SequenceNumber);
	Json += FString::Printf(TEXT("  \"simulationTimeSeconds\": %.6f,\n"), Request.Event.SimulationTimeSeconds);
	Json += TEXT("  \"payload\": {\n");
	Json += FString::Printf(
		TEXT("    \"source\": \"%s\",\n"),
		*FEdenTelemetryExportModel::EscapeJsonString(Request.Event.SourceSystem.ToString()));
	Json += FString::Printf(
		TEXT("    \"missionElapsedTimeSeconds\": %.6f,\n"),
		Request.Event.MissionElapsedTimeSeconds);
	Json += FString::Printf(
		TEXT("    \"detail\": \"%s\"\n"),
		*FEdenTelemetryExportModel::EscapeJsonString(Request.Event.Detail));
	Json += TEXT("  }\n");
	Json += TEXT("}\n");
	return FEdenOsWireSerializationResult::Succeeded(Json);
}

FEdenOsWireSerializationResult FEdenOsWireSerializationModel::BuildSessionCompleteJsonV1(
	const FEdenOsSessionCompleteRequestV1& Request)
{
	using namespace EdenOsWireSerializationPrivate;

	if (!HasIdentifier(Request.SessionId))
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS session complete requires a non-empty sessionId."));
	}
	if (!HasIdentifier(Request.CompletedAtUtcIso8601))
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS session complete requires completedAt."));
	}
	if (Request.FinalSequence.IsSet() && Request.FinalSequence.GetValue() < 0)
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS session complete finalSequence cannot be negative."));
	}
	if (Request.Ticks.IsSet() && Request.Ticks.GetValue() < 0)
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS session complete ticks cannot be negative."));
	}
	if (Request.AlertsCount.IsSet() && Request.AlertsCount.GetValue() < 0)
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS session complete alertsCount cannot be negative."));
	}

	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"schemaVersion\": %d,\n"), EdenOsWireContract::CurrentSchemaVersion);
	Json += FString::Printf(TEXT("  \"sessionId\": \"%s\",\n"), *FEdenTelemetryExportModel::EscapeJsonString(Request.SessionId));
	Json += FString::Printf(TEXT("  \"finalStatus\": \"%s\",\n"), *FEdenTelemetryExportModel::EscapeJsonString(FinalStatusToken(Request.FinalStatus)));
	Json += FString::Printf(
		TEXT("  \"completedAt\": \"%s\""),
		*FEdenTelemetryExportModel::EscapeJsonString(Request.CompletedAtUtcIso8601));
	if (Request.FinalSequence.IsSet())
	{
		Json += FString::Printf(TEXT(",\n  \"finalSequence\": %lld"), Request.FinalSequence.GetValue());
	}
	if (Request.Ticks.IsSet())
	{
		Json += FString::Printf(TEXT(",\n  \"ticks\": %d"), Request.Ticks.GetValue());
	}
	if (Request.AlertsCount.IsSet())
	{
		Json += FString::Printf(TEXT(",\n  \"alertsCount\": %d"), Request.AlertsCount.GetValue());
	}
	if (Request.HighestRiskSystem.IsSet() && !Request.HighestRiskSystem.GetValue().TrimStartAndEnd().IsEmpty())
	{
		Json += FString::Printf(
			TEXT(",\n  \"highestRiskSystem\": \"%s\""),
			*FEdenTelemetryExportModel::EscapeJsonString(Request.HighestRiskSystem.GetValue()));
	}
	Json += TEXT("\n}\n");
	return FEdenOsWireSerializationResult::Succeeded(Json);
}

FEdenOsWireSerializationResult FEdenOsWireSerializationModel::ValidateSchemaVersionFromJson(const FString& Json)
{
	const FString Key = TEXT("\"schemaVersion\"");
	const int32 KeyIndex = Json.Find(Key, ESearchCase::CaseSensitive);
	if (KeyIndex == INDEX_NONE)
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS wire JSON is missing schemaVersion."));
	}

	const int32 ColonIndex = Json.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyIndex + Key.Len());
	if (ColonIndex == INDEX_NONE)
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS wire JSON schemaVersion is malformed."));
	}

	int32 Cursor = ColonIndex + 1;
	while (Cursor < Json.Len() && FChar::IsWhitespace(Json[Cursor]))
	{
		++Cursor;
	}

	if (Cursor >= Json.Len() || !FChar::IsDigit(Json[Cursor]))
	{
		return FEdenOsWireSerializationResult::Failed(TEXT("EDEN OS wire JSON schemaVersion must be an integer."));
	}

	int32 SchemaVersion = 0;
	while (Cursor < Json.Len() && FChar::IsDigit(Json[Cursor]))
	{
		SchemaVersion = (SchemaVersion * 10) + (Json[Cursor] - TCHAR('0'));
		++Cursor;
	}

	if (SchemaVersion != EdenOsWireContract::CurrentSchemaVersion)
	{
		return FEdenOsWireSerializationResult::Failed(
			FString::Printf(TEXT("Unsupported EDEN OS wire schemaVersion %d."), SchemaVersion));
	}

	return FEdenOsWireSerializationResult::Succeeded(FString());
}
