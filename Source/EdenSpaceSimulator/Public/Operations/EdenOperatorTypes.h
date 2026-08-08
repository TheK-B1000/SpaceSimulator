// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdenOperatorTypes.generated.h"

UENUM(BlueprintType)
enum class EEdenThermalControlMode : uint8
{
	Off UMETA(DisplayName = "Off"),
	Nominal UMETA(DisplayName = "Nominal"),
	Boost UMETA(DisplayName = "Boost"),
	Emergency UMETA(DisplayName = "Emergency")
};

UENUM(BlueprintType)
enum class EEdenLoadShedMode : uint8
{
	Normal UMETA(DisplayName = "Normal"),
	Shed UMETA(DisplayName = "Shed")
};

UENUM(BlueprintType)
enum class EEdenPropulsionPriorityMode : uint8
{
	Full UMETA(DisplayName = "Full"),
	Reduced UMETA(DisplayName = "Reduced")
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOperatorControlConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Operations", meta = (ClampMin = "0.0"))
	float BoostDissipationDegreesCelsiusPerSecond = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Operations", meta = (ClampMin = "0.0"))
	float EmergencyDissipationDegreesCelsiusPerSecond = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Operations", meta = (ClampMin = "0.0"))
	float BoostCoolingDemandKilowatts = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Operations", meta = (ClampMin = "0.0"))
	float EmergencyCoolingDemandKilowatts = 4.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Operations", meta = (ClampMin = "0.0"))
	float LoadShedDemandReductionKilowatts = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Operations", meta = (ClampMin = "0.0"))
	float LoadShedDissipationReductionDegreesCelsiusPerSecond = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Operations", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReducedThrustAuthority = 0.5f;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOperatorIntent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Operations")
	EEdenThermalControlMode ThermalMode = EEdenThermalControlMode::Nominal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Operations")
	EEdenLoadShedMode LoadShedMode = EEdenLoadShedMode::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Operations")
	EEdenPropulsionPriorityMode PropulsionPriority = EEdenPropulsionPriorityMode::Full;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOperatorResolvedModifiers
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	float OperatorDemandKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	float OperatorDissipationDegreesCelsiusPerSecond = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	float ThrustAuthority = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	bool bStabilizationAssistAvailable = true;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOperatorStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	EEdenThermalControlMode ThermalMode = EEdenThermalControlMode::Nominal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	EEdenLoadShedMode LoadShedMode = EEdenLoadShedMode::Normal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	EEdenPropulsionPriorityMode PropulsionPriority = EEdenPropulsionPriorityMode::Full;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	float OperatorDemandKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	float OperatorDissipationDegreesCelsiusPerSecond = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	float ThrustAuthority = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	bool bStabilizationAssistAvailable = true;
};
