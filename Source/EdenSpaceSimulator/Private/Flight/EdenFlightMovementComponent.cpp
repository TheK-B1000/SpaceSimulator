// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementComponent.h"

#include "Components/SphereComponent.h"
#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Engine/World.h"

UEdenFlightMovementComponent::UEdenFlightMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UEdenFlightMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
	ResetFlightMovement();
}

void UEdenFlightMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	if (NewUpdatedComponent && !NewUpdatedComponent->IsA<USphereComponent>())
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s rejected UpdatedComponent '%s'. Six-axis flight requires the C++ sphere collision root."),
			*GetNameSafe(this),
			*GetNameSafe(NewUpdatedComponent));
		return;
	}

	Super::SetUpdatedComponent(NewUpdatedComponent);
}

void UEdenFlightMovementComponent::StopMovementImmediately()
{
	Super::StopMovementImmediately();
	AngularVelocityLocalDegreesPerSecond = FVector::ZeroVector;
	PropulsionDemandNormalized = 0.0f;
	AppliedBodyForceNewtons = FVector::ZeroVector;
	LastAccelerationWorldMetersPerSecondSquared = FVector::ZeroVector;
	bWasBlockedLastMove = false;
}

float UEdenFlightMovementComponent::GetPropulsionDemandNormalized() const
{
	if (!FMath::IsFinite(PropulsionDemandNormalized))
	{
		return 0.0f;
	}

	return FMath::Clamp(PropulsionDemandNormalized, 0.0f, 1.0f);
}

int32 UEdenFlightMovementComponent::MoveWithCommand(const FEdenFlightInputCommand& Command, float DeltaTimeSeconds)
{
	if (!ValidateUpdatedComponentForFlight())
	{
		return 0;
	}

	if (!FEdenFlightMovementModel::IsValidDeltaTime(DeltaTimeSeconds) || IsFlightTimePaused())
	{
		return 0;
	}

	const FEdenFlightStepSettings ActiveStep = ResolveStepSettings();
	const double FixedStepSeconds = ActiveStep.GetFixedStepSeconds();
	const FEdenFlightPropulsionSettings ActivePropulsion = ResolvePropulsionSettings(FixedStepSeconds);

	int32 DroppedSteps = 0;
	const int32 StepsToRun = FEdenFlightMovementModel::CalculateFixedSteps(
		DeltaTimeSeconds,
		FixedStepSeconds,
		ActiveStep.MaxStepsPerUpdate,
		FlightAccumulatorSeconds,
		DroppedSteps);

	if (DroppedSteps > 0)
	{
		DroppedFlightStepCount += DroppedSteps;
		LogDroppedSteps(DroppedSteps, FixedStepSeconds);
	}

	for (int32 StepIndex = 0; StepIndex < StepsToRun; ++StepIndex)
	{
		RunFlightStep(Command, ActivePropulsion, FixedStepSeconds);
	}

	return StepsToRun;
}

void UEdenFlightMovementComponent::RunFlightStep(
	const FEdenFlightInputCommand& Command,
	const FEdenFlightPropulsionSettings& ActivePropulsion,
	double FixedStepSeconds)
{
	FEdenFlightStepInput StepInput;
	StepInput.Command = Command;
	StepInput.Translation.VelocityWorldMetersPerSecond = EdenFlightUnits::CentimetersToMeters(Velocity);
	StepInput.AngularVelocityLocalDegreesPerSecond = AngularVelocityLocalDegreesPerSecond;
	StepInput.BodyRotation = UpdatedComponent->GetComponentQuat();
	StepInput.MassKilograms = ActivePropulsion.DryMassKilograms;
	StepInput.ThrustAuthority = GetEffectiveThrustAuthority();
	StepInput.bAssistAvailable = bStabilizationAssistAvailable;

	const FEdenFlightStepOutput StepOutput =
		FEdenFlightMovementModel::StepFlight(StepInput, ActivePropulsion, MovementSettings, FixedStepSeconds);

	LogInvalidInputState(StepOutput.bInputWasSanitized);

	bFlightAssistRequested = Command.bStabilizationEnabled;
	bFlightAssistActive = StepOutput.bAssistActive;
	LastThrustEnvelope = StepOutput.Envelope;
	AppliedBodyForceNewtons = StepOutput.AppliedBodyForceNewtons;
	LastAccelerationWorldMetersPerSecondSquared = StepOutput.Translation.AccelerationWorldMetersPerSecondSquared;
	PropulsionDemandNormalized = StepOutput.PropulsionDemandNormalized;
	Velocity = EdenFlightUnits::MetersToCentimeters(StepOutput.Translation.State.VelocityWorldMetersPerSecond);
	AngularVelocityLocalDegreesPerSecond = StepOutput.AngularVelocityLocalDegreesPerSecond;

	const FVector DisplacementCentimeters = EdenFlightUnits::MetersToCentimeters(StepOutput.Translation.DisplacementWorldMeters);
	FHitResult Hit;
	SafeMoveUpdatedComponent(DisplacementCentimeters, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (Hit.IsValidBlockingHit())
	{
		// Contact removes the inward normal momentum (inelastic); tangential momentum is preserved.
		Velocity = FEdenFlightMovementModel::RemoveInwardVelocity(Velocity, Hit.Normal);
		LogBlockingHitState(Hit);

		if (!Velocity.IsNearlyZero())
		{
			FHitResult SlideHit;
			SlideAlongSurface(Velocity * FixedStepSeconds, 1.0f - Hit.Time, Hit.Normal, SlideHit, true);
		}
	}
	else
	{
		bWasBlockedLastMove = false;
	}

	// Exact compare: a default-tolerance Equals would drop slow per-step rotations at 120 Hz.
	if (!StepOutput.NextBodyRotation.Equals(UpdatedComponent->GetComponentQuat(), 0.0))
	{
		FHitResult RotationHit;
		SafeMoveUpdatedComponent(FVector::ZeroVector, StepOutput.NextBodyRotation, false, RotationHit);
	}

	FlightSimulationTimeSeconds += FixedStepSeconds;
	++FlightStepCount;
}

void UEdenFlightMovementComponent::ResetFlightMovement()
{
	StopMovementImmediately();
	ResetFlightSimulationState();
	ThrustAuthority = 1.0f;
	bStabilizationAssistAvailable = true;
}

void UEdenFlightMovementComponent::ResetFlightSimulationState()
{
	FlightAccumulatorSeconds = 0.0;
	FlightSimulationTimeSeconds = 0.0;
	FlightStepCount = 0;
	DroppedFlightStepCount = 0;
	LastThrustEnvelope = FEdenFlightThrustEnvelope();
	bFlightAssistRequested = true;
	bFlightAssistActive = true;
}

bool UEdenFlightMovementComponent::IsFlightTimePaused() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (World->IsPaused())
	{
		return true;
	}

	const UEdenSimulationClockSubsystem* SimulationClock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	return SimulationClock && SimulationClock->IsSimulationPaused();
}

