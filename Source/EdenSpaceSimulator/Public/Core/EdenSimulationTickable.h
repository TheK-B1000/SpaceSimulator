// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "EdenSimulationTickable.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEdenSimulationTickable : public UInterface
{
	GENERATED_BODY()
};

class EDENSPACESIMULATOR_API IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	virtual void AdvanceSimulation(float FixedDeltaSeconds) = 0;
};
