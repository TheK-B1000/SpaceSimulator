// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdenFlightTypes.generated.h"

/**
 * Normalized player flight command.
 *
 * Translation input is local space:
 * X = forward/reverse, Y = right/left, Z = up/down.
 *
 * Rotation input is local body axis intent:
 * X = pitch, Y = yaw, Z = roll.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightInputCommand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight")
	FVector TranslationInput = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight")
	FVector RotationInput = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight")
	bool bStabilizationEnabled = true;
};

/**
 * Mutable input intent owned by the player controller.
 *
 * The controller stores this value between Enhanced Input events and sends a
 * sanitized command snapshot to the pawn each tick.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightInputIntent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FEdenFlightInputCommand CurrentCommand;

	void Reset()
	{
		CurrentCommand.TranslationInput = FVector::ZeroVector;
		CurrentCommand.RotationInput = FVector::ZeroVector;
		CurrentCommand.bStabilizationEnabled = true;
	}
};

/**
 * Per-spacecraft rigid-body rotation parameters, in SI units.
 *
 * Body axes follow the actor: X forward (roll axis), Y right (pitch axis),
 * Z up (yaw axis). The inertia tensor is diagonal in these principal axes.
 * Torque ratings are symmetric per axis and are design values.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightAttitudeSettings
{
	GENERATED_BODY()

	/**
	 * Principal moments of inertia about body X, Y, Z in kg*m^2.
	 * Default: solid box 20 m (X) x 10 m (Y) x 5 m (Z) of 10 000 kg, I = m/12 (b^2 + c^2).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Attitude")
	FVector PrincipalMomentsOfInertiaKilogramMetersSquared = FVector(104166.67, 354166.67, 416666.67);

	/** Control torque about body X in N*m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Attitude", meta = (ClampMin = "0.0"))
	float RollTorqueNewtonMeters = 80000.0f;

	/** Control torque about body Y in N*m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Attitude", meta = (ClampMin = "0.0"))
	float PitchTorqueNewtonMeters = 180000.0f;

	/** Control torque about body Z in N*m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Attitude", meta = (ClampMin = "0.0"))
	float YawTorqueNewtonMeters = 200000.0f;

	/**
	 * Effective RCS lever arm for torque about body X, Y, Z in metres. Attitude torque costs
	 * |tau_i| / LeverArm_i of thruster output, billed as propulsion demand. Default: half-width
	 * (5 m) for roll, half-length (10 m) for pitch and yaw of the 20 x 10 x 5 m hull.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Attitude")
	FVector ThrusterLeverArmsMeters = FVector(5.0, 10.0, 10.0);

	/** Flight Assist: full stick commands this body rate. Reached only through torque; not a cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Assist", meta = (ClampMin = "0.0", Units = "deg/s"))
	float AssistMaxAngularRateDegreesPerSecond = 60.0f;

	/** Flight Assist rate-hold time constant. Must be at least four flight steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Assist", meta = (ClampMin = "0.001", Units = "s"))
	float AssistAngularResponseTimeSeconds = 0.2f;

	/** Orientation-hold controller time constant (autopilot / future EDEN seam). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Assist", meta = (ClampMin = "0.001", Units = "s"))
	float OrientationHoldResponseTimeSeconds = 1.0f;
};

/**
 * Per-spacecraft Newtonian translation parameters, in SI units.
 *
 * Thrust ratings are lumped per body axis and direction (X forward, Y right,
 * Z up). They are design values, not measurements of a real vehicle.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightPropulsionSettings
{
	GENERATED_BODY()

	/** Mass without propellant. Propellant mass joins in ExecPlan 0008 Checkpoint D. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Propulsion", meta = (ClampMin = "1.0", Units = "kg"))
	float DryMassKilograms = 10000.0f;

	/** +X body thrust. Also the reference thrust for PropulsionDemandNormalized. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Propulsion", meta = (ClampMin = "0.0", Units = "N"))
	float ForwardThrustNewtons = 100000.0f;

	/** -X body thrust. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Propulsion", meta = (ClampMin = "0.0", Units = "N"))
	float ReverseThrustNewtons = 50000.0f;

	/** +Y and -Y body thrust. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Propulsion", meta = (ClampMin = "0.0", Units = "N"))
	float LateralThrustNewtons = 20000.0f;

	/** +Z and -Z body thrust. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Propulsion", meta = (ClampMin = "0.0", Units = "N"))
	float VerticalThrustNewtons = 20000.0f;

	/** Flight Assist: full stick commands this body-axis velocity. Not a speed cap; assist reaches it only through thrust. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Assist", meta = (ClampMin = "0.0", Units = "m/s"))
	float AssistMaxSpeedMetersPerSecond = 100.0f;

	/** Flight Assist proportional time constant. Must be at least four flight steps for a monotone, stable response. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Assist", meta = (ClampMin = "0.001", Units = "s"))
	float AssistResponseTimeSeconds = 0.2f;
};

/** Fixed-step cadence for flight dynamics, independent of the 10 Hz resource/mission clock. */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightStepSettings
{
	GENERATED_BODY()

	/** Stored as a rate so the step is exact in double precision (a float 1/120 s would drift step counts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Simulation", meta = (ClampMin = "1.0", Units = "Hz"))
	float FixedStepHz = 120.0f;

	double GetFixedStepSeconds() const
	{
		return FMath::IsFinite(FixedStepHz) && FixedStepHz > 0.0f ? 1.0 / static_cast<double>(FixedStepHz) : 0.0;
	}

	/** Steps allowed per update before excess time is dropped. Dropped time is counted and logged, never silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight|Simulation", meta = (ClampMin = "1"))
	int32 MaxStepsPerUpdate = 60;
};

/**
 * Read-only flight dynamics state for HUD, debugging, telemetry, and future
 * AI pilots. SI units, world frame unless noted. Not serialized on the EDEN wire.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightDynamicsSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (Units = "m/s"))
	FVector VelocityWorldMetersPerSecond = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (Units = "m/s"))
	float SpeedMetersPerSecond = 0.0f;

	/** Thrust acceleration on the last flight step (excludes collision response). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AccelerationWorldMetersPerSecondSquared = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AppliedBodyForceNewtons = FVector::ZeroVector;

	/**
	 * Total thruster output on the last step: sum |F_i| for translation plus sum |tau_i| / LeverArm_i
	 * for attitude, in newtons. Unclamped, unlike PropulsionDemandNormalized.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	float ThrusterOutputNewtons = 0.0f;

	/** Thrust available right now along +X/+Y/+Z, after every authority limit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AvailablePositiveThrustNewtons = FVector::ZeroVector;

	/** Thrust available right now along -X/-Y/-Z (magnitudes), after every authority limit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AvailableNegativeThrustNewtons = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (Units = "kg"))
	float MassKilograms = 0.0f;

	/** Product of every thrust limit (operator today; fuel/power from Checkpoint D). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	float EffectiveThrustAuthority = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	bool bFlightAssistRequested = true;

	/** Requested and available (load shed can remove assist). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	bool bFlightAssistActive = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FQuat OrientationWorld = FQuat::Identity;

	/** Body axes X (roll), Y (pitch), Z (yaw), in rad/s. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AngularVelocityBodyRadiansPerSecond = FVector::ZeroVector;

	/** Mean over the last flight step, body axes, rad/s^2. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AngularAccelerationBodyRadiansPerSecondSquared = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AppliedBodyTorqueNewtonMeters = FVector::ZeroVector;

	/** Torque available right now about each body axis (symmetric magnitude), after every authority limit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AvailableTorqueNewtonMeters = FVector::ZeroVector;

	/** World-frame angular momentum in kg*m^2/s; constant when no torque acts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	FVector AngularMomentumWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	float RotationalKineticEnergyJoules = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (Units = "s"))
	double FlightSimulationTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	int64 FlightStepCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	int64 DroppedFlightStepCount = 0;
};
