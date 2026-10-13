// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"
#include "Systems/EdenResourceTypes.h"

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
	double DryMassKilograms = 0.0;

	/** True when a propellant supply is simulated. False: propellant is unconstrained and massless. */
	bool bPropellantMetered = false;

	/** Stored propellant at the start of the step (metered only). Read-only here; the supply owns it. */
	double PropellantMassKilograms = 0.0;

	/** Operator authority in [0,1]. Shapes the controller envelope (what may be requested). */
	double ThrustAuthority = 1.0;

	/** Power fraction in [0,1]. Limits delivery after the controller has asked. */
	double PowerThrustFraction = 1.0;

	bool bAssistAvailable = true;
};

/**
 * First half of a flight step: what the controllers request, what power lets them deliver,
 * and how much propellant that delivery needs. No state changes.
 */
struct EDENSPACESIMULATOR_API FEdenFlightStepPlan
{
	FEdenFlightInputCommand Command;
	FEdenFlightThrustEnvelope Envelope;
	FEdenFlightTorqueEnvelope TorqueEnvelope;
	FVector RequestedBodyForceNewtons = FVector::ZeroVector;
	FVector RequestedBodyTorqueNewtonMeters = FVector::ZeroVector;
	double PowerThrustFraction = 1.0;
	FVector PoweredBodyForceNewtons = FVector::ZeroVector;
	FVector PoweredBodyTorqueNewtonMeters = FVector::ZeroVector;
	double RequestedThrusterOutputNewtons = 0.0;
	double PoweredThrusterOutputNewtons = 0.0;

	/** Propellant the powered thrust needs over the whole step: output * dt / (Isp g0). */
	double RequiredPropellantKilograms = 0.0;

	double MassAtStartKilograms = 0.0;
	bool bAssistActive = false;
	bool bInputWasSanitized = false;
	bool bPlanWasValid = true;
};

struct EDENSPACESIMULATOR_API FEdenFlightStepOutput
{
	FEdenFlightTranslationStepResult Translation;
	FEdenFlightRotationStepResult Rotation;
	FVector RequestedBodyForceNewtons = FVector::ZeroVector;
	FVector RequestedBodyTorqueNewtonMeters = FVector::ZeroVector;

	/** Delivered, averaged over the step. */
	FVector AppliedBodyForceNewtons = FVector::ZeroVector;
	FVector AppliedBodyTorqueNewtonMeters = FVector::ZeroVector;

	FEdenFlightThrustEnvelope Envelope;
	FEdenFlightTorqueEnvelope TorqueEnvelope;
	double RequestedThrusterOutputNewtons = 0.0;
	double DeliveredThrusterOutputNewtons = 0.0;
	double PowerThrustFraction = 1.0;

	/** Part of the step the thrusters burned before propellant ran out, in [0,1]. */
	double PropellantBurnFraction = 1.0;
	double PropellantRequiredKilograms = 0.0;
	double PropellantConsumedKilograms = 0.0;
	double MassAtEndKilograms = 0.0;
	float PropulsionDemandNormalized = 0.0f;
	bool bPowerLimited = false;
	bool bPropellantLimited = false;
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

	/** Standard gravity: the unit conversion in mdot = F / (Isp g0). Not a force on the spacecraft. */
	static constexpr double StandardGravityMetersPerSecondSquared = 9.80665;

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

	/**
	 * Total thruster output in newtons: sum |F_i| (translation) + sum |tau_i| / LeverArm_i (attitude).
	 * Every controller, pilot or autopilot, pays for the thrust it uses through this one measure.
	 */
	static double CalculateThrusterOutputNewtons(
		const FVector& AppliedBodyForceNewtons,
		const FVector& AppliedBodyTorqueNewtonMeters,
		const FEdenFlightAttitudeSettings& AttitudeSettings);

	/** Fraction of reference (forward) thrust in use: ThrusterOutput / ForwardThrust, clamped to [0,1]. */
	static float CalculatePropulsionDemandNormalized(double ThrusterOutputNewtons, const FEdenFlightPropulsionSettings& Settings);

	/** mdot = ThrusterOutput / (Isp g0), kg/s. */
	static double CalculatePropellantMassFlowKilogramsPerSecond(double ThrusterOutputNewtons, const FEdenFlightPropulsionSettings& Settings);

	/** 1 for Normal/Warning, CriticalPowerThrustFraction for Critical, DepletedPowerThrustFraction for Depleted. */
	static double CalculatePowerThrustFraction(EEdenPowerState PowerState, const FEdenFlightPropulsionSettings& Settings);

	/**
	 * Inertia scales with total mass (propellant assumed distributed like the hull): I(m) = I_dry * m / m_dry.
	 * Exactly 1 when no propellant is aboard, so unmetered flight matches Checkpoint B bit for bit.
	 */
	static double CalculateInertiaScale(double MassKilograms, double DryMassKilograms);

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
	 * Step part 1 (pure): controllers -> requested force/torque -> power-limited delivery ->
	 * propellant required. Mass is dry + stored propellant.
	 */
	static FEdenFlightStepPlan PlanFlightStep(
		const FEdenFlightStepInput& Input,
		const FEdenFlightPropulsionSettings& PropulsionSettings,
		const FEdenFlightAttitudeSettings& AttitudeSettings,
		double DeltaTimeSeconds);

	/**
	 * Step part 2 (pure), after the single propellant transaction granted GrantedPropellantKilograms:
	 * burn at powered thrust for f dt (f = granted / required) at midpoint mass, then coast for
	 * (1 - f) dt with zero force and torque. No negative propellant, no free acceleration.
	 */
	static FEdenFlightStepOutput ExecuteFlightStep(
		const FEdenFlightStepInput& Input,
		const FEdenFlightStepPlan& Plan,
		double GrantedPropellantKilograms,
		const FEdenFlightPropulsionSettings& PropulsionSettings,
		const FEdenFlightAttitudeSettings& AttitudeSettings,
		double DeltaTimeSeconds);

	/**
	 * One complete fixed flight step without collision: Plan, grant min(required, stored) when
	 * metered (the whole requirement when not), Execute. The movement component performs the same
	 * sequence with the real supply DrawPropellantKilograms between Plan and Execute; the
	 * deterministic test harness calls this.
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
