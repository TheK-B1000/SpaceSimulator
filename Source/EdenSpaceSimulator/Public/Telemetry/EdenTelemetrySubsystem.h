// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"
#include "Subsystems/WorldSubsystem.h"
#include "Telemetry/EdenTelemetryTypes.h"
#include "Telemetry/EdenAfterActionModel.h"
#include "Missions/EdenMissionTypes.h"
#include "Systems/EdenResourceTypes.h"
#include "Operations/EdenOperatorTypes.h"
#include "Operations/EdenAlertTypes.h"

#include "EdenTelemetrySubsystem.generated.h"

class AEdenSpacecraftPawn;
class UEdenAlertSubsystem;
class UEdenFlightMovementComponent;
class UEdenFuelSystemComponent;
class UEdenMissionSubsystem;
class UEdenOperatorControlComponent;
class UEdenPowerSystemComponent;
class UEdenSimulationClockSubsystem;
class UEdenThermalSystemComponent;

UCLASS()
class EDENSPACESIMULATOR_API UEdenTelemetrySubsystem : public UWorldSubsystem, public IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Telemetry")
	void ClearHistory();

	UFUNCTION(BlueprintPure, Category = "Eden|Telemetry")
	TArray<FEdenTelemetryEvent> GetEventHistory() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Telemetry")
	TArray<FEdenTelemetrySnapshot> GetSnapshotHistory() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Telemetry")
	FEdenTelemetrySessionMetadata GetSessionMetadata() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Telemetry")
	FString GetSessionId() const;

	/** Builds Telemetry Export Schema v1 JSON (wire contract for 0007). */
	UFUNCTION(BlueprintCallable, Category = "Eden|Telemetry")
	FString ExportSessionJsonV1() const;

	/**
	 * Writes ExportSessionJsonV1 to Saved/Telemetry/<sessionId>.json.
	 * Returns absolute path on success, empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Eden|Telemetry")
	FString WriteSessionJsonV1ToDisk() const;

	UFUNCTION(BlueprintCallable, Category = "Eden|Telemetry")
	FEdenAfterActionResult BuildAfterActionResult() const;

private:
	void BindSources();
	void UnbindSources();
	void EnsureBound();
	void RecordEvent(EEdenTelemetryEventType EventType, FName SourceSystem, FName EventId, const FString& Detail);
	void UpdateAggregates(const FEdenTelemetrySnapshot& Snapshot);
	void StoreSnapshotIfDue(const FEdenTelemetrySnapshot& Snapshot);
	FEdenTelemetrySnapshot AssembleSnapshot() const;
	int64 NextSequence();

	UFUNCTION()
	void HandleMissionStateChanged(EEdenMissionState PreviousState, EEdenMissionState NewState);

	UFUNCTION()
	void HandleMissionPhaseChanged(EEdenMissionPhase PreviousPhase, EEdenMissionPhase NewPhase);

	UFUNCTION()
	void HandleObjectiveStateChanged(FName ObjectiveId, EEdenObjectiveState PreviousState, EEdenObjectiveState NewState);

	UFUNCTION()
	void HandleOperatorIntentChanged(FEdenOperatorIntent PreviousIntent, FEdenOperatorIntent NewIntent);

	UFUNCTION()
	void HandleAlertRaised(FEdenAlert Alert);

	UFUNCTION()
	void HandleAlertCleared(FName AlertId);

	UFUNCTION()
	void HandleFuelStateChanged(EEdenFuelState PreviousState, EEdenFuelState NewState);

	UFUNCTION()
	void HandlePowerStateChanged(EEdenPowerState PreviousState, EEdenPowerState NewState);

	UFUNCTION()
	void HandleThermalStateChanged(EEdenThermalState PreviousState, EEdenThermalState NewState);

	UPROPERTY(Transient)
	TArray<FEdenTelemetryEvent> EventHistory;

	UPROPERTY(Transient)
	TArray<FEdenTelemetrySnapshot> SnapshotHistory;

	UPROPERTY(Transient)
	FEdenTelemetrySessionMetadata SessionMetadata;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenSimulationClockSubsystem> RegisteredClock;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenMissionSubsystem> BoundMission;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenAlertSubsystem> BoundAlerts;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenOperatorControlComponent> BoundOperator;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenFuelSystemComponent> BoundFuel;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenPowerSystemComponent> BoundPower;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenThermalSystemComponent> BoundThermal;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenFlightMovementComponent> BoundMovement;

	int64 NextSequenceNumber = 1;
	int32 StepsSinceSnapshot = 0;
	int32 SnapshotDecimationSteps = 5;
	int32 MaxEvents = 512;
	int32 MaxSnapshots = 256;
	bool bHasAggregateSeed = false;
	FString ActiveSessionId;
};
