// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Systems/EdenResourceTypes.h"

struct EDENSPACESIMULATOR_API FEdenSimulationClockDebugSnapshot
{
	bool bClockAvailable = false;
	float ElapsedSimulationTimeSeconds = 0.0f;
	float FixedStepSeconds = 0.0f;
	bool bPaused = false;
	int32 SubscriberCount = 0;
	int32 LastDroppedSteps = 0;
};

struct EDENSPACESIMULATOR_API FEdenFuelDebugSnapshot
{
	bool bComponentAvailable = false;
	bool bConfigurationValid = false;
	bool bRegisteredWithClock = false;
	float FuelQuantityKilograms = 0.0f;
	float CapacityKilograms = 0.0f;
	float FuelPercent = 0.0f;
	float PropulsionDemandNormalized = 0.0f;
	EEdenFuelState FuelState = EEdenFuelState::Depleted;
};

struct EDENSPACESIMULATOR_API FEdenPowerDebugSnapshot
{
	bool bComponentAvailable = false;
	bool bConfigurationValid = false;
	bool bRegisteredWithClock = false;
	float BatteryChargeKilowattHours = 0.0f;
	float BatteryCapacityKilowattHours = 0.0f;
	float ChargePercent = 0.0f;
	float GenerationKilowatts = 0.0f;
	float DemandKilowatts = 0.0f;
	float NetPowerKilowatts = 0.0f;
	EEdenPowerState PowerState = EEdenPowerState::Depleted;
};

struct EDENSPACESIMULATOR_API FEdenThermalDebugSnapshot
{
	bool bComponentAvailable = false;
	bool bConfigurationValid = false;
	bool bRegisteredWithClock = false;
	float TemperatureCelsius = 0.0f;
	float AmbientTemperatureCelsius = 0.0f;
	float HeatGenerationDegreesCelsiusPerSecond = 0.0f;
	float DissipationDegreesCelsiusPerSecond = 0.0f;
	EEdenThermalState ThermalState = EEdenThermalState::Overheated;
};

struct EDENSPACESIMULATOR_API FEdenSpacecraftSystemsDebugSnapshot
{
	FEdenSimulationClockDebugSnapshot Clock;
	FEdenFuelDebugSnapshot Fuel;
	FEdenPowerDebugSnapshot Power;
	FEdenThermalDebugSnapshot Thermal;
};
