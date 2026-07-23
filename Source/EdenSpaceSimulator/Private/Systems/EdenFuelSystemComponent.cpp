// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenFuelSystemComponent.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Flight/EdenPropulsionDemandSource.h"
#include "GameFramework/Actor.h"
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
		RefreshPropulsionDemandSource();
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

	const float DemandNormalized = ResolveConsumptionDemandNormalized();
	const FEdenFuelStepResult StepResult = FEdenFuelModel::Step(
		ActiveFuelConfig,
		CurrentSnapshot,
		DemandNormalized,
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
		bHasValidFuelConfiguration = false;
		DisableFuelSimulation(TEXT("invalid explicit fuel configuration"));
		return false;
	}

	ActiveFuelConfig = FuelConfig;
	ConsumptionDemandNormalized = 0.0f;
	bFuelSimulationEnabled = true;
	bHasValidFuelConfiguration = true;
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

bool UEdenFuelSystemComponent::RefreshPropulsionDemandSource()
{
	bPropulsionDemandSourceDiscoveryComplete = true;
	bLoggedExpiredPropulsionDemandSource = false;
	bLoggedSanitizedPropulsionDemand = false;
	PropulsionDemandSourceComponent.Reset();
	ConsumptionDemandNormalized = 0.0f;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s could not discover propulsion demand source; no owning actor is available. Using zero demand."),
			*MakeLogContext());
		return false;
	}

	TInlineComponentArray<UActorComponent*> OwnerComponents(OwnerActor);
	TArray<UActorComponent*> ValidDemandSources;
	for (UActorComponent* Component : OwnerComponents)
	{
		if (!IsValid(Component))
		{
			continue;
		}

		if (Cast<IEdenPropulsionDemandSource>(Component))
		{
			ValidDemandSources.Add(Component);
		}
	}

	if (ValidDemandSources.Num() == 0)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s found no propulsion demand source on owner '%s'. Using zero demand."),
			*MakeLogContext(),
			*GetNameSafe(OwnerActor));
		return false;
	}

	if (ValidDemandSources.Num() > 1)
	{
		UE_LOG(
			LogEdenSystems,
			Error,
			TEXT("%s found multiple propulsion demand sources on owner '%s'. Resolve ambiguity before enabling fuel consumption."),
			*MakeLogContext(),
			*GetNameSafe(OwnerActor));
		return false;
	}

	PropulsionDemandSourceComponent = ValidDemandSources[0];
	UE_LOG(
		LogEdenSystems,
		Verbose,
		TEXT("%s discovered propulsion demand source '%s'."),
		*MakeLogContext(),
		*GetNameSafe(PropulsionDemandSourceComponent.Get()));
	return true;
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

FEdenFuelDebugSnapshot UEdenFuelSystemComponent::GetFuelDebugSnapshot() const
{
	FEdenFuelDebugSnapshot Snapshot;
	Snapshot.bComponentAvailable = true;
	Snapshot.bConfigurationValid = bHasValidFuelConfiguration;
	Snapshot.bRegisteredWithClock = RegisteredSimulationClock.IsValid();
	Snapshot.FuelQuantityKilograms = CurrentSnapshot.FuelQuantityKilograms;
	Snapshot.CapacityKilograms = ActiveFuelConfig.CapacityKilograms;
	Snapshot.FuelPercent = CurrentSnapshot.FuelFraction * 100.0f;
	Snapshot.PropulsionDemandNormalized = GetDebugPropulsionDemandNormalized();
	Snapshot.FuelState = CurrentSnapshot.FuelState;
	return Snapshot;
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
		DisableFuelSimulation(TEXT("missing world for simulation clock registration"));
		return false;
	}

	UEdenSimulationClockSubsystem* SimulationClock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	if (!SimulationClock)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot register with simulation clock; subsystem is unavailable."), *MakeLogContext());
		DisableFuelSimulation(TEXT("simulation clock subsystem unavailable"));
		return false;
	}

	if (!SimulationClock->RegisterSimulationTickable(this))
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s failed to register with simulation clock."), *MakeLogContext());
		DisableFuelSimulation(TEXT("simulation clock registration failed"));
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
		bHasValidFuelConfiguration = false;
		DisableFuelSimulation(TEXT("missing FuelConfigDataAsset"));
		return false;
	}

	const FEdenFuelConfig& FuelConfig = FuelConfigDataAsset->FuelConfig;
	if (!ValidateAndLogConfig(FuelConfig))
	{
		bHasValidFuelConfiguration = false;
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
	PropulsionDemandSourceComponent.Reset();
	bPropulsionDemandSourceDiscoveryComplete = false;
	bLoggedExpiredPropulsionDemandSource = false;
	bLoggedSanitizedPropulsionDemand = false;
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

float UEdenFuelSystemComponent::ResolveConsumptionDemandNormalized()
{
	if (!bPropulsionDemandSourceDiscoveryComplete)
	{
		return ConsumptionDemandNormalized;
	}

	UActorComponent* SourceComponent = PropulsionDemandSourceComponent.Get();
	if (!SourceComponent)
	{
		if (!bLoggedExpiredPropulsionDemandSource)
		{
			UE_LOG(
				LogEdenSystems,
				Warning,
				TEXT("%s has no valid propulsion demand source. Using zero demand."),
				*MakeLogContext());
			bLoggedExpiredPropulsionDemandSource = true;
		}

		ConsumptionDemandNormalized = 0.0f;
		return 0.0f;
	}

	const IEdenPropulsionDemandSource* DemandSource = Cast<IEdenPropulsionDemandSource>(SourceComponent);
	if (!DemandSource)
	{
		if (!bLoggedExpiredPropulsionDemandSource)
		{
			UE_LOG(
				LogEdenSystems,
				Warning,
				TEXT("%s propulsion demand source '%s' is invalid. Using zero demand."),
				*MakeLogContext(),
				*GetNameSafe(SourceComponent));
			bLoggedExpiredPropulsionDemandSource = true;
		}

		PropulsionDemandSourceComponent.Reset();
		ConsumptionDemandNormalized = 0.0f;
		return 0.0f;
	}

	bool bDemandWasSanitized = false;
	const float DemandNormalized = FEdenFuelModel::SanitizeDemandNormalized(
		DemandSource->GetPropulsionDemandNormalized(),
		&bDemandWasSanitized);

	if (bDemandWasSanitized && !bLoggedSanitizedPropulsionDemand)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized propulsion demand from source '%s' to %f."),
			*MakeLogContext(),
			*GetNameSafe(SourceComponent),
			DemandNormalized);
	}

	bLoggedExpiredPropulsionDemandSource = false;
	bLoggedSanitizedPropulsionDemand = bDemandWasSanitized;
	ConsumptionDemandNormalized = DemandNormalized;
	return DemandNormalized;
}

float UEdenFuelSystemComponent::GetDebugPropulsionDemandNormalized() const
{
	if (!bPropulsionDemandSourceDiscoveryComplete)
	{
		return ConsumptionDemandNormalized;
	}

	const UActorComponent* SourceComponent = PropulsionDemandSourceComponent.Get();
	const IEdenPropulsionDemandSource* DemandSource = Cast<IEdenPropulsionDemandSource>(SourceComponent);
	if (!DemandSource)
	{
		return 0.0f;
	}

	return FEdenFuelModel::SanitizeDemandNormalized(DemandSource->GetPropulsionDemandNormalized());
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
