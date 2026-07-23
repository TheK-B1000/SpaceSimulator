// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Systems/EdenResourceTypes.h"

#include "EdenFuelSystemTestListener.generated.h"

UCLASS()
class UEdenFuelSystemTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleFuelStateChanged(EEdenFuelState PreviousState, EEdenFuelState NewState);

	UFUNCTION()
	void HandleFuelDepleted();

	UPROPERTY()
	TArray<EEdenFuelState> PreviousStates;

	UPROPERTY()
	TArray<EEdenFuelState> NewStates;

	int32 DepletedEventCount = 0;
};
