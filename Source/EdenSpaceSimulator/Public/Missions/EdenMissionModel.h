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

	static EEdenMissionState EvaluateOutcome(
		const FEdenMissionRuntimeState& State,
		const FEdenMissionDefinitionConfig& Definition);

	static FEdenMissionRuntimeState ResetRuntimeState();

	static FEdenMissionStateSnapshot MakeSnapshot(const FEdenMissionRuntimeState& State, FName MissionId);
};
