// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenOs/EdenExternalCommandExecutor.h"
#include "EdenOs/EdenExternalCommandRouter.h"
#include "EdenOs/EdenOsAdvisoryModel.h"
#include "EdenOs/EdenOsConnectionSettings.h"
#include "EdenOs/EdenOsTelemetrySink.h"
#include "EdenOs/EdenOsUnrealHttpTransport.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Telemetry/EdenTelemetryExportModel.h"
#include "Telemetry/EdenTelemetrySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"

bool UEdenOsAdapterSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UEdenOsAdapterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Advisory evaluation ticks off the simulation clock, so the clock must exist before we register.
	Collection.InitializeDependency(UEdenSimulationClockSubsystem::StaticClass());

	OwnedHttpTransport = MakeUnique<FEdenOsUnrealHttpTransport>();
	ActiveHttpTransport = OwnedHttpTransport.Get();
	bAcceptTransportCallbacks = true;
	EnsureExternalCommandRouter();
	EnsureExternalCommandExecutor();

	const UEdenOsConnectionSettings* Settings = GetDefault<UEdenOsConnectionSettings>();
	ApplyRuntimeConfig(Settings ? Settings->MakeConnectionConfig() : FEdenOsConnectionConfig());
	RegisterTelemetrySinkIfNeeded();
	RegisterWithSimulationClock();
}

void UEdenOsAdapterSubsystem::Deinitialize()
{
	bAcceptTransportCallbacks = false;
	UnregisterFromSimulationClock();
	ResetAdvisoryRuntimeState();
	UnregisterTelemetrySink();
	ResetTransportRuntimeState();
	RuntimeConfig = FEdenOsConnectionConfig();
	LastValidationResult = FEdenOsValidationResult();
	ConnectionSnapshot = FEdenOsConnectionSnapshot();
	ExternalCommandRouter = nullptr;
	ExternalCommandExecutor = nullptr;
	OwnedHttpTransport.Reset();
	ActiveHttpTransport = nullptr;

	Super::Deinitialize();
}

FEdenOsConnectionSnapshot UEdenOsAdapterSubsystem::GetConnectionSnapshot() const
{
	return ConnectionSnapshot;
}

FEdenOsValidationResult UEdenOsAdapterSubsystem::GetLastValidationResult() const
{
	return LastValidationResult;
}

bool UEdenOsAdapterSubsystem::ApplyRuntimeConfig(const FEdenOsConnectionConfig& InConfig)
{
	const bool bWasEnabled = RuntimeConfig.bEnabled;
	RuntimeConfig = InConfig;
	ResetTransportRuntimeState();
	ResetAdvisoryRuntimeState();
	RefreshSnapshotFromRuntimeConfig();
	if (bWasEnabled && !RuntimeConfig.bEnabled)
	{
		UnregisterTelemetrySink();
	}
	RegisterTelemetrySinkIfNeeded();
	// Re-attempt clock registration: a world may configure the adapter after subsystem startup, and
	// the clock rejects duplicate registrations, so this is safe to call repeatedly.
	RegisterWithSimulationClock();
	return LastValidationResult.IsValid();
}

bool UEdenOsAdapterSubsystem::EnableRuntimeConnection(const FString& BaseUrl)
{
	const FString PreservedJwt = RuntimeConfig.RuntimeBearerJwt;
	const UEdenOsConnectionSettings* Settings = GetDefault<UEdenOsConnectionSettings>();
	FEdenOsConnectionConfig Config = Settings ? Settings->MakeConnectionConfig() : FEdenOsConnectionConfig();
	Config.bEnabled = true;
	const FString TrimmedBaseUrl = BaseUrl.TrimStartAndEnd();
	if (!TrimmedBaseUrl.IsEmpty())
	{
		Config.BaseUrl = TrimmedBaseUrl;
	}
	Config.RuntimeBearerJwt = PreservedJwt;
	return ApplyRuntimeConfig(Config);
}

void UEdenOsAdapterSubsystem::SetRuntimeBearerJwt(const FString& InBearerJwt)
{
	RuntimeConfig.RuntimeBearerJwt = InBearerJwt;
	RefreshSnapshotFromRuntimeConfig();
}

void UEdenOsAdapterSubsystem::ClearRuntimeBearerJwt()
{
	RuntimeConfig.RuntimeBearerJwt.Reset();
	RefreshSnapshotFromRuntimeConfig();
}

bool UEdenOsAdapterSubsystem::LoadRuntimeBearerJwtFromEnvironment(const FString& VariableName)
{
	if (VariableName.IsEmpty())
	{
		return false;
	}

	const FString RuntimeJwt = FPlatformMisc::GetEnvironmentVariable(*VariableName).TrimStartAndEnd();
	if (RuntimeJwt.IsEmpty())
	{
		ClearRuntimeBearerJwt();
		return false;
	}

	SetRuntimeBearerJwt(RuntimeJwt);
	return true;
}

