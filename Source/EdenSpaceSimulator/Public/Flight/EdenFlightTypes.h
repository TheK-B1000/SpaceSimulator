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
 * Kinematic flight tuning. Units are explicit because Unreal movement uses
 * centimeters and degrees for these values.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightMovementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxLinearSpeedCmPerSecond = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float LinearAccelerationCmPerSecondSquared = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float LinearDecelerationCmPerSecondSquared = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0", Units = "deg/s"))
	float MaxAngularSpeedDegreesPerSecond = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0"))
	float AngularAccelerationDegreesPerSecondSquared = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0"))
	float AngularDecelerationDegreesPerSecondSquared = 135.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight", meta = (ClampMin = "0.0"))
	float InputReleaseTolerance = 0.001f;
};
