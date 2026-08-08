// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorHudTypes.h"

FEdenOperatorHudSnapshot FEdenOperatorHudModel::Assemble(
	const FEdenMissionStateSnapshot& Mission,
	const FEdenFuelStateSnapshot& Fuel,
	const FEdenPowerStateSnapshot& Power,
	const FEdenThermalStateSnapshot& Thermal,
	const FEdenOperatorStateSnapshot& Operator,
	const TArray<FEdenAlert>& Alerts,
	float SimulationTimeSeconds)
{
	FEdenOperatorHudSnapshot Snapshot;
	Snapshot.MissionId = Mission.ActiveMissionId;
	Snapshot.MissionState = Mission.MissionState;
	Snapshot.MissionPhase = Mission.MissionPhase;
	Snapshot.MissionElapsedTimeSeconds = Mission.MissionElapsedTimeSeconds;
	Snapshot.Objectives = Mission.ObjectiveSnapshots;
	Snapshot.Fuel = Fuel;
	Snapshot.Power = Power;
	Snapshot.Thermal = Thermal;
	Snapshot.Operator = Operator;
	Snapshot.ActiveAlerts = Alerts;
	Snapshot.AssembledAtSimTimeSeconds = SimulationTimeSeconds;
	return Snapshot;
}
