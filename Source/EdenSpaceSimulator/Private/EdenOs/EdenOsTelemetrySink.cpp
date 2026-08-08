// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsTelemetrySink.h"

#include "EdenOs/EdenOsAdapterSubsystem.h"
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

	FEdenOsTelemetryIngestionRequestV1 Request;
	Request.Payload = Payload;
	const FEdenOsWireSerializationResult Serialized = FEdenOsWireSerializationModel::BuildTelemetryJsonV1(Request);
	if (!Serialized.IsSuccess())
	{
		return FEdenTelemetrySinkResult::Failed(Serialized.ErrorMessage);
	}

	FEdenOsQueuedRequest QueuedRequest;
	QueuedRequest.MessageType = EEdenOsOutboundMessageType::Telemetry;
	QueuedRequest.RoutePath =
		FEdenOsUrlModel::BuildSessionRoute(EdenOsWireContract::TelemetryRouteTemplate, Payload.SessionId);
	QueuedRequest.BodyJson = Serialized.Json;
	return Adapter->EnqueueOutboundRequest(MoveTemp(QueuedRequest));
}
