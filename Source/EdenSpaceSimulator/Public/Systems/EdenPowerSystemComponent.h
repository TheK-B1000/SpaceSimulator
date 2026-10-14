// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"
#include "Flight/EdenPropulsionPowerSource.h"
#include "Systems/EdenPowerModel.h"
#include "Systems/EdenResourceDebugTypes.h"

#include "EdenPowerSystemComponent.generated.h"

class UEdenPowerConfigDataAsset;
class UEdenSimulationClockSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FEdenPowerStateChangedSignature,
	EEdenPowerState,
	PreviousState,
	EEdenPowerState,
	NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEdenPowerDepletedSignature);

UCLASS(ClassGroup = (Eden), meta = (BlueprintSpawnableComponent))
class EDENSPACESIMULATOR_API UEdenPowerSystemComponent
	: public UActorComponent
	, public IEdenSimulationTickable
	, public IEdenPropulsionPowerSource
{
	GENERATED_BODY()

public:
	UEdenPowerSystemComponent();

	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

	// IEdenPropulsionPowerSource (read-only; power state stays owned here).
	virtual bool IsPropulsionPowerSourceAvailable() const override;
	virtual EEdenPowerState GetPropulsionPowerState() const override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool InitializePowerSimulation(const FEdenPowerConfig& PowerConfig);

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool ResetPowerState();

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool SetGenerationKilowatts(float GenerationKilowatts);

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool SetBaselineDemandKilowatts(float BaselineDemandKilowatts);

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool SetBatteryChargeKilowattHours(float BatteryChargeKilowattHours);

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool SetExternalDemandKilowatts(float ExternalDemandKilowatts);

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool ClearExternalDemand();

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool SetOperatorDemandKilowatts(float OperatorDemandKilowatts);

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool ClearOperatorDemand();

	/**
	 * Operator demand with its thermal-control share identified, so the power bus can serve cooling
	 * before auxiliary load (ExecPlan 0011, E1). SetOperatorDemandKilowatts treats all of it as non-cooling.
	 */
	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool SetOperatorDemandBreakdown(float TotalOperatorDemandKilowatts, float ThermalControlDemandKilowatts);

	/** Mission-declared battery discharge limit for this run (kW, > 0). Overrides the config until cleared. */
	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool SetBatteryDischargeLimitKilowatts(float BatteryMaxDischargeKilowatts);

	/** Restores the configured discharge limit (0 = unlimited). */
	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool ClearBatteryDischargeLimitOverride();

	UFUNCTION(BlueprintPure, Category = "Eden|Power")
	float GetEffectiveBatteryDischargeLimitKilowatts() const;

	/** Served / requested thermal-control power (1 when the bus is unlimited or cooling draws nothing). */
	UFUNCTION(BlueprintPure, Category = "Eden|Power")
	float GetCoolingAuthority() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Power")
	bool IsPowerSimulationEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Power")
	FEdenPowerConfig GetActivePowerConfig() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Power")
	FEdenPowerStateSnapshot GetPowerStateSnapshot() const;

	FEdenPowerDebugSnapshot GetPowerDebugSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool RegisterWithSimulationClock();

	UFUNCTION(BlueprintCallable, Category = "Eden|Power")
	bool UnregisterFromSimulationClock();

	UPROPERTY(BlueprintAssignable, Category = "Eden|Power")
	FEdenPowerStateChangedSignature OnPowerStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Power")
	FEdenPowerDepletedSignature OnPowerDepleted;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool InitializeFromConfiguredDataAsset();
	void DisablePowerSimulation(const FString& Reason);
	bool ValidateAndLogConfig(const FEdenPowerConfig& PowerConfig) const;
	void ApplySnapshot(const FEdenPowerStateSnapshot& NewSnapshot, bool bBroadcastEvents);
	bool SetOperatorDemandInternal(float OperatorDemandKilowatts);
	FString MakeLogContext() const;

	/** Thermal-control share of the operator demand (power-bus input). */
	float ThermalControlDemandKilowatts = 0.0f;

	/** Mission override of BatteryMaxDischargeKilowatts; < 0 = none (use the config). */
	float BatteryDischargeLimitOverrideKilowatts = -1.0f;

	/** Whether the bus was curtailing anything at the last applied snapshot (for transition logs). */
	bool bBusWasCurtailing = false;

	UPROPERTY(EditAnywhere, Category = "Eden|Power")
	TObjectPtr<UEdenPowerConfigDataAsset> PowerConfigDataAsset;

	UPROPERTY(Transient)
	FEdenPowerConfig ActivePowerConfig;

	UPROPERTY(Transient)
	FEdenPowerStateSnapshot CurrentSnapshot;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenSimulationClockSubsystem> RegisteredSimulationClock;

	UPROPERTY(Transient)
	bool bPowerSimulationEnabled = false;

	UPROPERTY(Transient)
	bool bHasValidPowerConfiguration = false;
};
