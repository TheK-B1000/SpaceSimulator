// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdenMissionTypes.generated.h"

UENUM(BlueprintType)
enum class EEdenMissionState : uint8
{
	Inactive,
	Ready,
	Running,
	Succeeded,
	Failed
};

UENUM(BlueprintType)
enum class EEdenMissionPhase : uint8
{
	Nominal,
	Warning,
	Impact,
	Recovery,
	Resolved
};

UENUM(BlueprintType)
enum class EEdenMissionCommandType : uint8
{
	None,
	SetMissionPhase,
	SetExternalHeatingRate,
	ClearExternalHeatingRate,
	SetExternalPowerDemand,
	ClearExternalPowerDemand,
	SetPowerGeneration,
	ActivateObjective
};

UENUM(BlueprintType)
enum class EEdenObjectiveType : uint8
{
	SurviveUntilTime,
	KeepTemperatureBelow,
	RestorePowerAbove,
	MaintainFuelAbove,
	/**
	 * Complete-seeking (ExecPlan 0011): mission heating over, temperature below TargetValue, and a stable
	 * or falling realized trend, held continuously for HoldSeconds; fails at DeadlineSeconds.
	 */
	RecoverThermalStability,
	/** Fail-only: battery charge fraction must not drop below TargetValue. */
	MaintainPowerAbove
};

UENUM(BlueprintType)
enum class EEdenObjectiveState : uint8
{
	Pending,
	Active,
	Completed,
	Failed
};

UENUM(BlueprintType)
enum class EEdenMissionEventState : uint8
{
	Pending,
	Executed,
	Skipped
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionEventConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName EventId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission", meta = (ClampMin = "0.0", Units = "s"))
	float TriggerTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionCommandType CommandType = EEdenMissionCommandType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float FloatParameter = 0.0f;

	/** Typed phase payload for SetMissionPhase. Do not encode phases through FloatParameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionPhase PhaseParameter = EEdenMissionPhase::Nominal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName NameParameter;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionObjectiveConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName ObjectiveId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenObjectiveType ObjectiveType = EEdenObjectiveType::SurviveUntilTime;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float TargetValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bRequired = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bActivateOnStart = false;

	/** RecoverThermalStability: continuous seconds the recovery conditions must hold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission", meta = (ClampMin = "0.0", Units = "s"))
	float HoldSeconds = 5.0f;

	/** Mission time by which a complete-seeking objective must complete; 0 = no deadline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission", meta = (ClampMin = "0.0", Units = "s"))
	float DeadlineSeconds = 0.0f;
};

/**
 * Explicit, scenario-scoped starting conditions (ExecPlan 0011). Applied once, on a fresh start, and only
 * what is declared: nothing else on the spacecraft (battery charge, propellant, flight state, operator
 * modes) is reset. A mission that declares nothing starts from the ship's current condition.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionInitialConditions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bSetInitialTemperature = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission", meta = (EditCondition = "bSetInitialTemperature"))
	float InitialTemperatureCelsius = 20.0f;

	/** Limit the power bus for this run (E1). Cleared again when the mission is aborted or reset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bSetBatteryDischargeLimit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission", meta = (EditCondition = "bSetBatteryDischargeLimit", ClampMin = "0.001"))
	float BatteryMaxDischargeKilowatts = 6.0f;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionDefinitionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName MissionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionEventConfig> Events;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionObjectiveConfig> Objectives;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FEdenMissionInitialConditions InitialConditions;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionEventRuntime
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName EventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionEventState EventState = EEdenMissionEventState::Pending;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionObjectiveRuntime
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName ObjectiveId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenObjectiveState State = EEdenObjectiveState::Pending;

	/** RecoverThermalStability: continuous seconds the recovery conditions have held. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float HoldElapsedSeconds = 0.0f;

	/** Mission time when the objective completed or failed; < 0 while unresolved. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float ResolvedAtSeconds = -1.0f;

	/** Why it failed, in plain words (empty unless Failed). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FString FailureReason;
};

/** Authoritative resource observations for one mission evaluation step. */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionObservations
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bHasThermal = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float TemperatureCelsius = TNumericLimits<float>::Lowest();

	/** Mission-applied (external) heating currently in force. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float ExternalHeatingDegreesCelsiusPerSecond = 0.0f;

	/** Realized temperature rate for the next step (thermal balance), C/s. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float RealizedNetDegreesCelsiusPerSecond = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float PowerChargeFraction = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float FuelFraction = 1.0f;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionState MissionState = EEdenMissionState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionPhase MissionPhase = EEdenMissionPhase::Nominal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float MissionElapsedTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionEventRuntime> EventStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionObjectiveRuntime> ObjectiveStates;

	/** Declared initial conditions were applied at this run's start (none declared: false). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bInitialConditionsApplied = false;

	/** This run limited the power bus; the override is cleared on abort and reset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bAppliedBatteryDischargeLimit = false;

	/** Why the mission reached its terminal state (first failed objective's reason, or success). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FString TerminalReason;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionState MissionState = EEdenMissionState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionPhase MissionPhase = EEdenMissionPhase::Nominal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float MissionElapsedTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName ActiveMissionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionObjectiveRuntime> ObjectiveSnapshots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FString TerminalReason;
};
