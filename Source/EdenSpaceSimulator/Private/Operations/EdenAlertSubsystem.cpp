// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenAlertSubsystem.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Engine/World.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"

bool UEdenAlertSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}

	return false;
}

void UEdenAlertSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureBound();
}

void UEdenAlertSubsystem::Deinitialize()
{
	UnbindFromTargets();
	ActiveAlerts.Reset();
	Super::Deinitialize();
}

TArray<FEdenAlert> UEdenAlertSubsystem::GetActiveAlerts() const
{
	const_cast<UEdenAlertSubsystem*>(this)->EnsureBound();
	return ActiveAlerts;
}

bool UEdenAlertSubsystem::AcknowledgeAlert(FName AlertId)
{
	for (FEdenAlert& Alert : ActiveAlerts)
	{
		if (Alert.AlertId == AlertId)
		{
			Alert.bAcknowledged = true;
			return true;
		}
	}

	return false;
}

void UEdenAlertSubsystem::ClearAllAlerts()
{
	TArray<FName> AlertIds;
	AlertIds.Reserve(ActiveAlerts.Num());
	for (const FEdenAlert& Alert : ActiveAlerts)
	{
		AlertIds.Add(Alert.AlertId);
	}

	ActiveAlerts.Reset();
	for (const FName AlertId : AlertIds)
	{
		OnAlertCleared.Broadcast(AlertId);
	}
}

void UEdenAlertSubsystem::BindToTargets()
{
	UnbindFromTargets();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (AEdenSpacecraftPawn* Pawn = Cast<AEdenSpacecraftPawn>(PC->GetPawn()))
		{
			if (UEdenFuelSystemComponent* Fuel = Pawn->GetFuelSystemComponent())
			{
				BoundFuel = Fuel;
				Fuel->OnFuelStateChanged.AddDynamic(this, &UEdenAlertSubsystem::HandleFuelStateChanged);
			}
			if (UEdenPowerSystemComponent* Power = Pawn->GetPowerSystemComponent())
			{
				BoundPower = Power;
				Power->OnPowerStateChanged.AddDynamic(this, &UEdenAlertSubsystem::HandlePowerStateChanged);
			}
			if (UEdenThermalSystemComponent* Thermal = Pawn->GetThermalSystemComponent())
			{
				BoundThermal = Thermal;
				Thermal->OnThermalStateChanged.AddDynamic(this, &UEdenAlertSubsystem::HandleThermalStateChanged);
			}
		}
	}

	if (UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>())
	{
		BoundMission = Mission;
		Mission->OnMissionStateChanged.AddDynamic(this, &UEdenAlertSubsystem::HandleMissionStateChanged);
	}
}

void UEdenAlertSubsystem::EnsureBound()
{
	if (!BoundFuel.IsValid() || !BoundPower.IsValid() || !BoundThermal.IsValid() || !BoundMission.IsValid())
	{
		BindToTargets();
	}
}

void UEdenAlertSubsystem::UnbindFromTargets()
{
	if (UEdenFuelSystemComponent* Fuel = BoundFuel.Get())
	{
		Fuel->OnFuelStateChanged.RemoveDynamic(this, &UEdenAlertSubsystem::HandleFuelStateChanged);
	}
	if (UEdenPowerSystemComponent* Power = BoundPower.Get())
	{
		Power->OnPowerStateChanged.RemoveDynamic(this, &UEdenAlertSubsystem::HandlePowerStateChanged);
	}
	if (UEdenThermalSystemComponent* Thermal = BoundThermal.Get())
	{
		Thermal->OnThermalStateChanged.RemoveDynamic(this, &UEdenAlertSubsystem::HandleThermalStateChanged);
	}
	if (UEdenMissionSubsystem* Mission = BoundMission.Get())
	{
		Mission->OnMissionStateChanged.RemoveDynamic(this, &UEdenAlertSubsystem::HandleMissionStateChanged);
	}

	BoundFuel.Reset();
	BoundPower.Reset();
	BoundThermal.Reset();
	BoundMission.Reset();
}

float UEdenAlertSubsystem::GetSimulationTimeSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			return Clock->GetElapsedSimulationTimeSeconds();
		}
	}

	return 0.0f;
}

void UEdenAlertSubsystem::RaiseAlert(
	FName AlertId,
	EEdenAlertSeverity Severity,
	FName SourceSystem,
	const FText& DisplayText)
{
	for (const FEdenAlert& Existing : ActiveAlerts)
	{
		if (Existing.AlertId == AlertId)
		{
			return;
		}
	}

	FEdenAlert Alert;
	Alert.AlertId = AlertId;
	Alert.Severity = Severity;
	Alert.DisplayText = DisplayText;
	Alert.SourceSystem = SourceSystem;
	Alert.RaisedAtSimTimeSeconds = GetSimulationTimeSeconds();
	Alert.bAcknowledged = false;
	ActiveAlerts.Add(Alert);
	EnforceAlertCap();
	OnAlertRaised.Broadcast(Alert);
}