FEdenTelemetrySinkResult UEdenOsAdapterSubsystem::EnqueueOutboundRequest(FEdenOsQueuedRequest Request)
{
	LastValidationResult = FEdenOsConnectionConfigModel::Validate(RuntimeConfig);
	ConnectionSnapshot.bEnabled = RuntimeConfig.bEnabled;
	ConnectionSnapshot.AuthorityMode = RuntimeConfig.AuthorityMode;
	ConnectionSnapshot.bExternalCommandValidationEnabled = RuntimeConfig.bExternalCommandValidationEnabled;
	ConnectionSnapshot.bExternalCommandExecutionEnabled = RuntimeConfig.bExternalCommandExecutionEnabled;
	ConnectionSnapshot.bExternalCommandAutomationEnabled = RuntimeConfig.bExternalCommandAutomationEnabled;
	ConnectionSnapshot.bHasBearerJwt = !RuntimeConfig.RuntimeBearerJwt.IsEmpty();
	if (!RuntimeConfig.bEnabled)
	{
		return FEdenTelemetrySinkResult::Failed(TEXT("EDEN OS transport is disabled."));
	}
	if (!LastValidationResult.IsValid())
	{
		return FEdenTelemetrySinkResult::Failed(LastValidationResult.GetFirstErrorOrEmpty());
	}
	if (Request.RoutePath.TrimStartAndEnd().IsEmpty())
	{
		return FEdenTelemetrySinkResult::Failed(TEXT("EDEN OS outbound request requires a route path."));
	}
	if (Request.BodyJson.TrimStartAndEnd().IsEmpty())
	{
		return FEdenTelemetrySinkResult::Failed(TEXT("EDEN OS outbound request requires a JSON body."));
	}

	const int32 OutstandingCount = OutboundQueue.Num() + (bTransportRequestInFlight ? 1 : 0);
	if (OutstandingCount >= RuntimeConfig.MaxQueueDepth)
	{
		++DroppedOutboundMessageCount;
		const FEdenOsHttpResult DropResult =
			FEdenOsHttpResult::Failed(0, TEXT("EDEN OS outbound queue full; dropped newest message."));
		ConnectionSnapshot = FEdenOsTransportModel::MakeSnapshotForOutcome(
			ConnectionSnapshot,
			RuntimeConfig,
			LastValidationResult,
			bTransportRequestInFlight,
			OutboundQueue.Num(),
			DroppedOutboundMessageCount,
			&DropResult);
		ConnectionSnapshot.LastErrorSummary = DropResult.ErrorSummary;
		return FEdenTelemetrySinkResult::Failed(TEXT("EDEN OS outbound queue full; dropped newest message."));
	}

	Request.SequenceNumber = NextOutboundSequenceNumber++;
	OutboundQueue.Add(MoveTemp(Request));
	ConnectionSnapshot = FEdenOsTransportModel::MakeSnapshotForOutcome(
		ConnectionSnapshot,
		RuntimeConfig,
		LastValidationResult,
		bTransportRequestInFlight,
		OutboundQueue.Num(),
		DroppedOutboundMessageCount,
		nullptr);

	PumpOutboundQueue();
	return FEdenTelemetrySinkResult::Succeeded(TEXT("eden-os:queued"));
}

FString UEdenOsAdapterSubsystem::GetDefaultScenarioId() const
{
	return RuntimeConfig.DefaultScenarioId;
}

void UEdenOsAdapterSubsystem::SetHttpTransportForTesting(IEdenOsHttpTransport* InTransport)
{
	ActiveHttpTransport = InTransport ? InTransport : OwnedHttpTransport.Get();
}

bool UEdenOsAdapterSubsystem::IsUsingProductionHttpTransportForTesting() const
{
	return OwnedHttpTransport.IsValid() && ActiveHttpTransport == OwnedHttpTransport.Get();
}

TArray<FEdenOsDeliveryRecord> UEdenOsAdapterSubsystem::GetDeliveryHistoryForTesting() const
{
	return DeliveryHistory;
}

void UEdenOsAdapterSubsystem::RefreshSnapshotFromRuntimeConfig()
{
	LastValidationResult = FEdenOsConnectionConfigModel::Validate(RuntimeConfig);
	const FEdenOsConnectionSnapshot InitialSnapshot =
		FEdenOsConnectionConfigModel::MakeInitialSnapshot(RuntimeConfig, LastValidationResult);
	ConnectionSnapshot = FEdenOsTransportModel::MakeSnapshotForOutcome(
		InitialSnapshot,
		RuntimeConfig,
		LastValidationResult,
		bTransportRequestInFlight,
		OutboundQueue.Num(),
		DroppedOutboundMessageCount,
		nullptr);
}

