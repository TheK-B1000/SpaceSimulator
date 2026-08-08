#include "Flight/EdenFlightHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Systems/EdenResourceDebugTypes.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING
namespace EdenFlightHUDDebug
{
const FName EdenSystemsDebugName(TEXT("EdenSystems"));
const FName EdenMissionDebugName(TEXT("EdenMission"));

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
	if (ShouldDisplayDebug(EdenFlightHUDDebug::EdenMissionDebugName))
	{
		DrawEdenMissionOverlay();
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

	const UEdenMissionSubsystem* MissionSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UEdenMissionSubsystem>() : nullptr;
	if (MissionSubsystem && MissionSubsystem->GetMissionState() != EEdenMissionState::Inactive)
	{
		DrawLine(
			FString::Printf(
				TEXT("Mission: ID='%s' State=%s Phase=%s Elapsed=%.2fs Running=%s"),
				*MissionSubsystem->GetActiveMissionId().ToString(),
				*EdenFlightHUDDebug::EnumText(MissionSubsystem->GetMissionState()),
				*EdenFlightHUDDebug::EnumText(MissionSubsystem->GetMissionPhase()),
				MissionSubsystem->GetMissionElapsedTimeSeconds(),
				*EdenFlightHUDDebug::BoolText(MissionSubsystem->IsMissionRunning())),
			FColor::Magenta);
	}
}

void AEdenFlightHUD::DrawEdenMissionOverlay()
{
	if (!Canvas || !PlayerOwner)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	if (!MissionSubsystem)
	{
		Canvas->SetDrawColor(FColor::Red);
		Canvas->DrawText(
			GEngine ? GEngine->GetSmallFont() : nullptr,
			TEXT("Eden Mission: UEdenMissionSubsystem not available in world"),
			32.0f,
			140.0f);
		return;
	}

	const FEdenMissionStateSnapshot Snapshot = MissionSubsystem->GetMissionStateSnapshot();
	const FEdenMissionRuntimeState RuntimeState = MissionSubsystem->GetMissionRuntimeState();
	const FEdenMissionDefinitionConfig DefConfig = MissionSubsystem->GetActiveMissionDefinition();

	const UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	float Y = 140.0f;
	const float X = 32.0f;
	const float LineHeight = 16.0f;

	auto DrawLine = [this, Font, X, &Y, LineHeight](const FString& Text, const FColor& Color)
	{
		Canvas->SetDrawColor(Color);
		Canvas->DrawText(Font, Text, X, Y);
		Y += LineHeight;
	};

	DrawLine(TEXT("Eden Mission (ShowDebug EdenMission)"), FColor::Magenta);
	DrawLine(
		FString::Printf(
			TEXT("Mission: ID='%s' State=%s Phase=%s Elapsed=%.2fs Running=%s"),
			*Snapshot.ActiveMissionId.ToString(),
			*EdenFlightHUDDebug::EnumText(Snapshot.MissionState),
			*EdenFlightHUDDebug::EnumText(Snapshot.MissionPhase),
			Snapshot.MissionElapsedTimeSeconds,
			*EdenFlightHUDDebug::BoolText(MissionSubsystem->IsMissionRunning())),
		FColor::White);

	if (DefConfig.Objectives.Num() > 0)
	{
		DrawLine(TEXT("Objectives:"), FColor::Cyan);
		for (const FEdenMissionObjectiveConfig& ObjConfig : DefConfig.Objectives)
		{
			EEdenObjectiveState ObjState = EEdenObjectiveState::Pending;
			for (const FEdenMissionObjectiveRuntime& RuntimeObj : RuntimeState.ObjectiveStates)
			{
				if (RuntimeObj.ObjectiveId == ObjConfig.ObjectiveId)
				{
					ObjState = RuntimeObj.State;
					break;
				}
			}

			FColor ObjColor = FColor::White;
			if (ObjState == EEdenObjectiveState::Completed)
			{
				ObjColor = FColor::Green;
			}
			else if (ObjState == EEdenObjectiveState::Failed)
			{
				ObjColor = FColor::Red;
			}
			else if (ObjState == EEdenObjectiveState::Active)
			{
				ObjColor = FColor::Yellow;
			}

			DrawLine(
				FString::Printf(
					TEXT("  [%s] '%s' (%s Target=%.1f State=%s)"),
					ObjConfig.bRequired ? TEXT("REQ") : TEXT("OPT"),
					*ObjConfig.ObjectiveId.ToString(),
					*EdenFlightHUDDebug::EnumText(ObjConfig.ObjectiveType),
					ObjConfig.TargetValue,
					*EdenFlightHUDDebug::EnumText(ObjState)),
				ObjColor);
		}
	}

	if (DefConfig.Events.Num() > 0)
	{
		DrawLine(TEXT("Timeline Events:"), FColor::Orange);
		for (const FEdenMissionEventConfig& EvtConfig : DefConfig.Events)
		{
			EEdenMissionEventState EvtState = EEdenMissionEventState::Pending;
			for (const FEdenMissionEventRuntime& RuntimeEvt : RuntimeState.EventStates)
			{
				if (RuntimeEvt.EventId == EvtConfig.EventId)
				{
					EvtState = RuntimeEvt.EventState;
					break;
				}
			}

			DrawLine(
				FString::Printf(
					TEXT("  T=%.1fs [%s] '%s' (Cmd=%s Param=%.1f)"),
					EvtConfig.TriggerTimeSeconds,
					*EdenFlightHUDDebug::EnumText(EvtState),
					*EvtConfig.EventId.ToString(),
					*EdenFlightHUDDebug::EnumText(EvtConfig.CommandType),
					EvtConfig.FloatParameter),
				EvtState == EEdenMissionEventState::Executed ? FColor::Green : FColor::Silver);
		}
	}
}
#endif
