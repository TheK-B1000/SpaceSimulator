// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"

struct EDENSPACESIMULATOR_API FEdenFlightVelocityState
{
	FVector LinearVelocityWorldCmPerSecond = FVector::ZeroVector;
	FVector AngularVelocityLocalDegreesPerSecond = FVector::ZeroVector;

	void Reset()
	{
		LinearVelocityWorldCmPerSecond = FVector::ZeroVector;
		AngularVelocityLocalDegreesPerSecond = FVector::ZeroVector;
	}
};

struct EDENSPACESIMULATOR_API FEdenFlightIntegrationResult
{
	FEdenFlightInputCommand SanitizedCommand;
	FEdenFlightVelocityState VelocityState;
	bool bInputWasSanitized = false;
	bool bDeltaTimeWasValid = true;
};

/**
 * Deterministic, map-free flight-domain math used by UEdenFlightMovementComponent.
 */
struct EDENSPACESIMULATOR_API FEdenFlightMovementModel
{
	static FEdenFlightInputCommand SanitizeCommand(
		const FEdenFlightInputCommand& Command,
		bool* bOutInputWasSanitized = nullptr);

	static bool IsValidDeltaTime(float DeltaTimeSeconds);

	static FEdenFlightIntegrationResult IntegrateVelocity(
		const FEdenFlightVelocityState& CurrentVelocityState,
		const FEdenFlightInputCommand& Command,
		const FEdenFlightMovementSettings& Settings,
		const FQuat& BodyRotation,
		float DeltaTimeSeconds);

	static FVector RemoveInwardVelocity(const FVector& Velocity, const FVector& ImpactNormal);

private:
	static FVector SanitizeNormalizedVector(const FVector& Value, bool& bInOutWasSanitized);
	static FVector ClampVectorMagnitude(const FVector& Value, float MaxMagnitude);
	static FVector IntegrateAxisVector(
		const FVector& CurrentValue,
		const FVector& NormalizedInput,
		float MaxSpeed,
		float Acceleration,
		float Deceleration,
		float InputReleaseTolerance,
		bool bStabilizationEnabled,
		float DeltaTimeSeconds);
	static double MoveScalarToward(double Current, double Target, double MaxDelta);
	static bool IsFiniteVector(const FVector& Value);
};
