// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenPowerSystemComponent.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Systems/EdenPowerConfigDataAsset.h"

UEdenPowerSystemComponent::UEdenPowerSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEdenPowerSystemComponent::BeginPlay()
{
	Super::BeginPlay();

	if (InitializeFromConfiguredDataAsset())
	{
		RegisterWithSimulationClock();
	}
}

void UEdenPowerSystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSimulationClock();

	Super::EndPlay(EndPlayReason);
}

void UEdenPowerSystemComponent::AdvanceSimulation(float FixedDeltaSeconds)
{
	if (!bPowerSimulationEnabled)
	{
		return;
	}

	const FEdenPowerStepResult StepResult = FEdenPowerModel::Step(ActivePowerConfig, CurrentSnapshot, FixedDeltaSeconds);

	if (!StepResult.bConfigWasValid)
	{
		DisablePowerSimulation(TEXT("active power configuration became invalid during simulation"));
		return;
	}

	if (!StepResult.bDeltaTimeWasValid)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s rejected invalid FixedDeltaSeconds=%f; power state was not advanced."),
			*MakeLogContext(),
			FixedDeltaSeconds);
		return;
	}

	if (StepResult.bGenerationWasSanitized || StepResult.bBaselineDemandWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized power generation/demand to Generation=%f kW Demand=%f kW."),
			*MakeLogContext(),
			StepResult.Snapshot.GenerationKilowatts,
			StepResult.Snapshot.BaselineDemandKilowatts);
	}

	ApplySnapshot(StepResult.Snapshot, true);
}

bool UEdenPowerSystemComponent::InitializePowerSimulation(const FEdenPowerConfig& PowerConfig)
{
	if (!ValidateAndLogConfig(PowerConfig))
	{
		bHasValidPowerConfiguration = false;
		DisablePowerSimulation(TEXT("invalid explicit power configuration"));
		return false;
	}

	ActivePowerConfig = PowerConfig;
	bPowerSimulationEnabled = true;
	bHasValidPowerConfiguration = true;
	ApplySnapshot(FEdenPowerModel::MakeInitialSnapshot(ActivePowerConfig), false);

	return true;
}

bool UEdenPowerSystemComponent::ResetPowerState()
{
	if (!bPowerSimulationEnabled)
	{
		if (!InitializeFromConfiguredDataAsset())
		{
			UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot reset; power simulation is disabled."), *MakeLogContext());
			return false;
		}
	}

	ApplySnapshot(FEdenPowerModel::MakeInitialSnapshot(ActivePowerConfig), true);

	return true;
}

bool UEdenPowerSystemComponent::SetGenerationKilowatts(float GenerationKilowatts)
{
	if (!bPowerSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set generation; power simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bGenerationWasSanitized = false;
	const float SanitizedGenerationKilowatts =
		FEdenPowerModel::SanitizeNonnegativeKilowatts(GenerationKilowatts, &bGenerationWasSanitized);
	if (bGenerationWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized requested generation from %f kW to %f kW."),
			*MakeLogContext(),
			GenerationKilowatts,
			SanitizedGenerationKilowatts);
	}

	ApplySnapshot(
		FEdenPowerModel::MakeSnapshot(
			ActivePowerConfig,
			CurrentSnapshot.BatteryChargeKilowattHours,
			SanitizedGenerationKilowatts,
			CurrentSnapshot.BaselineDemandKilowatts),
		true);

	return !bGenerationWasSanitized;
}

bool UEdenPowerSystemComponent::SetBaselineDemandKilowatts(float BaselineDemandKilowatts)
{
	if (!bPowerSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set demand; power simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bDemandWasSanitized = false;
	const float SanitizedDemandKilowatts =
		FEdenPowerModel::SanitizeNonnegativeKilowatts(BaselineDemandKilowatts, &bDemandWasSanitized);
	if (bDemandWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized requested demand from %f kW to %f kW."),
			*MakeLogContext(),
			BaselineDemandKilowatts,
			SanitizedDemandKilowatts);
	}

	ApplySnapshot(
		FEdenPowerModel::MakeSnapshot(
			ActivePowerConfig,
			CurrentSnapshot.BatteryChargeKilowattHours,
			CurrentSnapshot.GenerationKilowatts,
			SanitizedDemandKilowatts),
		true);

	return !bDemandWasSanitized;
}

bool UEdenPowerSystemComponent::SetBatteryChargeKilowattHours(float BatteryChargeKilowattHours)
{
	if (!bPowerSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set battery charge; power simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bChargeWasSanitized = false;
	const float ClampedChargeKilowattHours = FEdenPowerModel::ClampBatteryChargeKilowattHours(
		BatteryChargeKilowattHours,
		ActivePowerConfig.BatteryCapacityKilowattHours,
		&bChargeWasSanitized);

	if (bChargeWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s clamped requested battery charge from %f kWh to %f kWh."),
			*MakeLogContext(),
			BatteryChargeKilowattHours,
			ClampedChargeKilowattHours);
	}

	ApplySnapshot(
		FEdenPowerModel::MakeSnapshot(
			ActivePowerConfig,
			ClampedChargeKilowattHours,
			CurrentSnapshot.GenerationKilowatts,
			CurrentSnapshot.BaselineDemandKilowatts),
		true);

	return !bChargeWasSanitized;
}

bool UEdenPowerSystemComponent::IsPowerSimulationEnabled() const
{
	return bPowerSimulationEnabled;
}

FEdenPowerConfig UEdenPowerSystemComponent::GetActivePowerConfig() const
{
	return ActivePowerConfig;
}

