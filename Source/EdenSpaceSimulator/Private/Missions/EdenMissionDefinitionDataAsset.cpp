// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/EdenMissionDefinitionDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FEdenMissionDefinitionConfig UEdenMissionDefinitionDataAsset::CreateSolarEventEmergencyDefinition()
{
	FEdenMissionDefinitionConfig Config;
	Config.MissionId = FName("SolarCrisis");
	Config.DisplayName = FText::FromString(TEXT("Solar Event Emergency"));

	// Objectives
	FEdenMissionObjectiveConfig SurviveObj;
	SurviveObj.ObjectiveId = FName("SurviveSolarEvent");
	SurviveObj.DisplayName = FText::FromString(TEXT("Survive Solar Event (50s)"));
	SurviveObj.ObjectiveType = EEdenObjectiveType::SurviveUntilTime;
	SurviveObj.TargetValue = 50.0f;
	SurviveObj.bRequired = true;
	SurviveObj.bActivateOnStart = true;
	Config.Objectives.Add(SurviveObj);

	FEdenMissionObjectiveConfig TempObj;
	TempObj.ObjectiveId = FName("PreventOverheating");
	TempObj.DisplayName = FText::FromString(TEXT("Prevent Overheating (< 90 C)"));
	TempObj.ObjectiveType = EEdenObjectiveType::KeepTemperatureBelow;
	TempObj.TargetValue = 90.0f;
	TempObj.bRequired = true;
	TempObj.bActivateOnStart = true;
	Config.Objectives.Add(TempObj);

	FEdenMissionObjectiveConfig PowerObj;
	PowerObj.ObjectiveId = FName("RestoreBatteryCharge");
	PowerObj.DisplayName = FText::FromString(TEXT("Restore Battery Charge (> 50%)"));
	PowerObj.ObjectiveType = EEdenObjectiveType::RestorePowerAbove;
	PowerObj.TargetValue = 0.5f;
	PowerObj.bRequired = true;
	PowerObj.bActivateOnStart = true;
	Config.Objectives.Add(PowerObj);

	FEdenMissionObjectiveConfig FuelObj;
	FuelObj.ObjectiveId = FName("ConservePropellant");
	FuelObj.DisplayName = FText::FromString(TEXT("Conserve Propellant (> 20%)"));
	FuelObj.ObjectiveType = EEdenObjectiveType::MaintainFuelAbove;
	FuelObj.TargetValue = 0.2f;
	FuelObj.bRequired = true;
	FuelObj.bActivateOnStart = true;
	Config.Objectives.Add(FuelObj);

	// Events
	FEdenMissionEventConfig EvtNominal;
	EvtNominal.EventId = FName("NominalStart");
	EvtNominal.TriggerTimeSeconds = 0.0f;
	EvtNominal.CommandType = EEdenMissionCommandType::SetMissionPhase;
	EvtNominal.PhaseParameter = EEdenMissionPhase::Nominal;
	Config.Events.Add(EvtNominal);

	FEdenMissionEventConfig EvtWarning;
	EvtWarning.EventId = FName("SolarWarning");
	EvtWarning.TriggerTimeSeconds = 5.0f;
	EvtWarning.CommandType = EEdenMissionCommandType::SetMissionPhase;
	EvtWarning.PhaseParameter = EEdenMissionPhase::Warning;
	Config.Events.Add(EvtWarning);

	FEdenMissionEventConfig EvtImpactPhase;
	EvtImpactPhase.EventId = FName("SolarImpactPhase");
	EvtImpactPhase.TriggerTimeSeconds = 10.0f;
	EvtImpactPhase.CommandType = EEdenMissionCommandType::SetMissionPhase;
	EvtImpactPhase.PhaseParameter = EEdenMissionPhase::Impact;
	Config.Events.Add(EvtImpactPhase);

	FEdenMissionEventConfig EvtImpactHeat;
	EvtImpactHeat.EventId = FName("SolarImpactHeating");
	EvtImpactHeat.TriggerTimeSeconds = 10.0f;
	EvtImpactHeat.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
	EvtImpactHeat.FloatParameter = 2.0f;
	Config.Events.Add(EvtImpactHeat);

	FEdenMissionEventConfig EvtImpactDemand;
	EvtImpactDemand.EventId = FName("SolarImpactPowerDemand");
	EvtImpactDemand.TriggerTimeSeconds = 10.0f;
	EvtImpactDemand.CommandType = EEdenMissionCommandType::SetExternalPowerDemand;
	EvtImpactDemand.FloatParameter = 1.0f;
	Config.Events.Add(EvtImpactDemand);

	FEdenMissionEventConfig EvtRecoveryPhase;
	EvtRecoveryPhase.EventId = FName("SolarRecoveryPhase");
	EvtRecoveryPhase.TriggerTimeSeconds = 30.0f;
	EvtRecoveryPhase.CommandType = EEdenMissionCommandType::SetMissionPhase;
	EvtRecoveryPhase.PhaseParameter = EEdenMissionPhase::Recovery;
	Config.Events.Add(EvtRecoveryPhase);

	FEdenMissionEventConfig EvtRecoveryHeat;
	EvtRecoveryHeat.EventId = FName("SolarRecoveryClearHeating");
	EvtRecoveryHeat.TriggerTimeSeconds = 30.0f;
	EvtRecoveryHeat.CommandType = EEdenMissionCommandType::ClearExternalHeatingRate;
	Config.Events.Add(EvtRecoveryHeat);

	FEdenMissionEventConfig EvtRecoveryDemand;
	EvtRecoveryDemand.EventId = FName("SolarRecoveryClearDemand");
	EvtRecoveryDemand.TriggerTimeSeconds = 30.0f;
	EvtRecoveryDemand.CommandType = EEdenMissionCommandType::ClearExternalPowerDemand;
	Config.Events.Add(EvtRecoveryDemand);

	FEdenMissionEventConfig EvtResolutionPhase;
	EvtResolutionPhase.EventId = FName("SolarResolutionPhase");
	EvtResolutionPhase.TriggerTimeSeconds = 50.0f;
	EvtResolutionPhase.CommandType = EEdenMissionCommandType::SetMissionPhase;
	EvtResolutionPhase.PhaseParameter = EEdenMissionPhase::Resolved;
	Config.Events.Add(EvtResolutionPhase);

	return Config;
}

#if WITH_EDITOR
EDataValidationResult UEdenMissionDefinitionDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);

	TArray<FString> ValidationErrors;
	if (!FEdenMissionModel::ValidateDefinition(MissionDefinition, &ValidationErrors))
	{
		for (const FString& ValidationError : ValidationErrors)
		{
			Context.AddError(FText::FromString(ValidationError));
		}

		return EDataValidationResult::Invalid;
	}

	return SuperResult == EDataValidationResult::Invalid ? SuperResult : EDataValidationResult::Valid;
}
#endif

