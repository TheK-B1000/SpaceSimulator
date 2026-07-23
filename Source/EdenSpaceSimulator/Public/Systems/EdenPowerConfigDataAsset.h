// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Systems/EdenPowerModel.h"

#include "EdenPowerConfigDataAsset.generated.h"

UCLASS(BlueprintType)
class EDENSPACESIMULATOR_API UEdenPowerConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Power")
	FEdenPowerConfig PowerConfig;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