void UEdenOsAdapterSubsystem::RegisterTelemetrySinkIfNeeded()
{
	if (!RuntimeConfig.bEnabled || !LastValidationResult.IsValid() || RegisteredTelemetrySubsystem.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenTelemetrySubsystem* TelemetrySubsystem = World->GetSubsystem<UEdenTelemetrySubsystem>();
	if (!TelemetrySubsystem)
	{
		return;
	}

	if (!OwnedTelemetrySink.IsValid())
	{
		OwnedTelemetrySink = MakeUnique<FEdenOsTelemetrySink>(*this);
	}

	if (TelemetrySubsystem->RegisterTelemetrySink(OwnedTelemetrySink.Get()))
	{
		RegisteredTelemetrySubsystem = TelemetrySubsystem;
	}
}

void UEdenOsAdapterSubsystem::UnregisterTelemetrySink()
{
	if (UEdenTelemetrySubsystem* TelemetrySubsystem = RegisteredTelemetrySubsystem.Get())
	{
		if (OwnedTelemetrySink.IsValid())
		{
			TelemetrySubsystem->UnregisterTelemetrySink(OwnedTelemetrySink.Get());
		}
	}
	RegisteredTelemetrySubsystem.Reset();
	OwnedTelemetrySink.Reset();
}

void UEdenOsAdapterSubsystem::PumpOutboundQueue()
{
	if (bTransportRequestInFlight || OutboundQueue.IsEmpty())
	{
		return;
	}

	if (!ActiveHttpTransport)
	{
		HandleTransportCompleted(
			FEdenOsHttpResult::Failed(0, TEXT("EDEN OS HTTP transport is unavailable.")),
			EEdenOsOutboundMessageType::Telemetry,
			TEXT("<missing-route>"),
			0);
		return;
	}

	FEdenOsQueuedRequest Request = MoveTemp(OutboundQueue[0]);
	OutboundQueue.RemoveAt(0, 1, EAllowShrinking::No);
	const EEdenOsOutboundMessageType MessageType = Request.MessageType;
	const FString RoutePath = Request.RoutePath;
	const int64 SequenceNumber = Request.SequenceNumber;
	bTransportRequestInFlight = true;
	ConnectionSnapshot = FEdenOsTransportModel::MakeSnapshotForOutcome(
		ConnectionSnapshot,
		RuntimeConfig,
		LastValidationResult,
		bTransportRequestInFlight,
		OutboundQueue.Num(),
		DroppedOutboundMessageCount,
		nullptr);

	FEdenOsHttpRequestData HttpRequest;
	HttpRequest.Url = FEdenOsUrlModel::JoinBaseUrlAndRoute(RuntimeConfig.BaseUrl, Request.RoutePath);
	HttpRequest.BodyJson = MoveTemp(Request.BodyJson);
	HttpRequest.AuthorizationBearerJwt = RuntimeConfig.RuntimeBearerJwt;
	HttpRequest.TimeoutSeconds = RuntimeConfig.RequestTimeoutSeconds;

	TWeakObjectPtr<UEdenOsAdapterSubsystem> WeakThis(this);
	const bool bStarted = ActiveHttpTransport->SendAsync(
		HttpRequest,
		FEdenOsHttpCompletion::CreateLambda(
			[WeakThis, MessageType, RoutePath, SequenceNumber](FEdenOsHttpResult Result)
			{
				if (UEdenOsAdapterSubsystem* Adapter = WeakThis.Get())
				{
					Adapter->HandleTransportCompleted(Result, MessageType, RoutePath, SequenceNumber);
				}
			}));

	if (!bStarted)
	{
		HandleTransportCompleted(
			FEdenOsHttpResult::Failed(0, TEXT("EDEN OS HTTP request could not be started.")),
			MessageType,
			RoutePath,
			SequenceNumber);
	}
}

void UEdenOsAdapterSubsystem::HandleTransportCompleted(
	const FEdenOsHttpResult& Result,
	EEdenOsOutboundMessageType MessageType,
	const FString& RoutePath,
	int64 SequenceNumber)
{
	if (!bAcceptTransportCallbacks)
	{
		return;
	}

	bTransportRequestInFlight = false;
	AppendDeliveryRecord(MessageType, RoutePath, SequenceNumber, Result);
	ConnectionSnapshot = FEdenOsTransportModel::MakeSnapshotForOutcome(
		ConnectionSnapshot,
		RuntimeConfig,
		LastValidationResult,
		bTransportRequestInFlight,
		OutboundQueue.Num(),
		DroppedOutboundMessageCount,
		&Result);
	const bool bIgnoreAdvisoryFailureAfterComplete =
		MessageType == EEdenOsOutboundMessageType::Advisory
		&& !Result.IsSuccess()
		&& HasSessionCompletedOrCompletionQueued();
	if (Result.IsSuccess())
	{
		bHasSuccessfulTransportDelivery = true;
	}
	else if (bHasSuccessfulTransportDelivery && !bIgnoreAdvisoryFailureAfterComplete)
	{
		ConnectionSnapshot.ConnectionState = EEdenOsConnectionState::Degraded;
	}
	else if (bIgnoreAdvisoryFailureAfterComplete && bHasSuccessfulTransportDelivery)
	{
		// Keep Connected: a late advisory losing the race to SessionComplete is expected, not degraded.
		ConnectionSnapshot.ConnectionState = EEdenOsConnectionState::Connected;
		ConnectionSnapshot.LastErrorSummary.Reset();
	}

	if (MessageType == EEdenOsOutboundMessageType::Advisory)
	{
		HandleAdvisoryTransportCompleted(Result, SequenceNumber);
	}
	else if (MessageType == EEdenOsOutboundMessageType::CommandProposal)
	{
		HandleCommandProposalTransportCompleted(Result, SequenceNumber);
	}
	else if (MessageType == EEdenOsOutboundMessageType::SessionComplete && Result.IsSuccess())
	{
		// Terminal session: drop any advisory/command-proposal work that would 409 against ProjectEden.
		CancelPendingAdvisoryDispatches(TEXT("session completed"));
		CancelPendingCommandProposalDispatches(TEXT("session completed"));
	}

	PumpOutboundQueue();
}

void UEdenOsAdapterSubsystem::AppendDeliveryRecord(
	EEdenOsOutboundMessageType MessageType,
	const FString& RoutePath,
	int64 SequenceNumber,
	const FEdenOsHttpResult& Result)
{
	FEdenOsDeliveryRecord Record;
	Record.MessageType = MessageType;
	Record.RoutePath = RoutePath;
	Record.SequenceNumber = SequenceNumber;
	Record.HttpStatusCode = Result.HttpStatusCode;
	Record.bSucceeded = Result.IsSuccess();
	Record.ErrorSummary = Result.ErrorSummary;
	Record.ResponseBodyJson = Result.ResponseBodyJson;
	DeliveryHistory.Add(MoveTemp(Record));

	constexpr int32 MaxDeliveryHistoryRecords = 256;
	if (DeliveryHistory.Num() > MaxDeliveryHistoryRecords)
	{
		DeliveryHistory.RemoveAt(0, DeliveryHistory.Num() - MaxDeliveryHistoryRecords, EAllowShrinking::No);
	}
}

void UEdenOsAdapterSubsystem::ResetTransportRuntimeState()
{
	OutboundQueue.Reset();
	DeliveryHistory.Reset();
	if (OwnedTelemetrySink.IsValid())
	{
		OwnedTelemetrySink->ResetSessionDeliveryState();
	}
	DroppedOutboundMessageCount = 0;
	NextOutboundSequenceNumber = 1;
	bTransportRequestInFlight = false;
	bHasSuccessfulTransportDelivery = false;
}

bool UEdenOsAdapterSubsystem::RegisterWithSimulationClock()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	if (!Clock)
	{
		return false;
	}

	if (!Clock->RegisterSimulationTickable(this, EdenSimulationClockPriority::Advisory))
	{
		return false;
	}

	RegisteredSimulationClock = Clock;
	return true;
}

bool UEdenOsAdapterSubsystem::UnregisterFromSimulationClock()
{
	if (!RegisteredSimulationClock.IsValid())
	{
		return false;
	}

	RegisteredSimulationClock->UnregisterSimulationTickable(this);
	RegisteredSimulationClock.Reset();
	return true;
}

void UEdenOsAdapterSubsystem::AdvanceSimulation(float FixedDeltaSeconds)
{
	(void)FixedDeltaSeconds;

	++AdvisoryTickCount;

	// Runs at EdenSimulationClockPriority::Advisory (300), strictly after systems (0), mission (100),
	// and telemetry (200). Everything read below is therefore settled state for this step.
	EvaluateAdvisoryForSettledStep();
}

