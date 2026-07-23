// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenThermalSystemComponent.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Systems/EdenThermalConfigDataAsset.h"

UEdenThermalSystemComponent::UEdenThermalSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEdenThermalSystemComponent::BeginPlay()
{
	Super::BeginPlay();

	if (InitializeFromConfiguredDataAsset())
	{
		if (RegisterWithSimulationClock())
		{
			UE_LOG(
				LogEdenSystems,
				Log,
				TEXT("%s thermal simulation active (%.2f C). Inspect live values with ShowDebug EdenSystems."),
				*MakeLogContext(),
				CurrentSnapshot.TemperatureCelsius);
		}
	}
}

void UEdenThermalSystemComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSimulationClock();

	Super::EndPlay(EndPlayReason);
}

void UEdenThermalSystemComponent::AdvanceSimulation(float FixedDeltaSeconds)
{
	if (!bThermalSimulationEnabled)
	{
		return;
	}

	const FEdenThermalStepResult StepResult =
		FEdenThermalModel::Step(ActiveThermalConfig, CurrentSnapshot, FixedDeltaSeconds);

	if (!StepResult.bConfigWasValid)
	{
		DisableThermalSimulation(TEXT("active thermal configuration became invalid during simulation"));
		return;
	}

	if (!StepResult.bDeltaTimeWasValid)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s rejected invalid FixedDeltaSeconds=%f; thermal state was not advanced."),
			*MakeLogContext(),
			FixedDeltaSeconds);
		return;
	}

	if (StepResult.bTemperatureWasSanitized
		|| StepResult.bHeatGenerationWasSanitized
		|| StepResult.bDissipationWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized thermal state to Temperature=%f C Heat=%f C/s Dissipation=%f C/s."),
			*MakeLogContext(),
			StepResult.Snapshot.TemperatureCelsius,
			StepResult.Snapshot.HeatGenerationDegreesCelsiusPerSecond,
			StepResult.Snapshot.DissipationDegreesCelsiusPerSecond);
	}

	ApplySnapshot(StepResult.Snapshot, true);
}

bool UEdenThermalSystemComponent::InitializeThermalSimulation(const FEdenThermalConfig& ThermalConfig)
{
	if (!ValidateAndLogConfig(ThermalConfig))
	{
		bHasValidThermalConfiguration = false;
		DisableThermalSimulation(TEXT("invalid explicit thermal configuration"));
		return false;
	}

	ActiveThermalConfig = ThermalConfig;
	bThermalSimulationEnabled = true;
	bHasValidThermalConfiguration = true;
	ApplySnapshot(FEdenThermalModel::MakeInitialSnapshot(ActiveThermalConfig), false);

	return true;
}

bool UEdenThermalSystemComponent::ResetThermalState()
{
	if (!bThermalSimulationEnabled)
	{
		if (!InitializeFromConfiguredDataAsset())
		{
			UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot reset; thermal simulation is disabled."), *MakeLogContext());
			return false;
		}
	}

	ApplySnapshot(FEdenThermalModel::MakeInitialSnapshot(ActiveThermalConfig), true);

	return true;
}

bool UEdenThermalSystemComponent::SetTemperatureCelsius(float TemperatureCelsius)
{
	if (!bThermalSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set temperature; thermal simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bTemperatureWasSanitized = false;
	const float ClampedTemperatureCelsius =
		FEdenThermalModel::ClampTemperatureCelsius(TemperatureCelsius, ActiveThermalConfig, &bTemperatureWasSanitized);

	if (bTemperatureWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s clamped requested temperature from %f C to %f C."),
			*MakeLogContext(),
			TemperatureCelsius,
			ClampedTemperatureCelsius);
	}

	ApplySnapshot(
		FEdenThermalModel::MakeSnapshot(
			ActiveThermalConfig,
			ClampedTemperatureCelsius,
			CurrentSnapshot.HeatGenerationDegreesCelsiusPerSecond,
			CurrentSnapshot.DissipationDegreesCelsiusPerSecond),
		true);

	return !bTemperatureWasSanitized;
}

