// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "EdenPropellantSupply.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEdenPropellantSupply : public UInterface
{
	GENERATED_BODY()
};

/**
 * Stored propellant, owned by the implementer (UEdenFuelSystemComponent).
 *
 * Flight reads the mass and makes exactly one DrawPropellantKilograms call per
 * flight step; the supply alone subtracts. Flight never writes propellant.
 */
class EDENSPACESIMULATOR_API IEdenPropellantSupply
{
	GENERATED_BODY()

public:
	/** False while the supply is not simulating (no config, disabled); flight then treats propellant as unmetered. */
	virtual bool IsPropellantSupplyAvailable() const = 0;

	virtual double GetPropellantMassKilograms() const = 0;

	/**
	 * Removes up to RequestedKilograms and returns the mass actually granted, in [0, min(request, stored)].
	 * Stored propellant never goes negative.
	 */
	virtual double DrawPropellantKilograms(double RequestedKilograms) = 0;
};
