// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Systems/EdenThermalModel.h"

#include "EdenThermalConfigDataAsset.generated.h"

UCLASS(BlueprintType)
class EDENSPACESIMULATOR_API UEdenThermalConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Thermal")
	FEdenThermalConfig ThermalConfig;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
