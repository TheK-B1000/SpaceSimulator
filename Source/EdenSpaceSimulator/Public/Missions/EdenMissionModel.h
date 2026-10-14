// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Missions/EdenMissionTypes.h"

struct EDENSPACESIMULATOR_API FEdenMissionStepResult
{
	FEdenMissionRuntimeState UpdatedState;
	TArray<FName> NewlyTriggeredEventIds;
	bool bMissionStateChanged = false;
};

struct EDENSPACESIMULATOR_API FEdenMissionModel
{
	static bool ValidateDefinition(const FEdenMissionDefinitionConfig& Definition, TArray<FString>* OutErrors = nullptr);
	static bool IsValidSimulationTime(float TimeSeconds);

	static bool CanTransition(EEdenMissionState Current, EEdenMissionState Target);
	static FEdenMissionRuntimeState InitializeRuntimeState(const FEdenMissionDefinitionConfig& Definition);
	
	static FEdenMissionStepResult StepTimeline(
		const FEdenMissionRuntimeState& CurrentState,
		const FEdenMissionDefinitionConfig& Definition,
		float FixedDeltaSeconds);

	static FEdenMissionRuntimeState ActivateObjective(const FEdenMissionRuntimeState& State, FName ObjectiveId);
	static FEdenMissionRuntimeState CompleteObjective(const FEdenMissionRuntimeState& State, FName ObjectiveId);
	static FEdenMissionRuntimeState FailObjective(const FEdenMissionRuntimeState& State, FName ObjectiveId);

	/**
	 * Evaluates active objectives against authoritative observation values.
	 * Power/fuel thresholds use normalized fractions in [0,1] (ChargeFraction / FuelFraction).
	 */
	static FEdenMissionRuntimeState EvaluateObjectives(
		const FEdenMissionRuntimeState& State,
		const FEdenMissionDefinitionConfig& Definition,
		float ThermalTemperatureCelsius,
		float PowerChargeFraction,
		float FuelFraction);

	/**
	 * Full evaluation (ExecPlan 0011): adds mission heating and the realized thermal trend for
	 * RecoverThermalStability, the step length for hold timers, and records resolution time and reason.
	 */
	static FEdenMissionRuntimeState EvaluateObjectives(
		const FEdenMissionRuntimeState& State,
		const FEdenMissionDefinitionConfig& Definition,
		const FEdenMissionObservations& Observations,
		float FixedDeltaSeconds);

	static EEdenMissionState EvaluateOutcome(
		const FEdenMissionRuntimeState& State,
		const FEdenMissionDefinitionConfig& Definition);

	/** Plain-words reason for a terminal state (first required failure, or all required objectives met). */
	static FString DescribeOutcome(
		const FEdenMissionRuntimeState& State,
		const FEdenMissionDefinitionConfig& Definition,
		EEdenMissionState Outcome);

	/** Realized rate at or below this counts as "stable or falling" (absorbs float noise at a plateau). */
	static constexpr float StableTrendToleranceDegreesCelsiusPerSecond = 0.01f;

	static FEdenMissionRuntimeState ResetRuntimeState();

	static FEdenMissionStateSnapshot MakeSnapshot(const FEdenMissionRuntimeState& State, FName MissionId);
};
