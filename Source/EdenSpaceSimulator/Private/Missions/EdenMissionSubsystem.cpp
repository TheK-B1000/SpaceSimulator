// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/EdenMissionSubsystem.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Engine/World.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "GameFramework/PlayerController.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Missions/EdenMissionDefinitionDataAsset.h"

void UEdenMissionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency(UEdenSimulationClockSubsystem::StaticClass());

	CurrentRuntimeState = FEdenMissionModel::ResetRuntimeState();
	ActiveMissionDefinition = FEdenMissionDefinitionConfig();
	RegisteredSimulationClock = nullptr;
	ClearMissionResourceTargets();
}

void UEdenMissionSubsystem::Deinitialize()
{
	UnregisterFromSimulationClock();
	ClearMissionExternalModifiers();
	ClearMissionResourceTargets();
	CurrentRuntimeState = FEdenMissionModel::ResetRuntimeState();
	ActiveMissionDefinition = FEdenMissionDefinitionConfig();

	Super::Deinitialize();
}

bool UEdenMissionSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UEdenMissionSubsystem::AdvanceSimulation(float FixedDeltaSeconds)
{
	if (CurrentRuntimeState.MissionState != EEdenMissionState::Running)
	{
		return;
	}

	const FEdenMissionStepResult StepResult = FEdenMissionModel::StepTimeline(
		CurrentRuntimeState,
		ActiveMissionDefinition,
		FixedDeltaSeconds);

	CurrentRuntimeState = StepResult.UpdatedState;

	const UEdenThermalSystemComponent* ThermalComp = GetThermalTarget();
	const UEdenPowerSystemComponent* PowerComp = GetPowerTarget();
	const UEdenFuelSystemComponent* FuelComp = GetFuelTarget();

	// Missing targets must not invent catastrophic resource readings.
	const float ThermalTemperatureCelsius = ThermalComp
		? ThermalComp->GetThermalStateSnapshot().TemperatureCelsius
		: TNumericLimits<float>::Lowest();
	const float PowerChargeFraction = PowerComp
		? PowerComp->GetPowerStateSnapshot().ChargeFraction
		: 1.0f;
	const float FuelFraction = FuelComp
		? FuelComp->GetFuelStateSnapshot().FuelFraction
		: 1.0f;

	const TArray<FEdenMissionObjectiveRuntime> PreviousObjectives = CurrentRuntimeState.ObjectiveStates;

	CurrentRuntimeState = FEdenMissionModel::EvaluateObjectives(
		CurrentRuntimeState,
		ActiveMissionDefinition,
		ThermalTemperatureCelsius,
		PowerChargeFraction,
		FuelFraction);

	for (const FEdenMissionObjectiveRuntime& NewObjective : CurrentRuntimeState.ObjectiveStates)
	{
		const FEdenMissionObjectiveRuntime* PreviousObjective = PreviousObjectives.FindByPredicate(
			[&NewObjective](const FEdenMissionObjectiveRuntime& Candidate)
			{
				return Candidate.ObjectiveId == NewObjective.ObjectiveId;
			});

		if (PreviousObjective && PreviousObjective->State != NewObjective.State)
		{
			OnObjectiveStateChanged.Broadcast(NewObjective.ObjectiveId, PreviousObjective->State, NewObjective.State);
		}
	}

	const EEdenMissionState EvaluatedOutcome = FEdenMissionModel::EvaluateOutcome(
		CurrentRuntimeState,
		ActiveMissionDefinition);

	if (EvaluatedOutcome != CurrentRuntimeState.MissionState)
	{
		TransitionMissionState(EvaluatedOutcome);
	}

	if (CurrentRuntimeState.MissionState != EEdenMissionState::Running)
	{
		return;
	}

	for (const FName& EventId : StepResult.NewlyTriggeredEventIds)
	{
		UE_LOG(
			LogEdenMission,
			Log,
			TEXT("%s triggered mission event '%s' at simulation time %.2fs."),
			*GetNameSafe(this),
			*EventId.ToString(),
			CurrentRuntimeState.MissionElapsedTimeSeconds);

		OnMissionEventTriggered.Broadcast(EventId);

		const FEdenMissionEventConfig* EventConfigPtr = ActiveMissionDefinition.Events.FindByPredicate(
			[&EventId](const FEdenMissionEventConfig& Config)
			{
				return Config.EventId == EventId;
			});

		if (EventConfigPtr)
		{
			ExecuteMissionEvent(*EventConfigPtr);
		}
		else
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s triggered event '%s' but no matching event config was found; dispatch skipped."),
				*GetNameSafe(this),
				*EventId.ToString());
		}
	}
}

