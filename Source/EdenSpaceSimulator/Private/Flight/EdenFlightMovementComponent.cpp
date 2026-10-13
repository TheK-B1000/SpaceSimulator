// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementComponent.h"

#include "Components/SphereComponent.h"
#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

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

void UEdenFlightMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshResourceSources();
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
	AngularVelocityBodyRadiansPerSecond = FVector::ZeroVector;
	PropulsionDemandNormalized = 0.0f;
	LastStep = FEdenFlightStepOutput();
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

	if (!bResourceSourcesDiscovered)
	{
		RefreshResourceSources();
	}

	const FEdenFlightStepSettings ActiveStep = ResolveStepSettings();
	const double FixedStepSeconds = ActiveStep.GetFixedStepSeconds();
	const FEdenFlightPropulsionSettings ActivePropulsion = ResolvePropulsionSettings(FixedStepSeconds);
	const FEdenFlightAttitudeSettings ActiveAttitude = ResolveAttitudeSettings(FixedStepSeconds);

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
		RunFlightStep(Command, ActivePropulsion, ActiveAttitude, FixedStepSeconds);
	}

	return StepsToRun;
}

void UEdenFlightMovementComponent::RunFlightStep(
	const FEdenFlightInputCommand& Command,
	const FEdenFlightPropulsionSettings& ActivePropulsion,
	const FEdenFlightAttitudeSettings& ActiveAttitude,
	double FixedStepSeconds)
{
	FEdenFlightStepInput StepInput;
	StepInput.Command = Command;
	StepInput.Translation.VelocityWorldMetersPerSecond = EdenFlightUnits::CentimetersToMeters(Velocity);
	StepInput.Rotation.Orientation = UpdatedComponent->GetComponentQuat();
	StepInput.Rotation.AngularVelocityBodyRadiansPerSecond = AngularVelocityBodyRadiansPerSecond;
	StepInput.DryMassKilograms = ActivePropulsion.DryMassKilograms;
	IEdenPropellantSupply* Supply = IsPropellantMetered() ? GetPropellantSupply() : nullptr;
	StepInput.bPropellantMetered = Supply != nullptr;
	StepInput.PropellantMassKilograms = Supply ? Supply->GetPropellantMassKilograms() : 0.0;
	StepInput.ThrustAuthority = GetThrustAuthority();
	StepInput.PowerThrustFraction = GetPowerThrustFraction();
	StepInput.bAssistAvailable = bStabilizationAssistAvailable;

	// 1. Plan (pure)  2. one propellant transaction with the fuel system  3. Execute (pure).
	const FEdenFlightStepPlan Plan = FEdenFlightMovementModel::PlanFlightStep(StepInput, ActivePropulsion, ActiveAttitude, FixedStepSeconds);
	const double GrantedPropellant = Supply && Plan.RequiredPropellantKilograms > 0.0
		? Supply->DrawPropellantKilograms(Plan.RequiredPropellantKilograms)
		: 0.0;
	const FEdenFlightStepOutput StepOutput =
		FEdenFlightMovementModel::ExecuteFlightStep(StepInput, Plan, GrantedPropellant, ActivePropulsion, ActiveAttitude, FixedStepSeconds);

	LogInvalidInputState(StepOutput.bInputWasSanitized);

	bFlightAssistRequested = Command.bStabilizationEnabled;
	bFlightAssistActive = StepOutput.bAssistActive;
	LastStep = StepOutput;
	PropulsionDemandNormalized = StepOutput.PropulsionDemandNormalized;
	Velocity = EdenFlightUnits::MetersToCentimeters(StepOutput.Translation.State.VelocityWorldMetersPerSecond);
	AngularVelocityBodyRadiansPerSecond = StepOutput.Rotation.State.AngularVelocityBodyRadiansPerSecond;

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
	const FQuat& NextOrientation = StepOutput.Rotation.State.Orientation;
	if (!NextOrientation.Equals(UpdatedComponent->GetComponentQuat(), 0.0))
	{
		FHitResult RotationHit;
		SafeMoveUpdatedComponent(FVector::ZeroVector, NextOrientation, false, RotationHit);
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
	bFlightAssistRequested = true;
	bFlightAssistActive = true;
	LastStep = FEdenFlightStepOutput();
}

bool UEdenFlightMovementComponent::RefreshResourceSources()
{
	bResourceSourcesDiscovered = true;
	PropellantSupplyComponent.Reset();
	PowerSourceComponent.Reset();

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	TInlineComponentArray<UActorComponent*> Components(OwnerActor);
	int32 SupplyCount = 0;
	int32 PowerCount = 0;
	for (UActorComponent* Component : Components)
	{
		if (!IsValid(Component))
		{
			continue;
		}

		if (Cast<IEdenPropellantSupply>(Component))
		{
			PropellantSupplyComponent = Component;
			++SupplyCount;
		}

		if (Cast<IEdenPropulsionPowerSource>(Component))
		{
			PowerSourceComponent = Component;
			++PowerCount;
		}
	}

	if (SupplyCount > 1 || PowerCount > 1)
	{
		UE_LOG(
			LogEdenFlight,
			Error,
			TEXT("%s found %d propellant supplies and %d power sources on '%s'; exactly one of each is allowed. Ignoring the ambiguous set."),
			*GetNameSafe(this),
			SupplyCount,
			PowerCount,
			*GetNameSafe(OwnerActor));
		if (SupplyCount > 1)
		{
			PropellantSupplyComponent.Reset();
		}
		if (PowerCount > 1)
		{
			PowerSourceComponent.Reset();
		}
	}

	return PropellantSupplyComponent.IsValid() || PowerSourceComponent.IsValid();
}

IEdenPropellantSupply* UEdenFlightMovementComponent::GetPropellantSupply() const
{
	return Cast<IEdenPropellantSupply>(PropellantSupplyComponent.Get());
}

const IEdenPropulsionPowerSource* UEdenFlightMovementComponent::GetPowerSource() const
{
	return Cast<IEdenPropulsionPowerSource>(PowerSourceComponent.Get());
}

bool UEdenFlightMovementComponent::IsPropellantMetered() const
{
	const IEdenPropellantSupply* Supply = GetPropellantSupply();
	return Supply && Supply->IsPropellantSupplyAvailable();
}

bool UEdenFlightMovementComponent::MetersPropellantDirectly() const
{
	return IsPropellantMetered();
}

float UEdenFlightMovementComponent::GetPowerThrustFraction() const
{
	const IEdenPropulsionPowerSource* Power = GetPowerSource();
	if (!Power || !Power->IsPropulsionPowerSourceAvailable())
	{
		return 1.0f;
	}

	return static_cast<float>(
		FEdenFlightMovementModel::CalculatePowerThrustFraction(Power->GetPropulsionPowerState(), GetActivePropulsionSettings()));
}

float UEdenFlightMovementComponent::GetPropellantMassKilograms() const
{
	const IEdenPropellantSupply* Supply = IsPropellantMetered() ? GetPropellantSupply() : nullptr;
	return Supply ? static_cast<float>(Supply->GetPropellantMassKilograms()) : 0.0f;
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

FEdenFlightAttitudeSettings UEdenFlightMovementComponent::GetActiveAttitudeSettings() const
{
	const double FixedStepSeconds = FEdenFlightMovementModel::ValidateStepSettings(StepSettings)
		? StepSettings.GetFixedStepSeconds()
		: FEdenFlightStepSettings().GetFixedStepSeconds();
	return FEdenFlightMovementModel::ValidateAttitudeSettings(AttitudeSettings, FixedStepSeconds)
		? AttitudeSettings
		: FEdenFlightAttitudeSettings();
}

FEdenFlightAttitudeSettings UEdenFlightMovementComponent::ResolveAttitudeSettings(double FixedStepSeconds)
{
	TArray<FString> Errors;
	if (FEdenFlightMovementModel::ValidateAttitudeSettings(AttitudeSettings, FixedStepSeconds, &Errors))
	{
		bLoggedInvalidAttitudeSettings = false;
		return AttitudeSettings;
	}

	if (!bLoggedInvalidAttitudeSettings)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s has invalid AttitudeSettings; using C++ defaults. %s"),
			*GetNameSafe(this),
			*FString::Join(Errors, TEXT(" ")));
		bLoggedInvalidAttitudeSettings = true;
	}

	return FEdenFlightAttitudeSettings();
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
	return GetThrustAuthority() * GetPowerThrustFraction();
}

