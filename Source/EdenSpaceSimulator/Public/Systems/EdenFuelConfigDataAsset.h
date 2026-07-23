// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Systems/EdenFuelModel.h"

#include "EdenFuelConfigDataAsset.generated.h"

UCLASS(BlueprintType)
class EDENSPACESIMULATOR_API UEdenFuelConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Fuel")
	FEdenFuelConfig FuelConfig;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
