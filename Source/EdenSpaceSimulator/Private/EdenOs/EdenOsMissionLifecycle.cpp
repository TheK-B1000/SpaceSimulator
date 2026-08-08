// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsMissionLifecycle.h"

namespace
{
	bool ResolveFinalStatusFromMissionState(
		EEdenMissionState MissionState,
		EEdenOsMissionFinalStatus& OutFinalStatus)
	{
		switch (MissionState)
		{
		case EEdenMissionState::Succeeded:
			OutFinalStatus = EEdenOsMissionFinalStatus::Succeeded;
			return true;
		case EEdenMissionState::Failed:
			OutFinalStatus = EEdenOsMissionFinalStatus::Failed;
			return true;
		default:
			return false;
		}
	}
}

int64 FEdenOsMissionLifecycleModel::ResolveLatestSequence(const FEdenTelemetrySessionPayload& Payload)
{
	int64 LatestSequence = Payload.Metadata.LastAvailableSequence;
	for (const FEdenTelemetryEvent& Event : Payload.Events)
	{
		LatestSequence = FMath::Max(LatestSequence, Event.SequenceNumber);
	}
	for (const FEdenTelemetrySnapshot& Snapshot : Payload.Snapshots)
	{
		LatestSequence = FMath::Max(LatestSequence, Snapshot.SequenceNumber);
	}
	return LatestSequence;
}

bool FEdenOsMissionLifecycleModel::ResolveFinalStatus(
	const FEdenTelemetrySessionPayload& Payload,
	EEdenOsMissionFinalStatus& OutFinalStatus)
{
	for (int32 Index = Payload.Events.Num() - 1; Index >= 0; --Index)
	{
		switch (Payload.Events[Index].EventType)
		{
		case EEdenTelemetryEventType::MissionSucceeded:
			OutFinalStatus = EEdenOsMissionFinalStatus::Succeeded;
			return true;
		case EEdenTelemetryEventType::MissionFailed:
			OutFinalStatus = EEdenOsMissionFinalStatus::Failed;
			return true;
		case EEdenTelemetryEventType::MissionAborted:
			OutFinalStatus = EEdenOsMissionFinalStatus::Aborted;
			return true;
		default:
			break;
		}
	}

	if (Payload.Snapshots.Num() > 0)
	{
		return ResolveFinalStatusFromMissionState(Payload.Snapshots.Last().Mission.MissionState, OutFinalStatus);
	}

	return false;
}

int32 FEdenOsMissionLifecycleModel::CountAlertRaisedEvents(const FEdenTelemetrySessionPayload& Payload)
{
	int32 AlertCount = 0;
	for (const FEdenTelemetryEvent& Event : Payload.Events)
	{
		if (Event.EventType == EEdenTelemetryEventType::AlertRaised)
		{
			++AlertCount;
		}
	}
	return AlertCount;
}

FEdenOsMissionSessionCreateRequestV1 FEdenOsMissionLifecycleModel::BuildSessionCreateRequest(
	const FEdenTelemetrySessionPayload& Payload,
	const FString& ScenarioId,
	const FString& StartedAtIso8601)
{
	FEdenOsMissionSessionCreateRequestV1 Request;
	Request.SessionId = Payload.GetSafeSessionId();
	Request.ScenarioId = ScenarioId;
	Request.StartedAtIso8601 = StartedAtIso8601;
	return Request;
}

bool FEdenOsMissionLifecycleModel::BuildSessionCompleteRequest(
	const FEdenTelemetrySessionPayload& Payload,
	const FString& CompletedAtIso8601,
	FEdenOsSessionCompleteRequestV1& OutRequest)
{
	EEdenOsMissionFinalStatus FinalStatus = EEdenOsMissionFinalStatus::Failed;
	if (!ResolveFinalStatus(Payload, FinalStatus))
	{
		return false;
	}

	OutRequest = FEdenOsSessionCompleteRequestV1();
	OutRequest.SessionId = Payload.GetSafeSessionId();
	OutRequest.FinalStatus = FinalStatus;
	OutRequest.CompletedAtUtcIso8601 = CompletedAtIso8601;

	const int64 LatestSequence = ResolveLatestSequence(Payload);
	if (LatestSequence >= 0)
	{
		OutRequest.FinalSequence = LatestSequence;
	}
	OutRequest.AlertsCount = CountAlertRaisedEvents(Payload);
	return true;
}
