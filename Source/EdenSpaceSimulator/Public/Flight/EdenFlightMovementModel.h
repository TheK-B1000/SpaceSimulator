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

/** Control torque available right now about body X, Y, Z (symmetric magnitudes), in N*m. */
struct EDENSPACESIMULATOR_API FEdenFlightTorqueEnvelope
{
	FVector MaxNewtonMeters = FVector::ZeroVector;
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

/** Rotational state: world orientation and body-frame angular velocity (rad/s). */
struct EDENSPACESIMULATOR_API FEdenFlightRotationState
{
	FQuat Orientation = FQuat::Identity;
	FVector AngularVelocityBodyRadiansPerSecond = FVector::ZeroVector;
};

struct EDENSPACESIMULATOR_API FEdenFlightRotationStepResult
{
	FEdenFlightRotationState State;

	/** Mean body angular acceleration over the step: (w' - w) / dt. */
	FVector AngularAccelerationBodyRadiansPerSecondSquared = FVector::ZeroVector;
	bool bStepWasValid = true;
};

/**
 * What a pilot, autopilot, or (in future) EDEN asks of the attitude actuators.
 * Every mode resolves to a body torque inside the same torque envelope, so no
 * caller can rotate the ship in a way the hardware could not.
 */
enum class EEdenFlightAttitudeRequestMode : uint8
{
	/** Fraction of available torque per body axis, in [-1, 1]. Flight Assist off. */
	Torque,

	/** Hold a body angular velocity (rad/s). Flight Assist on. */
	BodyRate,

	/** Rotate to and hold a world orientation. Autopilot seam; not exposed to pilots or EDEN yet. */
	Orientation
};

struct EDENSPACESIMULATOR_API FEdenFlightAttitudeRequest
{
	EEdenFlightAttitudeRequestMode Mode = EEdenFlightAttitudeRequestMode::Torque;
	FVector NormalizedBodyTorque = FVector::ZeroVector;
	FVector TargetBodyRateRadiansPerSecond = FVector::ZeroVector;
	FQuat TargetOrientationWorld = FQuat::Identity;

	static FEdenFlightAttitudeRequest MakeTorque(const FVector& NormalizedBodyTorque);
	static FEdenFlightAttitudeRequest MakeBodyRate(const FVector& TargetBodyRateRadiansPerSecond);
	static FEdenFlightAttitudeRequest MakeOrientation(const FQuat& TargetOrientationWorld);
};

/** Everything one fixed flight step needs. Built by the movement component or a test harness. */
struct EDENSPACESIMULATOR_API FEdenFlightStepInput
{
	FEdenFlightInputCommand Command;
	FEdenFlightTranslationState Translation;
	FEdenFlightRotationState Rotation;
	double MassKilograms = 0.0;

	/** Product of every thrust limit, in [0,1]. Scales translation thrust and attitude torque alike. */
	double ThrustAuthority = 1.0;

	bool bAssistAvailable = true;
};

struct EDENSPACESIMULATOR_API FEdenFlightStepOutput
{
	FEdenFlightTranslationStepResult Translation;
	FEdenFlightRotationStepResult Rotation;
	FVector AppliedBodyForceNewtons = FVector::ZeroVector;
	FVector AppliedBodyTorqueNewtonMeters = FVector::ZeroVector;
	FEdenFlightThrustEnvelope Envelope;
	FEdenFlightTorqueEnvelope TorqueEnvelope;
	float PropulsionDemandNormalized = 0.0f;
	bool bAssistActive = false;
	bool bInputWasSanitized = false;
	bool bStepWasValid = true;
};

/**
 * Deterministic, map-free flight-domain math used by UEdenFlightMovementComponent.
 *
 * Translation and rotation share one layering, each layer replaceable on its own:
 *   intent (FEdenFlightInputCommand, or an autopilot request)
 *     -> controller: thrust (ResolveDirectThrust / ResolveAssistThrust)
 *                    torque (MakePilotAttitudeRequest -> ResolveAttitudeTorque)
 *     -> envelope:   FEdenFlightThrustEnvelope / FEdenFlightTorqueEnvelope (physically available)
 *     -> dynamics:   IntegrateTranslation (F = m a) / IntegrateRotation (Euler's rigid-body equations)
 */
struct EDENSPACESIMULATOR_API FEdenFlightMovementModel
{
	/** Stick deflection at or below this magnitude counts as released. */
	static constexpr double InputReleaseTolerance = 0.001;

