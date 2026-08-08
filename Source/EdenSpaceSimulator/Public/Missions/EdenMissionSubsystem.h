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

UCLASS()
class EDENSPACESIMULATOR_API UEdenMissionSubsystem : public UWorldSubsystem, public IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	// IEdenSimulationTickable
	virtual void AdvanceSimulation(float FixedDeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool LoadMission(const FEdenMissionDefinitionConfig& Definition);

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool StartMission();

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool AbortMission();

	UFUNCTION(BlueprintCallable, Category = "Eden|Mission")
	bool ResetMission();

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

	UEdenThermalSystemComponent* FindThermalComponent() const;
	UEdenPowerSystemComponent* FindPowerComponent() const;

	UPROPERTY(Transient)
	FEdenMissionDefinitionConfig ActiveMissionDefinition;

	UPROPERTY(Transient)
	FEdenMissionRuntimeState CurrentRuntimeState;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenSimulationClockSubsystem> RegisteredSimulationClock;
};