bool UEdenThermalSystemComponent::SetHeatGenerationDegreesCelsiusPerSecond(float HeatGenerationDegreesCelsiusPerSecond)
{
	if (!bThermalSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set heat generation; thermal simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bHeatGenerationWasSanitized = false;
	const float SanitizedHeatGenerationDegreesCelsiusPerSecond =
		FEdenThermalModel::SanitizeNonnegativeDegreesCelsiusPerSecond(
			HeatGenerationDegreesCelsiusPerSecond,
			&bHeatGenerationWasSanitized);

	if (bHeatGenerationWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized requested heat generation from %f C/s to %f C/s."),
			*MakeLogContext(),
			HeatGenerationDegreesCelsiusPerSecond,
			SanitizedHeatGenerationDegreesCelsiusPerSecond);
	}

	ApplySnapshot(
		FEdenThermalModel::MakeSnapshot(
			ActiveThermalConfig,
			CurrentSnapshot.TemperatureCelsius,
			SanitizedHeatGenerationDegreesCelsiusPerSecond,
			CurrentSnapshot.DissipationDegreesCelsiusPerSecond),
		true);

	return !bHeatGenerationWasSanitized;
}

bool UEdenThermalSystemComponent::SetDissipationDegreesCelsiusPerSecond(float DissipationDegreesCelsiusPerSecond)
{
	if (!bThermalSimulationEnabled)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot set dissipation; thermal simulation is disabled."), *MakeLogContext());
		return false;
	}

	bool bDissipationWasSanitized = false;
	const float SanitizedDissipationDegreesCelsiusPerSecond =
		FEdenThermalModel::SanitizeNonnegativeDegreesCelsiusPerSecond(
			DissipationDegreesCelsiusPerSecond,
			&bDissipationWasSanitized);

	if (bDissipationWasSanitized)
	{
		UE_LOG(
			LogEdenSystems,
			Warning,
			TEXT("%s sanitized requested dissipation from %f C/s to %f C/s."),
			*MakeLogContext(),
			DissipationDegreesCelsiusPerSecond,
			SanitizedDissipationDegreesCelsiusPerSecond);
	}

	ApplySnapshot(
		FEdenThermalModel::MakeSnapshot(
			ActiveThermalConfig,
			CurrentSnapshot.TemperatureCelsius,
			CurrentSnapshot.HeatGenerationDegreesCelsiusPerSecond,
			SanitizedDissipationDegreesCelsiusPerSecond),
		true);

	return !bDissipationWasSanitized;
}

bool UEdenThermalSystemComponent::IsThermalSimulationEnabled() const
{
	return bThermalSimulationEnabled;
}

FEdenThermalConfig UEdenThermalSystemComponent::GetActiveThermalConfig() const
{
	return ActiveThermalConfig;
}

FEdenThermalStateSnapshot UEdenThermalSystemComponent::GetThermalStateSnapshot() const
{
	return CurrentSnapshot;
}

FEdenThermalDebugSnapshot UEdenThermalSystemComponent::GetThermalDebugSnapshot() const
{
	FEdenThermalDebugSnapshot Snapshot;
	Snapshot.bComponentAvailable = true;
	Snapshot.bConfigurationValid = bHasValidThermalConfiguration;
	Snapshot.bRegisteredWithClock = RegisteredSimulationClock.IsValid();
	Snapshot.TemperatureCelsius = CurrentSnapshot.TemperatureCelsius;
	Snapshot.AmbientTemperatureCelsius = ActiveThermalConfig.AmbientTemperatureCelsius;
	Snapshot.HeatGenerationDegreesCelsiusPerSecond = CurrentSnapshot.HeatGenerationDegreesCelsiusPerSecond;
	Snapshot.DissipationDegreesCelsiusPerSecond = CurrentSnapshot.DissipationDegreesCelsiusPerSecond;
	Snapshot.ThermalState = CurrentSnapshot.ThermalState;
	return Snapshot;
}

