// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"
#include "EdenOs/EdenExternalCommandTypes.h"
#include "EdenOs/EdenOsAdvisoryTypes.h"
#include "EdenOs/EdenOsTelemetrySink.h"
#include "EdenOs/EdenOsTypes.h"
#include "EdenOs/EdenOsTransport.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Telemetry/EdenTelemetrySink.h"
#include "Subsystems/WorldSubsystem.h"

#include "EdenOsAdapterSubsystem.generated.h"

class UEdenExternalCommandExecutor;
class UEdenExternalCommandRouter;
class UEdenOperatorControlComponent;
class UEdenSimulationClockSubsystem;

UCLASS()
class EDENSPACESIMULATOR_API UEdenOsAdapterSubsystem : public UWorldSubsystem, public IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// IEdenSimulationTickable. Observation only: registered at EdenSimulationClockPriority::Advisory
	// so it runs strictly after systems, mission, and telemetry have settled for the step.
	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Eden|OS")
	FEdenOsConnectionSnapshot GetConnectionSnapshot() const;

	FEdenOsValidationResult GetLastValidationResult() const;

	bool ApplyRuntimeConfig(const FEdenOsConnectionConfig& InConfig);
	/** Enable transport using config defaults; optional BaseUrl override. Preserves any injected RuntimeBearerJwt. */
	bool EnableRuntimeConnection(const FString& BaseUrl);
	void SetRuntimeBearerJwt(const FString& InBearerJwt);
	void ClearRuntimeBearerJwt();
	bool LoadRuntimeBearerJwtFromEnvironment(const FString& VariableName);
	FEdenTelemetrySinkResult EnqueueOutboundRequest(FEdenOsQueuedRequest Request);
	FString GetDefaultScenarioId() const;

	void SetHttpTransportForTesting(IEdenOsHttpTransport* InTransport);
	bool IsUsingProductionHttpTransportForTesting() const;
	TArray<FEdenOsDeliveryRecord> GetDeliveryHistoryForTesting() const;

	/** Most recent advisory context built by a settled-step evaluation. Invalid until one occurs. */
	FEdenOsAdvisoryContext GetLastAdvisoryContext() const;

	/** Most recent ProjectEden advisory accepted for HUD presentation. Invalid until one is accepted. */
	FEdenOsAcceptedAdvisory GetLatestAcceptedAdvisory() const;

	/**
	 * Checkpoint J: validate a typed external command proposal. Never executes.
	 * Uses active telemetry session + latest accepted advisory evaluation for correlation.
	 * On Valid, Outcome.ValidatedCommand binds exact typed parameters for Checkpoint K.
	 */
	FEdenExternalCommandValidationOutcome ValidateExternalCommandProposal(
		const FEdenExternalCommandProposal& Proposal);

	/**
	 * Checkpoint K: execute a previously validated artifact through operator control.
	 * Does not accept raw proposals. Does not auto-run after validation.
	 */
	FEdenExternalCommandExecutionResult ExecuteValidatedExternalCommand(
		const FEdenValidatedExternalCommand& Command);

	TArray<FEdenExternalCommandValidationRecord> GetExternalCommandValidationHistory() const;
	TArray<FEdenExternalCommandExecutionRecord> GetExternalCommandExecutionHistory() const;

	/** Test seam: install an accepted advisory without HTTP (validation-only proofs). */
	void SetLatestAcceptedAdvisoryForTesting(const FEdenOsAcceptedAdvisory& Advisory);

	/** Number of advisory evaluations performed since the adapter last reset its advisory cursors. */
	int32 GetAdvisoryEvaluationCount() const;

	FEdenOsAdvisoryContextBounds GetAdvisoryContextBounds() const;
	void SetAdvisoryContextBounds(const FEdenOsAdvisoryContextBounds& InBounds);

	bool RegisterWithSimulationClock();
	bool UnregisterFromSimulationClock();

