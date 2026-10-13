// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementModel.h"

namespace EdenFlightMovementModel
{
/** Assist must span at least this many flight steps to stay monotone (discrete lag factor 1 - dt/tau >= 0.75). */
constexpr double MinAssistResponseSteps = 4.0;

/** Absorbs representation error when a frame is an exact multiple of the fixed step (e.g. 1/60 vs 1/120). */
constexpr double FixedStepEpsilon = 1.0e-9;

/** Relative slack for the inertia triangle inequality (rounded design values such as a box tensor). */
constexpr double InertiaTriangleTolerance = 1.0e-6;

float SanitizeNonNegative(float Value)
{
	return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
}

double SanitizeAuthority(double Authority)
{
	return FMath::IsFinite(Authority) ? FMath::Clamp(Authority, 0.0, 1.0) : 0.0;
}

void ValidatePositive(const TCHAR* Name, double Value, bool& bInOutValid, TArray<FString>* OutErrors)
{
	if (!FMath::IsFinite(Value) || Value <= 0.0)
	{
		bInOutValid = false;
		if (OutErrors)
		{
			OutErrors->Add(FString::Printf(TEXT("%s must be finite and > 0. Value=%f"), Name, Value));
		}
	}
}

void ValidateNonNegative(const TCHAR* Name, double Value, bool& bInOutValid, TArray<FString>* OutErrors)
{
	if (!FMath::IsFinite(Value) || Value < 0.0)
	{
		bInOutValid = false;
		if (OutErrors)
		{
			OutErrors->Add(FString::Printf(TEXT("%s must be finite and >= 0. Value=%f"), Name, Value));
		}
	}
}

void ValidateResponseTime(const TCHAR* Name, double ResponseSeconds, double FlightStepSeconds, bool& bInOutValid, TArray<FString>* OutErrors)
{
	if (FlightStepSeconds > 0.0 && FMath::IsFinite(FlightStepSeconds) && ResponseSeconds < MinAssistResponseSteps * FlightStepSeconds)
	{
		bInOutValid = false;
		if (OutErrors)
		{
			OutErrors->Add(FString::Printf(
				TEXT("%s (%f) must be >= %.0f flight steps (%f s)."),
				Name,
				ResponseSeconds,
				MinAssistResponseSteps,
				MinAssistResponseSteps * FlightStepSeconds));
		}
	}
}

/** Hamilton product A (x) B: applies B, then A. Matches FQuat::RotateVector (v' = q v q*). */
FQuat HamiltonProduct(const FQuat& A, const FQuat& B)
{
	return FQuat(
		A.W * B.X + B.W * A.X + (A.Y * B.Z - A.Z * B.Y),
		A.W * B.Y + B.W * A.Y + (A.Z * B.X - A.X * B.Z),
		A.W * B.Z + B.W * A.Z + (A.X * B.Y - A.Y * B.X),
		A.W * B.W - (A.X * B.X + A.Y * B.Y + A.Z * B.Z));
}

FQuat AddScaled(const FQuat& Base, const FQuat& Delta, double Scale)
{
	return FQuat(Base.X + Delta.X * Scale, Base.Y + Delta.Y * Scale, Base.Z + Delta.Z * Scale, Base.W + Delta.W * Scale);
}

struct FRigidBodyRates
{
	FQuat OrientationRate = FQuat(0.0, 0.0, 0.0, 0.0);
	FVector AngularAcceleration = FVector::ZeroVector;
};

/** q_dot = q (x) (0, w) / 2;  w_dot = I^-1 (tau - w x (I w)). */
FRigidBodyRates EvaluateRigidBodyRates(const FQuat& Orientation, const FVector& AngularVelocity, const FVector& Torque, const FVector& Inertia)
{
	FRigidBodyRates Rates;
	const FVector AngularMomentumBody = Inertia * AngularVelocity;
	Rates.AngularAcceleration = (Torque - FVector::CrossProduct(AngularVelocity, AngularMomentumBody)) / Inertia;

	const FQuat Product = HamiltonProduct(Orientation, FQuat(AngularVelocity.X, AngularVelocity.Y, AngularVelocity.Z, 0.0));
	Rates.OrientationRate = FQuat(0.5 * Product.X, 0.5 * Product.Y, 0.5 * Product.Z, 0.5 * Product.W);
	return Rates;
}

bool IsFiniteQuat(const FQuat& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z) && FMath::IsFinite(Value.W)
		&& Value.SizeSquared() > UE_DOUBLE_SMALL_NUMBER;
}

bool IsFinitePositiveVector(const FVector& Inertia)
{
	return FEdenFlightMovementModel::IsFiniteVector(Inertia) && Inertia.X > 0.0 && Inertia.Y > 0.0 && Inertia.Z > 0.0;
}

/** Body-frame rotation vector (axis * angle, radians) that takes Current to Target along the shortest path. */
FVector OrientationErrorBody(const FQuat& Current, const FQuat& Target)
{
	FQuat Error = HamiltonProduct(Current.GetNormalized().Inverse(), Target.GetNormalized());
	if (Error.W < 0.0)
	{
		Error = FQuat(-Error.X, -Error.Y, -Error.Z, -Error.W);
	}

	const FVector Vector(Error.X, Error.Y, Error.Z);
	const double SinHalfAngle = Vector.Size();
	if (SinHalfAngle <= UE_DOUBLE_SMALL_NUMBER)
	{
		return 2.0 * Vector;
	}

	const double Angle = 2.0 * FMath::Atan2(SinHalfAngle, Error.W);
	return Vector * (Angle / SinHalfAngle);
}

double SanitizeUnitInput(double Value)
{
	if (!FMath::IsFinite(Value) || FMath::Abs(Value) <= FEdenFlightMovementModel::InputReleaseTolerance)
	{
		return 0.0;
	}

	return FMath::Clamp(Value, -1.0, 1.0);
}
}

