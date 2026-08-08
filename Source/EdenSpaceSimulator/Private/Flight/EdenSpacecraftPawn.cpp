// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenSpacecraftPawn.h"

#include "Components/SphereComponent.h"
#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Engine/CollisionProfile.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightTypes.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"

#if !UE_BUILD_SHIPPING
#include "DisplayDebugHelpers.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"
#include "UObject/UnrealType.h"
#endif

#if !UE_BUILD_SHIPPING
namespace EdenSpacecraftPawnDebug
{
const FName EdenSystemsDebugName(TEXT("EdenSystems"));

FString BoolText(bool bValue)
{
	return bValue ? TEXT("true") : TEXT("false");
}

template <typename TEnum>
FString EnumText(TEnum Value)
{
	return UEnum::GetValueAsString(Value);
}

void DrawLine(UCanvas* Canvas, const FString& Text, const FColor& Color)
{
	if (!Canvas)
	{
		return;
	}

	Canvas->DisplayDebugManager.SetDrawColor(Color);
	Canvas->DisplayDebugManager.DrawString(Text);
}

void DrawEdenSystems(UCanvas* Canvas, const FEdenSpacecraftSystemsDebugSnapshot& Snapshot)
{
	DrawLine(Canvas, TEXT("Eden Systems"), FColor::Green);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Clock: Available=%s Elapsed=%.2fs FixedStep=%.3fs Paused=%s Subscribers=%d LastDroppedSteps=%d"),
			*BoolText(Snapshot.Clock.bClockAvailable),
			Snapshot.Clock.ElapsedSimulationTimeSeconds,
			Snapshot.Clock.FixedStepSeconds,
			*BoolText(Snapshot.Clock.bPaused),
			Snapshot.Clock.SubscriberCount,
			Snapshot.Clock.LastDroppedSteps),
		FColor::White);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Fuel: Component=%s %.2f / %.2f kg (%.1f%%) Demand=%.2f State=%s ConfigValid=%s ClockRegistered=%s"),
			*BoolText(Snapshot.Fuel.bComponentAvailable),
			Snapshot.Fuel.FuelQuantityKilograms,
			Snapshot.Fuel.CapacityKilograms,
			Snapshot.Fuel.FuelPercent,
			Snapshot.Fuel.PropulsionDemandNormalized,
			*EnumText(Snapshot.Fuel.FuelState),
			*BoolText(Snapshot.Fuel.bConfigurationValid),
			*BoolText(Snapshot.Fuel.bRegisteredWithClock)),
		FColor::Cyan);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Power: Component=%s %.3f / %.3f kWh (%.1f%%) Gen=%.2f kW Demand=%.2f kW Net=%.2f kW State=%s ConfigValid=%s ClockRegistered=%s"),
			*BoolText(Snapshot.Power.bComponentAvailable),
			Snapshot.Power.BatteryChargeKilowattHours,
			Snapshot.Power.BatteryCapacityKilowattHours,
			Snapshot.Power.ChargePercent,
			Snapshot.Power.GenerationKilowatts,
			Snapshot.Power.DemandKilowatts,
			Snapshot.Power.NetPowerKilowatts,
			*EnumText(Snapshot.Power.PowerState),
			*BoolText(Snapshot.Power.bConfigurationValid),
			*BoolText(Snapshot.Power.bRegisteredWithClock)),
		FColor::Yellow);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Thermal: Component=%s %.2f C Ambient=%.2f C Heat=%.2f C/s Dissipation=%.2f C/s State=%s ConfigValid=%s ClockRegistered=%s"),
			*BoolText(Snapshot.Thermal.bComponentAvailable),
			Snapshot.Thermal.TemperatureCelsius,
			Snapshot.Thermal.AmbientTemperatureCelsius,
			Snapshot.Thermal.HeatGenerationDegreesCelsiusPerSecond,
			Snapshot.Thermal.DissipationDegreesCelsiusPerSecond,
			*EnumText(Snapshot.Thermal.ThermalState),
			*BoolText(Snapshot.Thermal.bConfigurationValid),
			*BoolText(Snapshot.Thermal.bRegisteredWithClock)),
		FColor::Orange);
}
}
#endif

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
	OperatorControlComponent = CreateDefaultSubobject<UEdenOperatorControlComponent>(TEXT("OperatorControl"));
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

#if !UE_BUILD_SHIPPING
void AEdenSpacecraftPawn::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	if (!Canvas || !DebugDisplay.IsDisplayOn(EdenSpacecraftPawnDebug::EdenSystemsDebugName))
	{
		return;
	}

	EdenSpacecraftPawnDebug::DrawEdenSystems(Canvas, GetEdenSystemsDebugSnapshot());
	YL = Canvas->DisplayDebugManager.GetMaxCharHeight();
	YPos = Canvas->DisplayDebugManager.GetYPos();
}
#endif

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

	if (OperatorControlComponent)
	{
		OperatorControlComponent->ResetOperatorControl();
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

UEdenOperatorControlComponent* AEdenSpacecraftPawn::GetOperatorControlComponent() const
{
	return OperatorControlComponent;
}

FEdenSpacecraftSystemsDebugSnapshot AEdenSpacecraftPawn::GetEdenSystemsDebugSnapshot() const
{
	FEdenSpacecraftSystemsDebugSnapshot Snapshot;

	if (const UWorld* World = GetWorld())
	{
		if (const UEdenSimulationClockSubsystem* SimulationClock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			Snapshot.Clock = SimulationClock->GetSimulationClockDebugSnapshot();
		}
	}

	if (FuelSystemComponent)
	{
		Snapshot.Fuel = FuelSystemComponent->GetFuelDebugSnapshot();
	}

	if (PowerSystemComponent)
	{
		Snapshot.Power = PowerSystemComponent->GetPowerDebugSnapshot();
	}

	if (ThermalSystemComponent)
	{
		Snapshot.Thermal = ThermalSystemComponent->GetThermalDebugSnapshot();
	}

	return Snapshot;
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