FEdenFlightPropulsionSettings UEdenFlightMovementComponent::GetActivePropulsionSettings() const
{
	const double FixedStepSeconds = FEdenFlightMovementModel::ValidateStepSettings(StepSettings)
		? StepSettings.GetFixedStepSeconds()
		: FEdenFlightStepSettings().GetFixedStepSeconds();
	return FEdenFlightMovementModel::ValidatePropulsionSettings(PropulsionSettings, FixedStepSeconds)
		? PropulsionSettings
		: FEdenFlightPropulsionSettings();
}

FEdenFlightPropulsionSettings UEdenFlightMovementComponent::ResolvePropulsionSettings(double FixedStepSeconds)
{
	TArray<FString> Errors;
	if (FEdenFlightMovementModel::ValidatePropulsionSettings(PropulsionSettings, FixedStepSeconds, &Errors))
	{
		bLoggedInvalidPropulsionSettings = false;
		return PropulsionSettings;
	}

	if (!bLoggedInvalidPropulsionSettings)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s has invalid PropulsionSettings; using C++ defaults. %s"),
			*GetNameSafe(this),
			*FString::Join(Errors, TEXT(" ")));
		bLoggedInvalidPropulsionSettings = true;
	}

	return FEdenFlightPropulsionSettings();
}

FEdenFlightStepSettings UEdenFlightMovementComponent::ResolveStepSettings()
{
	TArray<FString> Errors;
	if (FEdenFlightMovementModel::ValidateStepSettings(StepSettings, &Errors))
	{
		bLoggedInvalidStepSettings = false;
		return StepSettings;
	}

	if (!bLoggedInvalidStepSettings)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s has invalid StepSettings; using C++ defaults. %s"),
			*GetNameSafe(this),
			*FString::Join(Errors, TEXT(" ")));
		bLoggedInvalidStepSettings = true;
	}

	return FEdenFlightStepSettings();
}

float UEdenFlightMovementComponent::GetEffectiveThrustAuthority() const
{
	// Checkpoint D multiplies in fuel and power limits here; operator authority is the only limit today.
	return GetThrustAuthority();
}

float UEdenFlightMovementComponent::GetMassKilograms() const
{
	return GetActivePropulsionSettings().DryMassKilograms;
}

float UEdenFlightMovementComponent::GetSpeedMetersPerSecond() const
{
	return static_cast<float>(EdenFlightUnits::CentimetersToMeters(Velocity).Size());
}

FEdenFlightDynamicsSnapshot UEdenFlightMovementComponent::GetFlightDynamicsSnapshot() const
{
	FEdenFlightDynamicsSnapshot Snapshot;
	Snapshot.VelocityWorldMetersPerSecond = EdenFlightUnits::CentimetersToMeters(Velocity);
	Snapshot.SpeedMetersPerSecond = GetSpeedMetersPerSecond();
	Snapshot.AccelerationWorldMetersPerSecondSquared = LastAccelerationWorldMetersPerSecondSquared;
	Snapshot.AppliedBodyForceNewtons = AppliedBodyForceNewtons;

	// Report what the thrusters can deliver now, even before the first step has run.
	const FEdenFlightThrustEnvelope Envelope =
		FEdenFlightMovementModel::MakeThrustEnvelope(GetActivePropulsionSettings(), GetEffectiveThrustAuthority());
	Snapshot.AvailablePositiveThrustNewtons = Envelope.PositiveNewtons;
	Snapshot.AvailableNegativeThrustNewtons = Envelope.NegativeNewtons;

	Snapshot.MassKilograms = GetMassKilograms();
	Snapshot.EffectiveThrustAuthority = GetEffectiveThrustAuthority();
	Snapshot.bFlightAssistRequested = bFlightAssistRequested;
	Snapshot.bFlightAssistActive = bFlightAssistActive;
	Snapshot.FlightSimulationTimeSeconds = FlightSimulationTimeSeconds;
	Snapshot.FlightStepCount = FlightStepCount;
	Snapshot.DroppedFlightStepCount = DroppedFlightStepCount;
	return Snapshot;
}

