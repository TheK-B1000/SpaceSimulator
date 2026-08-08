// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Operations/EdenOperatorTypes.h"

#include "EdenOperatorControlConfigDataAsset.generated.h"

UCLASS(BlueprintType)
class EDENSPACESIMULATOR_API UEdenOperatorControlConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Operations")
	FEdenOperatorControlConfig OperatorControlConfig;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