bool UEdenMissionSubsystem::LoadMission(const FEdenMissionDefinitionConfig& Definition)
{
	if (CurrentRuntimeState.MissionState == EEdenMissionState::Running)
	{
		UE_LOG(
			LogEdenMission,
			Warning,
			TEXT("%s cannot load mission '%s' while a mission is already running. Abort or reset first."),
			*GetNameSafe(this),
			*Definition.MissionId.ToString());
		return false;
	}

	// A finished previous run may still hold its power-bus limit; it must not carry into the next mission.
	if (CurrentRuntimeState.bAppliedBatteryDischargeLimit)
	{
		if (UEdenPowerSystemComponent* PowerComp = GetPowerTarget())
		{
			PowerComp->ClearBatteryDischargeLimitOverride();
		}
		CurrentRuntimeState.bAppliedBatteryDischargeLimit = false;
	}

	TArray<FString> ValidationErrors;
	if (!FEdenMissionModel::ValidateDefinition(Definition, &ValidationErrors))
	{
		for (const FString& Error : ValidationErrors)
		{
			UE_LOG(
				LogEdenMission,
				Error,
				TEXT("%s rejected mission definition '%s': %s"),
				*GetNameSafe(this),
				*Definition.MissionId.ToString(),
				*Error);
		}
		return false;
	}

	ActiveMissionDefinition = Definition;
	const EEdenMissionState PreviousState = CurrentRuntimeState.MissionState;
	CurrentRuntimeState = FEdenMissionModel::InitializeRuntimeState(Definition);

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s loaded mission '%s' with %d events and %d objectives. State is now Ready."),
		*GetNameSafe(this),
		*Definition.MissionId.ToString(),
		Definition.Events.Num(),
		Definition.Objectives.Num());

	if (PreviousState != EEdenMissionState::Ready)
	{
		OnMissionStateChanged.Broadcast(PreviousState, EEdenMissionState::Ready);
	}
	return true;
}

bool UEdenMissionSubsystem::LoadMissionFromDefinitionAsset(const UEdenMissionDefinitionDataAsset* DefinitionAsset)
{
	if (!DefinitionAsset)
	{
		UE_LOG(
			LogEdenMission,
			Warning,
			TEXT("%s cannot load mission from a null definition asset."),
			*GetNameSafe(this));
		return false;
	}

	return LoadMission(DefinitionAsset->GetMissionDefinition());
}

bool UEdenMissionSubsystem::StartMission()
{
	if (!FEdenMissionModel::CanTransition(CurrentRuntimeState.MissionState, EEdenMissionState::Running))
	{
		UE_LOG(
			LogEdenMission,
			Warning,
			TEXT("%s cannot start mission '%s' from state '%s'. Mission must be Ready."),
			*GetNameSafe(this),
			*ActiveMissionDefinition.MissionId.ToString(),
			*UEnum::GetValueAsString(CurrentRuntimeState.MissionState));
		return false;
	}

	if (!GetThermalTarget() && !GetPowerTarget() && !GetFuelTarget())
	{
		TryResolveResourceTargetsFromPossessedSpacecraft();
	}

	if (!RegisterWithSimulationClock())
	{
		UE_LOG(
			LogEdenMission,
			Error,
			TEXT("%s failed to register with simulation clock when starting mission '%s'."),
			*GetNameSafe(this),
			*ActiveMissionDefinition.MissionId.ToString());
		return false;
	}

	// Ready -> Running is always a fresh run (Ready only follows LoadMission), so declared initial
	// conditions apply here and nowhere else.
	if (!ApplyInitialConditions())
	{
		UnregisterFromSimulationClock();
		return false;
	}

	TransitionMissionState(EEdenMissionState::Running);

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s started mission '%s'. Fixed-step timeline is now advancing."),
		*GetNameSafe(this),
		*ActiveMissionDefinition.MissionId.ToString());

	return true;
}

