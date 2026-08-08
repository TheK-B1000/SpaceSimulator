// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenTelemetrySubsystem.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Engine/World.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Operations/EdenAlertSubsystem.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"

bool UEdenTelemetrySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UEdenTelemetrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SessionMetadata.SnapshotIntervalSeconds = 0.1f * static_cast<float>(SnapshotDecimationSteps);
	ClearHistory();

	if (UWorld* World = GetWorld())
	{
		if (UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			if (Clock->RegisterSimulationTickable(this, EdenSimulationClockPriority::Observers))
			{
				RegisteredClock = Clock;
			}
		}
	}

	EnsureBound();
}

void UEdenTelemetrySubsystem::Deinitialize()
{
	UnbindSources();
	if (UEdenSimulationClockSubsystem* Clock = RegisteredClock.Get())
	{
		Clock->UnregisterSimulationTickable(this);
	}
	RegisteredClock.Reset();
	Super::Deinitialize();
}

void UEdenTelemetrySubsystem::AdvanceSimulation(float FixedDeltaSeconds)
{
	(void)FixedDeltaSeconds;
	EnsureBound();

	const FEdenTelemetrySnapshot Snapshot = AssembleSnapshot();
	UpdateAggregates(Snapshot);
	StoreSnapshotIfDue(Snapshot);
}

void UEdenTelemetrySubsystem::ClearHistory()
{
	EventHistory.Reset();
	SnapshotHistory.Reset();
	SessionMetadata = FEdenTelemetrySessionMetadata();
	SessionMetadata.SnapshotIntervalSeconds = 0.1f * static_cast<float>(SnapshotDecimationSteps);
	NextSequenceNumber = 1;
	StepsSinceSnapshot = 0;
	bHasAggregateSeed = false;
}

TArray<FEdenTelemetryEvent> UEdenTelemetrySubsystem::GetEventHistory() const
{
	return EventHistory;
}

TArray<FEdenTelemetrySnapshot> UEdenTelemetrySubsystem::GetSnapshotHistory() const
{
	return SnapshotHistory;
}

FEdenTelemetrySessionMetadata UEdenTelemetrySubsystem::GetSessionMetadata() const
{
	return SessionMetadata;
}

FString UEdenTelemetrySubsystem::ExportSessionJsonV1() const
{
	FString Json;
	Json += TEXT("{\n");
	Json += TEXT("  \"schema\": \"TelemetryExportSchemaV1\",\n");
	Json += FString::Printf(TEXT("  \"droppedSnapshotCount\": %d,\n"), SessionMetadata.DroppedSnapshotCount);
	Json += FString::Printf(TEXT("  \"droppedEventCount\": %d,\n"), SessionMetadata.DroppedEventCount);
	Json += FString::Printf(TEXT("  \"historyTruncated\": %s,\n"), SessionMetadata.bHistoryTruncated ? TEXT("true") : TEXT("false"));
	Json += FString::Printf(
		TEXT("  \"eventIntegrityCompromised\": %s,\n"),
		SessionMetadata.bEventIntegrityCompromised ? TEXT("true") : TEXT("false"));
	Json += FString::Printf(TEXT("  \"firstAvailableSequence\": %lld,\n"), SessionMetadata.FirstAvailableSequence);
	Json += FString::Printf(TEXT("  \"lastAvailableSequence\": %lld,\n"), SessionMetadata.LastAvailableSequence);
	Json += FString::Printf(TEXT("  \"snapshotIntervalSeconds\": %.3f,\n"), SessionMetadata.SnapshotIntervalSeconds);
	Json += FString::Printf(TEXT("  \"peakTemperatureCelsius\": %.3f,\n"), SessionMetadata.PeakTemperatureCelsius);
	Json += FString::Printf(
		TEXT("  \"minimumBatteryChargeFraction\": %.3f,\n"),
		SessionMetadata.MinimumBatteryChargeFraction);
	Json += FString::Printf(TEXT("  \"minimumFuelFraction\": %.3f,\n"), SessionMetadata.MinimumFuelFraction);
	Json += FString::Printf(TEXT("  \"eventCount\": %d,\n"), EventHistory.Num());
	Json += FString::Printf(TEXT("  \"snapshotCount\": %d\n"), SnapshotHistory.Num());
	Json += TEXT("}\n");
	return Json;
}

