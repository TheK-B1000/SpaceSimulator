// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsAdvisoryTypes.h"
#include "Missions/EdenMissionTypes.h"
#include "Operations/EdenAlertTypes.h"
#include "Operations/EdenOperatorTypes.h"
#include "Systems/EdenFuelModel.h"
#include "Systems/EdenPowerModel.h"
#include "Systems/EdenThermalModel.h"

#include "EdenOperatorHudTypes.generated.h"

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOperatorHudSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	FName MissionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	EEdenMissionState MissionState = EEdenMissionState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	EEdenMissionPhase MissionPhase = EEdenMissionPhase::Nominal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	float MissionElapsedTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	TArray<FEdenMissionObjectiveRuntime> Objectives;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	FEdenFuelStateSnapshot Fuel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	FEdenPowerStateSnapshot Power;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	FEdenThermalStateSnapshot Thermal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	FEdenOperatorStateSnapshot Operator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	TArray<FEdenAlert> ActiveAlerts;

	/** True when the adapter holds a validated ProjectEden advisory for display. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD|Advisory")
	bool bHasAdvisory = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD|Advisory")
	FString AdvisoryRecommendation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD|Advisory")
	FString AdvisoryRationale;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD|Advisory")
	FString AdvisoryId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD|Advisory")
	float AdvisoryIssuedSimulationTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|HUD")
	float AssembledAtSimTimeSeconds = 0.0f;
};

struct EDENSPACESIMULATOR_API FEdenOperatorHudModel
{
	static FEdenOperatorHudSnapshot Assemble(
		const FEdenMissionStateSnapshot& Mission,
		const FEdenFuelStateSnapshot& Fuel,
		const FEdenPowerStateSnapshot& Power,
		const FEdenThermalStateSnapshot& Thermal,
		const FEdenOperatorStateSnapshot& Operator,
		const TArray<FEdenAlert>& Alerts,
		float SimulationTimeSeconds,
		const FEdenOsAcceptedAdvisory& Advisory = FEdenOsAcceptedAdvisory());
};