FEdenFlightAttitudeRequest FEdenFlightAttitudeRequest::MakeTorque(const FVector& NormalizedBodyTorque)
{
	FEdenFlightAttitudeRequest Request;
	Request.Mode = EEdenFlightAttitudeRequestMode::Torque;
	Request.NormalizedBodyTorque = NormalizedBodyTorque;
	return Request;
}

FEdenFlightAttitudeRequest FEdenFlightAttitudeRequest::MakeBodyRate(const FVector& TargetBodyRateRadiansPerSecond)
{
	FEdenFlightAttitudeRequest Request;
	Request.Mode = EEdenFlightAttitudeRequestMode::BodyRate;
	Request.TargetBodyRateRadiansPerSecond = TargetBodyRateRadiansPerSecond;
	return Request;
}

FEdenFlightAttitudeRequest FEdenFlightAttitudeRequest::MakeOrientation(const FQuat& TargetOrientationWorld)
{
	FEdenFlightAttitudeRequest Request;
	Request.Mode = EEdenFlightAttitudeRequestMode::Orientation;
	Request.TargetOrientationWorld = TargetOrientationWorld;
	return Request;
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

bool FEdenFlightMovementModel::IsValidDeltaTime(double DeltaTimeSeconds)
{
	return FMath::IsFinite(DeltaTimeSeconds) && DeltaTimeSeconds > 0.0;
}

bool FEdenFlightMovementModel::ValidatePropulsionSettings(
	const FEdenFlightPropulsionSettings& Settings,
	double FlightStepSeconds,
	TArray<FString>* OutErrors)
{
	bool bValid = true;
	EdenFlightMovementModel::ValidatePositive(TEXT("DryMassKilograms"), Settings.DryMassKilograms, bValid, OutErrors);
	EdenFlightMovementModel::ValidateNonNegative(TEXT("ForwardThrustNewtons"), Settings.ForwardThrustNewtons, bValid, OutErrors);
	EdenFlightMovementModel::ValidateNonNegative(TEXT("ReverseThrustNewtons"), Settings.ReverseThrustNewtons, bValid, OutErrors);
	EdenFlightMovementModel::ValidateNonNegative(TEXT("LateralThrustNewtons"), Settings.LateralThrustNewtons, bValid, OutErrors);
	EdenFlightMovementModel::ValidateNonNegative(TEXT("VerticalThrustNewtons"), Settings.VerticalThrustNewtons, bValid, OutErrors);
	EdenFlightMovementModel::ValidateNonNegative(
		TEXT("AssistMaxSpeedMetersPerSecond"),
		Settings.AssistMaxSpeedMetersPerSecond,
		bValid,
		OutErrors);
	EdenFlightMovementModel::ValidatePositive(TEXT("AssistResponseTimeSeconds"), Settings.AssistResponseTimeSeconds, bValid, OutErrors);
	EdenFlightMovementModel::ValidatePositive(TEXT("SpecificImpulseSeconds"), Settings.SpecificImpulseSeconds, bValid, OutErrors);
	for (const TPair<const TCHAR*, float>& Fraction : {
			 TPair<const TCHAR*, float>(TEXT("CriticalPowerThrustFraction"), Settings.CriticalPowerThrustFraction),
			 TPair<const TCHAR*, float>(TEXT("DepletedPowerThrustFraction"), Settings.DepletedPowerThrustFraction)})
	{
		if (!FMath::IsFinite(Fraction.Value) || Fraction.Value < 0.0f || Fraction.Value > 1.0f)
		{
			bValid = false;
			if (OutErrors)
			{
				OutErrors->Add(FString::Printf(TEXT("%s must be within [0, 1]. Value=%f"), Fraction.Key, Fraction.Value));
			}
		}
	}
	if (bValid)
	{
		EdenFlightMovementModel::ValidateResponseTime(
			TEXT("AssistResponseTimeSeconds"),
			Settings.AssistResponseTimeSeconds,
			FlightStepSeconds,
			bValid,
			OutErrors);
	}

	return bValid;
}

bool FEdenFlightMovementModel::ValidateAttitudeSettings(
	const FEdenFlightAttitudeSettings& Settings,
	double FlightStepSeconds,
	TArray<FString>* OutErrors)
{
	bool bValid = true;
	const FVector& Inertia = Settings.PrincipalMomentsOfInertiaKilogramMetersSquared;
	EdenFlightMovementModel::ValidatePositive(TEXT("PrincipalMomentsOfInertia.X"), Inertia.X, bValid, OutErrors);
	EdenFlightMovementModel::ValidatePositive(TEXT("PrincipalMomentsOfInertia.Y"), Inertia.Y, bValid, OutErrors);
	EdenFlightMovementModel::ValidatePositive(TEXT("PrincipalMomentsOfInertia.Z"), Inertia.Z, bValid, OutErrors);

	// A real rigid body satisfies the triangle inequality on its principal moments.
	if (bValid)
	{
		const double Slack = EdenFlightMovementModel::InertiaTriangleTolerance * (Inertia.X + Inertia.Y + Inertia.Z);
		if (Inertia.X > Inertia.Y + Inertia.Z + Slack || Inertia.Y > Inertia.X + Inertia.Z + Slack
			|| Inertia.Z > Inertia.X + Inertia.Y + Slack)
		{
			bValid = false;
			if (OutErrors)
			{
				OutErrors->Add(FString::Printf(
					TEXT("PrincipalMomentsOfInertia %s violate the triangle inequality and cannot belong to a rigid body."),
					*Inertia.ToString()));
			}
		}
	}

	EdenFlightMovementModel::ValidateNonNegative(TEXT("RollTorqueNewtonMeters"), Settings.RollTorqueNewtonMeters, bValid, OutErrors);
	EdenFlightMovementModel::ValidateNonNegative(TEXT("PitchTorqueNewtonMeters"), Settings.PitchTorqueNewtonMeters, bValid, OutErrors);
	EdenFlightMovementModel::ValidateNonNegative(TEXT("YawTorqueNewtonMeters"), Settings.YawTorqueNewtonMeters, bValid, OutErrors);
	EdenFlightMovementModel::ValidatePositive(TEXT("ThrusterLeverArmsMeters.X"), Settings.ThrusterLeverArmsMeters.X, bValid, OutErrors);
	EdenFlightMovementModel::ValidatePositive(TEXT("ThrusterLeverArmsMeters.Y"), Settings.ThrusterLeverArmsMeters.Y, bValid, OutErrors);
	EdenFlightMovementModel::ValidatePositive(TEXT("ThrusterLeverArmsMeters.Z"), Settings.ThrusterLeverArmsMeters.Z, bValid, OutErrors);
	EdenFlightMovementModel::ValidateNonNegative(
		TEXT("AssistMaxAngularRateDegreesPerSecond"),
		Settings.AssistMaxAngularRateDegreesPerSecond,
		bValid,
		OutErrors);
	EdenFlightMovementModel::ValidatePositive(
		TEXT("AssistAngularResponseTimeSeconds"),
		Settings.AssistAngularResponseTimeSeconds,
		bValid,
		OutErrors);
	EdenFlightMovementModel::ValidatePositive(
		TEXT("OrientationHoldResponseTimeSeconds"),
		Settings.OrientationHoldResponseTimeSeconds,
		bValid,
		OutErrors);
	if (bValid)
	{
		EdenFlightMovementModel::ValidateResponseTime(
			TEXT("AssistAngularResponseTimeSeconds"),
			Settings.AssistAngularResponseTimeSeconds,
			FlightStepSeconds,
			bValid,
			OutErrors);
	}

	return bValid;
}

bool FEdenFlightMovementModel::ValidateStepSettings(const FEdenFlightStepSettings& Settings, TArray<FString>* OutErrors)
{
	bool bValid = true;
	EdenFlightMovementModel::ValidatePositive(TEXT("FixedStepHz"), Settings.FixedStepHz, bValid, OutErrors);
	if (Settings.MaxStepsPerUpdate < 1)
	{
		bValid = false;
		if (OutErrors)
		{
			OutErrors->Add(FString::Printf(TEXT("MaxStepsPerUpdate must be >= 1. Value=%d"), Settings.MaxStepsPerUpdate));
		}
	}

	return bValid;
}

FEdenFlightThrustEnvelope FEdenFlightMovementModel::MakeThrustEnvelope(
	const FEdenFlightPropulsionSettings& Settings,
	double Authority)
{
	const double SafeAuthority = EdenFlightMovementModel::SanitizeAuthority(Authority);
	const double Lateral = EdenFlightMovementModel::SanitizeNonNegative(Settings.LateralThrustNewtons);
	const double Vertical = EdenFlightMovementModel::SanitizeNonNegative(Settings.VerticalThrustNewtons);

	FEdenFlightThrustEnvelope Envelope;
	Envelope.PositiveNewtons =
		FVector(EdenFlightMovementModel::SanitizeNonNegative(Settings.ForwardThrustNewtons), Lateral, Vertical) * SafeAuthority;
	Envelope.NegativeNewtons =
		FVector(EdenFlightMovementModel::SanitizeNonNegative(Settings.ReverseThrustNewtons), Lateral, Vertical) * SafeAuthority;
	return Envelope;
}

FVector FEdenFlightMovementModel::ClampToEnvelope(const FVector& BodyForceNewtons, const FEdenFlightThrustEnvelope& Envelope)
{
	FVector Clamped = FVector::ZeroVector;
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const double Requested = BodyForceNewtons[AxisIndex];
		if (!FMath::IsFinite(Requested))
		{
			continue;
		}

		Clamped[AxisIndex] = FMath::Clamp(Requested, -Envelope.NegativeNewtons[AxisIndex], Envelope.PositiveNewtons[AxisIndex]);
	}

	return Clamped;
}

