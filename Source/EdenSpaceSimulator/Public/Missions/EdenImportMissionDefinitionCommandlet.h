// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "EdenImportMissionDefinitionCommandlet.generated.h"

/**
 * Editor-only: authors a UEdenMissionDefinitionDataAsset from a checked-in JSON mission definition, so
 * scenarios stay data (no C++ scenario factory) and their source is reviewable text.
 *
 *   UnrealEditor-Cmd.exe <uproject> -run=EdenImportMissionDefinition
 *       -Json=<path to .json> -Asset=/Game/Eden/Data/Missions/DA_Name
 *
 * The definition is validated with FEdenMissionModel::ValidateDefinition before anything is written.
 */
UCLASS()
class EDENSPACESIMULATOR_API UEdenImportMissionDefinitionCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UEdenImportMissionDefinitionCommandlet();

	virtual int32 Main(const FString& Params) override;
};
