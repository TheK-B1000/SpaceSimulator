// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Flight/EdenPropulsionDemandSource.h"

#include "EdenResourceIntegrationTestTypes.generated.h"

UCLASS()
class UEdenResourceIntegrationTestDemandComponent : public UActorComponent, public IEdenPropulsionDemandSource
{
	GENERATED_BODY()

public:
	virtual float GetPropulsionDemandNormalized() const override
	{
		return DemandNormalized;
	}

	float DemandNormalized = 0.0f;
};