FVector FEdenFlightMovementModel::ResolveDirectThrust(
	const FVector& NormalizedTranslationInput,
	const FEdenFlightThrustEnvelope& Envelope)
{
	FVector Force = FVector::ZeroVector;
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const double Input = NormalizedTranslationInput[AxisIndex];
		if (!FMath::IsFinite(Input))
		{
			continue;
		}

		Force[AxisIndex] = Input >= 0.0
			? Input * Envelope.PositiveNewtons[AxisIndex]
			: Input * Envelope.NegativeNewtons[AxisIndex];
	}

	return ClampToEnvelope(Force, Envelope);
}

FVector FEdenFlightMovementModel::ResolveAssistThrust(
	const FVector& NormalizedTranslationInput,
	const FVector& VelocityBodyMetersPerSecond,
	double MassKilograms,
	const FEdenFlightPropulsionSettings& Settings,
	const FEdenFlightThrustEnvelope& Envelope)
{
	if (!FMath::IsFinite(MassKilograms) || MassKilograms <= 0.0 || !IsFiniteVector(VelocityBodyMetersPerSecond))
	{
		return FVector::ZeroVector;
	}

	const double MaxSpeed = EdenFlightMovementModel::SanitizeNonNegative(Settings.AssistMaxSpeedMetersPerSecond);
	const double ResponseTime = FMath::IsFinite(Settings.AssistResponseTimeSeconds) && Settings.AssistResponseTimeSeconds > 0.0f
		? static_cast<double>(Settings.AssistResponseTimeSeconds)
		: static_cast<double>(FEdenFlightPropulsionSettings().AssistResponseTimeSeconds);

	FVector RequestedForce = FVector::ZeroVector;
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const double Input = FMath::IsFinite(NormalizedTranslationInput[AxisIndex]) ? NormalizedTranslationInput[AxisIndex] : 0.0;
		const double TargetVelocity = FMath::Abs(Input) > InputReleaseTolerance ? Input * MaxSpeed : 0.0;
		RequestedForce[AxisIndex] = MassKilograms * (TargetVelocity - VelocityBodyMetersPerSecond[AxisIndex]) / ResponseTime;
	}

	return ClampToEnvelope(RequestedForce, Envelope);
}

