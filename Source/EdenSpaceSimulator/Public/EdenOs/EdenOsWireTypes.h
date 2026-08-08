// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Telemetry/EdenTelemetrySink.h"

namespace EdenOsWireContract
{
	inline constexpr int32 CurrentSchemaVersion = 1;

	inline const TCHAR* MissionEnvironmentOrigin = TEXT("mission_environment");

	inline const TCHAR* CreateSessionRoute = TEXT("/api/missions/sessions");
	inline const TCHAR* TelemetryRouteTemplate = TEXT("/api/missions/sessions/{id}/telemetry");
	inline const TCHAR* EventsRouteTemplate = TEXT("/api/missions/sessions/{id}/events");
	inline const TCHAR* CompleteRouteTemplate = TEXT("/api/missions/sessions/{id}/complete");
}

struct EDENSPACESIMULATOR_API FEdenOsWireSerializationResult
{
	static FEdenOsWireSerializationResult Succeeded(FString InJson);
	static FEdenOsWireSerializationResult Failed(FString InErrorMessage);

	bool IsSuccess() const;

	FString Json;
	FString ErrorMessage;
};

struct EDENSPACESIMULATOR_API FEdenOsMissionSessionCreateRequestV1
{
	FString SessionId;
	FString ScenarioId;
	FString StartedAtIso8601;
};

struct EDENSPACESIMULATOR_API FEdenOsTelemetryIngestionRequestV1
{
	FEdenTelemetrySessionPayload Payload;
};

struct EDENSPACESIMULATOR_API FEdenOsEventIngestionRequestV1
{
	FString SessionId;
	FEdenTelemetryEvent Event;
};

enum class EEdenOsMissionFinalStatus : uint8
{
	Succeeded,
	Failed,
	Aborted
};

struct EDENSPACESIMULATOR_API FEdenOsSessionCompleteRequestV1
{
	FString SessionId;
	EEdenOsMissionFinalStatus FinalStatus = EEdenOsMissionFinalStatus::Succeeded;
	FString CompletedAtUtcIso8601;
	TOptional<int64> FinalSequence;
	TOptional<int32> Ticks;
	TOptional<int32> AlertsCount;
	TOptional<FString> HighestRiskSystem;
};

struct EDENSPACESIMULATOR_API FEdenOsWireSerializationModel
{
	static FEdenOsWireSerializationResult BuildSessionCreateJsonV1(
		const FEdenOsMissionSessionCreateRequestV1& Request);
	static FEdenOsWireSerializationResult BuildTelemetryJsonV1(
		const FEdenOsTelemetryIngestionRequestV1& Request);
	static FEdenOsWireSerializationResult BuildEventJsonV1(
		const FEdenOsEventIngestionRequestV1& Request);
	static FEdenOsWireSerializationResult BuildSessionCompleteJsonV1(
		const FEdenOsSessionCompleteRequestV1& Request);

	static FEdenOsWireSerializationResult ValidateSchemaVersionFromJson(const FString& Json);
};
