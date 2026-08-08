// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Operations/EdenOperatorTypes.h"

#include "EdenOperatorControlComponent.generated.h"

class UEdenFlightMovementComponent;
class UEdenOperatorControlConfigDataAsset;
class UEdenPowerSystemComponent;
class UEdenThermalSystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FEdenOperatorIntentChangedSignature,
	FEdenOperatorIntent,
	PreviousIntent,
	FEdenOperatorIntent,
	NewIntent);

UCLASS(ClassGroup = (Eden), meta = (BlueprintSpawnableComponent))
class EDENSPACESIMULATOR_API UEdenOperatorControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEdenOperatorControlComponent();

	UFUNCTION(BlueprintCallable, Category = "Eden|Operations")
	bool InitializeOperatorControl(const FEdenOperatorControlConfig& InConfig);

	UFUNCTION(BlueprintCallable, Category = "Eden|Operations")
	bool ResetOperatorControl();

	UFUNCTION(BlueprintCallable, Category = "Eden|Operations")
	bool SetThermalControlMode(EEdenThermalControlMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "Eden|Operations")
	bool SetLoadShedMode(EEdenLoadShedMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "Eden|Operations")
	bool SetPropulsionPriorityMode(EEdenPropulsionPriorityMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "Eden|Operations")
	bool CycleThermalControlMode();

	UFUNCTION(BlueprintCallable, Category = "Eden|Operations")
	bool ToggleLoadShedMode();

	UFUNCTION(BlueprintCallable, Category = "Eden|Operations")
	bool TogglePropulsionPriorityMode();

	UFUNCTION(BlueprintPure, Category = "Eden|Operations")
	bool IsOperatorControlEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Operations")
	FEdenOperatorIntent GetOperatorIntent() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Operations")
	FEdenOperatorStateSnapshot GetOperatorStateSnapshot() const;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Operations")
	FEdenOperatorIntentChangedSignature OnOperatorIntentChanged;

protected:
	virtual void BeginPlay() override;

private:
	bool InitializeFromConfiguredDataAsset();
	bool ApplyCurrentIntent(const FEdenOperatorIntent& PreviousIntent, bool bBroadcastEvents);
	bool ResolveTargets();
	FString MakeLogContext() const;

	UPROPERTY(EditAnywhere, Category = "Eden|Operations")
	TSoftObjectPtr<UEdenOperatorControlConfigDataAsset> OperatorControlConfigDataAsset;

	UPROPERTY(Transient)
	FEdenOperatorControlConfig ActiveConfig;

	UPROPERTY(Transient)
	FEdenOperatorIntent CurrentIntent;

	UPROPERTY(Transient)
	FEdenOperatorResolvedModifiers CurrentModifiers;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenPowerSystemComponent> CachedPower;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenThermalSystemComponent> CachedThermal;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenFlightMovementComponent> CachedMovement;

	UPROPERTY(Transient)
	bool bOperatorControlEnabled = false;
};
