// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Operations/EdenOperatorTypes.h"

struct EDENSPACESIMULATOR_API FEdenOperatorControlModel
{
	static bool ValidateConfig(const FEdenOperatorControlConfig& Config, TArray<FString>* OutErrors = nullptr);
	static FEdenOperatorResolvedModifiers ResolveIntent(
		const FEdenOperatorIntent& Intent,
		const FEdenOperatorControlConfig& Config);
	static FEdenOperatorStateSnapshot MakeSnapshot(
		const FEdenOperatorIntent& Intent,
		const FEdenOperatorResolvedModifiers& Modifiers);
};