FEdenFlightTranslationStepResult FEdenFlightMovementModel::IntegrateTranslation(
	const FEdenFlightTranslationState& State,
	const FVector& BodyForceNewtons,
	double MassKilograms,
	const FQuat& BodyRotation,
	double DeltaTimeSeconds)
{
	FEdenFlightTranslationStepResult Result;
	Result.State = State;

	if (!IsValidDeltaTime(DeltaTimeSeconds)
		|| !FMath::IsFinite(MassKilograms)
		|| MassKilograms <= 0.0
		|| !IsFiniteVector(BodyForceNewtons)
		|| !IsFiniteVector(State.PositionWorldMeters)
		|| !IsFiniteVector(State.VelocityWorldMetersPerSecond)
		|| BodyRotation.ContainsNaN())
	{
		Result.bStepWasValid = false;
		return Result;
	}

	const FVector Acceleration = BodyRotation.GetNormalized().RotateVector(BodyForceNewtons) / MassKilograms;
	const FVector& Velocity = State.VelocityWorldMetersPerSecond;

	Result.AccelerationWorldMetersPerSecondSquared = Acceleration;
	Result.DisplacementWorldMeters = (Velocity * DeltaTimeSeconds) + (Acceleration * (0.5 * DeltaTimeSeconds * DeltaTimeSeconds));
	Result.State.PositionWorldMeters = State.PositionWorldMeters + Result.DisplacementWorldMeters;
	Result.State.VelocityWorldMetersPerSecond = Velocity + (Acceleration * DeltaTimeSeconds);
	return Result;
}

double FEdenFlightMovementModel::CalculateThrusterOutputNewtons(
	const FVector& AppliedBodyForceNewtons,
	const FVector& AppliedBodyTorqueNewtonMeters,
	const FEdenFlightAttitudeSettings& AttitudeSettings)
{
	double Output = 0.0;
	if (IsFiniteVector(AppliedBodyForceNewtons))
	{
		Output += FMath::Abs(AppliedBodyForceNewtons.X) + FMath::Abs(AppliedBodyForceNewtons.Y) + FMath::Abs(AppliedBodyForceNewtons.Z);
	}

	const FVector& LeverArms = AttitudeSettings.ThrusterLeverArmsMeters;
	if (IsFiniteVector(AppliedBodyTorqueNewtonMeters) && EdenFlightMovementModel::IsFinitePositiveVector(LeverArms))
	{
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			Output += FMath::Abs(AppliedBodyTorqueNewtonMeters[AxisIndex]) / LeverArms[AxisIndex];
		}
	}

	return Output;
}

float FEdenFlightMovementModel::CalculatePropulsionDemandNormalized(double ThrusterOutputNewtons, const FEdenFlightPropulsionSettings& Settings)
{
	const double TotalThrust = ThrusterOutputNewtons;
	if (!FMath::IsFinite(TotalThrust) || TotalThrust <= 0.0)
	{
		return 0.0f;
	}

	const double ReferenceThrust = EdenFlightMovementModel::SanitizeNonNegative(Settings.ForwardThrustNewtons);
	if (ReferenceThrust <= 0.0)
	{
		return 1.0f;
	}

	return static_cast<float>(FMath::Clamp(TotalThrust / ReferenceThrust, 0.0, 1.0));
}

FVector FEdenFlightMovementModel::MapRotationInputToBodyAxes(const FVector& PitchYawRollInput)
{
	// FRotator: +pitch turns about -Y (nose up), +yaw about +Z (nose right), +roll about -X (right wing down).
	return FVector(
		-EdenFlightMovementModel::SanitizeUnitInput(PitchYawRollInput.Z),
		-EdenFlightMovementModel::SanitizeUnitInput(PitchYawRollInput.X),
		EdenFlightMovementModel::SanitizeUnitInput(PitchYawRollInput.Y));
}

FEdenFlightTorqueEnvelope FEdenFlightMovementModel::MakeTorqueEnvelope(const FEdenFlightAttitudeSettings& Settings, double Authority)
{
	FEdenFlightTorqueEnvelope Envelope;
	Envelope.MaxNewtonMeters = FVector(
		EdenFlightMovementModel::SanitizeNonNegative(Settings.RollTorqueNewtonMeters),
		EdenFlightMovementModel::SanitizeNonNegative(Settings.PitchTorqueNewtonMeters),
		EdenFlightMovementModel::SanitizeNonNegative(Settings.YawTorqueNewtonMeters))
		* EdenFlightMovementModel::SanitizeAuthority(Authority);
	return Envelope;
}

FVector FEdenFlightMovementModel::ClampToTorqueEnvelope(const FVector& BodyTorqueNewtonMeters, const FEdenFlightTorqueEnvelope& Envelope)
{
	FVector Clamped = FVector::ZeroVector;
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const double Requested = BodyTorqueNewtonMeters[AxisIndex];
		if (!FMath::IsFinite(Requested))
		{
			continue;
		}

		const double Limit = Envelope.MaxNewtonMeters[AxisIndex];
		Clamped[AxisIndex] = FMath::Clamp(Requested, -Limit, Limit);
	}

	return Clamped;
}

