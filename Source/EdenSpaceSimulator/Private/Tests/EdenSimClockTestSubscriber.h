// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"

#include "EdenSimClockTestSubscriber.generated.h"

class UEdenSimulationClockSubsystem;

UCLASS()
class UEdenSimClockPlainTestObject : public UObject
{
	GENERATED_BODY()
};

UCLASS()
class UEdenSimClockTestSubscriber : public UObject, public IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

	int32 AdvanceCallCount = 0;
	float LastFixedDeltaSeconds = 0.0f;
	float TotalAdvancedSeconds = 0.0f;
	bool bRegisterTargetOnFirstAdvance = false;
	bool bUnregisterTargetOnFirstAdvance = false;
	FName SubscriberName;
	TArray<FName>* ExecutionOrderLog = nullptr;
	TWeakObjectPtr<UEdenSimulationClockSubsystem> ClockToMutate;
	TWeakObjectPtr<UObject> SubscriberToRegister;
	TWeakObjectPtr<UObject> SubscriberToUnregister;
};
