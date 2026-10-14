// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightPresentationTypes.h"

namespace EdenFlightPresentationModel
{
double SafeNonNegative(double Value)
{
	return FMath::IsFinite(Value) ? FMath::Max(0.0, Value) : 0.0;
}
}

FVector2D FEdenFlightPresentationModel::AccumulateMouseStick(
	const FVector2D& StickPitchYaw,
	const FVector2D& StickDeltaCounts,
	const FEdenFlightMouseSettings& Settings)
{
	if (!FMath::IsFinite(StickDeltaCounts.X) || !FMath::IsFinite(StickDeltaCounts.Y))
	{
		return StickPitchYaw;
	}

	const double Sensitivity = EdenFlightPresentationModel::SafeNonNegative(Settings.Sensitivity);
	const double PitchSign = Settings.bInvertPitch ? -1.0 : 1.0;
	return FVector2D(
		FMath::Clamp(StickPitchYaw.X + StickDeltaCounts.X * Sensitivity * PitchSign, -1.0, 1.0),
		FMath::Clamp(StickPitchYaw.Y + StickDeltaCounts.Y * Sensitivity, -1.0, 1.0));
}

FVector2D FEdenFlightPresentationModel::CenterMouseStick(
	const FVector2D& StickPitchYaw,
	const FEdenFlightMouseSettings& Settings,
	double DeltaSeconds)
{
	const double Rate = EdenFlightPresentationModel::SafeNonNegative(Settings.AutoCenterRatePerSecond);
	if (Rate <= 0.0 || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		return StickPitchYaw;
	}

	return StickPitchYaw * FMath::Exp(-Rate * DeltaSeconds);
}

FVector2D FEdenFlightPresentationModel::ApplyStickDeadZone(const FVector2D& StickPitchYaw, const FEdenFlightMouseSettings& Settings)
{
	const double DeadZone = FMath::Clamp(static_cast<double>(Settings.DeadZone), 0.0, 0.5);
	auto Shape = [DeadZone](double Value)
	{
		const double Magnitude = FMath::Abs(Value);
		if (Magnitude <= DeadZone)
		{
			return 0.0;
		}
		return FMath::Sign(Value) * FMath::Min(1.0, (Magnitude - DeadZone) / (1.0 - DeadZone));
	};
	return FVector2D(Shape(StickPitchYaw.X), Shape(StickPitchYaw.Y));
}

double FEdenFlightPresentationModel::ComputeLimitingHalfFieldOfViewRadians(const FEdenFlightCameraSettings& Settings)
{
	const double HalfHorizontal = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(Settings.FieldOfViewDegrees), 30.0, 120.0) * 0.5);
	const double Aspect = FMath::Clamp(static_cast<double>(Settings.ViewAspectRatio), 0.5, 4.0);
	const double HalfVertical = FMath::Atan(FMath::Tan(HalfHorizontal) / Aspect);
	return FMath::Min(HalfHorizontal, HalfVertical);
}

double FEdenFlightPresentationModel::ComputeHullAngularExtentRadians(
	double HullRadiusCentimeters,
	double ArmLengthCentimeters,
	const FEdenFlightCameraSettings& Settings)
{
	const double Radius = FMath::Max(EdenFlightPresentationModel::SafeNonNegative(HullRadiusCentimeters), 100.0);
	const double Arm = EdenFlightPresentationModel::SafeNonNegative(ArmLengthCentimeters);
	const double Pitch = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(Settings.PitchDownDegrees), -45.0, 45.0));
	const double Height = Radius * EdenFlightPresentationModel::SafeNonNegative(Settings.HeightFraction);

	// Camera relative to the hull centre, in the boom's vertical plane: behind by Arm*cos(p), above by H + Arm*sin(p).
	const double Behind = Arm * FMath::Cos(Pitch);
	const double Above = Height + Arm * FMath::Sin(Pitch);
	const double Distance = FMath::Sqrt(Behind * Behind + Above * Above);
	if (Distance <= Radius)
	{
		return UE_DOUBLE_PI;
	}

	// The view axis points down by p; the hull centre is atan2(Above, Behind) below horizontal.
	const double CentreOffset = FMath::Abs(FMath::Atan2(Above, Behind) - Pitch);
	return CentreOffset + FMath::Asin(Radius / Distance);
}

double FEdenFlightPresentationModel::ComputeChaseArmLengthCentimeters(double HullRadiusCentimeters, const FEdenFlightCameraSettings& Settings)
{
	const double Radius = FMath::Max(EdenFlightPresentationModel::SafeNonNegative(HullRadiusCentimeters), 100.0);
	const double Limit = ComputeLimitingHalfFieldOfViewRadians(Settings);

	// The angular extent falls monotonically as the boom lengthens; bisect for the exact fit.
	double Near = Radius;
	double Far = Radius * 1000.0;
	if (ComputeHullAngularExtentRadians(Radius, Far, Settings) > Limit)
	{
		return Far * FMath::Max(1.0, static_cast<double>(Settings.FramingMargin));
	}
	for (int32 Iteration = 0; Iteration < 64; ++Iteration)
	{
		const double Mid = 0.5 * (Near + Far);
		if (ComputeHullAngularExtentRadians(Radius, Mid, Settings) > Limit)
		{
			Near = Mid;
		}
		else
		{
			Far = Mid;
		}
	}
	return Far * FMath::Max(1.0, static_cast<double>(Settings.FramingMargin));
}

float FEdenFlightPresentationModel::CalculateMainEngineVisualLevel(
	const FEdenFlightDynamicsSnapshot& Dynamics,
	const FEdenFlightPropulsionSettings& Propulsion)
{
	const double Forward = Dynamics.AppliedBodyForceNewtons.X;
	const double Rating = Propulsion.ForwardThrustNewtons;
	if (!FMath::IsFinite(Forward) || Forward <= 0.0 || !FMath::IsFinite(Rating) || Rating <= 0.0)
	{
		return 0.0f;
	}

	return static_cast<float>(FMath::Clamp(Forward / Rating, 0.0, 1.0));
}
