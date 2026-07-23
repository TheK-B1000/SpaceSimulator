// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenPowerThermalSystemTestListener.h"

void UEdenPowerSystemTestListener::HandlePowerStateChanged(EEdenPowerState PreviousState, EEdenPowerState NewState)
{
	PreviousStates.Add(PreviousState);
	NewStates.Add(NewState);
}

void UEdenPowerSystemTestListener::HandlePowerDepleted()
{
	++DepletedEventCount;
}

void UEdenThermalSystemTestListener::HandleThermalStateChanged(
	EEdenThermalState PreviousState,
	EEdenThermalState NewState)
{
	PreviousStates.Add(PreviousState);
	NewStates.Add(NewState);
}

void UEdenThermalSystemTestListener::HandleThermalOverheated()
{
	++OverheatedEventCount;
}
