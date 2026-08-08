// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Telemetry/EdenTelemetrySink.h"

struct EDENSPACESIMULATOR_API FEdenOsMissionLifecycleModel
{
	static int64 ResolveLatestSequence(const FEdenTelemetrySessionPayload& Payload);
	static bool ResolveFinalStatus(
		const FEdenTelemetrySessionPayload& Payload,
		EEdenOsMissionFinalStatus& OutFinalStatus);
	static int32 CountAlertRaisedEvents(const FEdenTelemetrySessionPayload& Payload);

	static FEdenOsMissionSessionCreateRequestV1 BuildSessionCreateRequest(
		const FEdenTelemetrySessionPayload& Payload,
		const FString& ScenarioId,
		const FString& StartedAtIso8601);

	static bool BuildSessionCompleteRequest(
		const FEdenTelemetrySessionPayload& Payload,
		const FString& CompletedAtIso8601,
		FEdenOsSessionCompleteRequestV1& OutRequest);
};
