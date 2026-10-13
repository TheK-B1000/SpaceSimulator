// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"

namespace EdenFlightUnits
{
constexpr double CentimetersPerMeter = 100.0;

inline FVector CentimetersToMeters(const FVector& Centimeters)
{
	return Centimeters / CentimetersPerMeter;
}

inline FVector MetersToCentimeters(const FVector& Meters)
{
	return Meters * CentimetersPerMeter;
}
}

/**
 * Thrust the spacecraft can produce right now, per body axis, in newtons.
 * Positive holds +X/+Y/+Z capability, Negative holds -X/-Y/-Z capability;
 * both are non-negative magnitudes. Any propulsion layout (lumped axes today,
 * individual engines later) reduces to this envelope for the controllers.
 */
struct EDENSPACESIMULATOR_API FEdenFlightThrustEnvelope
{
	FVector PositiveNewtons = FVector::ZeroVector;
	FVector NegativeNewtons = FVector::ZeroVector;
};

/** Translational state in SI units, world frame. */
struct EDENSPACESIMULATOR_API FEdenFlightTranslationState
{
	FVector PositionWorldMeters = FVector::ZeroVector;
	FVector VelocityWorldMetersPerSecond = FVector::ZeroVector;
};

struct EDENSPACESIMULATOR_API FEdenFlightTranslationStepResult
{
	FEdenFlightTranslationState State;
	FVector DisplacementWorldMeters = FVector::ZeroVector;
	FVector AccelerationWorldMetersPerSecondSquared = FVector::ZeroVector;
	bool bStepWasValid = true;
};

/** Kinematic angular state (until ExecPlan 0008 Checkpoint B). */
struct EDENSPACESIMULATOR_API FEdenFlightAngularStepResult
{
	FVector AngularVelocityLocalDegreesPerSecond = FVector::ZeroVector;
	bool bDeltaTimeWasValid = true;
};

/** Everything one fixed flight step needs. Built by the movement component or a test harness. */
struct EDENSPACESIMULATOR_API FEdenFlightStepInput
{
	FEdenFlightInputCommand Command;
	FEdenFlightTranslationState Translation;
	FVector AngularVelocityLocalDegreesPerSecond = FVector::ZeroVector;
	FQuat BodyRotation = FQuat::Identity;
	double MassKilograms = 0.0;

	/** Product of every thrust limit, in [0,1]. */
	double ThrustAuthority = 1.0;

	bool bAssistAvailable = true;
};

struct EDENSPACESIMULATOR_API FEdenFlightStepOutput
{
	FEdenFlightTranslationStepResult Translation;
	FVector AngularVelocityLocalDegreesPerSecond = FVector::ZeroVector;

	/** Orientation after this step's rotation; translation used the input orientation. */
	FQuat NextBodyRotation = FQuat::Identity;

	FVector AppliedBodyForceNewtons = FVector::ZeroVector;
	FEdenFlightThrustEnvelope Envelope;
	float PropulsionDemandNormalized = 0.0f;
	bool bAssistActive = false;
	bool bInputWasSanitized = false;
	bool bStepWasValid = true;
};

/**
 * Deterministic, map-free flight-domain math used by UEdenFlightMovementComponent.
 *
 * Layers, each replaceable without touching the others:
 *   intent (FEdenFlightInputCommand)
 *     -> flight controller (ResolveDirectThrust / ResolveAssistThrust) -> body-frame force request
 *     -> thrust envelope (MakeThrustEnvelope, ClampToEnvelope)         -> physically available force
 *     -> dynamics (IntegrateTranslation)                               -> F = m a, exact for constant force
 */
struct EDENSPACESIMULATOR_API FEdenFlightMovementModel
{
	static FEdenFlightInputCommand SanitizeCommand(
		const FEdenFlightInputCommand& Command,
		bool* bOutInputWasSanitized = nullptr);

	static bool IsValidDeltaTime(double DeltaTimeSeconds);

	static bool ValidatePropulsionSettings(
		const FEdenFlightPropulsionSettings& Settings,
		double FlightStepSeconds,
		TArray<FString>* OutErrors = nullptr);

	static bool ValidateStepSettings(const FEdenFlightStepSettings& Settings, TArray<FString>* OutErrors = nullptr);

	/** Thrust envelope for the settings scaled by Authority in [0,1]. */
	static FEdenFlightThrustEnvelope MakeThrustEnvelope(const FEdenFlightPropulsionSettings& Settings, double Authority);

	static FVector ClampToEnvelope(const FVector& BodyForceNewtons, const FEdenFlightThrustEnvelope& Envelope);

	/** Flight Assist off: stick deflection maps straight to a fraction of available thrust. */
	static FVector ResolveDirectThrust(const FVector& NormalizedTranslationInput, const FEdenFlightThrustEnvelope& Envelope);

	/**
	 * Flight Assist on: per body axis, track input * AssistMaxSpeed (0 on released axes)
	 * with a proportional force m * (v_target - v) / tau, saturated by the envelope.
	 */
	static FVector ResolveAssistThrust(
		const FVector& NormalizedTranslationInput,
		const FVector& VelocityBodyMetersPerSecond,
		double MassKilograms,
		const FEdenFlightPropulsionSettings& Settings,
		const FEdenFlightThrustEnvelope& Envelope,
		double InputReleaseTolerance);

	/**
	 * Newtonian translation for one step with constant body force:
	 *   a = R(q) F / m,  x' = x + v dt + a dt^2 / 2,  v' = v + a dt.
	 * Exact for constant force and orientation over the step. Zero force conserves momentum exactly.
	 */
	static FEdenFlightTranslationStepResult IntegrateTranslation(
		const FEdenFlightTranslationState& State,
		const FVector& BodyForceNewtons,
		double MassKilograms,
		const FQuat& BodyRotation,
		double DeltaTimeSeconds);

	/** Fraction of reference (forward) thrust in use: sum of |F_i| / ForwardThrust, clamped to [0,1]. */
	static float CalculatePropulsionDemandNormalized(
		const FVector& AppliedBodyForceNewtons,
		const FEdenFlightPropulsionSettings& Settings);

	/**
	 * One complete fixed flight step without collision: select the controller from the
	 * assist state, saturate by the envelope, integrate translation, then rotation.
	 * The movement component and the deterministic test harness both call this.
	 */
	static FEdenFlightStepOutput StepFlight(
		const FEdenFlightStepInput& Input,
		const FEdenFlightPropulsionSettings& PropulsionSettings,
		const FEdenFlightMovementSettings& RotationSettings,
		double DeltaTimeSeconds);

	static FEdenFlightAngularStepResult IntegrateAngularVelocity(
		const FVector& CurrentAngularVelocityLocalDegreesPerSecond,
		const FEdenFlightInputCommand& Command,
		const FEdenFlightMovementSettings& Settings,
		float DeltaTimeSeconds);

	/**
	 * Fixed-step accumulator in double precision. Returns steps to run this update.
	 * Steps beyond MaxSteps are dropped and reported through OutDroppedSteps; the
	 * caller must surface them.
	 */
	static int32 CalculateFixedSteps(
		double FrameDeltaSeconds,
		double FixedStepSeconds,
		int32 MaxSteps,
		double& InOutAccumulatorSeconds,
		int32& OutDroppedSteps);

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
