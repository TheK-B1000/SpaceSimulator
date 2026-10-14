// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"

#include "EdenFlightPresentationTypes.generated.h"

/**
 * Mouse "virtual stick": mouse movement accumulates into a persistent pitch/yaw deflection
 * instead of being read as per-frame pulses (which saturated to full deflection and dropped
 * to zero the frame the mouse stopped). Input shaping only; the physics is unchanged.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightMouseSettings
{
	GENERATED_BODY()

	/** Stick deflection per mouse count (Enhanced Input mouse delta). 0.02 = 50 counts for full deflection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Controls", meta = (ClampMin = "0.0001"))
	float Sensitivity = 0.02f;

	/** Deflection below this magnitude per axis is treated as centred; the rest is rescaled to [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Controls", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float DeadZone = 0.03f;

	/**
	 * Exponential return-to-centre rate (1/s). 0 = the stick stays where you leave it.
	 * 0.6/s keeps about half the deflection after 1.2 s, so a turn holds after the mouse stops.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Controls", meta = (ClampMin = "0.0"))
	float AutoCenterRatePerSecond = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Controls")
	bool bInvertPitch = false;
};

/** Third-person chase camera framing. Presentation only. */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenFlightCameraSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera")
	bool bEnableChaseCamera = true;

	/** Horizontal field of view (Unreal's convention). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "30.0", ClampMax = "120.0", Units = "deg"))
	float FieldOfViewDegrees = 75.0f;

	/** Width / height used for the vertical framing limit. 16:9 by default; wider screens only add room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "0.5", ClampMax = "4.0"))
	float ViewAspectRatio = 16.0f / 9.0f;

	/**
	 * Boom length multiplier beyond the exact fit, where the hull's bounding sphere just touches the
	 * top or bottom of the frame. The sphere is conservative for a long, flat hull seen from behind.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "1.0"))
	float FramingMargin = 1.15f;

	/** Boom pivot height above the hull centre, as a fraction of the hull radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "0.0"))
	float HeightFraction = 0.25f;

	/** Look-down angle of the boom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "-45.0", ClampMax = "45.0", Units = "deg"))
	float PitchDownDegrees = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera")
	bool bEnableCameraLag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "0.0"))
	float LocationLagSpeed = 12.0f;

	/**
	 * Location lag is capped at this fraction of the hull radius. Without a cap a lagging camera
	 * trails by speed / LocationLagSpeed, which grows without bound in Newtonian flight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "0.0"))
	float LocationLagMaxDistanceFraction = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "0.0"))
	float RotationLagSpeed = 9.0f;

	/** Extra field of view at full delivered forward thrust (sense of acceleration). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Camera", meta = (ClampMin = "0.0", Units = "deg"))
	float ThrustFieldOfViewKickDegrees = 5.0f;
};

/** Pure presentation and input-shaping math (testable, no world). */
struct EDENSPACESIMULATOR_API FEdenFlightPresentationModel
{
	/** Adds a mouse delta already in stick axes (X = pitch counts, Y = yaw counts, as IA_FlightRotate swizzles them), clamped to [-1, 1]. */
	static FVector2D AccumulateMouseStick(const FVector2D& StickPitchYaw, const FVector2D& StickDeltaCounts, const FEdenFlightMouseSettings& Settings);

	/** Exponential return to centre over DeltaSeconds (frame-rate independent). */
	static FVector2D CenterMouseStick(const FVector2D& StickPitchYaw, const FEdenFlightMouseSettings& Settings, double DeltaSeconds);

	/** Per-axis dead zone with rescale, so output still reaches exactly +/-1. */
	static FVector2D ApplyStickDeadZone(const FVector2D& StickPitchYaw, const FEdenFlightMouseSettings& Settings);

	/** The limiting (smaller) half-angle of the view: vertical for landscape aspect ratios. */
	static double ComputeLimitingHalfFieldOfViewRadians(const FEdenFlightCameraSettings& Settings);

	/**
	 * Angle from the view axis to the far edge of the hull's bounding sphere, for a boom of ArmLength
	 * (pivot HeightFraction * R above the hull centre, pitched down PitchDownDegrees). The whole sphere
	 * is in view while this is no larger than ComputeLimitingHalfFieldOfViewRadians.
	 */
	static double ComputeHullAngularExtentRadians(double HullRadiusCentimeters, double ArmLengthCentimeters, const FEdenFlightCameraSettings& Settings);

	/** Boom length that keeps the hull's bounding sphere fully in view (exact fit times FramingMargin). */
	static double ComputeChaseArmLengthCentimeters(double HullRadiusCentimeters, const FEdenFlightCameraSettings& Settings);

	/**
	 * Main-engine visual level in [0, 1]: delivered +X body force over the forward rating. Attitude,
	 * reverse, lateral and vertical thrust do not light the main engine; an empty tank or dead
	 * battery shows nothing because the force is delivered force.
	 */
	static float CalculateMainEngineVisualLevel(const FEdenFlightDynamicsSnapshot& Dynamics, const FEdenFlightPropulsionSettings& Propulsion);
};
