// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Telemetry/EdenTelemetryTypes.h"

/**
 * Immutable session payload passed from telemetry ownership to transport sinks.
 * Events and snapshots are read-only views into telemetry history for the duration of delivery.
 */
struct EDENSPACESIMULATOR_API FEdenTelemetrySessionPayload
{
	FEdenTelemetrySessionPayload() = default;

	FEdenTelemetrySessionPayload(
		TConstArrayView<FEdenTelemetryEvent> InEvents,
		TConstArrayView<FEdenTelemetrySnapshot> InSnapshots,
		const FEdenTelemetrySessionMetadata& InMetadata,
		FString InSessionId,
		FName InMissionId);

	FString GetSafeSessionId() const;

	TConstArrayView<FEdenTelemetryEvent> Events;
	TConstArrayView<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	FString SessionId;
	FName MissionId = NAME_None;
};

struct EDENSPACESIMULATOR_API FEdenTelemetrySinkResult
{
	static FEdenTelemetrySinkResult Succeeded(FString InDestination);
	static FEdenTelemetrySinkResult Failed(FString InErrorMessage);

	bool IsSuccess() const;

	EEdenSinkResult Result = EEdenSinkResult::Failed;
	FString Destination;
	FString ErrorMessage;
};

class EDENSPACESIMULATOR_API IEdenTelemetrySink
{
public:
	virtual ~IEdenTelemetrySink() = default;

	virtual FName GetTelemetrySinkName() const = 0;
	virtual FEdenTelemetrySinkResult DeliverTelemetrySession(const FEdenTelemetrySessionPayload& Payload) = 0;
};

class EDENSPACESIMULATOR_API FEdenLocalJsonTelemetrySink final : public IEdenTelemetrySink
{
public:
	explicit FEdenLocalJsonTelemetrySink(FString InOutputDirectory = FString());

	virtual FName GetTelemetrySinkName() const override;
	virtual FEdenTelemetrySinkResult DeliverTelemetrySession(const FEdenTelemetrySessionPayload& Payload) override;

private:
	FString ResolveOutputDirectory() const;

	FString OutputDirectory;
};
