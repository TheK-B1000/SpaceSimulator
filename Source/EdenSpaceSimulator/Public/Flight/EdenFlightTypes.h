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
 * Kinematic rotation tuning (ExecPlan 0008 Checkpoint B replaces this with
 * rigid-body rotation). Units are explicit because Unreal uses degrees.
 * Translation is Newtonian and configured by FEdenFlightPropulsionSettings.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightMovementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0", Units = "deg/s"))
	float MaxAngularSpeedDegreesPerSecond = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0"))
	float AngularAccelerationDegreesPerSecondSquared = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0"))
	float AngularDecelerationDegreesPerSecondSquared = 135.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0"))
	float InputReleaseTolerance = 0.001f;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (Units = "s"))
	double FlightSimulationTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	int64 FlightStepCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight")
	int64 DroppedFlightStepCount = 0;
};
