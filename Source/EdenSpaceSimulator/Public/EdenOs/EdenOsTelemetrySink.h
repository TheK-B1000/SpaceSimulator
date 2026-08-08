// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Telemetry/EdenTelemetrySink.h"

struct FEdenOsQueuedRequest;
class UEdenOsAdapterSubsystem;

class EDENSPACESIMULATOR_API FEdenOsTelemetrySink final : public IEdenTelemetrySink
{
public:
	explicit FEdenOsTelemetrySink(UEdenOsAdapterSubsystem& InAdapterSubsystem);

	virtual FName GetTelemetrySinkName() const override;
	virtual FEdenTelemetrySinkResult DeliverTelemetrySession(const FEdenTelemetrySessionPayload& Payload) override;
	void ResetSessionDeliveryState();

private:
	struct FSessionDeliveryState
	{
		FString SessionId;
		bool bCreateQueued = false;
		bool bCompleteQueued = false;
		int64 LastTelemetrySequence = INDEX_NONE;
		int64 LastEventSequence = INDEX_NONE;
	};

	void ResetSessionDeliveryStateFor(const FString& SessionId);
	FEdenTelemetrySinkResult QueueLifecycleRequest(FEdenOsQueuedRequest&& Request);
	static FString MakeUtcTimestampIso8601();

	TWeakObjectPtr<UEdenOsAdapterSubsystem> AdapterSubsystem;
	FSessionDeliveryState DeliveryState;
};