bool UEdenFlightMovementComponent::SetThrustAuthority(float NewThrustAuthority)
{
	if (!FMath::IsFinite(NewThrustAuthority))
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s rejected non-finite thrust authority."), *GetNameSafe(this));
		ThrustAuthority = 0.0f;
		return false;
	}

	ThrustAuthority = FMath::Clamp(NewThrustAuthority, 0.0f, 1.0f);
	return FMath::IsNearlyEqual(ThrustAuthority, NewThrustAuthority);
}

void UEdenFlightMovementComponent::ResetThrustAuthority()
{
	ThrustAuthority = 1.0f;
}

float UEdenFlightMovementComponent::GetThrustAuthority() const
{
	return FMath::IsFinite(ThrustAuthority) ? FMath::Clamp(ThrustAuthority, 0.0f, 1.0f) : 0.0f;
}

void UEdenFlightMovementComponent::SetStabilizationAssistAvailable(bool bAvailable)
{
	bStabilizationAssistAvailable = bAvailable;
}

bool UEdenFlightMovementComponent::IsStabilizationAssistAvailable() const
{
	return bStabilizationAssistAvailable;
}

FVector UEdenFlightMovementComponent::GetAngularVelocityLocalDegreesPerSecond() const
{
	return AngularVelocityLocalDegreesPerSecond;
}

void UEdenFlightMovementComponent::SetAngularVelocityLocalDegreesPerSecond(FVector NewAngularVelocityLocalDegreesPerSecond)
{
	if (!FMath::IsFinite(NewAngularVelocityLocalDegreesPerSecond.X)
		|| !FMath::IsFinite(NewAngularVelocityLocalDegreesPerSecond.Y)
		|| !FMath::IsFinite(NewAngularVelocityLocalDegreesPerSecond.Z))
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s rejected non-finite angular velocity."),
			*GetNameSafe(this));
		AngularVelocityLocalDegreesPerSecond = FVector::ZeroVector;
		return;
	}

	AngularVelocityLocalDegreesPerSecond = NewAngularVelocityLocalDegreesPerSecond;
}

bool UEdenFlightMovementComponent::ValidateUpdatedComponentForFlight() const
{
	if (!UpdatedComponent)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s has no UpdatedComponent."), *GetNameSafe(this));
		return false;
	}

	if (!UpdatedComponent->IsA<USphereComponent>())
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s UpdatedComponent '%s' is not a sphere collision root."),
			*GetNameSafe(this),
			*GetNameSafe(UpdatedComponent));
		return false;
	}

	const AActor* OwnerActor = UpdatedComponent->GetOwner();
	if (OwnerActor && OwnerActor->GetRootComponent() != UpdatedComponent)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s UpdatedComponent '%s' is not the owning actor root."),
			*GetNameSafe(this),
			*GetNameSafe(UpdatedComponent));
		return false;
	}

	if (UpdatedComponent->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s UpdatedComponent '%s' must be movable."),
			*GetNameSafe(this),
			*GetNameSafe(UpdatedComponent));
		return false;
	}

	return true;
}

void UEdenFlightMovementComponent::LogDroppedSteps(int32 DroppedSteps, double FixedStepSeconds)
{
	UE_LOG(
		LogEdenFlight,
		Warning,
		TEXT("%s dropped %d flight steps (%.3f s of simulated time) after exceeding MaxStepsPerUpdate=%d. Total dropped=%lld."),
		*GetNameSafe(this),
		DroppedSteps,
		DroppedSteps * FixedStepSeconds,
		StepSettings.MaxStepsPerUpdate,
		DroppedFlightStepCount);
}

void UEdenFlightMovementComponent::LogInvalidInputState(bool bInputWasSanitized)
{
	if (bInputWasSanitized && !bLoggedInvalidInputState)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s sanitized invalid or out-of-range flight input."), *GetNameSafe(this));
	}

	bLoggedInvalidInputState = bInputWasSanitized;
}

void UEdenFlightMovementComponent::LogBlockingHitState(const FHitResult& Hit)
{
	if (bWasBlockedLastMove)
	{
		return;
	}

	UE_LOG(
		LogEdenFlight,
		Verbose,
		TEXT("%s detected blocking hit against '%s'. Applying slide response."),
		*GetNameSafe(this),
		*GetNameSafe(Hit.GetActor()));
	bWasBlockedLastMove = true;
}
