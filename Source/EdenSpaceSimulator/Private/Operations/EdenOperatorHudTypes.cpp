// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorHudTypes.h"

FEdenOperatorHudSnapshot FEdenOperatorHudModel::Assemble(
	const FEdenMissionStateSnapshot& Mission,
	const FEdenFuelStateSnapshot& Fuel,
	const FEdenPowerStateSnapshot& Power,
	const FEdenThermalStateSnapshot& Thermal,
	const FEdenOperatorStateSnapshot& Operator,
	const TArray<FEdenAlert>& Alerts,
	float SimulationTimeSeconds,
	const FEdenOsAcceptedAdvisory& Advisory)
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

	// HUD mirrors adapter-owned advisory facts only — never invents fields.
	Snapshot.bHasAdvisory = Advisory.bIsValid;
	if (Advisory.bIsValid)
	{
		Snapshot.AdvisoryRecommendation = Advisory.Recommendation;
		Snapshot.AdvisoryRationale = Advisory.Rationale;
		Snapshot.AdvisoryId = Advisory.AdvisoryId;
		Snapshot.AdvisoryIssuedSimulationTimeSeconds = Advisory.IssuedSimulationTimeSeconds;
	}

	return Snapshot;
}
