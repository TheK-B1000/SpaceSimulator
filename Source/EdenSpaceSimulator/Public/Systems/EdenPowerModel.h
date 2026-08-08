// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Systems/EdenResourceTypes.h"

#include "EdenPowerModel.generated.h"

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenPowerConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Power", meta = (ClampMin = "0.000001"))
	float BatteryCapacityKilowattHours = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Power", meta = (ClampMin = "0.0"))
	float GenerationKilowatts = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Power", meta = (ClampMin = "0.0"))
	float BaselineDemandKilowatts = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialChargeFraction = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WarningThresholdFraction = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CriticalThresholdFraction = 0.1f;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenPowerStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	float BatteryChargeKilowattHours = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	float ChargeFraction = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	float GenerationKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	float BaselineDemandKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	float ExternalDemandKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	float OperatorDemandKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	float TotalDemandKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	float NetPowerKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	EEdenPowerState PowerState = EEdenPowerState::Depleted;
};

struct EDENSPACESIMULATOR_API FEdenPowerStepResult
{
	FEdenPowerStateSnapshot Snapshot;
	float EnergyDeltaKilowattHours = 0.0f;
	bool bConfigWasValid = true;
	bool bDeltaTimeWasValid = true;
	bool bGenerationWasSanitized = false;
	bool bBaselineDemandWasSanitized = false;
	bool bExternalDemandWasSanitized = false;
	bool bOperatorDemandWasSanitized = false;
};

struct EDENSPACESIMULATOR_API FEdenPowerModel
{
	static bool ValidateConfig(const FEdenPowerConfig& Config, TArray<FString>* OutErrors = nullptr);
	static bool IsValidDeltaTime(float DeltaTimeSeconds);
	static float SanitizeNonnegativeKilowatts(float Kilowatts, bool* bOutWasSanitized = nullptr);
	static float SanitizeFiniteKilowatts(float Kilowatts, bool* bOutWasSanitized = nullptr);
	static float ClampBatteryChargeKilowattHours(
		float BatteryChargeKilowattHours,
		float BatteryCapacityKilowattHours,
		bool* bOutWasSanitized = nullptr);
	static EEdenPowerState DerivePowerState(const FEdenPowerConfig& Config, float BatteryChargeKilowattHours);
	static FEdenPowerStateSnapshot MakeSnapshot(
		const FEdenPowerConfig& Config,
		float BatteryChargeKilowattHours,
		float GenerationKilowatts,
		float BaselineDemandKilowatts,
		float ExternalDemandKilowatts = 0.0f,
		float OperatorDemandKilowatts = 0.0f);
	static FEdenPowerStateSnapshot MakeInitialSnapshot(const FEdenPowerConfig& Config);
	static FEdenPowerStepResult Step(
		const FEdenPowerConfig& Config,
		const FEdenPowerStateSnapshot& CurrentSnapshot,
		float DeltaTimeSeconds);
};
