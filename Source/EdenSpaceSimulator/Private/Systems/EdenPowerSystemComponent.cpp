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
		if (RegisterWithSimulationClock())
		{
			UE_LOG(
				LogEdenSystems,
				Log,
				TEXT("%s power simulation active (%.3f / %.3f kWh, net %.2f kW). Inspect live values with ShowDebug EdenSystems."),
				*MakeLogContext(),
				CurrentSnapshot.BatteryChargeKilowattHours,
				ActivePowerConfig.BatteryCapacityKilowattHours,
				CurrentSnapshot.NetPowerKilowatts);
		}
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
	ThermalControlDemandKilowatts = 0.0f;
	BatteryDischargeLimitOverrideKilowatts = -1.0f;
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

	ThermalControlDemandKilowatts = 0.0f;
	BatteryDischargeLimitOverrideKilowatts = -1.0f;
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
			CurrentSnapshot.BaselineDemandKilowatts,
			CurrentSnapshot.ExternalDemandKilowatts,
			CurrentSnapshot.OperatorDemandKilowatts),
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
			SanitizedDemandKilowatts,
			CurrentSnapshot.ExternalDemandKilowatts,
			CurrentSnapshot.OperatorDemandKilowatts),
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
			CurrentSnapshot.BaselineDemandKilowatts,
			CurrentSnapshot.ExternalDemandKilowatts,
			CurrentSnapshot.OperatorDemandKilowatts),
		true);

	return !bChargeWasSanitized;
}

bool UEdenPowerSystemComponent::SetExternalDemandKilowatts(float ExternalDemandKilowatts)
{
	if (!bPowerSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set external demand; power simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bWasSanitized = false;
	const float SanitizedExternalDemand = FEdenPowerModel::SanitizeNonnegativeKilowatts(
		ExternalDemandKilowatts,
		&bWasSanitized);

	if (bWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized requested external demand from %f kW to %f kW."),
			*MakeLogContext(),
			ExternalDemandKilowatts,
			SanitizedExternalDemand);
	}

	const float PreviousExternalDemand = CurrentSnapshot.ExternalDemandKilowatts;
	ApplySnapshot(
		FEdenPowerModel::MakeSnapshot(
			ActivePowerConfig,
			CurrentSnapshot.BatteryChargeKilowattHours,
			CurrentSnapshot.GenerationKilowatts,
			CurrentSnapshot.BaselineDemandKilowatts,
			SanitizedExternalDemand,
			CurrentSnapshot.OperatorDemandKilowatts),
		true);
	if (PreviousExternalDemand != CurrentSnapshot.ExternalDemandKilowatts)
	{
		// Authoritative confirmation that mission demand was applied or cleared.
		UE_LOG(
			LogEdenSystems,
			Log,
			TEXT("%s external demand changed %.3f -> %.3f kW. Generation=%.3f kW | demand baseline %.3f + external %.3f + operator %+.3f = %.3f kW | net %+.3f kW | battery %.3f kWh"),
			*MakeLogContext(),
			PreviousExternalDemand,
			CurrentSnapshot.ExternalDemandKilowatts,
			CurrentSnapshot.GenerationKilowatts,
			CurrentSnapshot.BaselineDemandKilowatts,
			CurrentSnapshot.ExternalDemandKilowatts,
			CurrentSnapshot.OperatorDemandKilowatts,
			CurrentSnapshot.TotalDemandKilowatts,
			CurrentSnapshot.NetPowerKilowatts,
			CurrentSnapshot.BatteryChargeKilowattHours);
	}

	return !bWasSanitized;
}

bool UEdenPowerSystemComponent::ClearExternalDemand()
{
	return SetExternalDemandKilowatts(0.0f);
}

bool UEdenPowerSystemComponent::SetOperatorDemandKilowatts(float OperatorDemandKilowatts)
{
	ThermalControlDemandKilowatts = 0.0f;
	return SetOperatorDemandInternal(OperatorDemandKilowatts);
}

bool UEdenPowerSystemComponent::SetOperatorDemandBreakdown(float TotalOperatorDemandKilowatts, float InThermalControlDemandKilowatts)
{
	const float SanitizedCooling = FEdenPowerModel::SanitizeNonnegativeKilowatts(InThermalControlDemandKilowatts);
	ThermalControlDemandKilowatts = bPowerSimulationEnabled ? SanitizedCooling : ThermalControlDemandKilowatts;
	return SetOperatorDemandInternal(TotalOperatorDemandKilowatts);
}

bool UEdenPowerSystemComponent::SetBatteryDischargeLimitKilowatts(float BatteryMaxDischargeKilowatts)
{
	if (!bPowerSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set discharge limit; power simulation is disabled."), *MakeLogContext());
		return false;
	}
	if (!FMath::IsFinite(BatteryMaxDischargeKilowatts) || BatteryMaxDischargeKilowatts <= 0.0f)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s rejected discharge limit %f kW (must be finite and > 0)."), *MakeLogContext(), BatteryMaxDischargeKilowatts);
		return false;
	}

	BatteryDischargeLimitOverrideKilowatts = BatteryMaxDischargeKilowatts;
	ApplySnapshot(CurrentSnapshot, true);
	UE_LOG(
		LogEdenSystems,
		Log,
		TEXT("%s power bus limited: battery discharge %.3f kW + generation %.3f kW = capacity %.3f kW."),
		*MakeLogContext(),
		BatteryMaxDischargeKilowatts,
		CurrentSnapshot.GenerationKilowatts,
		CurrentSnapshot.BusCapacityKilowatts);
	return true;
}