void UEdenOsAdapterSubsystem::EvaluateAdvisoryForSettledStep()
{
	if (!RuntimeConfig.bEnabled)
	{
		return;
	}

	// Observe never enters the advisory-evaluation path.
	if (!FEdenOsAdvisoryModel::IsAdvisoryEvaluationPermitted(RuntimeConfig.AuthorityMode))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	if (!Telemetry)
	{
		return;
	}

	// Telemetry owns history. The adapter reads it and keeps only cursors of its own.
	const TArray<FEdenTelemetryEvent> Events = Telemetry->GetEventHistory();
	const TArray<FEdenTelemetrySnapshot> Snapshots = Telemetry->GetSnapshotHistory();
	if (Snapshots.Num() == 0)
	{
		// Nothing settled has been recorded yet; there is no state to reason about.
		return;
	}

	const FEdenTelemetrySnapshot& SettledSnapshot = Snapshots.Last();
	if (SettledSnapshot.Mission.MissionState != EEdenMissionState::Running)
	{
		// Advisories are in-mission only. Post-terminal evaluation would race SessionComplete
		// (ProjectEden returns 409 for completed sessions).
		return;
	}

	// Evaluation timing is owned by the simulation clock; telemetry owns observation history.
	// Reading the clock live is what keeps evaluation time distinct from the (decimated) snapshot
	// time and keeps the heartbeat on true fixed-step cadence rather than snapshot cadence.
	UEdenSimulationClockSubsystem* Clock = RegisteredSimulationClock.IsValid()
		? RegisteredSimulationClock.Get()
		: World->GetSubsystem<UEdenSimulationClockSubsystem>();
	if (!Clock)
	{
		return;
	}

	FEdenOsAdvisoryEvaluationInput Input;
	Input.Events = Events;
	Input.Snapshots = Snapshots;
	Input.Metadata = Telemetry->GetSessionMetadata();
	Input.SessionId = Telemetry->GetSessionId();
	Input.SimulationTimeSeconds = Clock->GetElapsedSimulationTimeSeconds();
	Input.MissionState = SettledSnapshot.Mission.MissionState;
	Input.AuthorityMode = RuntimeConfig.AuthorityMode;
	Input.HeartbeatIntervalSimulationSeconds = RuntimeConfig.AdvisoryHeartbeatSimulationSeconds;
	Input.Bounds = AdvisoryContextBounds;
	Input.LastEvaluatedSequence = LastEvaluatedTelemetrySequence;
	Input.LastEvaluationSimulationSeconds = LastAdvisoryEvaluationSimulationSeconds;
	Input.bHasEvaluatedBefore = bHasEvaluatedAdvisory;

	const FEdenOsAdvisoryEvaluationResult Result = FEdenOsAdvisoryModel::Evaluate(Input);
	if (!Result.bShouldEvaluate)
	{
		return;
	}

	LastAdvisoryContext = Result.Context;
	LastEvaluatedTelemetrySequence = Result.NewLastEvaluatedSequence;
	LastAdvisoryEvaluationSimulationSeconds = Result.NewLastEvaluationSimulationSeconds;
	bHasEvaluatedAdvisory = true;
	++AdvisoryEvaluationCount;

	DispatchAdvisoryEvaluation(Result.Context);
}

void UEdenOsAdapterSubsystem::DispatchAdvisoryEvaluation(const FEdenOsAdvisoryContext& Context)
{
	if (!Context.bIsValid || Context.SessionId.IsEmpty())
	{
		UE_LOG(LogEdenOs, Warning, TEXT("Advisory dispatch skipped: context invalid or sessionId empty."));
		return;
	}

	UWorld* World = GetWorld();
	UEdenTelemetrySubsystem* Telemetry = World ? World->GetSubsystem<UEdenTelemetrySubsystem>() : nullptr;
	if (!Telemetry)
	{
		UE_LOG(LogEdenOs, Warning, TEXT("Advisory dispatch skipped: telemetry subsystem unavailable."));
		return;
	}

	// ProjectEden rejects advisories for unknown sessions. Flush the EDEN lifecycle sink first so
	// session create (and any pending telemetry/events) is queued ahead of this advisory request.
	const FEdenTelemetrySinkDeliverySummary LifecycleFlush = Telemetry->DeliverSessionToRegisteredSinks();
	if (LifecycleFlush.AttemptedCount == 0 || LifecycleFlush.SucceededCount == 0)
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Advisory dispatch skipped: lifecycle flush did not queue session create for '%s'."),
			*Context.SessionId);
		return;
	}

	if (HasSessionCompletedOrCompletionQueued())
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Advisory dispatch skipped for session '%s': completion already queued or delivered."),
			*Context.SessionId);
		return;
	}

	const int64 EvaluationOrdinal = NextAdvisoryEvaluationOrdinal++;
	const FString EvaluationId = FString::Printf(TEXT("eval-%lld"), EvaluationOrdinal);

	FEdenOsAdvisoryRequestV1 Request;
	Request.SessionId = Context.SessionId;
	Request.EvaluationId = EvaluationId;
	Request.Context = Context;

	const FEdenOsWireSerializationResult Serialization =
		FEdenOsWireSerializationModel::BuildAdvisoryJsonV1(Request);
	if (!Serialization.IsSuccess())
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Advisory request serialization failed for %s: %s"),
			*EvaluationId,
			*Serialization.ErrorMessage);
		return;
	}

	const int64 SequenceNumber = NextOutboundSequenceNumber;
	FPendingAdvisoryDispatch Pending;
	Pending.EvaluationId = EvaluationId;
	Pending.Context = Context;
	Pending.EvaluationOrdinal = EvaluationOrdinal;
	PendingAdvisoryByOutboundSequence.Add(SequenceNumber, MoveTemp(Pending));

	FEdenOsQueuedRequest Queued;
	Queued.MessageType = EEdenOsOutboundMessageType::Advisory;
	Queued.RoutePath = FEdenOsUrlModel::BuildSessionRoute(
		EdenOsWireContract::AdvisoriesRouteTemplate,
		Context.SessionId);
	Queued.BodyJson = Serialization.Json;

	const FEdenTelemetrySinkResult EnqueueResult = EnqueueOutboundRequest(MoveTemp(Queued));
	if (!EnqueueResult.IsSuccess())
	{
		PendingAdvisoryByOutboundSequence.Remove(SequenceNumber);
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Advisory enqueue failed for %s: %s"),
			*EvaluationId,
			*EnqueueResult.ErrorMessage);
	}
}