bool UEdenMissionSubsystem::AbortMission()
{
	if (CurrentRuntimeState.MissionState != EEdenMissionState::Running)
	{
		UE_LOG(
			LogEdenMission,
			Warning,
			TEXT("%s cannot abort mission when not running (current state: %s)."),
			*GetNameSafe(this),
			*UEnum::GetValueAsString(CurrentRuntimeState.MissionState));
		return false;
	}

	UnregisterFromSimulationClock();
	ClearMissionExternalModifiers();
	TransitionMissionState(EEdenMissionState::Inactive);

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s aborted mission '%s'. Mission-applied external modifiers were cleared. State is now Inactive."),
		*GetNameSafe(this),
		*ActiveMissionDefinition.MissionId.ToString());

	return true;
}

bool UEdenMissionSubsystem::ResetMission()
{
	if (CurrentRuntimeState.MissionState == EEdenMissionState::Running)
	{
		UE_LOG(
			LogEdenMission,
			Warning,
			TEXT("%s cannot reset mission '%s' while running. Abort first."),
			*GetNameSafe(this),
			*ActiveMissionDefinition.MissionId.ToString());
		return false;
	}

	UnregisterFromSimulationClock();
	ClearMissionExternalModifiers();

	const EEdenMissionState PreviousState = CurrentRuntimeState.MissionState;
	CurrentRuntimeState = FEdenMissionModel::ResetRuntimeState();
	ActiveMissionDefinition = FEdenMissionDefinitionConfig();

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s reset mission runtime/definition state to Inactive and cleared mission-applied external modifiers."),
		*GetNameSafe(this));

	if (PreviousState != EEdenMissionState::Inactive)
	{
		OnMissionStateChanged.Broadcast(PreviousState, EEdenMissionState::Inactive);
	}

	return true;
}

bool UEdenMissionSubsystem::SetMissionResourceTargets(
	UEdenThermalSystemComponent* Thermal,
	UEdenPowerSystemComponent* Power,
	UEdenFuelSystemComponent* Fuel)
{
	CachedThermalTarget = Thermal;
	CachedPowerTarget = Power;
	CachedFuelTarget = Fuel;

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s cached mission resource targets. Thermal='%s' Power='%s' Fuel='%s'."),
		*GetNameSafe(this),
		*GetNameSafe(Thermal),
		*GetNameSafe(Power),
		*GetNameSafe(Fuel));

	return true;
}

void UEdenMissionSubsystem::ClearMissionResourceTargets()
{
	CachedThermalTarget.Reset();
	CachedPowerTarget.Reset();
	CachedFuelTarget.Reset();
}

bool UEdenMissionSubsystem::RegisterWithSimulationClock()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UEdenSimulationClockSubsystem* ClockSubsystem = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	if (!ClockSubsystem)
	{
		UE_LOG(
			LogEdenMission,
			Warning,
			TEXT("%s cannot find UEdenSimulationClockSubsystem in world."),
			*GetNameSafe(this));
		return false;
	}

	if (!ClockSubsystem->RegisterSimulationTickable(this, EdenSimulationClockPriority::Mission))
	{
		return false;
	}

	RegisteredSimulationClock = ClockSubsystem;
	return true;
}

bool UEdenMissionSubsystem::UnregisterFromSimulationClock()
{
	if (RegisteredSimulationClock.IsValid())
	{
		RegisteredSimulationClock->UnregisterSimulationTickable(this);
		RegisteredSimulationClock.Reset();
		return true;
	}

	return false;
}

