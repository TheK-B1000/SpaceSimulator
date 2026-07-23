// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenPowerConfigDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UEdenPowerConfigDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);

	TArray<FString> ValidationErrors;
	if (!FEdenPowerModel::ValidateConfig(PowerConfig, &ValidationErrors))
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
