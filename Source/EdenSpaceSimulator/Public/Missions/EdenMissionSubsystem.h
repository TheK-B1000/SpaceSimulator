// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/EdenSimulationTickable.h"
#include "Missions/EdenMissionModel.h"
#include "Missions/EdenMissionTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "EdenMissionSubsystem.generated.h"

class UEdenSimulationClockSubsystem;
class UEdenThermalSystemComponent;
class UEdenPowerSystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FEdenMissionStateChangedSignature,
	EEdenMissionState,
	PreviousState,
	EEdenMissionState,
	NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FEdenMissionPhaseChangedSignature,
	EEdenMissionPhase,
	PreviousPhase,
	EEdenMissionPhase,
	NewPhase);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FEdenMissionEventTriggeredSignature,
	FName,
	EventId);

/**
 * World-scoped mission orchestrator.
 *
 * Fixed-step ordering (locked):
 * 1) Resource systems advance with modifiers already active.
 * 2) Mission observes completed authoritative state, advances mission time,
 *    identifies due events, and dispatches commands.
 * 3) Newly applied modifiers affect resources on the NEXT fixed step.
 */
UCLASS()
class EDENSPACESIMULATOR_API UEdenMissionSubsystem : public UWorldSubsystem, public IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool LoadMission(const FEdenMissionDefinitionConfig& Definition);

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool StartMission();

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool AbortMission();

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool ResetMission();

	/**
	 * Caches weak, non-owning command targets for Checkpoint E resource dispatch.
	 * Prefer calling this when the mission/runtime relationship is established.
	 * Passing null clears that target slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool SetMissionResourceTargets(UEdenThermalSystemComponent* Thermal, UEdenPowerSystemComponent* Power);

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	void ClearMissionResourceTargets();

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool RegisterWithSimulationClock();

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool UnregisterFromSimulationClock();

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	EEdenMissionState GetMissionState() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	EEdenMissionPhase GetMissionPhase() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	float GetMissionElapsedTimeSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	FName GetActiveMissionId() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	bool IsMissionRunning() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Mission")
	FEdenMissionStateSnapshot GetMissionStateSnapshot() const;

	FEdenMissionRuntimeState GetMissionRuntimeState() const;
	FEdenMissionDefinitionConfig GetActiveMissionDefinition() const;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Mission")
	FEdenMissionStateChangedSignature OnMissionStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Mission")
	FEdenMissionPhaseChangedSignature OnMissionPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Mission")
	FEdenMissionEventTriggeredSignature OnMissionEventTriggered;

private:
	void TransitionMissionState(EEdenMissionState NewState);
	void TransitionMissionPhase(EEdenMissionPhase NewPhase);
	void ExecuteMissionEvent(const FEdenMissionEventConfig& EventConfig);
	void ClearMissionExternalModifiers();
	bool TryResolveResourceTargetsFromPossessedSpacecraft();
	UEdenThermalSystemComponent* GetThermalTarget() const;
	UEdenPowerSystemComponent* GetPowerTarget() const;
	bool IsFiniteCommandPayload(float Value) const;

	UPROPERTY(Transient)
	FEdenMissionDefinitionConfig ActiveMissionDefinition;

	UPROPERTY(Transient)
	FEdenMissionRuntimeState CurrentRuntimeState;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenSimulationClockSubsystem> RegisteredSimulationClock;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenThermalSystemComponent> CachedThermalTarget;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenPowerSystemComponent> CachedPowerTarget;
};
