// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementModel.h"

namespace EdenFlightMovementModel
{
constexpr float DefaultInputReleaseTolerance = 0.001f;

float SanitizeNonNegative(float Value)
{
	return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
}
}

FEdenFlightInputCommand FEdenFlightMovementModel::SanitizeCommand(
	const FEdenFlightInputCommand& Command,
	bool* bOutInputWasSanitized)
{
	bool bInputWasSanitized = false;

	FEdenFlightInputCommand SanitizedCommand;
	SanitizedCommand.TranslationInput = SanitizeNormalizedVector(Command.TranslationInput, bInputWasSanitized);
	SanitizedCommand.RotationInput = SanitizeNormalizedVector(Command.RotationInput, bInputWasSanitized);
	SanitizedCommand.bStabilizationEnabled = Command.bStabilizationEnabled;

	if (bOutInputWasSanitized)
	{
		*bOutInputWasSanitized = bInputWasSanitized;
	}

	return SanitizedCommand;
}

bool FEdenFlightMovementModel::IsValidDeltaTime(float DeltaTimeSeconds)
{
	return FMath::IsFinite(DeltaTimeSeconds) && DeltaTimeSeconds > 0.0f;
}

FEdenFlightIntegrationResult FEdenFlightMovementModel::IntegrateVelocity(
	const FEdenFlightVelocityState& CurrentVelocityState,
	const FEdenFlightInputCommand& Command,
	const FEdenFlightMovementSettings& Settings,
	const FQuat& BodyRotation,
	float DeltaTimeSeconds)
{
	FEdenFlightIntegrationResult Result;
	Result.SanitizedCommand = SanitizeCommand(Command, &Result.bInputWasSanitized);
	Result.VelocityState = CurrentVelocityState;
	Result.bDeltaTimeWasValid = IsValidDeltaTime(DeltaTimeSeconds);

	if (!Result.bDeltaTimeWasValid)
	{
		return Result;
	}

	const float MaxLinearSpeed = EdenFlightMovementModel::SanitizeNonNegative(Settings.MaxLinearSpeedCmPerSecond);
	const float LinearAcceleration = EdenFlightMovementModel::SanitizeNonNegative(Settings.LinearAccelerationCmPerSecondSquared);
	const float LinearDeceleration = EdenFlightMovementModel::SanitizeNonNegative(Settings.LinearDecelerationCmPerSecondSquared);
	const float MaxAngularSpeed = EdenFlightMovementModel::SanitizeNonNegative(Settings.MaxAngularSpeedDegreesPerSecond);
	const float AngularAcceleration = EdenFlightMovementModel::SanitizeNonNegative(Settings.AngularAccelerationDegreesPerSecondSquared);
	const float AngularDeceleration = EdenFlightMovementModel::SanitizeNonNegative(Settings.AngularDecelerationDegreesPerSecondSquared);
	const float InputReleaseTolerance = EdenFlightMovementModel::SanitizeNonNegative(
		FMath::Max(Settings.InputReleaseTolerance, EdenFlightMovementModel::DefaultInputReleaseTolerance));

	FVector LocalLinearVelocity = BodyRotation.UnrotateVector(CurrentVelocityState.LinearVelocityWorldCmPerSecond);
	LocalLinearVelocity = IntegrateAxisVector(
		LocalLinearVelocity,
		Result.SanitizedCommand.TranslationInput,
		MaxLinearSpeed,
		LinearAcceleration,
		LinearDeceleration,
		InputReleaseTolerance,
		Result.SanitizedCommand.bStabilizationEnabled,
		DeltaTimeSeconds);

	Result.VelocityState.LinearVelocityWorldCmPerSecond =
		BodyRotation.RotateVector(ClampVectorMagnitude(LocalLinearVelocity, MaxLinearSpeed));

	Result.VelocityState.AngularVelocityLocalDegreesPerSecond = IntegrateAxisVector(
		CurrentVelocityState.AngularVelocityLocalDegreesPerSecond,
		Result.SanitizedCommand.RotationInput,
		MaxAngularSpeed,
		AngularAcceleration,
		AngularDeceleration,
		InputReleaseTolerance,
		Result.SanitizedCommand.bStabilizationEnabled,
		DeltaTimeSeconds);
	Result.VelocityState.AngularVelocityLocalDegreesPerSecond =
		ClampVectorMagnitude(Result.VelocityState.AngularVelocityLocalDegreesPerSecond, MaxAngularSpeed);

	return Result;
}

