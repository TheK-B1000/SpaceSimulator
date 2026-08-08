// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsTelemetrySink.h"
#include "EdenOs/EdenOsTypes.h"
#include "EdenOs/EdenOsTransport.h"
#include "Telemetry/EdenTelemetrySink.h"
#include "Subsystems/WorldSubsystem.h"

#include "EdenOsAdapterSubsystem.generated.h"

UCLASS()
class EDENSPACESIMULATOR_API UEdenOsAdapterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	UFUNCTION(BlueprintPure, Category = "Eden|OS")
	FEdenOsConnectionSnapshot GetConnectionSnapshot() const;

	FEdenOsValidationResult GetLastValidationResult() const;

	bool ApplyRuntimeConfig(const FEdenOsConnectionConfig& InConfig);
	void SetRuntimeBearerJwt(const FString& InBearerJwt);
	void ClearRuntimeBearerJwt();
	bool LoadRuntimeBearerJwtFromEnvironment(const FString& VariableName);
	FEdenTelemetrySinkResult EnqueueOutboundRequest(FEdenOsQueuedRequest Request);

	void SetHttpTransportForTesting(IEdenOsHttpTransport* InTransport);

private:
	void RefreshSnapshotFromRuntimeConfig();
	void RegisterTelemetrySinkIfNeeded();
	void UnregisterTelemetrySink();
	void PumpOutboundQueue();
	void HandleTransportCompleted(const FEdenOsHttpResult& Result);
	void ResetTransportRuntimeState();

	FEdenOsConnectionConfig RuntimeConfig;
	FEdenOsValidationResult LastValidationResult;

	UPROPERTY(Transient)
	FEdenOsConnectionSnapshot ConnectionSnapshot;

	TArray<FEdenOsQueuedRequest> OutboundQueue;
	TUniquePtr<IEdenOsHttpTransport> OwnedHttpTransport;
	IEdenOsHttpTransport* ActiveHttpTransport = nullptr;
	TUniquePtr<class FEdenOsTelemetrySink> OwnedTelemetrySink;
	TWeakObjectPtr<class UEdenTelemetrySubsystem> RegisteredTelemetrySubsystem;
	int32 DroppedOutboundMessageCount = 0;
	int64 NextOutboundSequenceNumber = 1;
	bool bTransportRequestInFlight = false;
	bool bAcceptTransportCallbacks = true;
	bool bHasSuccessfulTransportDelivery = false;
};