EEdenMissionState UEdenMissionSubsystem::GetMissionState() const
{
	return CurrentRuntimeState.MissionState;
}

EEdenMissionPhase UEdenMissionSubsystem::GetMissionPhase() const
{
	return CurrentRuntimeState.MissionPhase;
}

float UEdenMissionSubsystem::GetMissionElapsedTimeSeconds() const
{
	return CurrentRuntimeState.MissionElapsedTimeSeconds;
}

FName UEdenMissionSubsystem::GetActiveMissionId() const
{
	return ActiveMissionDefinition.MissionId;
}

bool UEdenMissionSubsystem::IsMissionRunning() const
{
	return CurrentRuntimeState.MissionState == EEdenMissionState::Running;
}

FEdenMissionStateSnapshot UEdenMissionSubsystem::GetMissionStateSnapshot() const
{
	return FEdenMissionModel::MakeSnapshot(CurrentRuntimeState, ActiveMissionDefinition.MissionId);
}

FEdenMissionRuntimeState UEdenMissionSubsystem::GetMissionRuntimeState() const
{
	return CurrentRuntimeState;
}

FEdenMissionDefinitionConfig UEdenMissionSubsystem::GetActiveMissionDefinition() const
{
	return ActiveMissionDefinition;
}

void UEdenMissionSubsystem::TransitionMissionState(EEdenMissionState NewState)
{
	if (CurrentRuntimeState.MissionState == NewState)
	{
		return;
	}

	const EEdenMissionState PreviousState = CurrentRuntimeState.MissionState;
	CurrentRuntimeState.MissionState = NewState;

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s transitioned mission state from %s to %s."),
		*GetNameSafe(this),
		*UEnum::GetValueAsString(PreviousState),
		*UEnum::GetValueAsString(NewState));

	OnMissionStateChanged.Broadcast(PreviousState, NewState);
}

void UEdenMissionSubsystem::TransitionMissionPhase(EEdenMissionPhase NewPhase)
{
	if (CurrentRuntimeState.MissionPhase == NewPhase)
	{
		return;
	}

	const EEdenMissionPhase PreviousPhase = CurrentRuntimeState.MissionPhase;
	CurrentRuntimeState.MissionPhase = NewPhase;

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s transitioned mission phase from %s to %s."),
		*GetNameSafe(this),
		*UEnum::GetValueAsString(PreviousPhase),
		*UEnum::GetValueAsString(NewPhase));

	OnMissionPhaseChanged.Broadcast(PreviousPhase, NewPhase);
}

