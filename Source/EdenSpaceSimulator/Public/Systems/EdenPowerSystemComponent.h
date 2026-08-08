// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"
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
class EDENSPACESIMULATOR_API UEdenPowerSystemComponent : public UActorComponent, public IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	UEdenPowerSystemComponent();

	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

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
	FString MakeLogContext() const;

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