float UEdenFlightMovementComponent::GetMassKilograms() const
{
	return GetActivePropulsionSettings().DryMassKilograms + GetPropellantMassKilograms();
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
	Snapshot.AccelerationWorldMetersPerSecondSquared = LastStep.Translation.AccelerationWorldMetersPerSecondSquared;
	Snapshot.RequestedBodyForceNewtons = LastStep.RequestedBodyForceNewtons;
	Snapshot.AppliedBodyForceNewtons = LastStep.AppliedBodyForceNewtons;
	Snapshot.RequestedThrusterOutputNewtons = static_cast<float>(LastStep.RequestedThrusterOutputNewtons);
	Snapshot.DeliveredThrusterOutputNewtons = static_cast<float>(LastStep.DeliveredThrusterOutputNewtons);
	Snapshot.PropellantBurnFraction = static_cast<float>(LastStep.PropellantBurnFraction);
	Snapshot.bPowerLimited = LastStep.bPowerLimited;
	Snapshot.bPropellantMetered = IsPropellantMetered();

	const FEdenFlightPropulsionSettings ActivePropulsion = GetActivePropulsionSettings();
	Snapshot.PropellantFlowKilogramsPerSecond = Snapshot.bPropellantMetered
		? static_cast<float>(FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(
			  LastStep.DeliveredThrusterOutputNewtons,
			  ActivePropulsion))
		: 0.0f;
	Snapshot.PowerThrustFraction = GetPowerThrustFraction();
	Snapshot.DryMassKilograms = ActivePropulsion.DryMassKilograms;
	Snapshot.PropellantMassKilograms = GetPropellantMassKilograms();

	// Available now, even before the first step: operator x power, and nothing at all from an empty tank.
	const bool bTankEmpty = Snapshot.bPropellantMetered && Snapshot.PropellantMassKilograms <= 0.0f;
	Snapshot.bPropellantLimited = LastStep.bPropellantLimited || bTankEmpty;
	const double AvailableAuthority = bTankEmpty ? 0.0 : static_cast<double>(GetEffectiveThrustAuthority());
	const FEdenFlightThrustEnvelope Envelope = FEdenFlightMovementModel::MakeThrustEnvelope(ActivePropulsion, AvailableAuthority);
	Snapshot.AvailablePositiveThrustNewtons = Envelope.PositiveNewtons;
	Snapshot.AvailableNegativeThrustNewtons = Envelope.NegativeNewtons;

	const FEdenFlightAttitudeSettings ActiveAttitude = GetActiveAttitudeSettings();
	FEdenFlightRotationState RotationState;
	RotationState.Orientation = UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;
	RotationState.AngularVelocityBodyRadiansPerSecond = AngularVelocityBodyRadiansPerSecond;
	const FVector Inertia = ActiveAttitude.PrincipalMomentsOfInertiaKilogramMetersSquared
		* FEdenFlightMovementModel::CalculateInertiaScale(GetMassKilograms(), ActivePropulsion.DryMassKilograms);
	Snapshot.OrientationWorld = RotationState.Orientation;
	Snapshot.AngularVelocityBodyRadiansPerSecond = AngularVelocityBodyRadiansPerSecond;
	Snapshot.AngularAccelerationBodyRadiansPerSecondSquared = LastStep.Rotation.AngularAccelerationBodyRadiansPerSecondSquared;
	Snapshot.RequestedBodyTorqueNewtonMeters = LastStep.RequestedBodyTorqueNewtonMeters;
	Snapshot.AppliedBodyTorqueNewtonMeters = LastStep.AppliedBodyTorqueNewtonMeters;
	Snapshot.AvailableTorqueNewtonMeters = FEdenFlightMovementModel::MakeTorqueEnvelope(ActiveAttitude, AvailableAuthority).MaxNewtonMeters;
	Snapshot.AngularMomentumWorld = FEdenFlightMovementModel::CalculateAngularMomentumWorld(RotationState, Inertia);
	Snapshot.RotationalKineticEnergyJoules =
		static_cast<float>(FEdenFlightMovementModel::CalculateRotationalKineticEnergy(RotationState, Inertia));

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

FVector UEdenFlightMovementComponent::GetAngularVelocityBodyRadiansPerSecond() const
{
	return AngularVelocityBodyRadiansPerSecond;
}

void UEdenFlightMovementComponent::SetAngularVelocityBodyRadiansPerSecond(FVector NewAngularVelocityBodyRadiansPerSecond)
{
	if (!FEdenFlightMovementModel::IsFiniteVector(NewAngularVelocityBodyRadiansPerSecond))
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s rejected non-finite angular velocity."),
			*GetNameSafe(this));
		AngularVelocityBodyRadiansPerSecond = FVector::ZeroVector;
		return;
	}

	AngularVelocityBodyRadiansPerSecond = NewAngularVelocityBodyRadiansPerSecond;
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
