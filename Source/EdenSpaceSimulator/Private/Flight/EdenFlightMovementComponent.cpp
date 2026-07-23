// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementComponent.h"

#include "Components/SphereComponent.h"
#include "Core/EdenLogCategories.h"
#include "Flight/EdenFlightMovementModel.h"

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

void UEdenFlightMovementComponent::MoveWithCommand(const FEdenFlightInputCommand& Command, float DeltaTimeSeconds)
{
	if (!ValidateUpdatedComponentForFlight())
	{
		return;
	}

	if (!FEdenFlightMovementModel::IsValidDeltaTime(DeltaTimeSeconds))
	{
		return;
	}

	FEdenFlightVelocityState CurrentVelocityState;
	CurrentVelocityState.LinearVelocityWorldCmPerSecond = Velocity;
	CurrentVelocityState.AngularVelocityLocalDegreesPerSecond = AngularVelocityLocalDegreesPerSecond;

	const FEdenFlightIntegrationResult IntegrationResult = FEdenFlightMovementModel::IntegrateVelocity(
		CurrentVelocityState,
		Command,
		MovementSettings,
		UpdatedComponent->GetComponentQuat(),
		DeltaTimeSeconds);

	LogInvalidInputState(IntegrationResult.bInputWasSanitized);

	PropulsionDemandNormalized = FMath::Clamp(IntegrationResult.SanitizedCommand.TranslationInput.Size(), 0.0f, 1.0f);
	Velocity = IntegrationResult.VelocityState.LinearVelocityWorldCmPerSecond;
	AngularVelocityLocalDegreesPerSecond = IntegrationResult.VelocityState.AngularVelocityLocalDegreesPerSecond;

	FHitResult Hit;
	SafeMoveUpdatedComponent(Velocity * DeltaTimeSeconds, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (Hit.IsValidBlockingHit())
	{
		Velocity = FEdenFlightMovementModel::RemoveInwardVelocity(Velocity, Hit.Normal);
		LogBlockingHitState(Hit);

		if (!Velocity.IsNearlyZero())
		{
			FHitResult SlideHit;
			const FVector SlideDelta = Velocity * DeltaTimeSeconds;
			SlideAlongSurface(SlideDelta, 1.0f - Hit.Time, Hit.Normal, SlideHit, true);
		}
	}
	else
	{
		bWasBlockedLastMove = false;
	}

	ApplyRotation(DeltaTimeSeconds);
}

void UEdenFlightMovementComponent::ResetFlightMovement()
{
	StopMovementImmediately();
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

void UEdenFlightMovementComponent::ApplyRotation(float DeltaTimeSeconds)
{
	if (!UpdatedComponent || AngularVelocityLocalDegreesPerSecond.IsNearlyZero())
	{
		return;
	}

	const FRotator DeltaRotation(
		AngularVelocityLocalDegreesPerSecond.X * DeltaTimeSeconds,
		AngularVelocityLocalDegreesPerSecond.Y * DeltaTimeSeconds,
		AngularVelocityLocalDegreesPerSecond.Z * DeltaTimeSeconds);
	const FQuat NewRotation = (UpdatedComponent->GetComponentQuat() * DeltaRotation.Quaternion()).GetNormalized();

	FHitResult RotationHit;
	SafeMoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, RotationHit);
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