void UEdenMissionSubsystem::ExecuteMissionEvent(const FEdenMissionEventConfig& EventConfig)
{
	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s dispatching mission command '%s' for event '%s' (FloatParam=%.2f, Phase=%s, NameParam='%s')."),
		*GetNameSafe(this),
		*UEnum::GetValueAsString(EventConfig.CommandType),
		*EventConfig.EventId.ToString(),
		EventConfig.FloatParameter,
		*UEnum::GetValueAsString(EventConfig.PhaseParameter),
		*EventConfig.NameParameter.ToString());

	switch (EventConfig.CommandType)
	{
	case EEdenMissionCommandType::SetMissionPhase:
	{
		TransitionMissionPhase(EventConfig.PhaseParameter);
		return;
	}
	case EEdenMissionCommandType::SetExternalHeatingRate:
	{
		if (!IsFiniteCommandPayload(EventConfig.FloatParameter))
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s rejected SetExternalHeatingRate for event '%s': FloatParameter is not finite."),
				*GetNameSafe(this),
				*EventConfig.EventId.ToString());
			return;
		}

		UEdenThermalSystemComponent* ThermalComp = GetThermalTarget();
		if (!ThermalComp)
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s could not dispatch SetExternalHeatingRate for event '%s': thermal target unavailable. Single attempt, no retry."),
				*GetNameSafe(this),
				*EventConfig.EventId.ToString());
			return;
		}

		ThermalComp->SetExternalHeatingRateDegreesCelsiusPerSecond(EventConfig.FloatParameter);
		return;
	}
	case EEdenMissionCommandType::ClearExternalHeatingRate:
	{
		UEdenThermalSystemComponent* ThermalComp = GetThermalTarget();
		if (!ThermalComp)
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s could not dispatch ClearExternalHeatingRate for event '%s': thermal target unavailable. Single attempt, no retry."),
				*GetNameSafe(this),
				*EventConfig.EventId.ToString());
			return;
		}

		ThermalComp->ClearExternalHeatingRate();
		return;
	}
	case EEdenMissionCommandType::SetExternalPowerDemand:
	{
		if (!IsFiniteCommandPayload(EventConfig.FloatParameter))
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s rejected SetExternalPowerDemand for event '%s': FloatParameter is not finite."),
				*GetNameSafe(this),
				*EventConfig.EventId.ToString());
			return;
		}

		UEdenPowerSystemComponent* PowerComp = GetPowerTarget();
		if (!PowerComp)
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s could not dispatch SetExternalPowerDemand for event '%s': power target unavailable. Single attempt, no retry."),
				*GetNameSafe(this),
				*EventConfig.EventId.ToString());
			return;
		}

		PowerComp->SetExternalDemandKilowatts(EventConfig.FloatParameter);
		return;
	}
	case EEdenMissionCommandType::ClearExternalPowerDemand:
	{
		UEdenPowerSystemComponent* PowerComp = GetPowerTarget();
		if (!PowerComp)
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s could not dispatch ClearExternalPowerDemand for event '%s': power target unavailable. Single attempt, no retry."),
				*GetNameSafe(this),
				*EventConfig.EventId.ToString());
			return;
		}

		PowerComp->ClearExternalDemand();
		return;
	}
	case EEdenMissionCommandType::ActivateObjective:
	{
		CurrentRuntimeState = FEdenMissionModel::ActivateObjective(CurrentRuntimeState, EventConfig.NameParameter);
		return;
	}
	case EEdenMissionCommandType::SetPowerGeneration:
	case EEdenMissionCommandType::None:
	default:
	{
		UE_LOG(
			LogEdenMission,
			Warning,
			TEXT("%s unsupported mission command '%s' for event '%s'. No systems were mutated."),
			*GetNameSafe(this),
			*UEnum::GetValueAsString(EventConfig.CommandType),
			*EventConfig.EventId.ToString());
		return;
	}
	}
}

