// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct EDENSPACESIMULATOR_API FEdenFixedStepClockModel
{
	static bool IsValidDeltaTime(float DeltaTimeSeconds);
	static bool IsValidFixedStepSeconds(float FixedStepSeconds);
	static bool IsValidMaxCatchUpSteps(int32 MaxCatchUpSteps);

	static int32 CalculateSteps(
		float DeltaTimeSeconds,
		float FixedStepSeconds,
		int32 MaxCatchUpSteps,
		float& InOutAccumulatorSeconds,
		int32& OutDroppedSteps);
};
