// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenThermalConfigDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UEdenThermalConfigDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);

	TArray<FString> ValidationErrors;
	if (!FEdenThermalModel::ValidateConfig(ThermalConfig, &ValidationErrors))
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
