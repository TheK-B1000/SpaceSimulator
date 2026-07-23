// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenFuelSystemComponent.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Systems/EdenFuelConfigDataAsset.h"

UEdenFuelSystemComponent::UEdenFuelSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEdenFuelSystemComponent::BeginPlay()
{
	Super::BeginPlay();

	if (InitializeFromConfiguredDataAsset())
	{
		RegisterWithSimulationClock();
	}
}

void UEdenFuelSystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSimulationClock();

	Super::EndPlay(EndPlayReason);
}

void UEdenFuelSystemComponent::AdvanceSimulation(float FixedDeltaSeconds)
{
	if (!bFuelSimulationEnabled)
	{
		return;
	}

	const FEdenFuelStepResult StepResult = FEdenFuelModel::Step(
		ActiveFuelConfig,
		CurrentSnapshot,
		ConsumptionDemandNormalized,
		FixedDeltaSeconds);

	if (!StepResult.bConfigWasValid)
	{
		DisableFuelSimulation(TEXT("active fuel configuration became invalid during simulation"));
		return;
	}

	if (!StepResult.bDeltaTimeWasValid)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s rejected invalid FixedDeltaSeconds=%f; fuel state was not advanced."),
			*MakeLogContext(),
			FixedDeltaSeconds);
		return;
	}

	if (StepResult.bDemandWasSanitized)
	{
		ConsumptionDemandNormalized = StepResult.SanitizedDemandNormalized;
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized fuel consumption demand to %f."),
			*MakeLogContext(),
			ConsumptionDemandNormalized);
	}

	ApplySnapshot(StepResult.Snapshot, true);
}

bool UEdenFuelSystemComponent::InitializeFuelSimulation(const FEdenFuelConfig& FuelConfig)
{
	if (!ValidateAndLogConfig(FuelConfig))
	{
		DisableFuelSimulation(TEXT("invalid explicit fuel configuration"));
		return false;
	}

	ActiveFuelConfig = FuelConfig;
	ConsumptionDemandNormalized = 0.0f;
	bFuelSimulationEnabled = true;
	ApplySnapshot(FEdenFuelModel::MakeInitialSnapshot(ActiveFuelConfig), false);

	return true;
}

bool UEdenFuelSystemComponent::ResetFuelState()
{
	if (!bFuelSimulationEnabled)
	{
		if (!InitializeFromConfiguredDataAsset())
		{
			UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot reset; fuel simulation is disabled."), *MakeLogContext());
			return false;
		}
	}

	ConsumptionDemandNormalized = 0.0f;
	ApplySnapshot(FEdenFuelModel::MakeInitialSnapshot(ActiveFuelConfig), true);

	return true;
}

bool UEdenFuelSystemComponent::SetConsumptionDemandNormalized(float DemandNormalized)
{
	bool bDemandWasSanitized = false;
	ConsumptionDemandNormalized = FEdenFuelModel::SanitizeDemandNormalized(DemandNormalized, &bDemandWasSanitized);

	if (bDemandWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized requested fuel consumption demand from %f to %f."),
			*MakeLogContext(),
			DemandNormalized,
			ConsumptionDemandNormalized);
	}

	return !bDemandWasSanitized;
}

bool UEdenFuelSystemComponent::SetFuelQuantityKilograms(float FuelQuantityKilograms)
{
	if (!bFuelSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set fuel quantity; fuel simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bQuantityWasSanitized = false;
	const float ClampedFuelQuantityKilograms = FEdenFuelModel::ClampFuelQuantityKilograms(
		FuelQuantityKilograms,
		ActiveFuelConfig.CapacityKilograms,
		&bQuantityWasSanitized);

	if (bQuantityWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s clamped requested fuel quantity from %f kg to %f kg."),
			*MakeLogContext(),
			FuelQuantityKilograms,
			ClampedFuelQuantityKilograms);
	}

	ApplySnapshot(FEdenFuelModel::MakeSnapshot(ActiveFuelConfig, ClampedFuelQuantityKilograms), true);

	return !bQuantityWasSanitized;
}

bool UEdenFuelSystemComponent::IsFuelSimulationEnabled() const
{
	return bFuelSimulationEnabled;
}

