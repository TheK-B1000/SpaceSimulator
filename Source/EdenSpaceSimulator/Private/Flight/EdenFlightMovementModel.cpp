// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementModel.h"

namespace EdenFlightMovementModel
{
constexpr float DefaultInputReleaseTolerance = 0.001f;

/** Assist must span at least this many flight steps to stay monotone (discrete lag factor 1 - dt/tau >= 0.75). */
constexpr double MinAssistResponseSteps = 4.0;

/** Absorbs representation error when a frame is an exact multiple of the fixed step (e.g. 1/60 vs 1/120). */
constexpr double FixedStepEpsilon = 1.0e-9;

float SanitizeNonNegative(float Value)
{
	return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
}

double SanitizeAuthority(double Authority)
{
	return FMath::IsFinite(Authority) ? FMath::Clamp(Authority, 0.0, 1.0) : 0.0;
}

void ValidatePositive(const TCHAR* Name, float Value, bool& bInOutValid, TArray<FString>* OutErrors)
{
	if (!FMath::IsFinite(Value) || Value <= 0.0f)
	{
		bInOutValid = false;
		if (OutErrors)
		{
			OutErrors->Add(FString::Printf(TEXT("%s must be finite and > 0. Value=%f"), Name, Value));
		}
	}
}

void ValidateNonNegative(const TCHAR* Name, float Value, bool& bInOutValid, TArray<FString>* OutErrors)
{
	if (!FMath::IsFinite(Value) || Value < 0.0f)
	{
		bInOutValid = false;
		if (OutErrors)
		{
			OutErrors->Add(FString::Printf(TEXT("%s must be finite and >= 0. Value=%f"), Name, Value));
		}
	}
}
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

