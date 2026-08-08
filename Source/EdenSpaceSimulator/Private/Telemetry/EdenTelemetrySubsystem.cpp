// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenTelemetrySubsystem.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Engine/World.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Operations/EdenAlertSubsystem.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Telemetry/EdenTelemetryExportModel.h"

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
	ActiveSessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
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

FString UEdenTelemetrySubsystem::GetSessionId() const
{
	return ActiveSessionId;
}

FString UEdenTelemetrySubsystem::ExportSessionJsonV1() const
{
	FName MissionId = NAME_None;
	if (const UEdenMissionSubsystem* Mission = BoundMission.Get())
	{
		MissionId = Mission->GetMissionStateSnapshot().ActiveMissionId;
	}
	else if (SnapshotHistory.Num() > 0)
	{
		MissionId = SnapshotHistory.Last().Mission.ActiveMissionId;
	}

	return FEdenTelemetryExportModel::BuildSessionJsonV1(
		EventHistory,
		SnapshotHistory,
		SessionMetadata,
		ActiveSessionId.IsEmpty() ? TEXT("unknown-session") : ActiveSessionId,
		MissionId);
}

FString UEdenTelemetrySubsystem::WriteSessionJsonV1ToDisk() const
{
	const FString Json = ExportSessionJsonV1();
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry"));
	IFileManager::Get().MakeDirectory(*Directory, true);

	const FString SafeSessionId = ActiveSessionId.IsEmpty() ? TEXT("unknown-session") : ActiveSessionId;
	const FString Filename = FString::Printf(TEXT("telemetry_%s.json"), *SafeSessionId);
	const FString AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, Filename));

	if (!FFileHelper::SaveStringToFile(Json, *AbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogEdenTelemetry, Error, TEXT("Failed to write telemetry export to '%s'."), *AbsolutePath);
		return FString();
	}

	UE_LOG(LogEdenTelemetry, Log, TEXT("Wrote telemetry export '%s'."), *AbsolutePath);
	return AbsolutePath;
}

FEdenAfterActionResult UEdenTelemetrySubsystem::BuildAfterActionResult() const
{
	return FEdenAfterActionModel::Build(EventHistory, SnapshotHistory, SessionMetadata);
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
	RecordEvent(
		EEdenTelemetryEventType::AlertRaised,
		Alert.SourceSystem,
		Alert.AlertId,
		FString::Printf(
			TEXT("[%s] %s"),
			*UEnum::GetValueAsString(Alert.Severity),
			*Alert.DisplayText.ToString()));
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
