// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorControlConfigDataAsset.h"

#include "Operations/EdenOperatorControlModel.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UEdenOperatorControlConfigDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);

	TArray<FString> ValidationErrors;
	if (!FEdenOperatorControlModel::ValidateConfig(OperatorControlConfig, &ValidationErrors))
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