	if (bValid && IsValidDeltaTime(FlightStepSeconds)
		&& Settings.AssistResponseTimeSeconds < EdenFlightMovementModel::MinAssistResponseSteps * FlightStepSeconds)
	{
		bValid = false;
		if (OutErrors)
		{
			OutErrors->Add(FString::Printf(
				TEXT("AssistResponseTimeSeconds (%f) must be >= %.0f flight steps (%f s)."),
				Settings.AssistResponseTimeSeconds,
				EdenFlightMovementModel::MinAssistResponseSteps,
				EdenFlightMovementModel::MinAssistResponseSteps * FlightStepSeconds));
		}
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
	const FEdenFlightThrustEnvelope& Envelope,
	double InputReleaseTolerance)
{
	if (!FMath::IsFinite(MassKilograms) || MassKilograms <= 0.0 || !IsFiniteVector(VelocityBodyMetersPerSecond))
	{
		return FVector::ZeroVector;
	}

	const double MaxSpeed = EdenFlightMovementModel::SanitizeNonNegative(Settings.AssistMaxSpeedMetersPerSecond);
	const double ResponseTime = FMath::IsFinite(Settings.AssistResponseTimeSeconds) && Settings.AssistResponseTimeSeconds > 0.0f
		? static_cast<double>(Settings.AssistResponseTimeSeconds)
		: static_cast<double>(FEdenFlightPropulsionSettings().AssistResponseTimeSeconds);
	const double Tolerance = FMath::Max(InputReleaseTolerance, static_cast<double>(EdenFlightMovementModel::DefaultInputReleaseTolerance));

	FVector RequestedForce = FVector::ZeroVector;
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const double Input = FMath::IsFinite(NormalizedTranslationInput[AxisIndex]) ? NormalizedTranslationInput[AxisIndex] : 0.0;
		const double TargetVelocity = FMath::Abs(Input) > Tolerance ? Input * MaxSpeed : 0.0;
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

float FEdenFlightMovementModel::CalculatePropulsionDemandNormalized(
	const FVector& AppliedBodyForceNewtons,
	const FEdenFlightPropulsionSettings& Settings)
{
	if (!IsFiniteVector(AppliedBodyForceNewtons))
	{
		return 0.0f;
	}

	const double TotalThrust =
		FMath::Abs(AppliedBodyForceNewtons.X) + FMath::Abs(AppliedBodyForceNewtons.Y) + FMath::Abs(AppliedBodyForceNewtons.Z);
	if (TotalThrust <= 0.0)
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

FEdenFlightStepOutput FEdenFlightMovementModel::StepFlight(
	const FEdenFlightStepInput& Input,
	const FEdenFlightPropulsionSettings& PropulsionSettings,
	const FEdenFlightMovementSettings& RotationSettings,
	double DeltaTimeSeconds)
{
	FEdenFlightStepOutput Output;
	Output.Translation.State = Input.Translation;
	Output.AngularVelocityLocalDegreesPerSecond = Input.AngularVelocityLocalDegreesPerSecond;
	Output.NextBodyRotation = Input.BodyRotation;

	if (!IsValidDeltaTime(DeltaTimeSeconds))
	{
		Output.bStepWasValid = false;
		return Output;
	}

	FEdenFlightInputCommand Command = SanitizeCommand(Input.Command, &Output.bInputWasSanitized);
	Output.bAssistActive = Command.bStabilizationEnabled && Input.bAssistAvailable;
	Command.bStabilizationEnabled = Output.bAssistActive;

	const FQuat BodyRotation = Input.BodyRotation.GetNormalized();
	Output.Envelope = MakeThrustEnvelope(PropulsionSettings, Input.ThrustAuthority);

	if (Output.bAssistActive)
	{
		const FVector VelocityBody = BodyRotation.UnrotateVector(Input.Translation.VelocityWorldMetersPerSecond);
		Output.AppliedBodyForceNewtons = ResolveAssistThrust(
			Command.TranslationInput,
			VelocityBody,
			Input.MassKilograms,
			PropulsionSettings,
			Output.Envelope,
			RotationSettings.InputReleaseTolerance);
	}
	else
	{
		Output.AppliedBodyForceNewtons = ResolveDirectThrust(Command.TranslationInput, Output.Envelope);
	}

	Output.Translation =
		IntegrateTranslation(Input.Translation, Output.AppliedBodyForceNewtons, Input.MassKilograms, BodyRotation, DeltaTimeSeconds);
	if (!Output.Translation.bStepWasValid)
	{
		Output.AppliedBodyForceNewtons = FVector::ZeroVector;
		Output.bStepWasValid = false;
	}

	Output.PropulsionDemandNormalized = CalculatePropulsionDemandNormalized(Output.AppliedBodyForceNewtons, PropulsionSettings);

	const FEdenFlightAngularStepResult Angular = IntegrateAngularVelocity(
		Input.AngularVelocityLocalDegreesPerSecond,
		Command,
		RotationSettings,
		static_cast<float>(DeltaTimeSeconds));
	Output.AngularVelocityLocalDegreesPerSecond = Angular.AngularVelocityLocalDegreesPerSecond;

	if (!Output.AngularVelocityLocalDegreesPerSecond.IsZero())
	{
		const FRotator DeltaRotation(
			Output.AngularVelocityLocalDegreesPerSecond.X * DeltaTimeSeconds,
			Output.AngularVelocityLocalDegreesPerSecond.Y * DeltaTimeSeconds,
			Output.AngularVelocityLocalDegreesPerSecond.Z * DeltaTimeSeconds);
		Output.NextBodyRotation = (BodyRotation * DeltaRotation.Quaternion()).GetNormalized();
	}
	else
	{
		Output.NextBodyRotation = BodyRotation;
	}

	return Output;
}

FEdenFlightAngularStepResult FEdenFlightMovementModel::IntegrateAngularVelocity(
	const FVector& CurrentAngularVelocityLocalDegreesPerSecond,
	const FEdenFlightInputCommand& Command,
	const FEdenFlightMovementSettings& Settings,
	float DeltaTimeSeconds)
{
	FEdenFlightAngularStepResult Result;
	Result.AngularVelocityLocalDegreesPerSecond = CurrentAngularVelocityLocalDegreesPerSecond;
	Result.bDeltaTimeWasValid = IsValidDeltaTime(DeltaTimeSeconds);

	if (!Result.bDeltaTimeWasValid)
	{
		return Result;
	}

	bool bUnusedInputWasSanitized = false;
	const FEdenFlightInputCommand SanitizedCommand = SanitizeCommand(Command, &bUnusedInputWasSanitized);
	const float MaxAngularSpeed = EdenFlightMovementModel::SanitizeNonNegative(Settings.MaxAngularSpeedDegreesPerSecond);
	const float InputReleaseTolerance = EdenFlightMovementModel::SanitizeNonNegative(
		FMath::Max(Settings.InputReleaseTolerance, EdenFlightMovementModel::DefaultInputReleaseTolerance));

	Result.AngularVelocityLocalDegreesPerSecond = IntegrateAxisVector(
		CurrentAngularVelocityLocalDegreesPerSecond,
		SanitizedCommand.RotationInput,
		MaxAngularSpeed,
		EdenFlightMovementModel::SanitizeNonNegative(Settings.AngularAccelerationDegreesPerSecondSquared),
		EdenFlightMovementModel::SanitizeNonNegative(Settings.AngularDecelerationDegreesPerSecondSquared),
		InputReleaseTolerance,
		SanitizedCommand.bStabilizationEnabled,
		DeltaTimeSeconds);
	Result.AngularVelocityLocalDegreesPerSecond = ClampVectorMagnitude(Result.AngularVelocityLocalDegreesPerSecond, MaxAngularSpeed);
	return Result;
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

FVector FEdenFlightMovementModel::IntegrateAxisVector(
	const FVector& CurrentValue,
	const FVector& NormalizedInput,
	float MaxSpeed,
	float Acceleration,
	float Deceleration,
	float InputReleaseTolerance,
	bool bStabilizationEnabled,
	float DeltaTimeSeconds)
{
	if (MaxSpeed <= 0.0f || !IsFiniteVector(CurrentValue) || !IsFiniteVector(NormalizedInput))
	{
		return FVector::ZeroVector;
	}

	FVector IntegratedValue = CurrentValue;

	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const double InputValue = NormalizedInput[AxisIndex];
		const bool bHasInput = FMath::Abs(InputValue) > InputReleaseTolerance;

		if (bHasInput)
		{
			const double TargetValue = InputValue * MaxSpeed;
			IntegratedValue[AxisIndex] = MoveScalarToward(
				IntegratedValue[AxisIndex],
				TargetValue,
				static_cast<double>(Acceleration) * DeltaTimeSeconds);
		}
		else if (bStabilizationEnabled)
		{
			IntegratedValue[AxisIndex] = MoveScalarToward(
				IntegratedValue[AxisIndex],
				0.0,
				static_cast<double>(Deceleration) * DeltaTimeSeconds);
		}
	}

	return ClampVectorMagnitude(IntegratedValue, MaxSpeed);
}

double FEdenFlightMovementModel::MoveScalarToward(double Current, double Target, double MaxDelta)
{
	if (MaxDelta <= 0.0)
	{
		return Current;
	}

	if (Current < Target)
	{
		return FMath::Min(Current + MaxDelta, Target);
	}

	if (Current > Target)
	{
		return FMath::Max(Current - MaxDelta, Target);
	}

	return Current;
}

bool FEdenFlightMovementModel::IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}
