// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"
#include "Systems/EdenFuelModel.h"

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
class EDENSPACESIMULATOR_API UEdenFuelSystemComponent : public UActorComponent, public IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	UEdenFuelSystemComponent();

	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

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
	void ApplySnapshot(const FEdenFuelStateSnapshot& NewSnapshot, bool bBroadcastEvents);
	FString MakeLogContext() const;

	UPROPERTY(EditAnywhere, Category = "Eden|Fuel")
	TObjectPtr<UEdenFuelConfigDataAsset> FuelConfigDataAsset;

	UPROPERTY(Transient)
	FEdenFuelConfig ActiveFuelConfig;

	UPROPERTY(Transient)
	FEdenFuelStateSnapshot CurrentSnapshot;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenSimulationClockSubsystem> RegisteredSimulationClock;

	UPROPERTY(Transient)
	TWeakObjectPtr<UActorComponent> PropulsionDemandSourceComponent;

	UPROPERTY(Transient)
	float ConsumptionDemandNormalized = 0.0f;

	UPROPERTY(Transient)
	bool bFuelSimulationEnabled = false;

	UPROPERTY(Transient)
	bool bPropulsionDemandSourceDiscoveryComplete = false;

	UPROPERTY(Transient)
	bool bLoggedExpiredPropulsionDemandSource = false;

	UPROPERTY(Transient)
	bool bLoggedSanitizedPropulsionDemand = false;
};
