// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Systems/EdenResourceTypes.h"
#include "UObject/Interface.h"

#include "EdenPropulsionPowerSource.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEdenPropulsionPowerSource : public UInterface
{
	GENERATED_BODY()
};

/** Read-only electrical state for propulsion (valves, controllers). Implemented by UEdenPowerSystemComponent. */
class EDENSPACESIMULATOR_API IEdenPropulsionPowerSource
{
	GENERATED_BODY()

public:
	/** False while power is not simulating; flight then applies no power limit. */
	virtual bool IsPropulsionPowerSourceAvailable() const = 0;

	virtual EEdenPowerState GetPropulsionPowerState() const = 0;
};