void UEdenOsAdapterSubsystem::HandleAdvisoryTransportCompleted(
	const FEdenOsHttpResult& Result,
	int64 SequenceNumber)
{
	FPendingAdvisoryDispatch Pending;
	const bool bHadPending = PendingAdvisoryByOutboundSequence.RemoveAndCopyValue(SequenceNumber, Pending);
	if (!bHadPending)
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Advisory transport completed for unknown outbound sequence %lld; ignoring."),
			SequenceNumber);
		return;
	}

	if (!Result.IsSuccess())
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Advisory transport failed for %s: %s"),
			*Pending.EvaluationId,
			*Result.ErrorSummary);
		return;
	}

	const FEdenOsAdvisoryResponseParseResult Parsed =
		FEdenOsWireSerializationModel::ParseAdvisoryResponseV1(Result.ResponseBodyJson, Pending.EvaluationId);
	if (!Parsed.IsSuccess())
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Advisory response rejected for %s: %s"),
			*Pending.EvaluationId,
			*Parsed.ErrorMessage);
		return;
	}

	AcceptAdvisoryResponse(Parsed.Response, Pending);
}

void UEdenOsAdapterSubsystem::AcceptAdvisoryResponse(
	const FEdenOsAdvisoryResponseV1& Response,
	const FPendingAdvisoryDispatch& Pending)
{
	UWorld* World = GetWorld();
	UEdenTelemetrySubsystem* Telemetry = World ? World->GetSubsystem<UEdenTelemetrySubsystem>() : nullptr;

	float IssuedSimulationTimeSeconds = Pending.Context.SimulationTimeSeconds;
	if (World)
	{
		if (const UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			IssuedSimulationTimeSeconds = Clock->GetElapsedSimulationTimeSeconds();
		}
	}

	FString TriggerReasonsJson = TEXT("[");
	for (int32 Index = 0; Index < Pending.Context.TriggerReasons.Num(); ++Index)
	{
		if (Index > 0)
		{
			TriggerReasonsJson += TEXT(", ");
		}
		TriggerReasonsJson += FString::Printf(
			TEXT("\"%s\""),
			*FEdenOsWireSerializationModel::TriggerReasonToWireValue(Pending.Context.TriggerReasons[Index]));
	}
	TriggerReasonsJson += TEXT("]");

	// Detail carries §5.9 facts only — no invented severity/confidence/codes.
	const FString Detail = FString::Printf(
		TEXT(
			"{"
			"\"advisoryId\":\"%s\","
			"\"evaluationId\":\"%s\","
			"\"recommendation\":\"%s\","
			"\"rationale\":\"%s\","
			"\"evaluationSimulationTimeSeconds\":%.6f,"
			"\"contextSnapshotSimulationTimeSeconds\":%.6f,"
			"\"triggerReasons\":%s"
			"}"),
		*FEdenTelemetryExportModel::EscapeJsonString(Response.AdvisoryId),
		*FEdenTelemetryExportModel::EscapeJsonString(Response.EvaluationId),
		*FEdenTelemetryExportModel::EscapeJsonString(Response.Recommendation),
		*FEdenTelemetryExportModel::EscapeJsonString(Response.Rationale),
		Pending.Context.SimulationTimeSeconds,
		Pending.Context.ContextSnapshotSimulationTimeSeconds,
		*TriggerReasonsJson);

	if (Telemetry)
	{
		Telemetry->RecordObservationEvent(
			EEdenTelemetryEventType::EdenAdvisoryIssued,
			TEXT("EdenOs"),
			FName(*Response.AdvisoryId),
			Detail);
	}

	// Stale-callback safety for HUD: only newer-or-equal ordinals replace LatestAcceptedAdvisory.
	if (Pending.EvaluationOrdinal < LatestAcceptedAdvisoryOrdinal)
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Accepted stale advisory %s (eval %s); telemetry recorded, HUD not updated."),
			*Response.AdvisoryId,
			*Response.EvaluationId);
		return;
	}

	LatestAcceptedAdvisoryOrdinal = Pending.EvaluationOrdinal;
	LatestAcceptedAdvisory = FEdenOsAcceptedAdvisory();
	LatestAcceptedAdvisory.bIsValid = true;
	LatestAcceptedAdvisory.AdvisoryId = Response.AdvisoryId;
	LatestAcceptedAdvisory.EvaluationId = Response.EvaluationId;
	LatestAcceptedAdvisory.Recommendation = Response.Recommendation;
	LatestAcceptedAdvisory.Rationale = Response.Rationale;
	LatestAcceptedAdvisory.EvaluationSimulationTimeSeconds = Pending.Context.SimulationTimeSeconds;
	LatestAcceptedAdvisory.ContextSnapshotSimulationTimeSeconds =
		Pending.Context.ContextSnapshotSimulationTimeSeconds;
	LatestAcceptedAdvisory.IssuedSimulationTimeSeconds = IssuedSimulationTimeSeconds;
	LatestAcceptedAdvisory.TriggerReasons = Pending.Context.TriggerReasons;

	UE_LOG(
		LogEdenOs,
		Log,
		TEXT("Accepted advisory %s for evaluation %s (issued at sim t=%.3fs)."),
		*Response.AdvisoryId,
		*Response.EvaluationId,
		IssuedSimulationTimeSeconds);

	// Checkpoint L: only after LatestAcceptedAdvisory is updated for this non-stale evaluation.
	MaybeDispatchCommandProposalAutomation(Pending);
}

