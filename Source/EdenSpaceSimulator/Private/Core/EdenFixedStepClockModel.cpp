// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenFixedStepClockModel.h"

bool FEdenFixedStepClockModel::IsValidDeltaTime(float DeltaTimeSeconds)
{
	return FMath::IsFinite(DeltaTimeSeconds) && DeltaTimeSeconds > 0.0f;
}

bool FEdenFixedStepClockModel::IsValidFixedStepSeconds(float FixedStepSeconds)
{
	return FMath::IsFinite(FixedStepSeconds) && FixedStepSeconds > 0.0f;
}

bool FEdenFixedStepClockModel::IsValidMaxCatchUpSteps(int32 MaxCatchUpSteps)
{
	return MaxCatchUpSteps > 0;
}

int32 FEdenFixedStepClockModel::CalculateSteps(
	float DeltaTimeSeconds,
	float FixedStepSeconds,
	int32 MaxCatchUpSteps,
	float& InOutAccumulatorSeconds,
	int32& OutDroppedSteps)
{
	OutDroppedSteps = 0;

	if (!FMath::IsFinite(InOutAccumulatorSeconds) || InOutAccumulatorSeconds < 0.0f)
	{
		InOutAccumulatorSeconds = 0.0f;
	}

	if (!IsValidDeltaTime(DeltaTimeSeconds)
		|| !IsValidFixedStepSeconds(FixedStepSeconds)
		|| !IsValidMaxCatchUpSteps(MaxCatchUpSteps))
	{
		return 0;
	}

	const double AccumulatedSeconds =
		static_cast<double>(InOutAccumulatorSeconds) + static_cast<double>(DeltaTimeSeconds);
	const double FixedStep = static_cast<double>(FixedStepSeconds);
	const double DesiredStepsDouble = FMath::FloorToDouble((AccumulatedSeconds / FixedStep) + 1.0e-6);
	const int32 DesiredSteps = DesiredStepsDouble > static_cast<double>(TNumericLimits<int32>::Max())
		? TNumericLimits<int32>::Max()
		: static_cast<int32>(DesiredStepsDouble);
	const int32 StepsTaken = FMath::Min(DesiredSteps, MaxCatchUpSteps);

	OutDroppedSteps = FMath::Max(0, DesiredSteps - StepsTaken);

	const double AccountedSteps = OutDroppedSteps > 0
		? static_cast<double>(DesiredSteps)
		: static_cast<double>(StepsTaken);
	const double RemainderSeconds = FMath::Max(0.0, AccumulatedSeconds - (AccountedSteps * FixedStep));

	InOutAccumulatorSeconds = static_cast<float>(
		RemainderSeconds < static_cast<double>(KINDA_SMALL_NUMBER) ? 0.0 : RemainderSeconds);

	return StepsTaken;
}
