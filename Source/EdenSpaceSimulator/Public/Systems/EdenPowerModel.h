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

	/**
	 * Power-bus limit (ExecPlan 0011, E1): the most the battery can supply on top of generation, in kW.
	 * 0 = unlimited (every load is always fully served; the pre-0011 behaviour).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Power", meta = (ClampMin = "0.0"))
	float BatteryMaxDischargeKilowatts = 0.0f;
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

	// --- Power bus (ExecPlan 0011, E1). Inputs: ---

	/** Electrical demand of active thermal control (part of OperatorDemandKilowatts). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float ThermalControlDemandKilowatts = 0.0f;

	/** Effective battery discharge limit; 0 = unlimited bus. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float BatteryMaxDischargeKilowatts = 0.0f;

	// --- Allocation (priority: essential, thermal control, auxiliary, then charging): ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	bool bBusLimited = false;

	/** Generation plus available battery discharge (0 when the bus is unlimited). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float BusCapacityKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float EssentialRequestedKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float EssentialServedKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float ThermalControlServedKilowatts = 0.0f;

	/** Mission/external demand plus any non-cooling operator load, after load shed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float AuxiliaryRequestedKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float AuxiliaryServedKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float AuxiliaryCurtailedKilowatts = 0.0f;

	/** Total load actually powered. Equals TotalDemandKilowatts when nothing is curtailed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float DeliveredDemandKilowatts = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float BatteryDischargeKilowatts = 0.0f;

	/** Served / requested thermal-control power, in [0, 1]. Active cooling delivers this fraction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Power|Bus")
	float CoolingAuthority = 1.0f;
};

/** Deterministic power-bus allocation for one set of inputs. */
struct EDENSPACESIMULATOR_API FEdenPowerBusAllocation
{
	bool bLimited = false;
	double CapacityKilowatts = 0.0;
	double EssentialRequestedKilowatts = 0.0;
	double EssentialServedKilowatts = 0.0;
	double CoolingRequestedKilowatts = 0.0;
	double CoolingServedKilowatts = 0.0;
	double AuxiliaryRequestedKilowatts = 0.0;
	double AuxiliaryServedKilowatts = 0.0;
	double DeliveredKilowatts = 0.0;
	double CoolingAuthority = 1.0;
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

	/**
	 * Allocates the bus for the snapshot's inputs. Unlimited (limit <= 0): every load is served.
	 * Limited: capacity = generation + (battery charged ? min(limit, AvailableDischargeKilowatts) : 0),
	 * served in priority order essential > thermal control > auxiliary. AvailableDischargeKilowatts < 0
	 * means no energy cap (used for the instantaneous snapshot; Step passes the energy left this step).
	 */
	static FEdenPowerBusAllocation AllocateBus(
		const FEdenPowerStateSnapshot& Snapshot,
		float ThermalControlDemandKilowatts,
		float BatteryMaxDischargeKilowatts,
		double AvailableDischargeKilowatts = -1.0);

	/** Copies the bus inputs into the snapshot and fills its allocation fields (and limited-bus net power). */
	static FEdenPowerStateSnapshot WithBusAllocation(
		const FEdenPowerStateSnapshot& Snapshot,
		float ThermalControlDemandKilowatts,
		float BatteryMaxDischargeKilowatts);
};