private:
	struct FPendingAdvisoryDispatch
	{
		FString EvaluationId;
		FEdenOsAdvisoryContext Context;
		int64 EvaluationOrdinal = 0;
	};

	struct FPendingCommandProposalDispatch
	{
		FString EvaluationId;
		FString SessionId;
	};

	void EvaluateAdvisoryForSettledStep();
	void DispatchAdvisoryEvaluation(const FEdenOsAdvisoryContext& Context);
	void HandleAdvisoryTransportCompleted(const FEdenOsHttpResult& Result, int64 SequenceNumber);
	void AcceptAdvisoryResponse(
		const FEdenOsAdvisoryResponseV1& Response,
		const FPendingAdvisoryDispatch& Pending);
	void MaybeDispatchCommandProposalAutomation(const FPendingAdvisoryDispatch& Pending);
	void DispatchCommandProposalForEvaluation(const FString& EvaluationId, const FString& SessionId);
	void HandleCommandProposalTransportCompleted(const FEdenOsHttpResult& Result, int64 SequenceNumber);
	bool AreCommandProposalAutomationHttpGatesOpen() const;
	void RebindCommandProposalSessionIfNeeded(const FString& SessionId);
	void CancelPendingAdvisoryDispatches(const TCHAR* Reason);
	void CancelPendingCommandProposalDispatches(const TCHAR* Reason);
	bool HasSessionCompletedOrCompletionQueued() const;
	void ResetAdvisoryRuntimeState();
	void EnsureExternalCommandRouter();
	void EnsureExternalCommandExecutor();
	FEdenExternalCommandValidationContext BuildExternalCommandValidationContext() const;
	FEdenExternalCommandExecutionContext BuildExternalCommandExecutionContext() const;
	UEdenOperatorControlComponent* ResolveOperatorControlComponent() const;
	void EmitExternalCommandExecutedTelemetry(
		const FEdenValidatedExternalCommand& Command,
		const FEdenExternalCommandExecutionResult& Result);

	void RefreshSnapshotFromRuntimeConfig();
	void RegisterTelemetrySinkIfNeeded();
	void UnregisterTelemetrySink();
	void PumpOutboundQueue();
	void HandleTransportCompleted(
		const FEdenOsHttpResult& Result,
		EEdenOsOutboundMessageType MessageType,
		const FString& RoutePath,
		int64 SequenceNumber);
	void ResetTransportRuntimeState();
	void AppendDeliveryRecord(
		EEdenOsOutboundMessageType MessageType,
		const FString& RoutePath,
		int64 SequenceNumber,
		const FEdenOsHttpResult& Result);

	FEdenOsConnectionConfig RuntimeConfig;
	FEdenOsValidationResult LastValidationResult;

	UPROPERTY(Transient)
	FEdenOsConnectionSnapshot ConnectionSnapshot;

	TArray<FEdenOsQueuedRequest> OutboundQueue;
	TArray<FEdenOsDeliveryRecord> DeliveryHistory;
	TUniquePtr<IEdenOsHttpTransport> OwnedHttpTransport;
	IEdenOsHttpTransport* ActiveHttpTransport = nullptr;
	TUniquePtr<class FEdenOsTelemetrySink> OwnedTelemetrySink;
	TWeakObjectPtr<class UEdenTelemetrySubsystem> RegisteredTelemetrySubsystem;
	int32 DroppedOutboundMessageCount = 0;
	int64 NextOutboundSequenceNumber = 1;
	bool bTransportRequestInFlight = false;
	bool bAcceptTransportCallbacks = true;
	bool bHasSuccessfulTransportDelivery = false;

	TWeakObjectPtr<UEdenSimulationClockSubsystem> RegisteredSimulationClock;

	// Advisory bookkeeping. These are cursors into telemetry's own history plus records of this
	// adapter's evaluations. They are deliberately NOT copies of simulation or telemetry truth:
	// trend and transition detection read accepted 0006 history, never a shadow copy of it.
	FEdenOsAdvisoryContextBounds AdvisoryContextBounds;
	FEdenOsAdvisoryContext LastAdvisoryContext;
	FEdenOsAcceptedAdvisory LatestAcceptedAdvisory;
	TMap<int64, FPendingAdvisoryDispatch> PendingAdvisoryByOutboundSequence;
	int64 LastEvaluatedTelemetrySequence = 0;
	float LastAdvisoryEvaluationSimulationSeconds = 0.0f;
	bool bHasEvaluatedAdvisory = false;
	int32 AdvisoryEvaluationCount = 0;
	int64 NextAdvisoryEvaluationOrdinal = 1;
	int64 LatestAcceptedAdvisoryOrdinal = 0;

	// Checkpoint L automation bookkeeping only — not mission/advisory/operator truth.
	FString BoundCommandProposalSessionId;
	TSet<FString> RequestedProposalEvaluationIds;
	TMap<int64, FPendingCommandProposalDispatch> PendingCommandProposalByOutboundSequence;

	UPROPERTY(Transient)
	TObjectPtr<UEdenExternalCommandRouter> ExternalCommandRouter;

	UPROPERTY(Transient)
	TObjectPtr<UEdenExternalCommandExecutor> ExternalCommandExecutor;

public:
	/** Settled steps observed. Distinguishes "not ticking" from "ticking but gated" in diagnostics. */
	int32 GetAdvisoryTickCountForTesting() const { return AdvisoryTickCount; }

	/** Test seam: count of evaluations that have already attempted a command-proposal request. */
	int32 GetRequestedCommandProposalEvaluationCountForTesting() const
	{
		return RequestedProposalEvaluationIds.Num();
	}

	bool HasRequestedCommandProposalForEvaluationForTesting(const FString& EvaluationId) const
	{
		return RequestedProposalEvaluationIds.Contains(EvaluationId);
	}

private:
	int32 AdvisoryTickCount = 0;
};
