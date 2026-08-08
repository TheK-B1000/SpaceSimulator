// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Missions/EdenMissionTypes.h"

#include "EdenMissionTestListener.generated.h"

UCLASS()
class UEdenMissionTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleMissionStateChanged(EEdenMissionState PreviousState, EEdenMissionState NewState)
	{
		PreviousStates.Add(PreviousState);
		NewStates.Add(NewState);
	}

	UFUNCTION()
	void HandleMissionPhaseChanged(EEdenMissionPhase PreviousPhase, EEdenMissionPhase NewPhase)
	{
		PreviousPhases.Add(PreviousPhase);
		NewPhases.Add(NewPhase);
	}

	UFUNCTION()
	void HandleMissionEventTriggered(FName EventId)
	{
		TriggeredEvents.Add(EventId);
	}

	UPROPERTY()
	TArray<EEdenMissionState> PreviousStates;

	UPROPERTY()
	TArray<EEdenMissionState> NewStates;

	UPROPERTY()
	TArray<EEdenMissionPhase> PreviousPhases;

	UPROPERTY()
	TArray<EEdenMissionPhase> NewPhases;

	UPROPERTY()
	TArray<FName> TriggeredEvents;
};
