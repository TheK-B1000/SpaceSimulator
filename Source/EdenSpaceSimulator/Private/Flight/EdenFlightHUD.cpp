// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Systems/EdenResourceDebugTypes.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING
namespace EdenFlightHUDDebug
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
}
#endif

AEdenFlightHUD::AEdenFlightHUD()
{
	bShowHUD = true;
}

void AEdenFlightHUD::DrawHUD()
{
	Super::DrawHUD();

#if !UE_BUILD_SHIPPING
	if (ShouldDisplayDebug(EdenFlightHUDDebug::EdenSystemsDebugName))
	{
		DrawEdenSystemsOverlay();
	}
#endif
}

#if !UE_BUILD_SHIPPING
void AEdenFlightHUD::DrawEdenSystemsOverlay()
{
	if (!Canvas || !PlayerOwner)
	{
		return;
	}

	const AEdenSpacecraftPawn* SpacecraftPawn = Cast<AEdenSpacecraftPawn>(PlayerOwner->GetPawn());
	if (!SpacecraftPawn)
	{
		Canvas->SetDrawColor(FColor::Red);
		Canvas->DrawText(
			GEngine ? GEngine->GetSmallFont() : nullptr,
			TEXT("Eden Systems: no AEdenSpacecraftPawn possessed"),
			32.0f,
			48.0f);
		return;
	}

	const FEdenSpacecraftSystemsDebugSnapshot Snapshot = SpacecraftPawn->GetEdenSystemsDebugSnapshot();
	const UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	float Y = 48.0f;
	const float X = 32.0f;
	const float LineHeight = 16.0f;

	auto DrawLine = [this, Font, X, &Y, LineHeight](const FString& Text, const FColor& Color)
	{
		Canvas->SetDrawColor(Color);
		Canvas->DrawText(Font, Text, X, Y);
		Y += LineHeight;
	};

	DrawLine(TEXT("Eden Systems (ShowDebug EdenSystems)"), FColor::Green);
	DrawLine(
		FString::Printf(
			TEXT("Clock: Available=%s Elapsed=%.2fs FixedStep=%.3fs Paused=%s Subscribers=%d"),
			*EdenFlightHUDDebug::BoolText(Snapshot.Clock.bClockAvailable),
			Snapshot.Clock.ElapsedSimulationTimeSeconds,
			Snapshot.Clock.FixedStepSeconds,
			*EdenFlightHUDDebug::BoolText(Snapshot.Clock.bPaused),
			Snapshot.Clock.SubscriberCount),
		FColor::White);
	DrawLine(
		FString::Printf(
			TEXT("Fuel: %.2f / %.2f kg (%.1f%%) Demand=%.2f State=%s ConfigValid=%s ClockRegistered=%s"),
			Snapshot.Fuel.FuelQuantityKilograms,
			Snapshot.Fuel.CapacityKilograms,
			Snapshot.Fuel.FuelPercent,
			Snapshot.Fuel.PropulsionDemandNormalized,
			*EdenFlightHUDDebug::EnumText(Snapshot.Fuel.FuelState),
			*EdenFlightHUDDebug::BoolText(Snapshot.Fuel.bConfigurationValid),
			*EdenFlightHUDDebug::BoolText(Snapshot.Fuel.bRegisteredWithClock)),
		FColor::Cyan);
	DrawLine(
		FString::Printf(
			TEXT("Power: %.3f / %.3f kWh (%.1f%%) Gen=%.2f Demand=%.2f Net=%.2f State=%s"),
			Snapshot.Power.BatteryChargeKilowattHours,
			Snapshot.Power.BatteryCapacityKilowattHours,
			Snapshot.Power.ChargePercent,
			Snapshot.Power.GenerationKilowatts,
			Snapshot.Power.DemandKilowatts,
			Snapshot.Power.NetPowerKilowatts,
			*EdenFlightHUDDebug::EnumText(Snapshot.Power.PowerState)),
		FColor::Yellow);
	DrawLine(
		FString::Printf(
			TEXT("Thermal: %.2f C Ambient=%.2f C Heat=%.2f C/s Diss=%.2f C/s State=%s"),
			Snapshot.Thermal.TemperatureCelsius,
			Snapshot.Thermal.AmbientTemperatureCelsius,
			Snapshot.Thermal.HeatGenerationDegreesCelsiusPerSecond,
			Snapshot.Thermal.DissipationDegreesCelsiusPerSecond,
			*EdenFlightHUDDebug::EnumText(Snapshot.Thermal.ThermalState)),
		FColor::Orange);
}
#endif