FEdenFlightAttitudeRequest FEdenFlightMovementModel::MakePilotAttitudeRequest(
	const FVector& PitchYawRollInput,
	bool bAssistActive,
	const FEdenFlightAttitudeSettings& Settings)
{
	const FVector BodyInput = MapRotationInputToBodyAxes(PitchYawRollInput);
	if (!bAssistActive)
	{
		return FEdenFlightAttitudeRequest::MakeTorque(BodyInput);
	}

	const double MaxRate =
		FMath::DegreesToRadians(static_cast<double>(EdenFlightMovementModel::SanitizeNonNegative(Settings.AssistMaxAngularRateDegreesPerSecond)));
	return FEdenFlightAttitudeRequest::MakeBodyRate(BodyInput * MaxRate);
}

FVector FEdenFlightMovementModel::ResolveAttitudeTorque(
	const FEdenFlightAttitudeRequest& Request,
	const FEdenFlightRotationState& State,
	const FEdenFlightAttitudeSettings& Settings,
	const FEdenFlightTorqueEnvelope& Envelope)
{
	const FVector& Inertia = Settings.PrincipalMomentsOfInertiaKilogramMetersSquared;
	const FVector& AngularVelocity = State.AngularVelocityBodyRadiansPerSecond;
	if (!EdenFlightMovementModel::IsFinitePositiveVector(Inertia) || !IsFiniteVector(AngularVelocity))
	{
		return FVector::ZeroVector;
	}

	if (Request.Mode == EEdenFlightAttitudeRequestMode::Torque)
	{
		FVector Torque = FVector::ZeroVector;
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			Torque[AxisIndex] =
				EdenFlightMovementModel::SanitizeUnitInput(Request.NormalizedBodyTorque[AxisIndex]) * Envelope.MaxNewtonMeters[AxisIndex];
		}
		return ClampToTorqueEnvelope(Torque, Envelope);
	}

	FVector TargetRate = Request.TargetBodyRateRadiansPerSecond;
	if (Request.Mode == EEdenFlightAttitudeRequestMode::Orientation)
	{
		if (!EdenFlightMovementModel::IsFiniteQuat(Request.TargetOrientationWorld) || !EdenFlightMovementModel::IsFiniteQuat(State.Orientation))
		{
			return FVector::ZeroVector;
		}

		const FVector Error = EdenFlightMovementModel::OrientationErrorBody(State.Orientation, Request.TargetOrientationWorld);
		const double ResponseTime = FMath::Max(static_cast<double>(Settings.OrientationHoldResponseTimeSeconds), UE_DOUBLE_SMALL_NUMBER);
		const double MaxRate =
			FMath::DegreesToRadians(static_cast<double>(EdenFlightMovementModel::SanitizeNonNegative(Settings.AssistMaxAngularRateDegreesPerSecond)));
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			// Approach no faster than the actuators can stop from (with a sqrt(2) margin on w^2 = 2 alpha theta).
			const double Magnitude = FMath::Abs(Error[AxisIndex]);
			const double AvailableAngularAcceleration = Envelope.MaxNewtonMeters[AxisIndex] / Inertia[AxisIndex];
			const double Rate = FMath::Min3(Magnitude / ResponseTime, FMath::Sqrt(AvailableAngularAcceleration * Magnitude), MaxRate);
			TargetRate[AxisIndex] = Error[AxisIndex] >= 0.0 ? Rate : -Rate;
		}
	}

	if (!IsFiniteVector(TargetRate))
	{
		return FVector::ZeroVector;
	}

	const double ResponseTime = FMath::IsFinite(Settings.AssistAngularResponseTimeSeconds) && Settings.AssistAngularResponseTimeSeconds > 0.0f
		? static_cast<double>(Settings.AssistAngularResponseTimeSeconds)
		: static_cast<double>(FEdenFlightAttitudeSettings().AssistAngularResponseTimeSeconds);
	const FVector GyroscopicFeedForward = FVector::CrossProduct(AngularVelocity, Inertia * AngularVelocity);
	const FVector RequestedTorque = (Inertia * (TargetRate - AngularVelocity) / ResponseTime) + GyroscopicFeedForward;
	return ClampToTorqueEnvelope(RequestedTorque, Envelope);
}

