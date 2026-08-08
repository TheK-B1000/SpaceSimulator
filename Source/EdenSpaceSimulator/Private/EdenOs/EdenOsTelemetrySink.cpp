// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsTelemetrySink.h"

#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsMissionLifecycle.h"
#include "EdenOs/EdenOsTransport.h"
#include "EdenOs/EdenOsWireTypes.h"

FEdenOsTelemetrySink::FEdenOsTelemetrySink(UEdenOsAdapterSubsystem& InAdapterSubsystem)
	: AdapterSubsystem(&InAdapterSubsystem)
{
}

FName FEdenOsTelemetrySink::GetTelemetrySinkName() const
{
	return TEXT("EdenOsTelemetrySink");
}

FEdenTelemetrySinkResult FEdenOsTelemetrySink::DeliverTelemetrySession(const FEdenTelemetrySessionPayload& Payload)
{
	UEdenOsAdapterSubsystem* Adapter = AdapterSubsystem.Get();
	if (!Adapter)
	{
		return FEdenTelemetrySinkResult::Failed(TEXT("EDEN OS adapter subsystem is unavailable."));
	}

	const FString SafeSessionId = Payload.GetSafeSessionId();
	if (DeliveryState.SessionId != SafeSessionId)
	{
		ResetSessionDeliveryStateFor(SafeSessionId);
	}

	TArray<FString> QueueErrors;
	int32 QueuedCount = 0;

	if (!DeliveryState.bCreateQueued)
	{
		const FEdenOsMissionSessionCreateRequestV1 CreateRequest =
			FEdenOsMissionLifecycleModel::BuildSessionCreateRequest(
				Payload,
				Adapter->GetDefaultScenarioId(),
				MakeUtcTimestampIso8601());
		const FEdenOsWireSerializationResult SerializedCreate =
			FEdenOsWireSerializationModel::BuildSessionCreateJsonV1(CreateRequest);
		if (!SerializedCreate.IsSuccess())
		{
			return FEdenTelemetrySinkResult::Failed(SerializedCreate.ErrorMessage);
		}

		FEdenOsQueuedRequest QueuedCreate;
		QueuedCreate.MessageType = EEdenOsOutboundMessageType::SessionCreate;
		QueuedCreate.RoutePath = EdenOsWireContract::CreateSessionRoute;
		QueuedCreate.BodyJson = SerializedCreate.Json;
		const FEdenTelemetrySinkResult CreateResult = QueueLifecycleRequest(MoveTemp(QueuedCreate));
		if (!CreateResult.IsSuccess())
		{
			return CreateResult;
		}
		DeliveryState.bCreateQueued = true;
		++QueuedCount;
	}

	const int64 LatestSequence = FEdenOsMissionLifecycleModel::ResolveLatestSequence(Payload);
	if (LatestSequence > DeliveryState.LastTelemetrySequence)
	{
		FEdenOsTelemetryIngestionRequestV1 TelemetryRequest;
		TelemetryRequest.Payload = Payload;
		const FEdenOsWireSerializationResult SerializedTelemetry =
			FEdenOsWireSerializationModel::BuildTelemetryJsonV1(TelemetryRequest);
		if (!SerializedTelemetry.IsSuccess())
		{
			QueueErrors.Add(SerializedTelemetry.ErrorMessage);
		}
		else
		{
			FEdenOsQueuedRequest QueuedTelemetry;
			QueuedTelemetry.MessageType = EEdenOsOutboundMessageType::Telemetry;
			QueuedTelemetry.RoutePath =
				FEdenOsUrlModel::BuildSessionRoute(EdenOsWireContract::TelemetryRouteTemplate, SafeSessionId);
			QueuedTelemetry.BodyJson = SerializedTelemetry.Json;
			const FEdenTelemetrySinkResult TelemetryResult = QueueLifecycleRequest(MoveTemp(QueuedTelemetry));
			if (TelemetryResult.IsSuccess())
			{
				DeliveryState.LastTelemetrySequence = LatestSequence;
				++QueuedCount;
			}
			else
			{
				QueueErrors.Add(TelemetryResult.ErrorMessage);
			}
		}
	}

	for (const FEdenTelemetryEvent& Event : Payload.Events)
	{
		if (Event.SequenceNumber <= DeliveryState.LastEventSequence)
		{
			continue;
		}

		FEdenOsEventIngestionRequestV1 EventRequest;
		EventRequest.SessionId = SafeSessionId;
		EventRequest.Event = Event;
		const FEdenOsWireSerializationResult SerializedEvent =
			FEdenOsWireSerializationModel::BuildEventJsonV1(EventRequest);
		if (!SerializedEvent.IsSuccess())
		{
			QueueErrors.Add(SerializedEvent.ErrorMessage);
			continue;
		}

		FEdenOsQueuedRequest QueuedEvent;
		QueuedEvent.MessageType = EEdenOsOutboundMessageType::Event;
		QueuedEvent.RoutePath =
			FEdenOsUrlModel::BuildSessionRoute(EdenOsWireContract::EventsRouteTemplate, SafeSessionId);
		QueuedEvent.BodyJson = SerializedEvent.Json;
		const FEdenTelemetrySinkResult EventResult = QueueLifecycleRequest(MoveTemp(QueuedEvent));
		if (EventResult.IsSuccess())
		{
			DeliveryState.LastEventSequence = Event.SequenceNumber;
			++QueuedCount;
		}
		else
		{
			QueueErrors.Add(EventResult.ErrorMessage);
		}
	}

	if (!DeliveryState.bCompleteQueued)
	{
		FEdenOsSessionCompleteRequestV1 CompleteRequest;
		if (FEdenOsMissionLifecycleModel::BuildSessionCompleteRequest(Payload, MakeUtcTimestampIso8601(), CompleteRequest))
		{
			const FEdenOsWireSerializationResult SerializedComplete =
				FEdenOsWireSerializationModel::BuildSessionCompleteJsonV1(CompleteRequest);
			if (!SerializedComplete.IsSuccess())
			{
				QueueErrors.Add(SerializedComplete.ErrorMessage);
			}
			else
			{
				FEdenOsQueuedRequest QueuedComplete;
				QueuedComplete.MessageType = EEdenOsOutboundMessageType::SessionComplete;
				QueuedComplete.RoutePath =
					FEdenOsUrlModel::BuildSessionRoute(EdenOsWireContract::CompleteRouteTemplate, SafeSessionId);
				QueuedComplete.BodyJson = SerializedComplete.Json;
				const FEdenTelemetrySinkResult CompleteResult = QueueLifecycleRequest(MoveTemp(QueuedComplete));
				if (CompleteResult.IsSuccess())
				{
					DeliveryState.bCompleteQueued = true;
					++QueuedCount;
				}
				else
				{
					QueueErrors.Add(CompleteResult.ErrorMessage);
				}
			}
		}
	}

	if (!QueueErrors.IsEmpty())
	{
		return FEdenTelemetrySinkResult::Failed(FString::Join(QueueErrors, TEXT("; ")));
	}

	return FEdenTelemetrySinkResult::Succeeded(
		QueuedCount > 0 ? FString::Printf(TEXT("eden-os:lifecycle:%d"), QueuedCount) : TEXT("eden-os:no-new-session-facts"));
}

void FEdenOsTelemetrySink::ResetSessionDeliveryState()
{
	DeliveryState = FSessionDeliveryState();
}

void FEdenOsTelemetrySink::ResetSessionDeliveryStateFor(const FString& SessionId)
{
	DeliveryState = FSessionDeliveryState();
	DeliveryState.SessionId = SessionId;
}

FEdenTelemetrySinkResult FEdenOsTelemetrySink::QueueLifecycleRequest(FEdenOsQueuedRequest&& Request)
{
	UEdenOsAdapterSubsystem* Adapter = AdapterSubsystem.Get();
	if (!Adapter)
	{
		return FEdenTelemetrySinkResult::Failed(TEXT("EDEN OS adapter subsystem is unavailable."));
	}
	return Adapter->EnqueueOutboundRequest(MoveTemp(Request));
}

FString FEdenOsTelemetrySink::MakeUtcTimestampIso8601()
{
	return FDateTime::UtcNow().ToIso8601();
}
