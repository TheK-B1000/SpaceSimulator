// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "EdenPropulsionDemandSource.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEdenPropulsionDemandSource : public UInterface
{
	GENERATED_BODY()
};

class EDENSPACESIMULATOR_API IEdenPropulsionDemandSource
{
	GENERATED_BODY()

public:
	virtual float GetPropulsionDemandNormalized() const = 0;

	/**
	 * True when this source draws propellant itself through IEdenPropellantSupply. The fuel
	 * system must then not also bill demand x rate, so propellant is subtracted exactly once.
	 */
	virtual bool MetersPropellantDirectly() const
	{
		return false;
	}
};