void UEdenTelemetrySubsystem::BindSources()
{
	UnbindSources();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>())
	{
		BoundMission = Mission;
		Mission->OnMissionStateChanged.AddDynamic(this, &UEdenTelemetrySubsystem::HandleMissionStateChanged);
		Mission->OnMissionPhaseChanged.AddDynamic(this, &UEdenTelemetrySubsystem::HandleMissionPhaseChanged);
		Mission->OnObjectiveStateChanged.AddDynamic(this, &UEdenTelemetrySubsystem::HandleObjectiveStateChanged);
	}

	if (UEdenAlertSubsystem* Alerts = World->GetSubsystem<UEdenAlertSubsystem>())
	{
		BoundAlerts = Alerts;
		Alerts->OnAlertRaised.AddDynamic(this, &UEdenTelemetrySubsystem::HandleAlertRaised);
		Alerts->OnAlertCleared.AddDynamic(this, &UEdenTelemetrySubsystem::HandleAlertCleared);
	}

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (AEdenSpacecraftPawn* Pawn = Cast<AEdenSpacecraftPawn>(PC->GetPawn()))
		{
			if (UEdenOperatorControlComponent* Operator = Pawn->GetOperatorControlComponent())
			{
				BoundOperator = Operator;
				Operator->OnOperatorIntentChanged.AddDynamic(this, &UEdenTelemetrySubsystem::HandleOperatorIntentChanged);
			}
			if (UEdenFuelSystemComponent* Fuel = Pawn->GetFuelSystemComponent())
			{
				BoundFuel = Fuel;
				Fuel->OnFuelStateChanged.AddDynamic(this, &UEdenTelemetrySubsystem::HandleFuelStateChanged);
			}
			if (UEdenPowerSystemComponent* Power = Pawn->GetPowerSystemComponent())
			{
				BoundPower = Power;
				Power->OnPowerStateChanged.AddDynamic(this, &UEdenTelemetrySubsystem::HandlePowerStateChanged);
			}
			if (UEdenThermalSystemComponent* Thermal = Pawn->GetThermalSystemComponent())
			{
				BoundThermal = Thermal;
				Thermal->OnThermalStateChanged.AddDynamic(this, &UEdenTelemetrySubsystem::HandleThermalStateChanged);
			}
			BoundMovement = Pawn->GetFlightMovementComponent();
		}
	}
}

void UEdenTelemetrySubsystem::UnbindSources()
{
	if (UEdenMissionSubsystem* Mission = BoundMission.Get())
	{
		Mission->OnMissionStateChanged.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandleMissionStateChanged);
		Mission->OnMissionPhaseChanged.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandleMissionPhaseChanged);
		Mission->OnObjectiveStateChanged.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandleObjectiveStateChanged);
	}
	if (UEdenAlertSubsystem* Alerts = BoundAlerts.Get())
	{
		Alerts->OnAlertRaised.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandleAlertRaised);
		Alerts->OnAlertCleared.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandleAlertCleared);
	}
	if (UEdenOperatorControlComponent* Operator = BoundOperator.Get())
	{
		Operator->OnOperatorIntentChanged.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandleOperatorIntentChanged);
	}
	if (UEdenFuelSystemComponent* Fuel = BoundFuel.Get())
	{
		Fuel->OnFuelStateChanged.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandleFuelStateChanged);
	}
	if (UEdenPowerSystemComponent* Power = BoundPower.Get())
	{
		Power->OnPowerStateChanged.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandlePowerStateChanged);
	}
	if (UEdenThermalSystemComponent* Thermal = BoundThermal.Get())
	{
		Thermal->OnThermalStateChanged.RemoveDynamic(this, &UEdenTelemetrySubsystem::HandleThermalStateChanged);
	}

	BoundMission.Reset();
	BoundAlerts.Reset();
	BoundOperator.Reset();
	BoundFuel.Reset();
	BoundPower.Reset();
	BoundThermal.Reset();
	BoundMovement.Reset();
}

void UEdenTelemetrySubsystem::EnsureBound()
{
	if (!BoundMission.IsValid() || !BoundFuel.IsValid() || !BoundPower.IsValid() || !BoundThermal.IsValid())
	{
		BindSources();
	}
}

void UEdenTelemetrySubsystem::RecordEvent(
	EEdenTelemetryEventType EventType,
	FName SourceSystem,
	FName EventId,
	const FString& Detail)
{
	FEdenTelemetryEvent Event;
	Event.SequenceNumber = NextSequence();
	Event.EventType = EventType;
	Event.SourceSystem = SourceSystem;
	Event.EventId = EventId;
	Event.Detail = Detail;

	if (const UWorld* World = GetWorld())
	{
		if (const UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			Event.SimulationTimeSeconds = Clock->GetElapsedSimulationTimeSeconds();
		}
		if (const UEdenMissionSubsystem* Mission = BoundMission.Get())
		{
			Event.MissionElapsedTimeSeconds = Mission->GetMissionElapsedTimeSeconds();
		}
	}

	if (EventHistory.Num() >= MaxEvents)
	{
		EventHistory.RemoveAt(0);
		++SessionMetadata.DroppedEventCount;
		SessionMetadata.bHistoryTruncated = true;
		SessionMetadata.bEventIntegrityCompromised = true;
		UE_LOG(LogEdenTelemetry, Error, TEXT("Telemetry event history overflow; integrity compromised."));
	}

	EventHistory.Add(Event);
	SessionMetadata.LastAvailableSequence = Event.SequenceNumber;
	if (SessionMetadata.FirstAvailableSequence == 0)
	{
		SessionMetadata.FirstAvailableSequence = Event.SequenceNumber;
	}
}

