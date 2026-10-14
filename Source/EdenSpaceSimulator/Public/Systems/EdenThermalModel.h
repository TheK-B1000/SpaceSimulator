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
	float OperatorDissipationDegreesCelsiusPerSecond = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float EffectiveDissipationDegreesCelsiusPerSecond = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float ExternalHeatingRateDegreesCelsiusPerSecond = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	EEdenThermalState ThermalState = EEdenThermalState::Overheated;
};

/**
 * Thermal limits in force when a snapshot was taken, captured by value so a later
 * configuration change cannot alter what an earlier snapshot reports. bIsValid is false when
 * the configuration fails ValidateConfig; the other values are then not observations.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenThermalLimitsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float AmbientTemperatureCelsius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float WarningTemperatureCelsius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float CriticalTemperatureCelsius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	float AbsoluteMaxTemperatureCelsius = 0.0f;
};

/** Which bound, if any, kept the realized temperature rate from matching heating minus cooling. */
enum class EEdenThermalRateLimit : uint8
{
	None,
	/** Net heating, but the temperature is held at AbsoluteMaxTemperatureCelsius. */
	AbsoluteMax,
	/** Net cooling, but the temperature is held at AbsoluteMinTemperatureCelsius. */
	AbsoluteMin,
	/** Dissipation only pulls toward ambient; it cannot carry the temperature past it. */
	Ambient
};

/**
 * Diagnostic breakdown of the effective thermal balance, derived from the authoritative snapshot
 * with the same Step() the simulation uses. Read-only: computing it never changes thermal state,
 * and it is not part of the telemetry contract.
 */
struct EDENSPACESIMULATOR_API FEdenThermalBalance
{
	double TemperatureCelsius = 0.0;
	double BaseHeatingDegreesCelsiusPerSecond = 0.0;
	double ExternalHeatingDegreesCelsiusPerSecond = 0.0;
	double TotalHeatingDegreesCelsiusPerSecond = 0.0;
	double BaseDissipationDegreesCelsiusPerSecond = 0.0;
	double OperatorDissipationDegreesCelsiusPerSecond = 0.0;
	double EffectiveDissipationDegreesCelsiusPerSecond = 0.0;
	/** Total heating minus effective dissipation, before any bound applies. */
	double UnconstrainedNetDegreesCelsiusPerSecond = 0.0;
	/** Rate the next simulation step will actually produce (after ambient and absolute-limit bounds). */
	double RealizedNetDegreesCelsiusPerSecond = 0.0;
	EEdenThermalRateLimit Limit = EEdenThermalRateLimit::None;
	double StepSeconds = 0.0;
	bool bIsValid = false;
};

struct EDENSPACESIMULATOR_API FEdenThermalStepResult
{
	FEdenThermalStateSnapshot Snapshot;
	float TemperatureDeltaCelsius = 0.0f;
	bool bConfigWasValid = true;
	bool bDeltaTimeWasValid = true;
	bool bHeatGenerationWasSanitized = false;
	bool bDissipationWasSanitized = false;
	bool bOperatorDissipationWasSanitized = false;
	bool bExternalHeatingRateWasSanitized = false;
	bool bTemperatureWasSanitized = false;
};

struct EDENSPACESIMULATOR_API FEdenThermalModel
{
	static bool ValidateConfig(const FEdenThermalConfig& Config, TArray<FString>* OutErrors = nullptr);
	static bool IsValidDeltaTime(float DeltaTimeSeconds);
	static float SanitizeNonnegativeDegreesCelsiusPerSecond(float Rate, bool* bOutWasSanitized = nullptr);
	static float SanitizeFiniteDegreesCelsiusPerSecond(float Rate, bool* bOutWasSanitized = nullptr);
	static float ClampTemperatureCelsius(
		float TemperatureCelsius,
		const FEdenThermalConfig& Config,
		bool* bOutWasSanitized = nullptr);
	static EEdenThermalState DeriveThermalState(const FEdenThermalConfig& Config, float TemperatureCelsius);
	static FEdenThermalStateSnapshot MakeSnapshot(
		const FEdenThermalConfig& Config,
		float TemperatureCelsius,
		float HeatGenerationDegreesCelsiusPerSecond,
		float DissipationDegreesCelsiusPerSecond,
		float ExternalHeatingRateDegreesCelsiusPerSecond = 0.0f,
		float OperatorDissipationDegreesCelsiusPerSecond = 0.0f);
	static FEdenThermalStateSnapshot MakeInitialSnapshot(const FEdenThermalConfig& Config);
	/** Value copy of the configured limits; invalid when the configuration is invalid. */
	static FEdenThermalLimitsSnapshot MakeLimitsSnapshot(const FEdenThermalConfig& Config);
	static FEdenThermalStepResult Step(
		const FEdenThermalConfig& Config,
		const FEdenThermalStateSnapshot& CurrentSnapshot,
		float DeltaTimeSeconds);

	/** Effective thermal balance for the next step of StepSeconds. Pure; does not alter the snapshot. */
	static FEdenThermalBalance ComputeThermalBalance(
		const FEdenThermalConfig& Config,
		const FEdenThermalStateSnapshot& Snapshot,
		float StepSeconds);

	/** One-line log form of a balance: every heating and cooling term, the net rates, and any bound. */
	static FString DescribeThermalBalance(const FEdenThermalBalance& Balance);
};
