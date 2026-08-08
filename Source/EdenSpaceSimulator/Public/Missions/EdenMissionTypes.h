// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdenMissionTypes.generated.h"

UENUM(BlueprintType)
enum class EEdenMissionState : uint8
{
	Inactive,
	Ready,
	Running,
	Succeeded,
	Failed
};

UENUM(BlueprintType)
enum class EEdenMissionPhase : uint8
{
	Nominal,
	Warning,
	Impact,
	Recovery,
	Resolved
};

UENUM(BlueprintType)
enum class EEdenMissionCommandType : uint8
{
	None,
	SetMissionPhase,
	SetExternalHeatingRate,
	ClearExternalHeatingRate,
	SetExternalPowerDemand,
	ClearExternalPowerDemand,
	SetPowerGeneration,
	ActivateObjective
};

UENUM(BlueprintType)
enum class EEdenObjectiveType : uint8
{
	SurviveUntilTime,
	KeepTemperatureBelow,
	RestorePowerAbove,
	MaintainFuelAbove
};

UENUM(BlueprintType)
enum class EEdenObjectiveState : uint8
{
	Pending,
	Active,
	Completed,
	Failed
};

UENUM(BlueprintType)
enum class EEdenMissionEventState : uint8
{
	Pending,
	Executed,
	Skipped
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionEventConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName EventId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission", meta = (ClampMin = "0.0", Units = "s"))
	float TriggerTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionCommandType CommandType = EEdenMissionCommandType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float FloatParameter = 0.0f;

	/** Typed phase payload for SetMissionPhase. Do not encode phases through FloatParameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionPhase PhaseParameter = EEdenMissionPhase::Nominal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName NameParameter;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionObjectiveConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName ObjectiveId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenObjectiveType ObjectiveType = EEdenObjectiveType::SurviveUntilTime;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float TargetValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bRequired = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	bool bActivateOnStart = false;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionDefinitionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName MissionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionEventConfig> Events;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionObjectiveConfig> Objectives;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionEventRuntime
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName EventId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionEventState EventState = EEdenMissionEventState::Pending;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionObjectiveRuntime
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName ObjectiveId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenObjectiveState State = EEdenObjectiveState::Pending;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionState MissionState = EEdenMissionState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionPhase MissionPhase = EEdenMissionPhase::Nominal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float MissionElapsedTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionEventRuntime> EventStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionObjectiveRuntime> ObjectiveStates;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenMissionStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionState MissionState = EEdenMissionState::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	EEdenMissionPhase MissionPhase = EEdenMissionPhase::Nominal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	float MissionElapsedTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	FName ActiveMissionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Mission")
	TArray<FEdenMissionObjectiveRuntime> ObjectiveSnapshots;
};
