// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Missions/EdenMissionTypes.h"
#include "Telemetry/EdenTelemetryTypes.h"

#include "EdenOsAdvisoryTypes.generated.h"

/**
 * Why an advisory evaluation was performed.
 *
 * Canonical ordering is ascending enum value. TriggerReasons arrays are always sorted into this
 * order so that the same settled step produces the same reason sequence on every run.
 */
UENUM(BlueprintType)
enum class EEdenOsAdvisoryTriggerReason : uint8
{
	MissionPhaseTransition = 0,
	AlertTransition = 1,
	ObjectiveTransition = 2,
	OperatorAction = 3,
	Heartbeat = 4
};

/**
 * Bounded trend over the advisory context's recent-snapshot window.
 *
 * Values are sampled from accepted 0006 telemetry history only. This is a description of what the
 * recorded samples show, not a prediction.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOsAdvisoryTrend
{
	GENERATED_BODY()

	/** Value from the newest snapshot in the window. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	float LatestValue = 0.0f;

	/** Value from the oldest snapshot in the window. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	float EarliestValue = 0.0f;

	/** LatestValue - EarliestValue across the window. Zero when fewer than two samples exist. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	float Delta = 0.0f;

	/** Number of snapshots the trend was computed from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	int32 SampleCount = 0;

	/** False when the window held no snapshots. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	bool bHasData = false;
};

/** Explicit limits on how much telemetry history one advisory context may carry. */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOsAdvisoryContextBounds
{
	GENERATED_BODY()

	/** Newest-N snapshots retained. Oldest are dropped first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Advisory", meta = (ClampMin = "1"))
	int32 MaxSnapshots = 10;

	/** Newest-N events retained. Oldest are dropped first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Advisory", meta = (ClampMin = "1"))
	int32 MaxEvents = 20;
};

/**
 * Immutable settled-state context handed to EDEN reasoning.
 *
 * Every field is a value copy taken from accepted 0006 telemetry history at build time. The context
 * holds no references into telemetry's mutable arrays, so later simulation cannot alter a context
 * that was already built.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOsAdvisoryContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	int32 SchemaVersion = 1;

	/** False for a default-constructed context that was never built by an evaluation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FString SessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	float SimulationTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	float MissionElapsedTimeSeconds = 0.0f;

	/** Highest telemetry sequence number considered by this evaluation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	int64 EvaluatedThroughSequence = 0;

	/** All reasons that caused this single evaluation, in canonical ascending order. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	TArray<EEdenOsAdvisoryTriggerReason> TriggerReasons;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	EEdenMissionState MissionState = EEdenMissionState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	EEdenMissionPhase MissionPhase = EEdenMissionPhase::Nominal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FName ActiveMissionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	TArray<FEdenMissionObjectiveRuntime> ObjectiveStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FEdenFuelStateSnapshot Fuel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FEdenPowerStateSnapshot Power;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FEdenThermalStateSnapshot Thermal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FEdenFlightStateSnapshot Flight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FEdenOperatorStateSnapshot Operator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FEdenOsAdvisoryTrend TemperatureCelsiusTrend;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FEdenOsAdvisoryTrend BatteryChargeFractionTrend;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	FEdenOsAdvisoryTrend FuelFractionTrend;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	TArray<FEdenTelemetrySnapshot> RecentSnapshots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	TArray<FEdenTelemetryEvent> RecentEvents;

	/** True when bounds dropped older snapshots or events from this context. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	bool bContextTruncated = false;

	/** Mirrors 0006 session integrity: telemetry itself had already dropped history. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|OS|Advisory")
	bool bUpstreamHistoryTruncated = false;
};
