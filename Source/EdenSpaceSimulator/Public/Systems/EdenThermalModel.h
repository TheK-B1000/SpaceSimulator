// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Systems/EdenResourceTypes.h"

#include "EdenThermalModel.generated.h"

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenThermalConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float AbsoluteMinTemperatureCelsius = -100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float AmbientTemperatureCelsius = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float WarningTemperatureCelsius = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float CriticalTemperatureCelsius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float AbsoluteMaxTemperatureCelsius = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float InitialTemperatureCelsius = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal", meta = (ClampMin = "0.0"))
	float HeatGenerationDegreesCelsiusPerSecond = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal", meta = (ClampMin = "0.0"))
	float DissipationDegreesCelsiusPerSecond = 0.5f;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenThermalStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float TemperatureCelsius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float HeatGenerationDegreesCelsiusPerSecond = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float DissipationDegreesCelsiusPerSecond = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	EEdenThermalState ThermalState = EEdenThermalState::Overheated;
};

struct EDENSPACESIMULATOR_API FEdenThermalStepResult
{
	FEdenThermalStateSnapshot Snapshot;
	float TemperatureDeltaCelsius = 0.0f;
	bool bConfigWasValid = true;
	bool bDeltaTimeWasValid = true;
	bool bHeatGenerationWasSanitized = false;
	bool bDissipationWasSanitized = false;
	bool bTemperatureWasSanitized = false;
};

struct EDENSPACESIMULATOR_API FEdenThermalModel
{
	static bool ValidateConfig(const FEdenThermalConfig& Config, TArray<FString>* OutErrors = nullptr);
	static bool IsValidDeltaTime(float DeltaTimeSeconds);
	static float SanitizeNonnegativeDegreesCelsiusPerSecond(float Rate, bool* bOutWasSanitized = nullptr);
	static float ClampTemperatureCelsius(
		float TemperatureCelsius,
		const FEdenThermalConfig& Config,
		bool* bOutWasSanitized = nullptr);
	static EEdenThermalState DeriveThermalState(const FEdenThermalConfig& Config, float TemperatureCelsius);
	static FEdenThermalStateSnapshot MakeSnapshot(
		const FEdenThermalConfig& Config,
		float TemperatureCelsius,
		float HeatGenerationDegreesCelsiusPerSecond,
		float DissipationDegreesCelsiusPerSecond);
	static FEdenThermalStateSnapshot MakeInitialSnapshot(const FEdenThermalConfig& Config);
	static FEdenThermalStepResult Step(
		const FEdenThermalConfig& Config,
		const FEdenThermalStateSnapshot& CurrentSnapshot,
		float DeltaTimeSeconds);
};
