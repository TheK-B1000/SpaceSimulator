// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"
#include "Missions/EdenMissionTypes.h"
#include "Operations/EdenOperatorTypes.h"
#include "Systems/EdenFuelModel.h"
#include "Systems/EdenPowerModel.h"
#include "Systems/EdenThermalModel.h"

#include "EdenTelemetryTypes.generated.h"

UENUM(BlueprintType)
enum class EEdenTelemetryEventType : uint8
{
	None,
	MissionStarted,
	MissionSucceeded,
	MissionFailed,
	MissionAborted,
	PhaseChanged,
	ScheduledEventFired,
	ObjectiveStateChanged,
	ResourceStateTransition,
	OperatorCommandIssued,
	AlertRaised,
	AlertCleared
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenTelemetryEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float SimulationTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float MissionElapsedTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	int64 SequenceNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	EEdenTelemetryEventType EventType = EEdenTelemetryEventType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FName SourceSystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FName EventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FString Detail;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float ThrustAuthority = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	bool bStabilizationAssistAvailable = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float PropulsionDemandNormalized = 0.0f;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenTelemetrySnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float SimulationTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float MissionElapsedTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	int64 SequenceNumber = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenFlightStateSnapshot Flight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenFuelStateSnapshot Fuel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenPowerStateSnapshot Power;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenThermalStateSnapshot Thermal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenMissionStateSnapshot Mission;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenOperatorStateSnapshot Operator;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenTelemetrySessionMetadata
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	int32 DroppedSnapshotCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	int32 DroppedEventCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	bool bHistoryTruncated = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	bool bEventIntegrityCompromised = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	int64 FirstAvailableSequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	int64 LastAvailableSequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float SnapshotIntervalSeconds = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float PeakTemperatureCelsius = -FLT_MAX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float MinimumBatteryChargeFraction = FLT_MAX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	float MinimumFuelFraction = FLT_MAX;
};

UENUM(BlueprintType)
enum class EEdenSinkResult : uint8
{
	Success,
	Failed
};
