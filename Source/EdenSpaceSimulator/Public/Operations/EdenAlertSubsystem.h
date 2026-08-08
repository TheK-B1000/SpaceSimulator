// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Operations/EdenAlertTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/EdenResourceTypes.h"
#include "Missions/EdenMissionTypes.h"

#include "EdenAlertSubsystem.generated.h"

class AEdenSpacecraftPawn;
class UEdenFuelSystemComponent;
class UEdenMissionSubsystem;
class UEdenPowerSystemComponent;
class UEdenSimulationClockSubsystem;
class UEdenThermalSystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEdenAlertRaisedSignature, FEdenAlert, Alert);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEdenAlertClearedSignature, FName, AlertId);

UCLASS()
class EDENSPACESIMULATOR_API UEdenAlertSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	UFUNCTION(BlueprintPure, Category = "Eden|Alerts")
	TArray<FEdenAlert> GetActiveAlerts() const;

	UFUNCTION(BlueprintCallable, Category = "Eden|Alerts")
	bool AcknowledgeAlert(FName AlertId);

	UFUNCTION(BlueprintCallable, Category = "Eden|Alerts")
	void ClearAllAlerts();

	UPROPERTY(BlueprintAssignable, Category = "Eden|Alerts")
	FEdenAlertRaisedSignature OnAlertRaised;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Alerts")
	FEdenAlertClearedSignature OnAlertCleared;

private:
	void BindToTargets();
	void UnbindFromTargets();
	void EnsureBound();
	float GetSimulationTimeSeconds() const;
	void RaiseAlert(FName AlertId, EEdenAlertSeverity Severity, FName SourceSystem, const FText& DisplayText);
	void ClearAlert(FName AlertId);
	void EnforceAlertCap();

	UFUNCTION()
	void HandleFuelStateChanged(EEdenFuelState PreviousState, EEdenFuelState NewState);

	UFUNCTION()
	void HandlePowerStateChanged(EEdenPowerState PreviousState, EEdenPowerState NewState);

	UFUNCTION()
	void HandleThermalStateChanged(EEdenThermalState PreviousState, EEdenThermalState NewState);

	UFUNCTION()
	void HandleMissionStateChanged(EEdenMissionState PreviousState, EEdenMissionState NewState);

	UPROPERTY(Transient)
	TArray<FEdenAlert> ActiveAlerts;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenFuelSystemComponent> BoundFuel;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenPowerSystemComponent> BoundPower;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenThermalSystemComponent> BoundThermal;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEdenMissionSubsystem> BoundMission;

	static constexpr int32 MaxActiveAlerts = 8;
};