FVector FEdenFlightMovementModel::RemoveInwardVelocity(const FVector& Velocity, const FVector& ImpactNormal)
{
	if (!IsFiniteVector(Velocity) || !IsFiniteVector(ImpactNormal))
	{
		return FVector::ZeroVector;
	}

	const FVector Normal = ImpactNormal.GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return Velocity;
	}

	const double InwardSpeed = FVector::DotProduct(Velocity, Normal);
	if (InwardSpeed >= 0.0)
	{
		return Velocity;
	}

	return Velocity - (Normal * InwardSpeed);
}

FVector FEdenFlightMovementModel::SanitizeNormalizedVector(const FVector& Value, bool& bInOutWasSanitized)
{
	FVector SanitizedValue = Value;

	if (!FMath::IsFinite(SanitizedValue.X))
	{
		SanitizedValue.X = 0.0;
		bInOutWasSanitized = true;
	}

	if (!FMath::IsFinite(SanitizedValue.Y))
	{
		SanitizedValue.Y = 0.0;
		bInOutWasSanitized = true;
	}

	if (!FMath::IsFinite(SanitizedValue.Z))
	{
		SanitizedValue.Z = 0.0;
		bInOutWasSanitized = true;
	}

	const FVector ComponentClampedValue(
		FMath::Clamp(SanitizedValue.X, -1.0, 1.0),
		FMath::Clamp(SanitizedValue.Y, -1.0, 1.0),
		FMath::Clamp(SanitizedValue.Z, -1.0, 1.0));

	if (!ComponentClampedValue.Equals(SanitizedValue))
	{
		bInOutWasSanitized = true;
	}

	const FVector MagnitudeClampedValue = ClampVectorMagnitude(ComponentClampedValue, 1.0f);
	if (!MagnitudeClampedValue.Equals(ComponentClampedValue))
	{
		bInOutWasSanitized = true;
	}

	return MagnitudeClampedValue;
}

FVector FEdenFlightMovementModel::ClampVectorMagnitude(const FVector& Value, float MaxMagnitude)
{
	if (!IsFiniteVector(Value) || MaxMagnitude <= 0.0f)
	{
		return FVector::ZeroVector;
	}

	return Value.GetClampedToMaxSize(MaxMagnitude);
}

FVector FEdenFlightMovementModel::IntegrateAxisVector(
	const FVector& CurrentValue,
	const FVector& NormalizedInput,
	float MaxSpeed,
	float Acceleration,
	float Deceleration,
	float InputReleaseTolerance,
	bool bStabilizationEnabled,
	float DeltaTimeSeconds)
{
	if (MaxSpeed <= 0.0f || !IsFiniteVector(CurrentValue) || !IsFiniteVector(NormalizedInput))
	{
		return FVector::ZeroVector;
	}

	FVector IntegratedValue = CurrentValue;

	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const double InputValue = NormalizedInput[AxisIndex];
		const bool bHasInput = FMath::Abs(InputValue) > InputReleaseTolerance;

		if (bHasInput)
		{
			const double TargetValue = InputValue * MaxSpeed;
			IntegratedValue[AxisIndex] = MoveScalarToward(
				IntegratedValue[AxisIndex],
				TargetValue,
				static_cast<double>(Acceleration) * DeltaTimeSeconds);
		}
		else if (bStabilizationEnabled)
		{
			IntegratedValue[AxisIndex] = MoveScalarToward(
				IntegratedValue[AxisIndex],
				0.0,
				static_cast<double>(Deceleration) * DeltaTimeSeconds);
		}
	}

	return ClampVectorMagnitude(IntegratedValue, MaxSpeed);
}

double FEdenFlightMovementModel::MoveScalarToward(double Current, double Target, double MaxDelta)
{
	if (MaxDelta <= 0.0)
	{
		return Current;
	}

	if (Current < Target)
	{
		return FMath::Min(Current + MaxDelta, Target);
	}

	if (Current > Target)
	{
		return FMath::Max(Current - MaxDelta, Target);
	}

	return Current;
}

bool FEdenFlightMovementModel::IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}