bool UEdenPowerSystemComponent::ClearBatteryDischargeLimitOverride()
{
	if (!bPowerSimulationEnabled)
	{
		return false;
	}
	const bool bHadOverride = BatteryDischargeLimitOverrideKilowatts >= 0.0f;
	BatteryDischargeLimitOverrideKilowatts = -1.0f;
	ApplySnapshot(CurrentSnapshot, true);
	if (bHadOverride)
	{
		UE_LOG(
			LogEdenSystems,
			Log,
			TEXT("%s power bus discharge limit restored to config (%s)."),
			*MakeLogContext(),
			ActivePowerConfig.BatteryMaxDischargeKilowatts > 0.0f
				? *FString::Printf(TEXT("%.3f kW"), ActivePowerConfig.BatteryMaxDischargeKilowatts)
				: TEXT("unlimited"));
	}
	return true;
}

float UEdenPowerSystemComponent::GetEffectiveBatteryDischargeLimitKilowatts() const
{
	return BatteryDischargeLimitOverrideKilowatts >= 0.0f ? BatteryDischargeLimitOverrideKilowatts : ActivePowerConfig.BatteryMaxDischargeKilowatts;
}

float UEdenPowerSystemComponent::GetCoolingAuthority() const
{
	return bPowerSimulationEnabled ? CurrentSnapshot.CoolingAuthority : 1.0f;
}

bool UEdenPowerSystemComponent::SetOperatorDemandInternal(float OperatorDemandKilowatts)
{
	if (!bPowerSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set operator demand; power simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bWasSanitized = false;
	const float SanitizedOperatorDemand = FEdenPowerModel::SanitizeFiniteKilowatts(
		OperatorDemandKilowatts,
		&bWasSanitized);

	if (bWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized requested operator demand from %f kW to %f kW."),
			*MakeLogContext(),
			OperatorDemandKilowatts,
			SanitizedOperatorDemand);
	}

	ApplySnapshot(
		FEdenPowerModel::MakeSnapshot(
			ActivePowerConfig,
			CurrentSnapshot.BatteryChargeKilowattHours,
			CurrentSnapshot.GenerationKilowatts,
			CurrentSnapshot.BaselineDemandKilowatts,
			CurrentSnapshot.ExternalDemandKilowatts,
			SanitizedOperatorDemand),
		true);

	return !bWasSanitized;
}

bool UEdenPowerSystemComponent::ClearOperatorDemand()
{
	return SetOperatorDemandKilowatts(0.0f);
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
	Snapshot.DemandKilowatts = CurrentSnapshot.TotalDemandKilowatts;
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
	// Every snapshot carries the bus inputs owned here, so no setter can drop them.
	CurrentSnapshot = FEdenPowerModel::WithBusAllocation(
		NewSnapshot,
		ThermalControlDemandKilowatts,
		GetEffectiveBatteryDischargeLimitKilowatts());

	const bool bCurtailing = CurrentSnapshot.bBusLimited
		&& (CurrentSnapshot.AuxiliaryCurtailedKilowatts > 1.0e-4f || CurrentSnapshot.CoolingAuthority < 1.0f - 1.0e-4f);
	if (bBroadcastEvents && bCurtailing != bBusWasCurtailing)
	{
		UE_LOG(
			LogEdenSystems,
			Log,
			TEXT("%s power bus %s. Capacity=%.3f kW | essential %.3f/%.3f | cooling %.3f/%.3f (authority %.0f%%) | auxiliary %.3f/%.3f (curtailed %.3f) kW | battery %.3f kWh"),
			*MakeLogContext(),
			bCurtailing ? TEXT("curtailing load") : TEXT("serving all load"),
			CurrentSnapshot.BusCapacityKilowatts,
			CurrentSnapshot.EssentialServedKilowatts,
			CurrentSnapshot.EssentialRequestedKilowatts,
			CurrentSnapshot.ThermalControlServedKilowatts,
			CurrentSnapshot.ThermalControlDemandKilowatts,
			CurrentSnapshot.CoolingAuthority * 100.0f,
			CurrentSnapshot.AuxiliaryServedKilowatts,
			CurrentSnapshot.AuxiliaryRequestedKilowatts,
			CurrentSnapshot.AuxiliaryCurtailedKilowatts,
			CurrentSnapshot.BatteryChargeKilowattHours);
	}
	bBusWasCurtailing = bCurtailing;

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

bool UEdenPowerSystemComponent::IsPropulsionPowerSourceAvailable() const
{
	return IsPowerSimulationEnabled();
}

EEdenPowerState UEdenPowerSystemComponent::GetPropulsionPowerState() const
{
	return GetPowerStateSnapshot().PowerState;
}
