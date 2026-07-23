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
};
