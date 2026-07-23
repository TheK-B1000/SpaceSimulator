// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Systems/EdenResourceTypes.h"

#include "EdenPowerThermalSystemTestListener.generated.h"

UCLASS()
class UEdenPowerSystemTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandlePowerStateChanged(EEdenPowerState PreviousState, EEdenPowerState NewState);

	UFUNCTION()
	void HandlePowerDepleted();

	UPROPERTY()
	TArray<EEdenPowerState> PreviousStates;

	UPROPERTY()
	TArray<EEdenPowerState> NewStates;

	int32 DepletedEventCount = 0;
};

UCLASS()
class UEdenThermalSystemTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleThermalStateChanged(EEdenThermalState PreviousState, EEdenThermalState NewState);

	UFUNCTION()
	void HandleThermalOverheated();

	UPROPERTY()
	TArray<EEdenThermalState> PreviousStates;

	UPROPERTY()
	TArray<EEdenThermalState> NewStates;

	int32 OverheatedEventCount = 0;
};
