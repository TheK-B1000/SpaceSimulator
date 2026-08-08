// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/EdenMissionSubsystem.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UObjectIterator.h"

void UEdenMissionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency(UEdenSimulationClockSubsystem::StaticClass());

	CurrentRuntimeState = FEdenMissionModel::ResetRuntimeState();
	ActiveMissionDefinition = FEdenMissionDefinitionConfig();
	RegisteredSimulationClock = nullptr;
}

void UEdenMissionSubsystem::Deinitialize()
{
	UnregisterFromSimulationClock();
	ClearMissionExternalModifiers();
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
	}

	const EEdenMissionState EvaluatedOutcome = FEdenMissionModel::EvaluateOutcome(
		CurrentRuntimeState,
		ActiveMissionDefinition);

	if (EvaluatedOutcome != CurrentRuntimeState.MissionState)
	{
		TransitionMissionState(EvaluatedOutcome);
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
	CurrentRuntimeState = FEdenMissionModel::InitializeRuntimeState(Definition);

	UE_LOG(
		LogEdenMission,
		Log,
		TEXT("%s loaded mission '%s' with %d events and %d objectives. State is now Ready."),
		*GetNameSafe(this),
		*Definition.MissionId.ToString(),
		Definition.Events.Num(),
		Definition.Objectives.Num());

	OnMissionStateChanged.Broadcast(EEdenMissionState::Inactive, EEdenMissionState::Ready);
	return true;
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
		TEXT("%s aborted mission '%s'. State is now Inactive."),
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
		TEXT("%s reset mission state to Inactive."),
		*GetNameSafe(this));

	if (PreviousState != EEdenMissionState::Inactive)
	{
		OnMissionStateChanged.Broadcast(PreviousState, EEdenMissionState::Inactive);
	}

	return true;
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
		TEXT("%s executing mission command '%s' for event '%s' (FloatParam=%.2f, NameParam='%s')."),
		*GetNameSafe(this),
		*UEnum::GetValueAsString(EventConfig.CommandType),
		*EventConfig.EventId.ToString(),
		EventConfig.FloatParameter,
		*EventConfig.NameParameter.ToString());

	switch (EventConfig.CommandType)
	{
	case EEdenMissionCommandType::SetMissionPhase:
	{
		const EEdenMissionPhase TargetPhase = static_cast<EEdenMissionPhase>(
			FMath::Clamp(FMath::RoundToInt(EventConfig.FloatParameter), 0, static_cast<int32>(EEdenMissionPhase::Resolved)));
		TransitionMissionPhase(TargetPhase);
		break;
	}
	case EEdenMissionCommandType::SetExternalHeatingRate:
	{
		if (UEdenThermalSystemComponent* ThermalComp = FindThermalComponent())
		{
			ThermalComp->SetExternalHeatingRateDegreesCelsiusPerSecond(EventConfig.FloatParameter);
		}
		else
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s could not find thermal component to apply external heating rate."),
				*GetNameSafe(this));
		}
		break;
	}
	case EEdenMissionCommandType::ClearExternalHeatingRate:
	{
		if (UEdenThermalSystemComponent* ThermalComp = FindThermalComponent())
		{
			ThermalComp->ClearExternalHeatingRate();
		}
		break;
	}
	case EEdenMissionCommandType::SetExternalPowerDemand:
	{
		if (UEdenPowerSystemComponent* PowerComp = FindPowerComponent())
		{
			PowerComp->SetExternalDemandKilowatts(EventConfig.FloatParameter);
		}
		else
		{
			UE_LOG(
				LogEdenMission,
				Warning,
				TEXT("%s could not find power component to apply external demand."),
				*GetNameSafe(this));
		}
		break;
	}
	case EEdenMissionCommandType::ClearExternalPowerDemand:
	{
		if (UEdenPowerSystemComponent* PowerComp = FindPowerComponent())
		{
			PowerComp->ClearExternalDemand();
		}
		break;
	}
	case EEdenMissionCommandType::SetPowerGeneration:
	{
		if (UEdenPowerSystemComponent* PowerComp = FindPowerComponent())
		{
			PowerComp->SetGenerationKilowatts(EventConfig.FloatParameter);
		}
		break;
	}
	case EEdenMissionCommandType::ActivateObjective:
	{
		CurrentRuntimeState = FEdenMissionModel::ActivateObjective(CurrentRuntimeState, EventConfig.NameParameter);
		break;
	}
	default:
		break;
	}
}

void UEdenMissionSubsystem::ClearMissionExternalModifiers()
{
	if (UEdenThermalSystemComponent* ThermalComp = FindThermalComponent())
	{
		ThermalComp->ClearExternalHeatingRate();
	}

	if (UEdenPowerSystemComponent* PowerComp = FindPowerComponent())
	{
		PowerComp->ClearExternalDemand();
	}
}

UEdenThermalSystemComponent* UEdenMissionSubsystem::FindThermalComponent() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UEdenThermalSystemComponent* Comp = Pawn->FindComponentByClass<UEdenThermalSystemComponent>())
			{
				return Comp;
			}
		}
	}

	for (TObjectIterator<UEdenThermalSystemComponent> It; It; ++It)
	{
		if (It->GetWorld() == World)
		{
			return *It;
		}
	}

	return nullptr;
}

UEdenPowerSystemComponent* UEdenMissionSubsystem::FindPowerComponent() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UEdenPowerSystemComponent* Comp = Pawn->FindComponentByClass<UEdenPowerSystemComponent>())
			{
				return Comp;
			}
		}
	}

	for (TObjectIterator<UEdenPowerSystemComponent> It; It; ++It)
	{
		if (It->GetWorld() == World)
		{
			return *It;
		}
	}

	return nullptr;
}