void UEdenTelemetrySubsystem::UpdateAggregates(const FEdenTelemetrySnapshot& Snapshot)
{
	if (!bHasAggregateSeed)
	{
		SessionMetadata.PeakTemperatureCelsius = Snapshot.Thermal.TemperatureCelsius;
		SessionMetadata.MinimumBatteryChargeFraction = Snapshot.Power.ChargeFraction;
		SessionMetadata.MinimumFuelFraction = Snapshot.Fuel.FuelFraction;
		bHasAggregateSeed = true;
		return;
	}

	SessionMetadata.PeakTemperatureCelsius =
		FMath::Max(SessionMetadata.PeakTemperatureCelsius, Snapshot.Thermal.TemperatureCelsius);
	SessionMetadata.MinimumBatteryChargeFraction =
		FMath::Min(SessionMetadata.MinimumBatteryChargeFraction, Snapshot.Power.ChargeFraction);
	SessionMetadata.MinimumFuelFraction =
		FMath::Min(SessionMetadata.MinimumFuelFraction, Snapshot.Fuel.FuelFraction);
}

void UEdenTelemetrySubsystem::StoreSnapshotIfDue(const FEdenTelemetrySnapshot& Snapshot)
{
	++StepsSinceSnapshot;
	if (StepsSinceSnapshot < SnapshotDecimationSteps)
	{
		return;
	}

	StepsSinceSnapshot = 0;
	FEdenTelemetrySnapshot Stored = Snapshot;
	Stored.SequenceNumber = NextSequence();

	if (SnapshotHistory.Num() >= MaxSnapshots)
	{
		SnapshotHistory.RemoveAt(0);
		++SessionMetadata.DroppedSnapshotCount;
		SessionMetadata.bHistoryTruncated = true;
	}

	SnapshotHistory.Add(Stored);
	SessionMetadata.LastAvailableSequence = Stored.SequenceNumber;
	if (SessionMetadata.FirstAvailableSequence == 0)
	{
		SessionMetadata.FirstAvailableSequence = Stored.SequenceNumber;
	}
}

FEdenTelemetrySnapshot UEdenTelemetrySubsystem::AssembleSnapshot() const
{
	FEdenTelemetrySnapshot Snapshot;
	if (const UWorld* World = GetWorld())
	{
		if (const UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			Snapshot.SimulationTimeSeconds = Clock->GetElapsedSimulationTimeSeconds();
		}
	}

	if (const UEdenMissionSubsystem* Mission = BoundMission.Get())
	{
		Snapshot.Mission = Mission->GetMissionStateSnapshot();
		Snapshot.MissionElapsedTimeSeconds = Mission->GetMissionElapsedTimeSeconds();
	}
	if (const UEdenFuelSystemComponent* Fuel = BoundFuel.Get())
	{
		Snapshot.Fuel = Fuel->GetFuelStateSnapshot();
	}
	if (const UEdenPowerSystemComponent* Power = BoundPower.Get())
	{
		Snapshot.Power = Power->GetPowerStateSnapshot();
	}
	if (const UEdenThermalSystemComponent* Thermal = BoundThermal.Get())
	{
		Snapshot.Thermal = Thermal->GetThermalStateSnapshot();
	}
	if (const UEdenOperatorControlComponent* Operator = BoundOperator.Get())
	{
		Snapshot.Operator = Operator->GetOperatorStateSnapshot();
	}
	if (const UEdenFlightMovementComponent* Movement = BoundMovement.Get())
	{
		Snapshot.Flight.ThrustAuthority = Movement->GetThrustAuthority();
		Snapshot.Flight.bStabilizationAssistAvailable = Movement->IsStabilizationAssistAvailable();
		Snapshot.Flight.PropulsionDemandNormalized = Movement->GetPropulsionDemandNormalized();
	}

	return Snapshot;
}

int64 UEdenTelemetrySubsystem::NextSequence()
{
	return NextSequenceNumber++;
}