void UEdenOsAdapterSubsystem::CancelPendingAdvisoryDispatches(const TCHAR* Reason)
{
	int32 RemovedQueued = 0;
	for (int32 Index = OutboundQueue.Num() - 1; Index >= 0; --Index)
	{
		if (OutboundQueue[Index].MessageType == EEdenOsOutboundMessageType::Advisory)
		{
			PendingAdvisoryByOutboundSequence.Remove(OutboundQueue[Index].SequenceNumber);
			OutboundQueue.RemoveAt(Index, 1, EAllowShrinking::No);
			++RemovedQueued;
		}
	}

	const int32 RemovedPending = PendingAdvisoryByOutboundSequence.Num();
	PendingAdvisoryByOutboundSequence.Reset();

	if (RemovedQueued > 0 || RemovedPending > 0)
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Cancelled advisory dispatches (%d queued, %d pending maps) because %s."),
			RemovedQueued,
			RemovedPending,
			Reason ? Reason : TEXT("unspecified"));
	}
}

void UEdenOsAdapterSubsystem::CancelPendingCommandProposalDispatches(const TCHAR* Reason)
{
	int32 RemovedQueued = 0;
	for (int32 Index = OutboundQueue.Num() - 1; Index >= 0; --Index)
	{
		if (OutboundQueue[Index].MessageType == EEdenOsOutboundMessageType::CommandProposal)
		{
			PendingCommandProposalByOutboundSequence.Remove(OutboundQueue[Index].SequenceNumber);
			OutboundQueue.RemoveAt(Index, 1, EAllowShrinking::No);
			++RemovedQueued;
		}
	}

	const int32 RemovedPending = PendingCommandProposalByOutboundSequence.Num();
	PendingCommandProposalByOutboundSequence.Reset();

	if (RemovedQueued > 0 || RemovedPending > 0)
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Cancelled command-proposal dispatches (%d queued, %d pending maps) because %s."),
			RemovedQueued,
			RemovedPending,
			Reason ? Reason : TEXT("unspecified"));
	}
}

bool UEdenOsAdapterSubsystem::HasSessionCompletedOrCompletionQueued() const
{
	for (const FEdenOsDeliveryRecord& Record : DeliveryHistory)
	{
		if (Record.MessageType == EEdenOsOutboundMessageType::SessionComplete && Record.bSucceeded)
		{
			return true;
		}
	}
	for (const FEdenOsQueuedRequest& Request : OutboundQueue)
	{
		if (Request.MessageType == EEdenOsOutboundMessageType::SessionComplete)
		{
			return true;
		}
	}
	return false;
}

void UEdenOsAdapterSubsystem::ResetAdvisoryRuntimeState()
{
	LastAdvisoryContext = FEdenOsAdvisoryContext();
	LatestAcceptedAdvisory = FEdenOsAcceptedAdvisory();
	PendingAdvisoryByOutboundSequence.Reset();
	LastEvaluatedTelemetrySequence = 0;
	LastAdvisoryEvaluationSimulationSeconds = 0.0f;
	bHasEvaluatedAdvisory = false;
	AdvisoryEvaluationCount = 0;
	NextAdvisoryEvaluationOrdinal = 1;
	LatestAcceptedAdvisoryOrdinal = 0;
	BoundCommandProposalSessionId.Reset();
	RequestedProposalEvaluationIds.Reset();
	PendingCommandProposalByOutboundSequence.Reset();
	if (ExternalCommandRouter)
	{
		ExternalCommandRouter->ResetValidationState();
	}
	if (ExternalCommandExecutor)
	{
		ExternalCommandExecutor->ResetExecutionState();
	}
}

bool UEdenOsAdapterSubsystem::AreCommandProposalAutomationHttpGatesOpen() const
{
	return RuntimeConfig.AuthorityMode == EEdenOsAuthorityMode::AuthorizedControl
		&& RuntimeConfig.bExternalCommandValidationEnabled
		&& RuntimeConfig.bExternalCommandAutomationEnabled;
}

void UEdenOsAdapterSubsystem::RebindCommandProposalSessionIfNeeded(const FString& SessionId)
{
	if (BoundCommandProposalSessionId == SessionId)
	{
		return;
	}

	if (!BoundCommandProposalSessionId.IsEmpty())
	{
		CancelPendingCommandProposalDispatches(TEXT("session rebound"));
	}

	BoundCommandProposalSessionId = SessionId;
	RequestedProposalEvaluationIds.Reset();
}

void UEdenOsAdapterSubsystem::MaybeDispatchCommandProposalAutomation(const FPendingAdvisoryDispatch& Pending)
{
	if (!AreCommandProposalAutomationHttpGatesOpen())
	{
		return;
	}

	if (Pending.EvaluationId.IsEmpty() || Pending.Context.SessionId.IsEmpty())
	{
		return;
	}

	if (RequestedProposalEvaluationIds.Contains(Pending.EvaluationId))
	{
		return;
	}

	DispatchCommandProposalForEvaluation(Pending.EvaluationId, Pending.Context.SessionId);
}

void UEdenOsAdapterSubsystem::DispatchCommandProposalForEvaluation(
	const FString& EvaluationId,
	const FString& SessionId)
{
	if (!AreCommandProposalAutomationHttpGatesOpen())
	{
		return;
	}

	if (EvaluationId.IsEmpty() || SessionId.IsEmpty())
	{
		UE_LOG(LogEdenOs, Warning, TEXT("Command proposal dispatch skipped: missing evaluation or session id."));
		return;
	}

	RebindCommandProposalSessionIfNeeded(SessionId);

	if (RequestedProposalEvaluationIds.Contains(EvaluationId))
	{
		return;
	}

	if (HasSessionCompletedOrCompletionQueued())
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Command proposal dispatch skipped for %s: completion already queued or delivered."),
			*EvaluationId);
		return;
	}

	FEdenOsCommandProposalRequestV1 Request;
	Request.EvaluationId = EvaluationId;
	const FEdenOsWireSerializationResult Serialization =
		FEdenOsWireSerializationModel::BuildCommandProposalJsonV1(Request);
	if (!Serialization.IsSuccess())
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Command proposal request serialization failed for %s: %s"),
			*EvaluationId,
			*Serialization.ErrorMessage);
		return;
	}

	const int64 SequenceNumber = NextOutboundSequenceNumber;
	FPendingCommandProposalDispatch Pending;
	Pending.EvaluationId = EvaluationId;
	Pending.SessionId = SessionId;
	PendingCommandProposalByOutboundSequence.Add(SequenceNumber, Pending);
	RequestedProposalEvaluationIds.Add(EvaluationId);

	FEdenOsQueuedRequest Queued;
	Queued.MessageType = EEdenOsOutboundMessageType::CommandProposal;
	Queued.RoutePath = FEdenOsUrlModel::BuildSessionRoute(
		EdenOsWireContract::CommandProposalsRouteTemplate,
		SessionId);
	Queued.BodyJson = Serialization.Json;

	const FEdenTelemetrySinkResult EnqueueResult = EnqueueOutboundRequest(MoveTemp(Queued));
	if (!EnqueueResult.IsSuccess())
	{
		PendingCommandProposalByOutboundSequence.Remove(SequenceNumber);
		RequestedProposalEvaluationIds.Remove(EvaluationId);
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Command proposal enqueue failed for %s: %s"),
			*EvaluationId,
			*EnqueueResult.ErrorMessage);
	}
}

