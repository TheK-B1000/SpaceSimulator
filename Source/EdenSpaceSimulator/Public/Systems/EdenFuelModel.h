// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Systems/EdenResourceTypes.h"

#include "EdenFuelModel.generated.h"

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFuelConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Fuel", meta = (ClampMin = "0.000001", Units = "kg"))
	float CapacityKilograms = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Fuel", meta = (ClampMin = "0.0"))
	float ConsumptionRateKilogramsPerSecond = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Fuel", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InitialFuelFraction = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Fuel", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WarningThresholdFraction = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Fuel", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CriticalThresholdFraction = 0.1f;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFuelStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Fuel", meta = (Units = "kg"))
	float FuelQuantityKilograms = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Fuel")
	float FuelFraction = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Fuel")
	EEdenFuelState FuelState = EEdenFuelState::Depleted;
};

struct EDENSPACESIMULATOR_API FEdenFuelStepResult
{
	FEdenFuelStateSnapshot Snapshot;
	float FuelConsumedKilograms = 0.0f;
	float SanitizedDemandNormalized = 0.0f;
	bool bConfigWasValid = true;
	bool bDeltaTimeWasValid = true;
	bool bDemandWasSanitized = false;
};

struct EDENSPACESIMULATOR_API FEdenFuelModel
{
	static bool ValidateConfig(const FEdenFuelConfig& Config, TArray<FString>* OutErrors = nullptr);
	static bool IsValidDeltaTime(float DeltaTimeSeconds);
	static float SanitizeDemandNormalized(float DemandNormalized, bool* bOutWasSanitized = nullptr);
	static float ClampFuelQuantityKilograms(float FuelQuantityKilograms, float CapacityKilograms, bool* bOutWasSanitized = nullptr);
	static EEdenFuelState DeriveFuelState(const FEdenFuelConfig& Config, float FuelQuantityKilograms);
	static FEdenFuelStateSnapshot MakeSnapshot(const FEdenFuelConfig& Config, float FuelQuantityKilograms);
	static FEdenFuelStateSnapshot MakeInitialSnapshot(const FEdenFuelConfig& Config);
	static FEdenFuelStepResult Step(
		const FEdenFuelConfig& Config,
		const FEdenFuelStateSnapshot& CurrentSnapshot,
		float DemandNormalized,
		float DeltaTimeSeconds);
};