float UEdenFuelSystemComponent::GetConsumptionDemandNormalized() const
{
	return ConsumptionDemandNormalized;
}

FEdenFuelConfig UEdenFuelSystemComponent::GetActiveFuelConfig() const
{
	return ActiveFuelConfig;
}

FEdenFuelStateSnapshot UEdenFuelSystemComponent::GetFuelStateSnapshot() const
{
	return CurrentSnapshot;
}

bool UEdenFuelSystemComponent::RegisterWithSimulationClock()
{
	if (!bFuelSimulationEnabled)
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
		return false;
	}

	UEdenSimulationClockSubsystem* SimulationClock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	if (!SimulationClock)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot register with simulation clock; subsystem is unavailable."), *MakeLogContext());
		return false;
	}

	if (!SimulationClock->RegisterSimulationTickable(this))
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s failed to register with simulation clock."), *MakeLogContext());
		return false;
	}

	RegisteredSimulationClock = SimulationClock;
	return true;
}

bool UEdenFuelSystemComponent::UnregisterFromSimulationClock()
{
	if (!RegisteredSimulationClock.IsValid())
	{
		return false;
	}

	UEdenSimulationClockSubsystem* SimulationClock = RegisteredSimulationClock.Get();
	RegisteredSimulationClock.Reset();

	return SimulationClock->UnregisterSimulationTickable(this);
}

bool UEdenFuelSystemComponent::InitializeFromConfiguredDataAsset()
{
	if (!FuelConfigDataAsset)
	{
		DisableFuelSimulation(TEXT("missing FuelConfigDataAsset"));
		return false;
	}

	const FEdenFuelConfig& FuelConfig = FuelConfigDataAsset->FuelConfig;
	if (!ValidateAndLogConfig(FuelConfig))
	{
		DisableFuelSimulation(FString::Printf(TEXT("invalid FuelConfigDataAsset '%s'"), *GetNameSafe(FuelConfigDataAsset)));
		return false;
	}

	return InitializeFuelSimulation(FuelConfig);
}

void UEdenFuelSystemComponent::DisableFuelSimulation(const FString& Reason)
{
	UnregisterFromSimulationClock();

	bFuelSimulationEnabled = false;
	ConsumptionDemandNormalized = 0.0f;
	CurrentSnapshot = FEdenFuelStateSnapshot();

	UE_LOG(LogEdenSystems, Warning, TEXT("%s disabled fuel simulation: %s."), *MakeLogContext(), *Reason);
}

bool UEdenFuelSystemComponent::ValidateAndLogConfig(const FEdenFuelConfig& FuelConfig) const
{
	TArray<FString> ValidationErrors;
	if (FEdenFuelModel::ValidateConfig(FuelConfig, &ValidationErrors))
	{
		return true;
	}

	for (const FString& ValidationError : ValidationErrors)
	{
		UE_LOG(LogEdenSystems, Error, TEXT("%s invalid fuel configuration: %s"), *MakeLogContext(), *ValidationError);
	}

	return false;
}

void UEdenFuelSystemComponent::ApplySnapshot(const FEdenFuelStateSnapshot& NewSnapshot, bool bBroadcastEvents)
{
	const EEdenFuelState PreviousState = CurrentSnapshot.FuelState;
	CurrentSnapshot = NewSnapshot;

	if (!bBroadcastEvents || PreviousState == CurrentSnapshot.FuelState)
	{
		return;
	}

	UE_LOG(
		LogEdenSystems,
		Log,
		TEXT("%s fuel state changed from %s to %s. Fuel=%f kg Fraction=%f"),
		*MakeLogContext(),
		*UEnum::GetValueAsString(PreviousState),
		*UEnum::GetValueAsString(CurrentSnapshot.FuelState),
		CurrentSnapshot.FuelQuantityKilograms,
		CurrentSnapshot.FuelFraction);

	OnFuelStateChanged.Broadcast(PreviousState, CurrentSnapshot.FuelState);

	if (CurrentSnapshot.FuelState == EEdenFuelState::Depleted)
	{
		OnFuelDepleted.Broadcast();
	}
}

FString UEdenFuelSystemComponent::MakeLogContext() const
{
	return FString::Printf(
		TEXT("FuelSystem='%s' Owner='%s'"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()));
}
