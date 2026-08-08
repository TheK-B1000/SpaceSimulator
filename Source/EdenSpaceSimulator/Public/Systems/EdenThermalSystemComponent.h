// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"
#include "Systems/EdenResourceDebugTypes.h"
#include "Systems/EdenThermalModel.h"

#include "EdenThermalSystemComponent.generated.h"

class UEdenSimulationClockSubsystem;
class UEdenThermalConfigDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FEdenThermalStateChangedSignature,
	EEdenThermalState,
	PreviousState,
	EEdenThermalState,
	NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEdenThermalOverheatedSignature);

UCLASS(ClassGroup = (Eden), meta = (BlueprintSpawnableComponent))
class EDENSPACESIMULATOR_API UEdenThermalSystemComponent : public UActorComponent, public IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	UEdenThermalSystemComponent();

	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool InitializeThermalSimulation(const FEdenThermalConfig& ThermalConfig);

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool ResetThermalState();

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool SetTemperatureCelsius(float TemperatureCelsius);

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool SetHeatGenerationDegreesCelsiusPerSecond(float HeatGenerationDegreesCelsiusPerSecond);

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool SetDissipationDegreesCelsiusPerSecond(float DissipationDegreesCelsiusPerSecond);

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool SetExternalHeatingRateDegreesCelsiusPerSecond(float ExternalHeatingRateDegreesCelsiusPerSecond);

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool ClearExternalHeatingRate();

	UFUNCTION(BlueprintPure, Category = "Eden|Thermal")
	bool IsThermalSimulationEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Thermal")
	FEdenThermalConfig GetActiveThermalConfig() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Thermal")
	FEdenThermalStateSnapshot GetThermalStateSnapshot() const;

	FEdenThermalDebugSnapshot GetThermalDebugSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool RegisterWithSimulationClock();

	UFUNCTION(BlueprintCallable, Category = "Eden|Thermal")
	bool UnregisterFromSimulationClock();

	UPROPERTY(BlueprintAssignable, Category = "Eden|Thermal")
	FEdenThermalStateChangedSignature OnThermalStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Thermal")
	FEdenThermalOverheatedSignature OnThermalOverheated;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool InitializeFromConfiguredDataAsset();
	void DisableThermalSimulation(const FString& Reason);
	bool ValidateAndLogConfig(const FEdenThermalConfig& ThermalConfig) const;
	void ApplySnapshot(const FEdenThermalStateSnapshot& NewSnapshot, bool bBroadcastEvents);
	FString MakeLogContext() const;

	UPROPERTY(EditAnywhere, Category = "Eden|Thermal")
	TObjectPtr<UEdenThermalConfigDataAsset> ThermalConfigDataAsset;

	UPROPERTY(Transient)
	FEdenThermalConfig ActiveThermalConfig;

	UPROPERTY(Transient)
	FEdenThermalStateSnapshot CurrentSnapshot;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenSimulationClockSubsystem> RegisteredSimulationClock;

	UPROPERTY(Transient)
	bool bThermalSimulationEnabled = false;

	UPROPERTY(Transient)
	bool bHasValidThermalConfiguration = false;
};
