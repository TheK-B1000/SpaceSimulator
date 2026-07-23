// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenSpacecraftPawn.h"

#include "Components/SphereComponent.h"
#include "Core/EdenLogCategories.h"
#include "Engine/CollisionProfile.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightTypes.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"

AEdenSpacecraftPawn::AEdenSpacecraftPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	RequiredCollisionRoot = CreateDefaultSubobject<USphereComponent>(TEXT("RequiredCollisionRoot"));
	SetRootComponent(RequiredCollisionRoot);
	RequiredCollisionRoot->InitSphereRadius(100.0f);
	RequiredCollisionRoot->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	RequiredCollisionRoot->SetMobility(EComponentMobility::Movable);
	RequiredCollisionRoot->SetGenerateOverlapEvents(false);

	FlightMovementComponent = CreateDefaultSubobject<UEdenFlightMovementComponent>(TEXT("FlightMovementComponent"));
	FlightMovementComponent->SetUpdatedComponent(RequiredCollisionRoot);

	FuelSystemComponent = CreateDefaultSubobject<UEdenFuelSystemComponent>(TEXT("FuelSystem"));
	PowerSystemComponent = CreateDefaultSubobject<UEdenPowerSystemComponent>(TEXT("PowerSystem"));
	ThermalSystemComponent = CreateDefaultSubobject<UEdenThermalSystemComponent>(TEXT("ThermalSystem"));
}

UPawnMovementComponent* AEdenSpacecraftPawn::GetMovementComponent() const
{
	return FlightMovementComponent;
}

void AEdenSpacecraftPawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	RestoreRequiredCollisionRoot();

	if (FlightMovementComponent)
	{
		FlightMovementComponent->SetUpdatedComponent(RequiredCollisionRoot);
		FlightMovementComponent->ResetFlightMovement();
	}
}

void AEdenSpacecraftPawn::ApplyFlightInputCommand(const FEdenFlightInputCommand& Command, float DeltaTimeSeconds)
{
	if (!FlightMovementComponent)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s cannot apply flight command without a movement component."), *GetNameSafe(this));
		return;
	}

	RestoreRequiredCollisionRoot();
	FlightMovementComponent->MoveWithCommand(Command, DeltaTimeSeconds);
}

void AEdenSpacecraftPawn::ResetFlightState()
{
	if (FlightMovementComponent)
	{
		FlightMovementComponent->ResetFlightMovement();
	}
}

USphereComponent* AEdenSpacecraftPawn::GetRequiredCollisionRoot() const
{
	return RequiredCollisionRoot;
}

UEdenFlightMovementComponent* AEdenSpacecraftPawn::GetFlightMovementComponent() const
{
	return FlightMovementComponent;
}

UEdenFuelSystemComponent* AEdenSpacecraftPawn::GetFuelSystemComponent() const
{
	return FuelSystemComponent;
}

UEdenPowerSystemComponent* AEdenSpacecraftPawn::GetPowerSystemComponent() const
{
	return PowerSystemComponent;
}

UEdenThermalSystemComponent* AEdenSpacecraftPawn::GetThermalSystemComponent() const
{
	return ThermalSystemComponent;
}

void AEdenSpacecraftPawn::RestoreRequiredCollisionRoot()
{
	if (!RequiredCollisionRoot)
	{
		UE_LOG(LogEdenFlight, Error, TEXT("%s is missing the required C++ sphere collision root."), *GetNameSafe(this));
		return;
	}

	if (GetRootComponent() != RequiredCollisionRoot)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s restored the required C++ sphere collision root. Blueprint composition may tune it but may not replace it."),
			*GetNameSafe(this));
		SetRootComponent(RequiredCollisionRoot);
	}
}