void UEdenOsAdapterSubsystem::HandleCommandProposalTransportCompleted(
	const FEdenOsHttpResult& Result,
	int64 SequenceNumber)
{
	FPendingCommandProposalDispatch Pending;
	const bool bHadPending = PendingCommandProposalByOutboundSequence.RemoveAndCopyValue(SequenceNumber, Pending);
	if (!bHadPending)
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Command proposal transport completed for unknown outbound sequence %lld; ignoring."),
			SequenceNumber);
		return;
	}

	if (!AreCommandProposalAutomationHttpGatesOpen())
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Discarding command proposal response for %s: automation HTTP gates closed."),
			*Pending.EvaluationId);
		return;
	}

	FString ActiveSessionId;
	if (const UWorld* World = GetWorld())
	{
		if (const UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>())
		{
			ActiveSessionId = Telemetry->GetSessionId();
		}
	}

	const bool bSessionStale =
		Pending.SessionId.IsEmpty()
		|| (!ActiveSessionId.IsEmpty() && ActiveSessionId != Pending.SessionId)
		|| (!BoundCommandProposalSessionId.IsEmpty() && BoundCommandProposalSessionId != Pending.SessionId);
	const bool bEvaluationStale =
		!LatestAcceptedAdvisory.bIsValid
		|| LatestAcceptedAdvisory.EvaluationId != Pending.EvaluationId;

	if (bSessionStale || bEvaluationStale)
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Discarding command proposal response for %s: stale session/evaluation correlation."),
			*Pending.EvaluationId);
		return;
	}

	if (!Result.IsSuccess())
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Command proposal transport failed for %s: %s"),
			*Pending.EvaluationId,
			*Result.ErrorSummary);
		return;
	}

	const FEdenOsCommandProposalResponseParseResult Parsed =
		FEdenOsWireSerializationModel::ParseCommandProposalResponseV1(
			Result.HttpStatusCode,
			Result.ResponseBodyJson,
			Pending.SessionId,
			Pending.EvaluationId);

	if (!Parsed.IsSuccess())
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("Command proposal response rejected for %s: %s"),
			*Pending.EvaluationId,
			*Parsed.ErrorMessage);
		return;
	}

	if (Parsed.bNoProposal)
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Command proposal returned no proposal for evaluation %s."),
			*Pending.EvaluationId);
		return;
	}

	// Mandatory J boundary: network data becomes a typed proposal, never a validated artifact.
	const FEdenExternalCommandValidationOutcome Validation =
		ValidateExternalCommandProposal(Parsed.Proposal);
	if (!Validation.IsValid() || !Validation.bHasValidatedCommand)
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Command proposal for %s failed J validation (reason=%d)."),
			*Pending.EvaluationId,
			static_cast<int32>(Validation.RejectionReason));
		return;
	}

	if (!RuntimeConfig.bExternalCommandExecutionEnabled)
	{
		// Dry-run: validated artifact is not retained for deferred execute.
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("Command proposal for %s validated in dry-run; execution disabled, not deferred."),
			*Pending.EvaluationId);
		return;
	}

	// Mandatory K boundary: only ExecuteValidatedExternalCommand may converge controls.
	const FEdenExternalCommandExecutionResult Execution =
		ExecuteValidatedExternalCommand(Validation.ValidatedCommand);
	UE_LOG(
		LogEdenOs,
		Log,
		TEXT("Command proposal for %s execution outcome=%d rejection=%d."),
		*Pending.EvaluationId,
		static_cast<int32>(Execution.Outcome),
		static_cast<int32>(Execution.RejectionReason));
}

void UEdenOsAdapterSubsystem::EnsureExternalCommandRouter()
{
	if (!ExternalCommandRouter)
	{
		ExternalCommandRouter = NewObject<UEdenExternalCommandRouter>(this);
	}
}

void UEdenOsAdapterSubsystem::EnsureExternalCommandExecutor()
{
	if (!ExternalCommandExecutor)
	{
		ExternalCommandExecutor = NewObject<UEdenExternalCommandExecutor>(this);
	}
}

FEdenExternalCommandValidationContext UEdenOsAdapterSubsystem::BuildExternalCommandValidationContext() const
{
	FEdenExternalCommandValidationContext Context;
	Context.bExternalCommandValidationEnabled = RuntimeConfig.bExternalCommandValidationEnabled;
	Context.AuthorityMode = RuntimeConfig.AuthorityMode;

	if (RuntimeConfig.bEnabled)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>())
			{
				Context.ActiveSessionId = Telemetry->GetSessionId();
			}
		}
	}

	if (LatestAcceptedAdvisory.bIsValid)
	{
		Context.bHasAcceptedEvaluation = true;
		Context.LatestAcceptedEvaluationId = LatestAcceptedAdvisory.EvaluationId;
	}

	return Context;
}