	static FEdenFlightInputCommand SanitizeCommand(
		const FEdenFlightInputCommand& Command,
		bool* bOutInputWasSanitized = nullptr);

	static bool IsValidDeltaTime(double DeltaTimeSeconds);

	static bool ValidatePropulsionSettings(
		const FEdenFlightPropulsionSettings& Settings,
		double FlightStepSeconds,
		TArray<FString>* OutErrors = nullptr);

	static bool ValidateAttitudeSettings(
		const FEdenFlightAttitudeSettings& Settings,
		double FlightStepSeconds,
		TArray<FString>* OutErrors = nullptr);

	static bool ValidateStepSettings(const FEdenFlightStepSettings& Settings, TArray<FString>* OutErrors = nullptr);

	// --- Translation ---------------------------------------------------------

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
		const FEdenFlightThrustEnvelope& Envelope);

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

	// --- Rotation ------------------------------------------------------------

	/**
	 * Maps pilot rotation input (X pitch, Y yaw, Z roll; Unreal FRotator sign convention)
	 * to body-axis fractions (X roll, Y pitch, Z yaw) in the quaternion convention used
	 * by IntegrateRotation. FRotator pitch and roll turn about -Y and -X; yaw about +Z.
	 */
	static FVector MapRotationInputToBodyAxes(const FVector& PitchYawRollInput);

	static FEdenFlightTorqueEnvelope MakeTorqueEnvelope(const FEdenFlightAttitudeSettings& Settings, double Authority);

	static FVector ClampToTorqueEnvelope(const FVector& BodyTorqueNewtonMeters, const FEdenFlightTorqueEnvelope& Envelope);

	/** Pilot intent -> attitude request: assist off is Torque, assist on is BodyRate (stick * AssistMaxAngularRate). */
	static FEdenFlightAttitudeRequest MakePilotAttitudeRequest(
		const FVector& PitchYawRollInput,
		bool bAssistActive,
		const FEdenFlightAttitudeSettings& Settings);

	/**
	 * Resolves any attitude request to a body torque inside the envelope.
	 *   Torque:      tau_i = u_i * tau_max_i
	 *   BodyRate:    tau = I (w_target - w) / tau_a + w x (I w)   (gyroscopic feed-forward), saturated
	 *   Orientation: rotation-vector error e -> w_target_i = sign(e_i) min(|e_i| / T_o, sqrt(alpha_i |e_i|), W_max) -> BodyRate
	 */
	static FVector ResolveAttitudeTorque(
		const FEdenFlightAttitudeRequest& Request,
		const FEdenFlightRotationState& State,
		const FEdenFlightAttitudeSettings& Settings,
		const FEdenFlightTorqueEnvelope& Envelope);

	/**
	 * Rigid-body rotation for one step with constant body torque, classical RK4 on (q, w):
	 *   q_dot = q (x) (0, w) / 2
	 *   w_dot = I^-1 (tau - w x (I w))        (Euler's equations, principal axes)
	 * The quaternion is renormalized after the step.
	 */
	static FEdenFlightRotationStepResult IntegrateRotation(
		const FEdenFlightRotationState& State,
		const FVector& BodyTorqueNewtonMeters,
		const FVector& PrincipalMomentsOfInertia,
		double DeltaTimeSeconds);

	/** L_world = R(q) (I w), kg*m^2/s. */
	static FVector CalculateAngularMomentumWorld(const FEdenFlightRotationState& State, const FVector& PrincipalMomentsOfInertia);

	/** E = w . (I w) / 2, joules. */
	static double CalculateRotationalKineticEnergy(const FEdenFlightRotationState& State, const FVector& PrincipalMomentsOfInertia);

	// --- Step ----------------------------------------------------------------

	/**
	 * One complete fixed flight step without collision: choose controllers from the
	 * assist state, saturate by the envelopes, integrate translation with the starting
	 * orientation, then integrate rotation. The movement component and the deterministic
	 * test harness both call this.
	 */
	static FEdenFlightStepOutput StepFlight(
		const FEdenFlightStepInput& Input,
		const FEdenFlightPropulsionSettings& PropulsionSettings,
		const FEdenFlightAttitudeSettings& AttitudeSettings,
		double DeltaTimeSeconds);

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

	static bool IsFiniteVector(const FVector& Value);

private:
	static FVector SanitizeNormalizedVector(const FVector& Value, bool& bInOutWasSanitized);
	static FVector ClampVectorMagnitude(const FVector& Value, float MaxMagnitude);
};
