// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Missions/EdenMissionModel.h"
#include "Missions/EdenMissionTypes.h"

#include "EdenMissionDefinitionDataAsset.generated.h"

UCLASS(BlueprintType)
class EDENSPACESIMULATOR_API UEdenMissionDefinitionDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FEdenMissionDefinitionConfig MissionDefinition;

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	FName GetMissionId() const { return MissionDefinition.MissionId; }

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	FText GetDisplayName() const { return MissionDefinition.DisplayName; }

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	const FEdenMissionDefinitionConfig& GetMissionDefinition() const { return MissionDefinition; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