FEdenExternalCommandExecutionContext UEdenOsAdapterSubsystem::BuildExternalCommandExecutionContext() const
{
	FEdenExternalCommandExecutionContext Context;
	Context.bExternalCommandExecutionEnabled = RuntimeConfig.bExternalCommandExecutionEnabled;
	Context.bExternalCommandValidationEnabled = RuntimeConfig.bExternalCommandValidationEnabled;
	Context.AuthorityMode = RuntimeConfig.AuthorityMode;

	if (const UWorld* World = GetWorld())
	{
		if (const UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>())
		{
			Context.ActiveSessionId = Telemetry->GetSessionId();
		}
		if (const UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			Context.AttemptSimulationTimeSeconds = Clock->GetElapsedSimulationTimeSeconds();
		}
	}

	if (LatestAcceptedAdvisory.bIsValid)
	{
		Context.bHasAcceptedEvaluation = true;
		Context.LatestAcceptedEvaluationId = LatestAcceptedAdvisory.EvaluationId;
	}

	return Context;
}

UEdenOperatorControlComponent* UEdenOsAdapterSubsystem::ResolveOperatorControlComponent() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (UEdenOperatorControlComponent* Operator = It->FindComponentByClass<UEdenOperatorControlComponent>())
		{
			if (Operator->IsOperatorControlEnabled())
			{
				return Operator;
			}
		}
	}
	return nullptr;
}

void UEdenOsAdapterSubsystem::EmitExternalCommandExecutedTelemetry(
	const FEdenValidatedExternalCommand& Command,
	const FEdenExternalCommandExecutionResult& Result)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	if (!Telemetry)
	{
		return;
	}

	const FString CommandTypeLabel = StaticEnum<EEdenExternalCommandType>()
		? StaticEnum<EEdenExternalCommandType>()->GetNameStringByValue(static_cast<int64>(Command.CommandType))
		: TEXT("Unknown");
	const int32 ColonIndex =
		CommandTypeLabel.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	const FString ShortCommandType =
		ColonIndex == INDEX_NONE ? CommandTypeLabel : CommandTypeLabel.Mid(ColonIndex + 2);

	const FString Detail = FString::Printf(
		TEXT(
			"{"
			"\"ProposalId\":\"%s\","
			"\"SessionId\":\"%s\","
			"\"EvaluationId\":\"%s\","
			"\"CommandType\":\"%s\","
			"\"PreviousMode\":\"%s\","
			"\"RequestedMode\":\"%s\","
			"\"ResultingMode\":\"%s\","
			"\"Source\":\"eden_authorized_control\""
			"}"),
		*FEdenTelemetryExportModel::EscapeJsonString(Command.ProposalId),
		*FEdenTelemetryExportModel::EscapeJsonString(Command.SessionId),
		*FEdenTelemetryExportModel::EscapeJsonString(Command.EvaluationId),
		*FEdenTelemetryExportModel::EscapeJsonString(ShortCommandType),
		*FEdenTelemetryExportModel::EscapeJsonString(Result.PreviousModeLabel),
		*FEdenTelemetryExportModel::EscapeJsonString(Result.RequestedModeLabel),
		*FEdenTelemetryExportModel::EscapeJsonString(Result.ResultingModeLabel));

	Telemetry->RecordObservationEvent(
		EEdenTelemetryEventType::EdenExternalCommandExecuted,
		TEXT("EdenOs"),
		FName(*Command.ProposalId),
		Detail);
}

FEdenExternalCommandValidationOutcome UEdenOsAdapterSubsystem::ValidateExternalCommandProposal(
	const FEdenExternalCommandProposal& Proposal)
{
	EnsureExternalCommandRouter();
	return ExternalCommandRouter->ValidateProposal(Proposal, BuildExternalCommandValidationContext());
}

FEdenExternalCommandExecutionResult UEdenOsAdapterSubsystem::ExecuteValidatedExternalCommand(
	const FEdenValidatedExternalCommand& Command)
{
	EnsureExternalCommandExecutor();
	const FEdenExternalCommandExecutionResult Result = ExternalCommandExecutor->ExecuteValidatedCommand(
		Command,
		BuildExternalCommandExecutionContext(),
		ResolveOperatorControlComponent());

	if (Result.IsExecuted())
	{
		EmitExternalCommandExecutedTelemetry(Command, Result);
	}

	return Result;
}

TArray<FEdenExternalCommandValidationRecord> UEdenOsAdapterSubsystem::GetExternalCommandValidationHistory() const
{
	if (!ExternalCommandRouter)
	{
		return TArray<FEdenExternalCommandValidationRecord>();
	}
	return ExternalCommandRouter->GetValidationHistory();
}

TArray<FEdenExternalCommandExecutionRecord> UEdenOsAdapterSubsystem::GetExternalCommandExecutionHistory() const
{
	if (!ExternalCommandExecutor)
	{
		return TArray<FEdenExternalCommandExecutionRecord>();
	}
	return ExternalCommandExecutor->GetExecutionHistory();
}

void UEdenOsAdapterSubsystem::SetLatestAcceptedAdvisoryForTesting(const FEdenOsAcceptedAdvisory& Advisory)
{
	LatestAcceptedAdvisory = Advisory;
	if (Advisory.bIsValid)
	{
		LatestAcceptedAdvisoryOrdinal = FMath::Max(LatestAcceptedAdvisoryOrdinal, int64(1));
	}
	else
	{
		LatestAcceptedAdvisoryOrdinal = 0;
	}
}

FEdenOsAdvisoryContext UEdenOsAdapterSubsystem::GetLastAdvisoryContext() const
{
	return LastAdvisoryContext;
}

FEdenOsAcceptedAdvisory UEdenOsAdapterSubsystem::GetLatestAcceptedAdvisory() const
{
	return LatestAcceptedAdvisory;
}

int32 UEdenOsAdapterSubsystem::GetAdvisoryEvaluationCount() const
{
	return AdvisoryEvaluationCount;
}

FEdenOsAdvisoryContextBounds UEdenOsAdapterSubsystem::GetAdvisoryContextBounds() const
{
	return AdvisoryContextBounds;
}

void UEdenOsAdapterSubsystem::SetAdvisoryContextBounds(const FEdenOsAdvisoryContextBounds& InBounds)
{
	AdvisoryContextBounds = InBounds;
}