bool UEdenMissionSubsystem::ApplyInitialConditions()
{
	const FEdenMissionInitialConditions& Initial = ActiveMissionDefinition.InitialConditions;
	if (!Initial.bSetInitialTemperature && !Initial.bSetBatteryDischargeLimit)
	{
		return true; // Start from the spacecraft's current physical condition.
	}

	UEdenThermalSystemComponent* ThermalComp = GetThermalTarget();
	UEdenPowerSystemComponent* PowerComp = GetPowerTarget();
	if ((Initial.bSetInitialTemperature && (!ThermalComp || !ThermalComp->IsThermalSimulationEnabled()))
		|| (Initial.bSetBatteryDischargeLimit && (!PowerComp || !PowerComp->IsPowerSimulationEnabled())))
	{
		UE_LOG(
			LogEdenMission,
			Error,
			TEXT("%s cannot start mission '%s': it declares initial conditions but the %s target is missing or not simulating."),
			*GetNameSafe(this),
			*ActiveMissionDefinition.MissionId.ToString(),
			Initial.bSetInitialTemperature && (!ThermalComp || !ThermalComp->IsThermalSimulationEnabled()) ? TEXT("thermal") : TEXT("power"));
		return false;
	}

	// Leftover mission-owned disturbances must not leak into the scripted start.
	ClearMissionExternalModifiers();

	if (Initial.bSetInitialTemperature && !ThermalComp->SetTemperatureCelsius(Initial.InitialTemperatureCelsius))
	{
		UE_LOG(
			LogEdenMission,
			Error,
			TEXT("%s cannot start mission '%s': initial temperature %.3f C was rejected by the thermal system."),
			*GetNameSafe(this),
			*ActiveMissionDefinition.MissionId.ToString(),
			Initial.InitialTemperatureCelsius);
		return false;
	}

	if (Initial.bSetBatteryDischargeLimit)
	{
		if (!PowerComp->SetBatteryDischargeLimitKilowatts(Initial.BatteryMaxDischargeKilowatts))
		{
			UE_LOG(
				LogEdenMission,
				Error,
				TEXT("%s cannot start mission '%s': battery discharge limit %.3f kW was rejected."),
				*GetNameSafe(this),
				*ActiveMissionDefinition.MissionId.ToString(),
				Initial.BatteryMaxDischargeKilowatts);
			return false;
		}
		CurrentRuntimeState.bAppliedBatteryDischargeLimit = true;
	}

	CurrentRuntimeState.bInitialConditionsApplied = true;
	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s applied declared initial conditions for '%s': %s%s. Battery charge, propellant, flight state and operator modes are unchanged."),
		*GetNameSafe(this),
		*ActiveMissionDefinition.MissionId.ToString(),
		Initial.bSetInitialTemperature ? *FString::Printf(TEXT("temperature %.2f C"), Initial.InitialTemperatureCelsius) : TEXT("temperature unchanged"),
		Initial.bSetBatteryDischargeLimit ? *FString::Printf(TEXT(", power bus discharge limit %.2f kW"), Initial.BatteryMaxDischargeKilowatts) : TEXT(""));
	return true;
}

void UEdenMissionSubsystem::ClearMissionExternalModifiers()
{
	if (UEdenThermalSystemComponent* ThermalComp = GetThermalTarget())
	{
		if (!ThermalComp->ClearExternalHeatingRate())
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s failed to clear external heating rate on thermal target '%s'."),
				*GetNameSafe(this),
				*GetNameSafe(ThermalComp));
		}
	}

	if (UEdenPowerSystemComponent* PowerComp = GetPowerTarget())
	{
		if (CurrentRuntimeState.bAppliedBatteryDischargeLimit)
		{
			PowerComp->ClearBatteryDischargeLimitOverride();
			CurrentRuntimeState.bAppliedBatteryDischargeLimit = false;
		}
		if (!PowerComp->ClearExternalDemand())
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s failed to clear external demand on power target '%s'."),
				*GetNameSafe(this),
				*GetNameSafe(PowerComp));
		}
	}
}

bool UEdenMissionSubsystem::TryResolveResourceTargetsFromPossessedSpacecraft()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	AEdenSpacecraftPawn* SpacecraftPawn = Cast<AEdenSpacecraftPawn>(PlayerController->GetPawn());
	if (!SpacecraftPawn)
	{
		return false;
	}

	CachedThermalTarget = SpacecraftPawn->GetThermalSystemComponent();
	CachedPowerTarget = SpacecraftPawn->GetPowerSystemComponent();
	CachedFuelTarget = SpacecraftPawn->GetFuelSystemComponent();

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s resolved mission resource targets from possessed spacecraft '%s'."),
		*GetNameSafe(this),
		*GetNameSafe(SpacecraftPawn));

	return CachedThermalTarget.IsValid() || CachedPowerTarget.IsValid() || CachedFuelTarget.IsValid();
}

UEdenThermalSystemComponent* UEdenMissionSubsystem::GetThermalTarget() const
{
	return CachedThermalTarget.Get();
}

UEdenPowerSystemComponent* UEdenMissionSubsystem::GetPowerTarget() const
{
	return CachedPowerTarget.Get();
}

UEdenFuelSystemComponent* UEdenMissionSubsystem::GetFuelTarget() const
{
	return CachedFuelTarget.Get();
}

bool UEdenMissionSubsystem::IsFiniteCommandPayload(float Value) const
{
	return FMath::IsFinite(Value);
}
