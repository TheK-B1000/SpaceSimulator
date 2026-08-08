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
	FName MissionId = NAME_None;
	float StartSimulationTimeSeconds = 0.0f;
	FString StartedAtUtcIso8601;
};

struct EDENSPACESIMULATOR_API FEdenOsTelemetryIngestionRequestV1
{
	FEdenTelemetrySessionPayload Payload;
};

struct EDENSPACESIMULATOR_API FEdenOsEventIngestionRequestV1
{
	FString SessionId;
	FName MissionId = NAME_None;
	FEdenTelemetryEvent Event;
};

struct EDENSPACESIMULATOR_API FEdenOsSessionCompleteRequestV1
{
	FEdenTelemetrySessionPayload Payload;
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