void UEdenAlertSubsystem::ClearAlert(FName AlertId)
{
	const int32 Removed = ActiveAlerts.RemoveAll([AlertId](const FEdenAlert& Alert) { return Alert.AlertId == AlertId; });
	if (Removed > 0)
	{
		OnAlertCleared.Broadcast(AlertId);
	}
}

void UEdenAlertSubsystem::EnforceAlertCap()
{
	while (ActiveAlerts.Num() > MaxActiveAlerts)
	{
		int32 EvictIndex = INDEX_NONE;
		for (int32 Index = 0; Index < ActiveAlerts.Num(); ++Index)
		{
			if (ActiveAlerts[Index].Severity == EEdenAlertSeverity::Info)
			{
				EvictIndex = Index;
				break;
			}
		}

		if (EvictIndex == INDEX_NONE)
		{
			for (int32 Index = 0; Index < ActiveAlerts.Num(); ++Index)
			{
				if (ActiveAlerts[Index].Severity == EEdenAlertSeverity::Warning)
				{
					EvictIndex = Index;
					break;
				}
			}
		}

		if (EvictIndex == INDEX_NONE)
		{
			UE_LOG(
				LogEdenOperations,
				Error,
				TEXT("Alert list exceeded cap %d with only Critical/Emergency alerts; refusing silent drop."),
				MaxActiveAlerts);
			break;
		}

		const FName EvictedId = ActiveAlerts[EvictIndex].AlertId;
		ActiveAlerts.RemoveAt(EvictIndex);
		OnAlertCleared.Broadcast(EvictedId);
	}
}

void UEdenAlertSubsystem::HandleFuelStateChanged(EEdenFuelState PreviousState, EEdenFuelState NewState)
{
	(void)PreviousState;
	if (NewState == EEdenFuelState::Warning)
	{
		RaiseAlert(TEXT("FuelWarning"), EEdenAlertSeverity::Warning, TEXT("Fuel"), FText::FromString(TEXT("Fuel warning")));
	}
	else if (NewState == EEdenFuelState::Critical || NewState == EEdenFuelState::Depleted)
	{
		RaiseAlert(TEXT("FuelCritical"), EEdenAlertSeverity::Critical, TEXT("Fuel"), FText::FromString(TEXT("Fuel critical")));
	}
	else if (NewState == EEdenFuelState::Normal)
	{
		ClearAlert(TEXT("FuelWarning"));
		ClearAlert(TEXT("FuelCritical"));
	}
}

void UEdenAlertSubsystem::HandlePowerStateChanged(EEdenPowerState PreviousState, EEdenPowerState NewState)
{
	(void)PreviousState;
	if (NewState == EEdenPowerState::Warning)
	{
		RaiseAlert(TEXT("PowerWarning"), EEdenAlertSeverity::Warning, TEXT("Power"), FText::FromString(TEXT("Battery warning")));
	}
	else if (NewState == EEdenPowerState::Critical || NewState == EEdenPowerState::Depleted)
	{
		RaiseAlert(TEXT("PowerCritical"), EEdenAlertSeverity::Critical, TEXT("Power"), FText::FromString(TEXT("Battery critical")));
	}
	else if (NewState == EEdenPowerState::Normal)
	{
		ClearAlert(TEXT("PowerWarning"));
		ClearAlert(TEXT("PowerCritical"));
	}
}

void UEdenAlertSubsystem::HandleThermalStateChanged(EEdenThermalState PreviousState, EEdenThermalState NewState)
{
	(void)PreviousState;
	if (NewState == EEdenThermalState::Warning)
	{
		RaiseAlert(TEXT("ThermalWarning"), EEdenAlertSeverity::Warning, TEXT("Thermal"), FText::FromString(TEXT("Thermal warning")));
	}
	else if (NewState == EEdenThermalState::Critical || NewState == EEdenThermalState::Overheated)
	{
		RaiseAlert(
			TEXT("ThermalCritical"),
			EEdenAlertSeverity::Critical,
			TEXT("Thermal"),
			FText::FromString(TEXT("Thermal critical")));
	}
	else if (NewState == EEdenThermalState::Normal)
	{
		ClearAlert(TEXT("ThermalWarning"));
		ClearAlert(TEXT("ThermalCritical"));
	}
}

void UEdenAlertSubsystem::HandleMissionStateChanged(EEdenMissionState PreviousState, EEdenMissionState NewState)
{
	(void)PreviousState;
	if (NewState == EEdenMissionState::Failed)
	{
		RaiseAlert(TEXT("MissionFailed"), EEdenAlertSeverity::Emergency, TEXT("Mission"), FText::FromString(TEXT("Mission failed")));
	}
	else if (NewState == EEdenMissionState::Succeeded)
	{
		RaiseAlert(TEXT("MissionSucceeded"), EEdenAlertSeverity::Info, TEXT("Mission"), FText::FromString(TEXT("Mission succeeded")));
	}
	else if (NewState == EEdenMissionState::Running)
	{
		ClearAlert(TEXT("MissionFailed"));
		ClearAlert(TEXT("MissionSucceeded"));
	}
}
