// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenAfterActionModel.h"

FEdenAfterActionResult FEdenAfterActionModel::Build(
	const TArray<FEdenTelemetryEvent>& Events,
	const TArray<FEdenTelemetrySnapshot>& Snapshots,
	const FEdenTelemetrySessionMetadata& Metadata)
{
	FEdenAfterActionResult Result;
	Result.SnapshotIntervalSeconds = Metadata.SnapshotIntervalSeconds;
	Result.bHistoryTruncated = Metadata.bHistoryTruncated;
	Result.bEventIntegrityCompromised = Metadata.bEventIntegrityCompromised;
	Result.PeakRecordedSimulationTemperatureCelsius =
		Metadata.PeakTemperatureCelsius > -FLT_MAX * 0.5f ? Metadata.PeakTemperatureCelsius : 0.0f;
	Result.LowestRecordedBatteryChargeFraction =
		Metadata.MinimumBatteryChargeFraction < FLT_MAX * 0.5f ? Metadata.MinimumBatteryChargeFraction : 0.0f;
	Result.LowestRecordedFuelFraction =
		Metadata.MinimumFuelFraction < FLT_MAX * 0.5f ? Metadata.MinimumFuelFraction : 0.0f;

	if (Snapshots.Num() > 0)
	{
		Result.DurationSeconds =
			Snapshots.Last().MissionElapsedTimeSeconds - Snapshots[0].MissionElapsedTimeSeconds;
		Result.FinalMissionState = Snapshots.Last().Mission.MissionState;
	}
	else if (Events.Num() > 0)
	{
		Result.DurationSeconds = Events.Last().MissionElapsedTimeSeconds - Events[0].MissionElapsedTimeSeconds;
	}

	for (const FEdenTelemetryEvent& Event : Events)
	{
		if (Event.EventType == EEdenTelemetryEventType::OperatorCommandIssued)
		{
			++Result.OperatorCommandCount;
		}
		if (Event.EventType == EEdenTelemetryEventType::MissionSucceeded)
		{
			Result.FinalMissionState = EEdenMissionState::Succeeded;
		}
		else if (Event.EventType == EEdenTelemetryEventType::MissionFailed)
		{
			Result.FinalMissionState = EEdenMissionState::Failed;
		}
		else if (Event.EventType == EEdenTelemetryEventType::MissionAborted)
		{
			Result.FinalMissionState = EEdenMissionState::Inactive;
		}
	}

	return Result;
}
