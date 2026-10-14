// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorControlModel.h"

namespace EdenOperatorControlModelPrivate
{
void AddValidationError(TArray<FString>* OutErrors, const FString& ErrorMessage)
{
	if (OutErrors)
	{
		OutErrors->Add(ErrorMessage);
	}
}
}

bool FEdenOperatorControlModel::ValidateConfig(const FEdenOperatorControlConfig& Config, TArray<FString>* OutErrors)
{
	bool bIsValid = true;

	const float NonNegativeFields[] = {
		Config.NominalDissipationDegreesCelsiusPerSecond,
		Config.NominalCoolingDemandKilowatts,
		Config.BoostDissipationDegreesCelsiusPerSecond,
		Config.EmergencyDissipationDegreesCelsiusPerSecond,
		Config.BoostCoolingDemandKilowatts,
		Config.EmergencyCoolingDemandKilowatts,
		Config.LoadShedDemandReductionKilowatts,
		Config.LoadShedDissipationReductionDegreesCelsiusPerSecond,
		Config.ReducedThrustAuthority};

	for (const float Value : NonNegativeFields)
	{
		if (!FMath::IsFinite(Value) || Value < 0.0f)
		{
			bIsValid = false;
			EdenOperatorControlModelPrivate::AddValidationError(
				OutErrors,
				TEXT("Operator control config fields must be finite and nonnegative."));
			break;
		}
	}

	if (!FMath::IsFinite(Config.ReducedThrustAuthority)
		|| Config.ReducedThrustAuthority < 0.0f
		|| Config.ReducedThrustAuthority > 1.0f)
	{
		bIsValid = false;
		EdenOperatorControlModelPrivate::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("ReducedThrustAuthority must be within [0, 1]. Value=%f"),
				Config.ReducedThrustAuthority));
	}

	if (Config.EmergencyDissipationDegreesCelsiusPerSecond + KINDA_SMALL_NUMBER
		< Config.BoostDissipationDegreesCelsiusPerSecond)
	{
		bIsValid = false;
		EdenOperatorControlModelPrivate::AddValidationError(
			OutErrors,
			TEXT("EmergencyDissipationDegreesCelsiusPerSecond must be >= BoostDissipationDegreesCelsiusPerSecond."));
	}

	if (Config.EmergencyCoolingDemandKilowatts + KINDA_SMALL_NUMBER < Config.BoostCoolingDemandKilowatts)
	{
		bIsValid = false;
		EdenOperatorControlModelPrivate::AddValidationError(
			OutErrors,
			TEXT("EmergencyCoolingDemandKilowatts must be >= BoostCoolingDemandKilowatts."));
	}

	return bIsValid;
}

FEdenOperatorResolvedModifiers FEdenOperatorControlModel::ResolveIntent(
	const FEdenOperatorIntent& Intent,
	const FEdenOperatorControlConfig& Config)
{
	FEdenOperatorResolvedModifiers Modifiers;

	if (!ValidateConfig(Config))
	{
		return Modifiers;
	}

	// Each mode selects one active-cooling level and its electrical cost (they replace, not stack).
	switch (Intent.ThermalMode)
	{
	case EEdenThermalControlMode::Nominal:
		Modifiers.ActiveCoolingDegreesCelsiusPerSecond = Config.NominalDissipationDegreesCelsiusPerSecond;
		Modifiers.ThermalControlDemandKilowatts = Config.NominalCoolingDemandKilowatts;
		break;
	case EEdenThermalControlMode::Boost:
		Modifiers.ActiveCoolingDegreesCelsiusPerSecond = Config.BoostDissipationDegreesCelsiusPerSecond;
		Modifiers.ThermalControlDemandKilowatts = Config.BoostCoolingDemandKilowatts;
		break;
	case EEdenThermalControlMode::Emergency:
		Modifiers.ActiveCoolingDegreesCelsiusPerSecond = Config.EmergencyDissipationDegreesCelsiusPerSecond;
		Modifiers.ThermalControlDemandKilowatts = Config.EmergencyCoolingDemandKilowatts;
		break;
	case EEdenThermalControlMode::Off:
	default:
		// Passive radiation only.
		break;
	}
	Modifiers.OperatorDissipationDegreesCelsiusPerSecond += Modifiers.ActiveCoolingDegreesCelsiusPerSecond;
	Modifiers.OperatorDemandKilowatts += Modifiers.ThermalControlDemandKilowatts;

	if (Intent.LoadShedMode == EEdenLoadShedMode::Shed)
	{
		Modifiers.OperatorDemandKilowatts -= Config.LoadShedDemandReductionKilowatts;
		Modifiers.OperatorDissipationDegreesCelsiusPerSecond -=
			Config.LoadShedDissipationReductionDegreesCelsiusPerSecond;
		Modifiers.bStabilizationAssistAvailable = false;
	}

	Modifiers.ThrustAuthority = Intent.PropulsionPriority == EEdenPropulsionPriorityMode::Reduced
		? Config.ReducedThrustAuthority
		: 1.0f;

	return Modifiers;
}

FEdenOperatorStateSnapshot FEdenOperatorControlModel::MakeSnapshot(
	const FEdenOperatorIntent& Intent,
	const FEdenOperatorResolvedModifiers& Modifiers)
{
	FEdenOperatorStateSnapshot Snapshot;
	Snapshot.ThermalMode = Intent.ThermalMode;
	Snapshot.LoadShedMode = Intent.LoadShedMode;
	Snapshot.PropulsionPriority = Intent.PropulsionPriority;
	Snapshot.OperatorDemandKilowatts = Modifiers.OperatorDemandKilowatts;
	Snapshot.OperatorDissipationDegreesCelsiusPerSecond = Modifiers.OperatorDissipationDegreesCelsiusPerSecond;
	Snapshot.ThrustAuthority = Modifiers.ThrustAuthority;
	Snapshot.bStabilizationAssistAvailable = Modifiers.bStabilizationAssistAvailable;
	return Snapshot;
}