FEdenFlightRotationStepResult FEdenFlightMovementModel::IntegrateRotation(
	const FEdenFlightRotationState& State,
	const FVector& BodyTorqueNewtonMeters,
	const FVector& PrincipalMomentsOfInertia,
	double DeltaTimeSeconds)
{
	FEdenFlightRotationStepResult Result;
	Result.State = State;

	if (!IsValidDeltaTime(DeltaTimeSeconds)
		|| !EdenFlightMovementModel::IsFinitePositiveVector(PrincipalMomentsOfInertia)
		|| !IsFiniteVector(BodyTorqueNewtonMeters)
		|| !IsFiniteVector(State.AngularVelocityBodyRadiansPerSecond)
		|| !EdenFlightMovementModel::IsFiniteQuat(State.Orientation))
	{
		Result.bStepWasValid = false;
		return Result;
	}

	using EdenFlightMovementModel::AddScaled;
	using EdenFlightMovementModel::EvaluateRigidBodyRates;
	using EdenFlightMovementModel::FRigidBodyRates;

	const double H = DeltaTimeSeconds;
	const FQuat Q0 = State.Orientation.GetNormalized();
	const FVector W0 = State.AngularVelocityBodyRadiansPerSecond;
	const FVector& Tau = BodyTorqueNewtonMeters;
	const FVector& I = PrincipalMomentsOfInertia;

	const FRigidBodyRates K1 = EvaluateRigidBodyRates(Q0, W0, Tau, I);
	const FRigidBodyRates K2 = EvaluateRigidBodyRates(AddScaled(Q0, K1.OrientationRate, 0.5 * H), W0 + K1.AngularAcceleration * (0.5 * H), Tau, I);
	const FRigidBodyRates K3 = EvaluateRigidBodyRates(AddScaled(Q0, K2.OrientationRate, 0.5 * H), W0 + K2.AngularAcceleration * (0.5 * H), Tau, I);
	const FRigidBodyRates K4 = EvaluateRigidBodyRates(AddScaled(Q0, K3.OrientationRate, H), W0 + K3.AngularAcceleration * H, Tau, I);

	FQuat Q1 = Q0;
	Q1 = AddScaled(Q1, K1.OrientationRate, H / 6.0);
	Q1 = AddScaled(Q1, K2.OrientationRate, H / 3.0);
	Q1 = AddScaled(Q1, K3.OrientationRate, H / 3.0);
	Q1 = AddScaled(Q1, K4.OrientationRate, H / 6.0);

	const FVector W1 = W0
		+ (K1.AngularAcceleration + (K2.AngularAcceleration * 2.0) + (K3.AngularAcceleration * 2.0) + K4.AngularAcceleration) * (H / 6.0);

	if (!EdenFlightMovementModel::IsFiniteQuat(Q1) || !IsFiniteVector(W1))
	{
		Result.bStepWasValid = false;
		return Result;
	}

	Result.State.Orientation = Q1.GetNormalized();
	Result.State.AngularVelocityBodyRadiansPerSecond = W1;
	Result.AngularAccelerationBodyRadiansPerSecondSquared = (W1 - W0) / H;
	return Result;
}

FVector FEdenFlightMovementModel::CalculateAngularMomentumWorld(const FEdenFlightRotationState& State, const FVector& PrincipalMomentsOfInertia)
{
	return State.Orientation.GetNormalized().RotateVector(PrincipalMomentsOfInertia * State.AngularVelocityBodyRadiansPerSecond);
}

double FEdenFlightMovementModel::CalculateRotationalKineticEnergy(
	const FEdenFlightRotationState& State,
	const FVector& PrincipalMomentsOfInertia)
{
	const FVector& W = State.AngularVelocityBodyRadiansPerSecond;
	return 0.5 * FVector::DotProduct(W, PrincipalMomentsOfInertia * W);
}

double FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(
	double ThrusterOutputNewtons,
	const FEdenFlightPropulsionSettings& Settings)
{
	if (!FMath::IsFinite(ThrusterOutputNewtons) || ThrusterOutputNewtons <= 0.0
		|| !FMath::IsFinite(Settings.SpecificImpulseSeconds) || Settings.SpecificImpulseSeconds <= 0.0f)
	{
		return 0.0;
	}

	return ThrusterOutputNewtons / (static_cast<double>(Settings.SpecificImpulseSeconds) * StandardGravityMetersPerSecondSquared);
}

double FEdenFlightMovementModel::CalculatePowerThrustFraction(EEdenPowerState PowerState, const FEdenFlightPropulsionSettings& Settings)
{
	switch (PowerState)
	{
	case EEdenPowerState::Critical:
		return EdenFlightMovementModel::SanitizeAuthority(Settings.CriticalPowerThrustFraction);
	case EEdenPowerState::Depleted:
		return EdenFlightMovementModel::SanitizeAuthority(Settings.DepletedPowerThrustFraction);
	case EEdenPowerState::Normal:
	case EEdenPowerState::Warning:
	default:
		return 1.0;
	}
}

double FEdenFlightMovementModel::CalculateInertiaScale(double MassKilograms, double DryMassKilograms)
{
	if (!FMath::IsFinite(MassKilograms) || !FMath::IsFinite(DryMassKilograms) || MassKilograms <= 0.0 || DryMassKilograms <= 0.0)
	{
		return 1.0;
	}

	return MassKilograms / DryMassKilograms;
}