bool UEdenThermalSystemComponent::RegisterWithSimulationClock()
{
	if (!bThermalSimulationEnabled)
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
		DisableThermalSimulation(TEXT("missing world for simulation clock registration"));
		return false;
	}

	UEdenSimulationClockSubsystem* SimulationClock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	if (!SimulationClock)
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s cannot register with simulation clock; subsystem is unavailable."), *MakeLogContext());
		DisableThermalSimulation(TEXT("simulation clock subsystem unavailable"));
		return false;
	}

	if (!SimulationClock->RegisterSimulationTickable(this))
	{
		UE_LOG(LogEdenSystems, Warning, TEXT("%s failed to register with simulation clock."), *MakeLogContext());
		DisableThermalSimulation(TEXT("simulation clock registration failed"));
		return false;
	}

	RegisteredSimulationClock = SimulationClock;
	return true;
}

bool UEdenThermalSystemComponent::UnregisterFromSimulationClock()
{
	if (!RegisteredSimulationClock.IsValid())
	{
		return false;
	}

	UEdenSimulationClockSubsystem* SimulationClock = RegisteredSimulationClock.Get();
	RegisteredSimulationClock.Reset();

	return SimulationClock->UnregisterSimulationTickable(this);
}

bool UEdenThermalSystemComponent::InitializeFromConfiguredDataAsset()
{
	if (!ThermalConfigDataAsset)
	{
		bHasValidThermalConfiguration = false;
		DisableThermalSimulation(TEXT("missing ThermalConfigDataAsset"));
		return false;
	}

	const FEdenThermalConfig& ThermalConfig = ThermalConfigDataAsset->ThermalConfig;
	if (!ValidateAndLogConfig(ThermalConfig))
	{
		bHasValidThermalConfiguration = false;
		DisableThermalSimulation(FString::Printf(TEXT("invalid ThermalConfigDataAsset '%s'"), *GetNameSafe(ThermalConfigDataAsset)));
		return false;
	}

	return InitializeThermalSimulation(ThermalConfig);
}

void UEdenThermalSystemComponent::DisableThermalSimulation(const FString& Reason)
{
	UnregisterFromSimulationClock();

	bThermalSimulationEnabled = false;
	CurrentSnapshot = FEdenThermalStateSnapshot();

	UE_LOG(LogEdenSystems, Warning, TEXT("%s disabled thermal simulation: %s."), *MakeLogContext(), *Reason);
}

bool UEdenThermalSystemComponent::ValidateAndLogConfig(const FEdenThermalConfig& ThermalConfig) const
{
	TArray<FString> ValidationErrors;
	if (FEdenThermalModel::ValidateConfig(ThermalConfig, &ValidationErrors))
	{
		return true;
	}

	for (const FString& ValidationError : ValidationErrors)
	{
		UE_LOG(LogEdenSystems, Error, TEXT("%s invalid thermal configuration: %s"), *MakeLogContext(), *ValidationError);
	}

	return false;
}

void UEdenThermalSystemComponent::ApplySnapshot(const FEdenThermalStateSnapshot& NewSnapshot, bool bBroadcastEvents)
{
	const EEdenThermalState PreviousState = CurrentSnapshot.ThermalState;
	CurrentSnapshot = NewSnapshot;

	if (!bBroadcastEvents || PreviousState == CurrentSnapshot.ThermalState)
	{
		return;
	}

	UE_LOG(
		LogEdenSystems,
		Log,
		TEXT("%s thermal state changed from %s to %s. Temperature=%f C Heat=%f C/s Dissipation=%f C/s"),
		*MakeLogContext(),
		*UEnum::GetValueAsString(PreviousState),
		*UEnum::GetValueAsString(CurrentSnapshot.ThermalState),
		CurrentSnapshot.TemperatureCelsius,
		CurrentSnapshot.HeatGenerationDegreesCelsiusPerSecond,
		CurrentSnapshot.DissipationDegreesCelsiusPerSecond);

	OnThermalStateChanged.Broadcast(PreviousState, CurrentSnapshot.ThermalState);

	if (CurrentSnapshot.ThermalState == EEdenThermalState::Overheated)
	{
		OnThermalOverheated.Broadcast();
	}
}

FString UEdenThermalSystemComponent::MakeLogContext() const
{
	return FString::Printf(
		TEXT("ThermalSystem='%s' Owner='%s'"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()));
}
