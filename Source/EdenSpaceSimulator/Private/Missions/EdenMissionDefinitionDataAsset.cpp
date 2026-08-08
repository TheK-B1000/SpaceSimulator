// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/EdenMissionDefinitionDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UEdenMissionDefinitionDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);

	TArray<FString> ValidationErrors;
	if (!FEdenMissionModel::ValidateDefinition(MissionDefinition, &ValidationErrors))
	{
		for (const FString& ValidationError : ValidationErrors)
		{
			Context.AddError(FText::FromString(ValidationError));
		}

		return EDataValidationResult::Invalid;
	}

	return SuperResult == EDataValidationResult::Invalid ? SuperResult : EDataValidationResult::Valid;
}
#endif
