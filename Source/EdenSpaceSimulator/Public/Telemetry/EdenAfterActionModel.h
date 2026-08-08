// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Missions/EdenMissionTypes.h"
#include "Telemetry/EdenTelemetryTypes.h"

#include "EdenAfterActionModel.generated.h"

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenAfterActionObjectiveLine
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	FName ObjectiveId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	EEdenObjectiveState State = EEdenObjectiveState::Pending;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenAfterActionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	FName MissionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	float DurationSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	float PeakRecordedSimulationTemperatureCelsius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	float LowestRecordedBatteryChargeFraction = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	float LowestRecordedFuelFraction = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	float FinalFuelFraction = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	float SnapshotIntervalSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	bool bHistoryTruncated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	bool bEventIntegrityCompromised = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	int32 OperatorCommandCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	int32 CriticalAlertCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	EEdenMissionState FinalMissionState = EEdenMissionState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|AAR")
	TArray<FEdenAfterActionObjectiveLine> Objectives;
};

struct EDENSPACESIMULATOR_API FEdenAfterActionModel
{
	static FEdenAfterActionResult Build(
		const TArray<FEdenTelemetryEvent>& Events,
		const TArray<FEdenTelemetrySnapshot>& Snapshots,
		const FEdenTelemetrySessionMetadata& Metadata);
};