FEdenFlightStepPlan FEdenFlightMovementModel::PlanFlightStep(
	const FEdenFlightStepInput& Input,
	const FEdenFlightPropulsionSettings& PropulsionSettings,
	const FEdenFlightAttitudeSettings& AttitudeSettings,
	double DeltaTimeSeconds)
{
	FEdenFlightStepPlan Plan;
	Plan.bPlanWasValid = IsValidDeltaTime(DeltaTimeSeconds) && FMath::IsFinite(Input.DryMassKilograms) && Input.DryMassKilograms > 0.0;
	if (!Plan.bPlanWasValid)
	{
		return Plan;
	}

	Plan.Command = SanitizeCommand(Input.Command, &Plan.bInputWasSanitized);
	Plan.bAssistActive = Plan.Command.bStabilizationEnabled && Input.bAssistAvailable;

	const double StoredPropellant = Input.bPropellantMetered && FMath::IsFinite(Input.PropellantMassKilograms)
		? FMath::Max(0.0, Input.PropellantMassKilograms)
		: 0.0;
	Plan.MassAtStartKilograms = Input.DryMassKilograms + StoredPropellant;

	const FQuat BodyRotation = Input.Rotation.Orientation.GetNormalized();
	FEdenFlightRotationState RotationState = Input.Rotation;
	RotationState.Orientation = BodyRotation;

	// Requests: controllers work inside the operator-limited envelopes, with the current total mass.
	Plan.Envelope = MakeThrustEnvelope(PropulsionSettings, Input.ThrustAuthority);
	if (Plan.bAssistActive)
	{
		const FVector VelocityBody = BodyRotation.UnrotateVector(Input.Translation.VelocityWorldMetersPerSecond);
		Plan.RequestedBodyForceNewtons = ResolveAssistThrust(
			Plan.Command.TranslationInput,
			VelocityBody,
			Plan.MassAtStartKilograms,
			PropulsionSettings,
			Plan.Envelope);
	}
	else
	{
		Plan.RequestedBodyForceNewtons = ResolveDirectThrust(Plan.Command.TranslationInput, Plan.Envelope);
	}

	Plan.TorqueEnvelope = MakeTorqueEnvelope(AttitudeSettings, Input.ThrustAuthority);
	FEdenFlightAttitudeSettings ControllerAttitude = AttitudeSettings;
	ControllerAttitude.PrincipalMomentsOfInertiaKilogramMetersSquared *=
		CalculateInertiaScale(Plan.MassAtStartKilograms, Input.DryMassKilograms);
	const FEdenFlightAttitudeRequest AttitudeRequest =
		MakePilotAttitudeRequest(Plan.Command.RotationInput, Plan.bAssistActive, AttitudeSettings);
	Plan.RequestedBodyTorqueNewtonMeters = ResolveAttitudeTorque(AttitudeRequest, RotationState, ControllerAttitude, Plan.TorqueEnvelope);

	// Delivery: power limits what the thrusters can actually produce.
	Plan.PowerThrustFraction = EdenFlightMovementModel::SanitizeAuthority(Input.PowerThrustFraction);
	Plan.PoweredBodyForceNewtons = Plan.RequestedBodyForceNewtons * Plan.PowerThrustFraction;
	Plan.PoweredBodyTorqueNewtonMeters = Plan.RequestedBodyTorqueNewtonMeters * Plan.PowerThrustFraction;
	Plan.RequestedThrusterOutputNewtons =
		CalculateThrusterOutputNewtons(Plan.RequestedBodyForceNewtons, Plan.RequestedBodyTorqueNewtonMeters, AttitudeSettings);
	Plan.PoweredThrusterOutputNewtons =
		CalculateThrusterOutputNewtons(Plan.PoweredBodyForceNewtons, Plan.PoweredBodyTorqueNewtonMeters, AttitudeSettings);

	// Propellant: what the powered thrust needs for the whole step (mdot = F / (Isp g0)).
	Plan.RequiredPropellantKilograms = Input.bPropellantMetered
		? CalculatePropellantMassFlowKilogramsPerSecond(Plan.PoweredThrusterOutputNewtons, PropulsionSettings) * DeltaTimeSeconds
		: 0.0;
	return Plan;
}

FEdenFlightStepOutput FEdenFlightMovementModel::ExecuteFlightStep(
	const FEdenFlightStepInput& Input,
	const FEdenFlightStepPlan& Plan,
	double GrantedPropellantKilograms,
	const FEdenFlightPropulsionSettings& PropulsionSettings,
	const FEdenFlightAttitudeSettings& AttitudeSettings,
	double DeltaTimeSeconds)
{
	FEdenFlightStepOutput Output;
	Output.Translation.State = Input.Translation;
	Output.Rotation.State = Input.Rotation;
	Output.bInputWasSanitized = Plan.bInputWasSanitized;
	Output.bAssistActive = Plan.bAssistActive;
	Output.Envelope = Plan.Envelope;
	Output.TorqueEnvelope = Plan.TorqueEnvelope;
	Output.RequestedBodyForceNewtons = Plan.RequestedBodyForceNewtons;
	Output.RequestedBodyTorqueNewtonMeters = Plan.RequestedBodyTorqueNewtonMeters;
	Output.RequestedThrusterOutputNewtons = Plan.RequestedThrusterOutputNewtons;
	Output.PowerThrustFraction = Plan.PowerThrustFraction;
	Output.PropellantRequiredKilograms = Plan.RequiredPropellantKilograms;
	Output.MassAtEndKilograms = Plan.MassAtStartKilograms;

	if (!Plan.bPlanWasValid || !IsValidDeltaTime(DeltaTimeSeconds))
	{
		Output.bStepWasValid = false;
		return Output;
	}

	// Burn fraction from the single propellant transaction.
	double Granted = 0.0;
	double BurnFraction = 1.0;
	if (Input.bPropellantMetered && Plan.RequiredPropellantKilograms > 0.0)
	{
		Granted = FMath::IsFinite(GrantedPropellantKilograms)
			? FMath::Clamp(GrantedPropellantKilograms, 0.0, Plan.RequiredPropellantKilograms)
			: 0.0;
		BurnFraction = Granted >= Plan.RequiredPropellantKilograms ? 1.0 : Granted / Plan.RequiredPropellantKilograms;
	}

	const double BurnSeconds = BurnFraction * DeltaTimeSeconds;
	const double CoastSeconds = BurnFraction >= 1.0 ? 0.0 : DeltaTimeSeconds - BurnSeconds;
	const double MassAtStart = Plan.MassAtStartKilograms;
	const double MassDuringBurn = MassAtStart - 0.5 * Granted;
	const double MassAtEnd = MassAtStart - Granted;
	const FQuat BodyRotation = Input.Rotation.Orientation.GetNormalized();
	const FVector& DryInertia = AttitudeSettings.PrincipalMomentsOfInertiaKilogramMetersSquared;

	// Translation: powered thrust while propellant lasts (orientation at step start, Checkpoint A), then coast.
	FEdenFlightTranslationStepResult Burn;
	Burn.State = Input.Translation;
	if (BurnSeconds > 0.0)
	{
		Burn = IntegrateTranslation(Input.Translation, Plan.PoweredBodyForceNewtons, MassDuringBurn, BodyRotation, BurnSeconds);
	}

	if (CoastSeconds > 0.0 && Burn.bStepWasValid)
	{
		const FEdenFlightTranslationStepResult Coast =
			IntegrateTranslation(Burn.State, FVector::ZeroVector, MassAtEnd, BodyRotation, CoastSeconds);
		Output.Translation = Coast;
		Output.Translation.DisplacementWorldMeters = Burn.DisplacementWorldMeters + Coast.DisplacementWorldMeters;
		Output.Translation.AccelerationWorldMetersPerSecondSquared =
			(Coast.State.VelocityWorldMetersPerSecond - Input.Translation.VelocityWorldMetersPerSecond) / DeltaTimeSeconds;
	}
	else
	{
		Output.Translation = Burn;
	}

	// Rotation: powered torque while propellant lasts, then torque-free (Checkpoint B dynamics, current inertia).
	FEdenFlightRotationStepResult BurnRotation;
	BurnRotation.State = Input.Rotation;
	if (BurnSeconds > 0.0)
	{
		BurnRotation = IntegrateRotation(
			Input.Rotation,
			Plan.PoweredBodyTorqueNewtonMeters,
			DryInertia * CalculateInertiaScale(MassDuringBurn, Input.DryMassKilograms),
			BurnSeconds);
	}

	if (CoastSeconds > 0.0 && BurnRotation.bStepWasValid)
	{
		const FEdenFlightRotationStepResult CoastRotation = IntegrateRotation(
			BurnRotation.State,
			FVector::ZeroVector,
			DryInertia * CalculateInertiaScale(MassAtEnd, Input.DryMassKilograms),
			CoastSeconds);
		Output.Rotation = CoastRotation;
		Output.Rotation.AngularAccelerationBodyRadiansPerSecondSquared =
			(CoastRotation.State.AngularVelocityBodyRadiansPerSecond - Input.Rotation.AngularVelocityBodyRadiansPerSecond) / DeltaTimeSeconds;
	}
	else
	{
		Output.Rotation = BurnRotation;
	}

	Output.bStepWasValid = Output.Translation.bStepWasValid && Output.Rotation.bStepWasValid;
	if (!Output.bStepWasValid)
	{
		Output.Translation.State = Input.Translation;
		Output.Rotation.State = Input.Rotation;
		return Output;
	}

	// Delivered = powered x burn fraction; propellant and demand follow delivery, never the request.
	Output.AppliedBodyForceNewtons = Plan.PoweredBodyForceNewtons * BurnFraction;
	Output.AppliedBodyTorqueNewtonMeters = Plan.PoweredBodyTorqueNewtonMeters * BurnFraction;
	Output.DeliveredThrusterOutputNewtons = Plan.PoweredThrusterOutputNewtons * BurnFraction;
	Output.PropulsionDemandNormalized = CalculatePropulsionDemandNormalized(Output.DeliveredThrusterOutputNewtons, PropulsionSettings);
	Output.PropellantBurnFraction = BurnFraction;
	Output.PropellantConsumedKilograms = Granted;
	Output.MassAtEndKilograms = MassAtEnd;
	Output.bPowerLimited = Plan.PowerThrustFraction < 1.0 && Plan.RequestedThrusterOutputNewtons > 0.0;
	Output.bPropellantLimited = Input.bPropellantMetered && Plan.PoweredThrusterOutputNewtons > 0.0 && BurnFraction < 1.0;
	return Output;
}

