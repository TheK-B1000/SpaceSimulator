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
	AlertCleared,
	/**
	 * An EDEN OS advisory was received, validated, and issued.
	 *
	 * The event's SimulationTimeSeconds is the ISSUANCE time — when the validated response was
	 * accepted — not the evaluation time that produced the request. See ExecPlan 0007 §5.9.
	 */
	EdenAdvisoryIssued,
	/**
	 * An authorized validated EDEN external command converged through operator control.
	 * SimulationTimeSeconds is the time when resulting operator mode matched the request.
	 */
	EdenExternalCommandExecuted
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

	/**
	 * In-process flight dynamics (velocity, acceleration, available thrust, mass, assist).
	 * Not part of Telemetry Export Schema v1 or the EDEN OS wire contract.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenFlightDynamicsSnapshot Dynamics;
};

/**
 * Which resource owners were bound and simulating when a snapshot was assembled.
 *
 * An unavailable owner leaves default values (0.0, Normal, Full) in its snapshot struct. Those
 * defaults are not observations, so consumers that must distinguish "unknown" from "observed
 * zero" (the EDEN OS rich advisory context) read these flags first.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenTelemetrySourceAvailability
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	bool bThermal = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	bool bPower = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	bool bOperator = false;
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

	/** Source validity for Thermal, Power and Operator above. Not part of Export Schema v1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenTelemetrySourceAvailability Sources;

	/**
	 * Thermal limits captured at this snapshot's simulation step (invalid when thermal is
	 * unavailable). Not part of Export Schema v1.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Telemetry")
	FEdenThermalLimitsSnapshot ThermalLimits;
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