void UEdenTelemetrySubsystem::HandleMissionStateChanged(EEdenMissionState PreviousState, EEdenMissionState NewState)
{
	EEdenTelemetryEventType Type = EEdenTelemetryEventType::None;
	switch (NewState)
	{
	case EEdenMissionState::Running:
		Type = EEdenTelemetryEventType::MissionStarted;
		break;
	case EEdenMissionState::Succeeded:
		Type = EEdenTelemetryEventType::MissionSucceeded;
		break;
	case EEdenMissionState::Failed:
		Type = EEdenTelemetryEventType::MissionFailed;
		break;
	case EEdenMissionState::Inactive:
		if (PreviousState == EEdenMissionState::Running)
		{
			Type = EEdenTelemetryEventType::MissionAborted;
		}
		break;
	default:
		break;
	}

	if (Type != EEdenTelemetryEventType::None)
	{
		RecordEvent(
			Type,
			TEXT("Mission"),
			*UEnum::GetValueAsString(NewState),
			FString::Printf(TEXT("%s -> %s"), *UEnum::GetValueAsString(PreviousState), *UEnum::GetValueAsString(NewState)));
	}
}

void UEdenTelemetrySubsystem::HandleMissionPhaseChanged(EEdenMissionPhase PreviousPhase, EEdenMissionPhase NewPhase)
{
	RecordEvent(
		EEdenTelemetryEventType::PhaseChanged,
		TEXT("Mission"),
		*UEnum::GetValueAsString(NewPhase),
		FString::Printf(TEXT("%s -> %s"), *UEnum::GetValueAsString(PreviousPhase), *UEnum::GetValueAsString(NewPhase)));
}

void UEdenTelemetrySubsystem::HandleObjectiveStateChanged(
	FName ObjectiveId,
	EEdenObjectiveState PreviousState,
	EEdenObjectiveState NewState)
{
	RecordEvent(
		EEdenTelemetryEventType::ObjectiveStateChanged,
		TEXT("Mission"),
		ObjectiveId,
		FString::Printf(TEXT("%s -> %s"), *UEnum::GetValueAsString(PreviousState), *UEnum::GetValueAsString(NewState)));
}

void UEdenTelemetrySubsystem::HandleOperatorIntentChanged(FEdenOperatorIntent PreviousIntent, FEdenOperatorIntent NewIntent)
{
	(void)PreviousIntent;
	RecordEvent(
		EEdenTelemetryEventType::OperatorCommandIssued,
		TEXT("Operator"),
		TEXT("IntentChanged"),
		FString::Printf(
			TEXT("Thermal=%s Shed=%s Propulsion=%s"),
			*UEnum::GetValueAsString(NewIntent.ThermalMode),
			*UEnum::GetValueAsString(NewIntent.LoadShedMode),
			*UEnum::GetValueAsString(NewIntent.PropulsionPriority)));
}

void UEdenTelemetrySubsystem::HandleAlertRaised(FEdenAlert Alert)
{
	RecordEvent(EEdenTelemetryEventType::AlertRaised, Alert.SourceSystem, Alert.AlertId, Alert.DisplayText.ToString());
}

void UEdenTelemetrySubsystem::HandleAlertCleared(FName AlertId)
{
	RecordEvent(EEdenTelemetryEventType::AlertCleared, TEXT("Alert"), AlertId, TEXT("cleared"));
}

void UEdenTelemetrySubsystem::HandleFuelStateChanged(EEdenFuelState PreviousState, EEdenFuelState NewState)
{
	RecordEvent(
		EEdenTelemetryEventType::ResourceStateTransition,
		TEXT("Fuel"),
		*UEnum::GetValueAsString(NewState),
		FString::Printf(TEXT("%s -> %s"), *UEnum::GetValueAsString(PreviousState), *UEnum::GetValueAsString(NewState)));
}

void UEdenTelemetrySubsystem::HandlePowerStateChanged(EEdenPowerState PreviousState, EEdenPowerState NewState)
{
	RecordEvent(
		EEdenTelemetryEventType::ResourceStateTransition,
		TEXT("Power"),
		*UEnum::GetValueAsString(NewState),
		FString::Printf(TEXT("%s -> %s"), *UEnum::GetValueAsString(PreviousState), *UEnum::GetValueAsString(NewState)));
}

void UEdenTelemetrySubsystem::HandleThermalStateChanged(EEdenThermalState PreviousState, EEdenThermalState NewState)
{
	RecordEvent(
		EEdenTelemetryEventType::ResourceStateTransition,
		TEXT("Thermal"),
		*UEnum::GetValueAsString(NewState),
		FString::Printf(TEXT("%s -> %s"), *UEnum::GetValueAsString(PreviousState), *UEnum::GetValueAsString(NewState)));
}