FEdenFlightStepOutput FEdenFlightMovementModel::StepFlight(
	const FEdenFlightStepInput& Input,
	const FEdenFlightPropulsionSettings& PropulsionSettings,
	const FEdenFlightAttitudeSettings& AttitudeSettings,
	double DeltaTimeSeconds)
{
	const FEdenFlightStepPlan Plan = PlanFlightStep(Input, PropulsionSettings, AttitudeSettings, DeltaTimeSeconds);
	const double Stored = FMath::IsFinite(Input.PropellantMassKilograms) ? FMath::Max(0.0, Input.PropellantMassKilograms) : 0.0;
	const double Granted = Input.bPropellantMetered ? FMath::Min(Plan.RequiredPropellantKilograms, Stored) : 0.0;
	return ExecuteFlightStep(Input, Plan, Granted, PropulsionSettings, AttitudeSettings, DeltaTimeSeconds);
}

int32 FEdenFlightMovementModel::CalculateFixedSteps(
	double FrameDeltaSeconds,
	double FixedStepSeconds,
	int32 MaxSteps,
	double& InOutAccumulatorSeconds,
	int32& OutDroppedSteps)
{
	OutDroppedSteps = 0;

	if (!FMath::IsFinite(InOutAccumulatorSeconds) || InOutAccumulatorSeconds < 0.0)
	{
		InOutAccumulatorSeconds = 0.0;
	}

	if (!IsValidDeltaTime(FrameDeltaSeconds) || !IsValidDeltaTime(FixedStepSeconds) || MaxSteps < 1)
	{
		return 0;
	}

	const double TotalSeconds = InOutAccumulatorSeconds + FrameDeltaSeconds;
	const double DesiredStepsDouble = FMath::FloorToDouble((TotalSeconds / FixedStepSeconds) + EdenFlightMovementModel::FixedStepEpsilon);
	const int64 DesiredSteps = DesiredStepsDouble >= static_cast<double>(TNumericLimits<int32>::Max())
		? static_cast<int64>(TNumericLimits<int32>::Max())
		: static_cast<int64>(DesiredStepsDouble);
	const int32 StepsTaken = static_cast<int32>(FMath::Min<int64>(DesiredSteps, MaxSteps));

	OutDroppedSteps = static_cast<int32>(DesiredSteps - StepsTaken);
	InOutAccumulatorSeconds = FMath::Max(0.0, TotalSeconds - (static_cast<double>(DesiredSteps) * FixedStepSeconds));
	return StepsTaken;
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

bool FEdenFlightMovementModel::IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}
