// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"
#include "Flight/EdenPropellantSupply.h"
#include "Systems/EdenFuelModel.h"
#include "Systems/EdenResourceDebugTypes.h"

#include "EdenFuelSystemComponent.generated.h"

class UEdenFuelConfigDataAsset;
class UEdenSimulationClockSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FEdenFuelStateChangedSignature,
	EEdenFuelState,
	PreviousState,
	EEdenFuelState,
	NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEdenFuelDepletedSignature);

UCLASS(ClassGroup = (Eden), meta = (BlueprintSpawnableComponent))
class EDENSPACESIMULATOR_API UEdenFuelSystemComponent
	: public UActorComponent
	, public IEdenSimulationTickable
	, public IEdenPropellantSupply
{
	GENERATED_BODY()

public:
	UEdenFuelSystemComponent();

	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

	// IEdenPropellantSupply: this component is the only owner and subtractor of stored propellant.
	virtual bool IsPropellantSupplyAvailable() const override;
	virtual double GetPropellantMassKilograms() const override;
	virtual double DrawPropellantKilograms(double RequestedKilograms) override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Fuel")
	bool InitializeFuelSimulation(const FEdenFuelConfig& FuelConfig);

	UFUNCTION(BlueprintCallable, Category = "Eden|Fuel")
	bool ResetFuelState();

	UFUNCTION(BlueprintCallable, Category = "Eden|Fuel")
	bool SetConsumptionDemandNormalized(float DemandNormalized);

	UFUNCTION(BlueprintCallable, Category = "Eden|Fuel")
	bool SetFuelQuantityKilograms(float FuelQuantityKilograms);

	UFUNCTION(BlueprintCallable, Category = "Eden|Fuel")
	bool RefreshPropulsionDemandSource();

	UFUNCTION(BlueprintPure, Category = "Eden|Fuel")
	bool IsFuelSimulationEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Fuel")
	float GetConsumptionDemandNormalized() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Fuel")
	FEdenFuelConfig GetActiveFuelConfig() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Fuel")
	FEdenFuelStateSnapshot GetFuelStateSnapshot() const;

	FEdenFuelDebugSnapshot GetFuelDebugSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Eden|Fuel")
	bool RegisterWithSimulationClock();

	UFUNCTION(BlueprintCallable, Category = "Eden|Fuel")
	bool UnregisterFromSimulationClock();

	UPROPERTY(BlueprintAssignable, Category = "Eden|Fuel")
	FEdenFuelStateChangedSignature OnFuelStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Fuel")
	FEdenFuelDepletedSignature OnFuelDepleted;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool InitializeFromConfiguredDataAsset();
	void DisableFuelSimulation(const FString& Reason);
	bool ValidateAndLogConfig(const FEdenFuelConfig& FuelConfig) const;
	float ResolveConsumptionDemandNormalized();
	float GetDebugPropulsionDemandNormalized() const;
	/** PreciseKilograms, when given, becomes the ledger value; otherwise the ledger follows the snapshot. */
	void ApplySnapshot(const FEdenFuelStateSnapshot& NewSnapshot, bool bBroadcastEvents, const double* PreciseKilograms = nullptr);
	bool IsDemandSourceMeteringPropellant() const;
	FString MakeLogContext() const;

	UPROPERTY(EditAnywhere, Category = "Eden|Fuel")
	TObjectPtr<UEdenFuelConfigDataAsset> FuelConfigDataAsset;

	UPROPERTY(Transient)
	FEdenFuelConfig ActiveFuelConfig;

	UPROPERTY(Transient)
	FEdenFuelStateSnapshot CurrentSnapshot;

	/**
	 * Authoritative stored propellant in double precision. Per-step flight draws (~0.008 kg at 120 Hz)
	 * are below float resolution on a 100 kg tank; CurrentSnapshot's float is derived for display.
	 */
	double PropellantLedgerKilograms = 0.0;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenSimulationClockSubsystem> RegisteredSimulationClock;

	UPROPERTY(Transient)
	TWeakObjectPtr<UActorComponent> PropulsionDemandSourceComponent;

	UPROPERTY(Transient)
	float ConsumptionDemandNormalized = 0.0f;

	UPROPERTY(Transient)
	bool bFuelSimulationEnabled = false;

	UPROPERTY(Transient)
	bool bHasValidFuelConfiguration = false;

	UPROPERTY(Transient)
	bool bPropulsionDemandSourceDiscoveryComplete = false;

	UPROPERTY(Transient)
	bool bLoggedExpiredPropulsionDemandSource = false;

	UPROPERTY(Transient)
	bool bLoggedSanitizedPropulsionDemand = false;
};
