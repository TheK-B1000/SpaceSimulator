// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"

#include "EdenOs/EdenOsConnectionSettings.h"
#include "EdenOs/EdenOsTelemetrySink.h"
#include "EdenOs/EdenOsUnrealHttpTransport.h"
#include "Telemetry/EdenTelemetrySubsystem.h"
#include "Engine/World.h"
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

	OwnedHttpTransport = MakeUnique<FEdenOsUnrealHttpTransport>();
	ActiveHttpTransport = OwnedHttpTransport.Get();
	bAcceptTransportCallbacks = true;

	const UEdenOsConnectionSettings* Settings = GetDefault<UEdenOsConnectionSettings>();
	ApplyRuntimeConfig(Settings ? Settings->MakeConnectionConfig() : FEdenOsConnectionConfig());
	RegisterTelemetrySinkIfNeeded();
}

void UEdenOsAdapterSubsystem::Deinitialize()
{
	bAcceptTransportCallbacks = false;
	UnregisterTelemetrySink();
	ResetTransportRuntimeState();
	RuntimeConfig = FEdenOsConnectionConfig();
	LastValidationResult = FEdenOsValidationResult();
	ConnectionSnapshot = FEdenOsConnectionSnapshot();
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
	RefreshSnapshotFromRuntimeConfig();
	if (bWasEnabled && !RuntimeConfig.bEnabled)
	{
		UnregisterTelemetrySink();
	}
	RegisterTelemetrySinkIfNeeded();
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
	if (Result.IsSuccess())
	{
		bHasSuccessfulTransportDelivery = true;
	}
	else if (bHasSuccessfulTransportDelivery)
	{
		ConnectionSnapshot.ConnectionState = EEdenOsConnectionState::Degraded;
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