FEdenPowerStateSnapshot UEdenPowerSystemComponent::GetPowerStateSnapshot() const
{
	return CurrentSnapshot;
}

FEdenPowerDebugSnapshot UEdenPowerSystemComponent::GetPowerDebugSnapshot() const
{
	FEdenPowerDebugSnapshot Snapshot;
	Snapshot.bComponentAvailable = true;
	Snapshot.bConfigurationValid = bHasValidPowerConfiguration;
	Snapshot.bRegisteredWithClock = RegisteredSimulationClock.IsValid();
	Snapshot.BatteryChargeKilowattHours = CurrentSnapshot.BatteryChargeKilowattHours;
	Snapshot.BatteryCapacityKilowattHours = ActivePowerConfig.BatteryCapacityKilowattHours;
	Snapshot.ChargePercent = CurrentSnapshot.ChargeFraction * 100.0f;
	Snapshot.GenerationKilowatts = CurrentSnapshot.GenerationKilowatts;
	Snapshot.DemandKilowatts = CurrentSnapshot.BaselineDemandKilowatts;
	Snapshot.NetPowerKilowatts = CurrentSnapshot.NetPowerKilowatts;
	Snapshot.PowerState = CurrentSnapshot.PowerState;
	return Snapshot;
}

bool UEdenPowerSystemComponent::RegisterWithSimulationClock()
{
	if (!bPowerSimulationEnabled)
	{
		return false;
	}

	if (RegisteredSimulationClock.IsValid())
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot register with simulation clock; no world is available."), *MakeLogContext());
		DisablePowerSimulation(TEXT("missing world for simulation clock registration"));
		return false;
	}

	UEdenSimulationClockSubsystem* SimulationClock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	if (!SimulationClock)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot register with simulation clock; subsystem is unavailable."), *MakeLogContext());
		DisablePowerSimulation(TEXT("simulation clock subsystem unavailable"));
		return false;
	}

	if (!SimulationClock->RegisterSimulationTickable(this))
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s failed to register with simulation clock."), *MakeLogContext());
		DisablePowerSimulation(TEXT("simulation clock registration failed"));
		return false;
	}

	RegisteredSimulationClock = SimulationClock;
	return true;
}

bool UEdenPowerSystemComponent::UnregisterFromSimulationClock()
{
	if (!RegisteredSimulationClock.IsValid())
	{
		return false;
	}

	UEdenSimulationClockSubsystem* SimulationClock = RegisteredSimulationClock.Get();
	RegisteredSimulationClock.Reset();

	return SimulationClock->UnregisterSimulationTickable(this);
}

bool UEdenPowerSystemComponent::InitializeFromConfiguredDataAsset()
{
	if (!PowerConfigDataAsset)
	{
		bHasValidPowerConfiguration = false;
		DisablePowerSimulation(TEXT("missing PowerConfigDataAsset"));
		return false;
	}

	const FEdenPowerConfig& PowerConfig = PowerConfigDataAsset->PowerConfig;
	if (!ValidateAndLogConfig(PowerConfig))
	{
		bHasValidPowerConfiguration = false;
		DisablePowerSimulation(FString::Printf(TEXT("invalid PowerConfigDataAsset '%s'"), *GetNameSafe(PowerConfigDataAsset)));
		return false;
	}

	return InitializePowerSimulation(PowerConfig);
}

void UEdenPowerSystemComponent::DisablePowerSimulation(const FString& Reason)
{
	UnregisterFromSimulationClock();

	bPowerSimulationEnabled = false;
	CurrentSnapshot = FEdenPowerStateSnapshot();

	UE_LOG(LogEdenSystems, Warning, TEXT("%s disabled power simulation: %s."), *MakeLogContext(), *Reason);
}

bool UEdenPowerSystemComponent::ValidateAndLogConfig(const FEdenPowerConfig& PowerConfig) const
{
	TArray<FString> ValidationErrors;
	if (FEdenPowerModel::ValidateConfig(PowerConfig, &ValidationErrors))
	{
		return true;
	}

	for (const FString& ValidationError : ValidationErrors)
	{
		UE_LOG(LogEdenSystems, Error, TEXT("%s invalid power configuration: %s"), *MakeLogContext(), *ValidationError);
	}

	return false;
}

void UEdenPowerSystemComponent::ApplySnapshot(const FEdenPowerStateSnapshot& NewSnapshot, bool bBroadcastEvents)
{
	const EEdenPowerState PreviousState = CurrentSnapshot.PowerState;
	CurrentSnapshot = NewSnapshot;

	if (!bBroadcastEvents || PreviousState == CurrentSnapshot.PowerState)
	{
		return;
	}

	UE_LOG(
		LogEdenSystems,
		Log,
		TEXT("%s power state changed from %s to %s. Charge=%f kWh Fraction=%f Net=%f kW"),
		*MakeLogContext(),
		*UEnum::GetValueAsString(PreviousState),
		*UEnum::GetValueAsString(CurrentSnapshot.PowerState),
		CurrentSnapshot.BatteryChargeKilowattHours,
		CurrentSnapshot.ChargeFraction,
		CurrentSnapshot.NetPowerKilowatts);

	OnPowerStateChanged.Broadcast(PreviousState, CurrentSnapshot.PowerState);

	if (CurrentSnapshot.PowerState == EEdenPowerState::Depleted)
	{
		OnPowerDepleted.Broadcast();
	}
}

FString UEdenPowerSystemComponent::MakeLogContext() const
{
	return FString::Printf(
		TEXT("PowerSystem='%s' Owner='%s'"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()));
}
