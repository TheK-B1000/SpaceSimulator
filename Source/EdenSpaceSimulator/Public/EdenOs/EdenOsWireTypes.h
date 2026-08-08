// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsAdvisoryTypes.h"
#include "Telemetry/EdenTelemetrySink.h"

namespace EdenOsWireContract
{
	inline constexpr int32 CurrentSchemaVersion = 1;

	inline const TCHAR* MissionEnvironmentOrigin = TEXT("mission_environment");

	inline const TCHAR* CreateSessionRoute = TEXT("/api/missions/sessions");
	inline const TCHAR* TelemetryRouteTemplate = TEXT("/api/missions/sessions/{id}/telemetry");
	inline const TCHAR* EventsRouteTemplate = TEXT("/api/missions/sessions/{id}/events");
	inline const TCHAR* CompleteRouteTemplate = TEXT("/api/missions/sessions/{id}/complete");
	inline const TCHAR* AdvisoriesRouteTemplate = TEXT("/api/missions/sessions/{id}/advisories");
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

/** One advisory evaluation sent to ProjectEden. Built from an accepted H context; never rebuilt. */
struct EDENSPACESIMULATOR_API FEdenOsAdvisoryRequestV1
{
	FString SessionId;

	/** Stable identity for this evaluation. Must survive a later HTTP completion unchanged. */
	FString EvaluationId;

	FEdenOsAdvisoryContext Context;
};

/** Parsed §19.3 response. Informational only: it carries no executable action. */
struct EDENSPACESIMULATOR_API FEdenOsAdvisoryResponseV1
{
	int32 SchemaVersion = 0;
	FString AdvisoryId;
	FString EvaluationId;
	FString Recommendation;
	FString Rationale;
};

struct EDENSPACESIMULATOR_API FEdenOsAdvisoryResponseParseResult
{
	static FEdenOsAdvisoryResponseParseResult Succeeded(FEdenOsAdvisoryResponseV1 InResponse);
	static FEdenOsAdvisoryResponseParseResult Failed(FString InErrorMessage);

	bool IsSuccess() const;

	bool bSuccess = false;
	FEdenOsAdvisoryResponseV1 Response;
	FString ErrorMessage;
};

struct EDENSPACESIMULATOR_API FEdenOsWireSerializationModel
{
	/**
	 * Locked §19.2a wire vocabulary. Exactly five values, no aliases; the retired
	 * "meaningful_operator_action" spelling is deliberately not produced.
	 */
	static FString TriggerReasonToWireValue(EEdenOsAdvisoryTriggerReason Reason);

	static FEdenOsWireSerializationResult BuildAdvisoryJsonV1(const FEdenOsAdvisoryRequestV1& Request);

	/**
	 * Parses and validates a §19.3 response.
	 * ExpectedEvaluationId correlates the response to its pending evaluation; a mismatch fails.
	 */
	static FEdenOsAdvisoryResponseParseResult ParseAdvisoryResponseV1(
		const FString& Json,
		const FString& ExpectedEvaluationId);

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
