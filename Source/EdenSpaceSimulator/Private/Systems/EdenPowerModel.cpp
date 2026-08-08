// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenPowerModel.h"

namespace EdenPowerModel
{
void AddValidationError(TArray<FString>* OutErrors, const FString& ErrorMessage)
{
	if (OutErrors)
	{
		OutErrors->Add(ErrorMessage);
	}
}
}

bool FEdenPowerModel::ValidateConfig(const FEdenPowerConfig& Config, TArray<FString>* OutErrors)
{
	bool bIsValid = true;

	if (!FMath::IsFinite(Config.BatteryCapacityKilowattHours) || Config.BatteryCapacityKilowattHours <= 0.0f)
	{
		bIsValid = false;
		EdenPowerModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("BatteryCapacityKilowattHours must be positive and finite. Value=%f"),
				Config.BatteryCapacityKilowattHours));
	}

	if (!FMath::IsFinite(Config.GenerationKilowatts) || Config.GenerationKilowatts < 0.0f)
	{
		bIsValid = false;
		EdenPowerModel::AddValidationError(
			OutErrors,
			FString::Printf(TEXT("GenerationKilowatts must be nonnegative and finite. Value=%f"), Config.GenerationKilowatts));
	}

	if (!FMath::IsFinite(Config.BaselineDemandKilowatts) || Config.BaselineDemandKilowatts < 0.0f)
	{
		bIsValid = false;
		EdenPowerModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("BaselineDemandKilowatts must be nonnegative and finite. Value=%f"),
				Config.BaselineDemandKilowatts));
	}

	if (!FMath::IsFinite(Config.InitialChargeFraction)
		|| Config.InitialChargeFraction < 0.0f
		|| Config.InitialChargeFraction > 1.0f)
	{
		bIsValid = false;
		EdenPowerModel::AddValidationError(
			OutErrors,
			FString::Printf(TEXT("InitialChargeFraction must be within [0, 1]. Value=%f"), Config.InitialChargeFraction));
	}

	const bool bCriticalThresholdValid = FMath::IsFinite(Config.CriticalThresholdFraction)
		&& Config.CriticalThresholdFraction >= 0.0f
		&& Config.CriticalThresholdFraction <= 1.0f;
	const bool bWarningThresholdValid = FMath::IsFinite(Config.WarningThresholdFraction)
		&& Config.WarningThresholdFraction >= 0.0f
		&& Config.WarningThresholdFraction <= 1.0f;

	if (!bCriticalThresholdValid || !bWarningThresholdValid)
	{
		bIsValid = false;
		EdenPowerModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("CriticalThresholdFraction and WarningThresholdFraction must be finite values within [0, 1]. Critical=%f Warning=%f"),
				Config.CriticalThresholdFraction,
				Config.WarningThresholdFraction));
	}
	else if (!(Config.CriticalThresholdFraction < Config.WarningThresholdFraction))
	{
		bIsValid = false;
		EdenPowerModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("Power thresholds must satisfy 0 <= CriticalThresholdFraction < WarningThresholdFraction <= 1. Critical=%f Warning=%f"),
				Config.CriticalThresholdFraction,
				Config.WarningThresholdFraction));
	}

	return bIsValid;
}

bool FEdenPowerModel::IsValidDeltaTime(float DeltaTimeSeconds)
{
	return FMath::IsFinite(DeltaTimeSeconds) && DeltaTimeSeconds > 0.0f;
}

float FEdenPowerModel::SanitizeNonnegativeKilowatts(float Kilowatts, bool* bOutWasSanitized)
{
	bool bWasSanitized = false;
	float SanitizedKilowatts = Kilowatts;

	if (!FMath::IsFinite(SanitizedKilowatts))
	{
		SanitizedKilowatts = 0.0f;
		bWasSanitized = true;
	}
	else if (SanitizedKilowatts < 0.0f)
	{
		SanitizedKilowatts = 0.0f;
		bWasSanitized = true;
	}

	if (bOutWasSanitized)
	{
		*bOutWasSanitized = bWasSanitized;
	}

	return SanitizedKilowatts;
}

float FEdenPowerModel::ClampBatteryChargeKilowattHours(
	float BatteryChargeKilowattHours,
	float BatteryCapacityKilowattHours,
	bool* bOutWasSanitized)
{
	bool bWasSanitized = false;
	float SanitizedCharge = BatteryChargeKilowattHours;

	if (!FMath::IsFinite(SanitizedCharge))
	{
		SanitizedCharge = 0.0f;
		bWasSanitized = true;
	}

	const float SafeCapacity = FMath::IsFinite(BatteryCapacityKilowattHours) && BatteryCapacityKilowattHours > 0.0f
		? BatteryCapacityKilowattHours
		: 0.0f;
	const float ClampedCharge = FMath::Clamp(SanitizedCharge, 0.0f, SafeCapacity);
	bWasSanitized = bWasSanitized || !FMath::IsNearlyEqual(ClampedCharge, BatteryChargeKilowattHours);

	if (bOutWasSanitized)
	{
		*bOutWasSanitized = bWasSanitized;
	}

	return ClampedCharge;
}

EEdenPowerState FEdenPowerModel::DerivePowerState(const FEdenPowerConfig& Config, float BatteryChargeKilowattHours)
{
	if (!ValidateConfig(Config))
	{
		return EEdenPowerState::Depleted;
	}

	const float SafeChargeKilowattHours =
		ClampBatteryChargeKilowattHours(BatteryChargeKilowattHours, Config.BatteryCapacityKilowattHours);
	if (SafeChargeKilowattHours <= 0.0f)
	{
		return EEdenPowerState::Depleted;
	}

	const float ChargeFraction = SafeChargeKilowattHours / Config.BatteryCapacityKilowattHours;
	if (ChargeFraction <= Config.CriticalThresholdFraction)
	{
		return EEdenPowerState::Critical;
	}

	if (ChargeFraction <= Config.WarningThresholdFraction)
	{
		return EEdenPowerState::Warning;
	}

	return EEdenPowerState::Normal;
}

FEdenPowerStateSnapshot FEdenPowerModel::MakeSnapshot(
	const FEdenPowerConfig& Config,
	float BatteryChargeKilowattHours,
	float GenerationKilowatts,
	float BaselineDemandKilowatts,
	float ExternalDemandKilowatts)
{
	FEdenPowerStateSnapshot Snapshot;

	if (!ValidateConfig(Config))
	{
		return Snapshot;
	}

	bool bGenerationWasSanitized = false;
	bool bBaselineDemandWasSanitized = false;
	bool bExternalDemandWasSanitized = false;
	Snapshot.GenerationKilowatts = SanitizeNonnegativeKilowatts(GenerationKilowatts, &bGenerationWasSanitized);
	Snapshot.BaselineDemandKilowatts = SanitizeNonnegativeKilowatts(BaselineDemandKilowatts, &bBaselineDemandWasSanitized);
	Snapshot.ExternalDemandKilowatts = SanitizeNonnegativeKilowatts(ExternalDemandKilowatts, &bExternalDemandWasSanitized);
	Snapshot.NetPowerKilowatts = Snapshot.GenerationKilowatts - (Snapshot.BaselineDemandKilowatts + Snapshot.ExternalDemandKilowatts);
	Snapshot.BatteryChargeKilowattHours =
		ClampBatteryChargeKilowattHours(BatteryChargeKilowattHours, Config.BatteryCapacityKilowattHours);
	Snapshot.ChargeFraction = Config.BatteryCapacityKilowattHours > 0.0f
		? Snapshot.BatteryChargeKilowattHours / Config.BatteryCapacityKilowattHours
		: 0.0f;
	Snapshot.PowerState = DerivePowerState(Config, Snapshot.BatteryChargeKilowattHours);

	return Snapshot;
}

FEdenPowerStateSnapshot FEdenPowerModel::MakeInitialSnapshot(const FEdenPowerConfig& Config)
{
	if (!ValidateConfig(Config))
	{
		return FEdenPowerStateSnapshot();
	}

	return MakeSnapshot(
		Config,
		Config.BatteryCapacityKilowattHours * Config.InitialChargeFraction,
		Config.GenerationKilowatts,
		Config.BaselineDemandKilowatts,
		0.0f);
}

FEdenPowerStepResult FEdenPowerModel::Step(
	const FEdenPowerConfig& Config,
	const FEdenPowerStateSnapshot& CurrentSnapshot,
	float DeltaTimeSeconds)
{
	FEdenPowerStepResult Result;
	Result.Snapshot = MakeSnapshot(
		Config,
		CurrentSnapshot.BatteryChargeKilowattHours,
		CurrentSnapshot.GenerationKilowatts,
		CurrentSnapshot.BaselineDemandKilowatts,
		CurrentSnapshot.ExternalDemandKilowatts);

	if (!ValidateConfig(Config))
	{
		Result.bConfigWasValid = false;
		return Result;
	}

	bool bGenerationWasSanitized = false;
	bool bBaselineDemandWasSanitized = false;
	bool bExternalDemandWasSanitized = false;
	const float GenerationKilowatts =
		SanitizeNonnegativeKilowatts(CurrentSnapshot.GenerationKilowatts, &bGenerationWasSanitized);
	const float BaselineDemandKilowatts =
		SanitizeNonnegativeKilowatts(CurrentSnapshot.BaselineDemandKilowatts, &bBaselineDemandWasSanitized);
	const float ExternalDemandKilowatts =
		SanitizeNonnegativeKilowatts(CurrentSnapshot.ExternalDemandKilowatts, &bExternalDemandWasSanitized);
	Result.bGenerationWasSanitized = bGenerationWasSanitized;
	Result.bBaselineDemandWasSanitized = bBaselineDemandWasSanitized;
	Result.bExternalDemandWasSanitized = bExternalDemandWasSanitized;

	if (!IsValidDeltaTime(DeltaTimeSeconds))
	{
		Result.bDeltaTimeWasValid = false;
		Result.Snapshot = MakeSnapshot(
			Config,
			CurrentSnapshot.BatteryChargeKilowattHours,
			GenerationKilowatts,
			BaselineDemandKilowatts,
			ExternalDemandKilowatts);
		return Result;
	}

	const double TotalDemandKilowatts =
		static_cast<double>(BaselineDemandKilowatts) + static_cast<double>(ExternalDemandKilowatts);
	const double NetPowerKilowatts =
		static_cast<double>(GenerationKilowatts) - TotalDemandKilowatts;
	const double EnergyDeltaKilowattHours =
		NetPowerKilowatts * (static_cast<double>(DeltaTimeSeconds) / 3600.0);

	if (!FMath::IsFinite(EnergyDeltaKilowattHours))
	{
		Result.EnergyDeltaKilowattHours = 0.0f;
		Result.Snapshot = MakeSnapshot(Config, 0.0f, GenerationKilowatts, BaselineDemandKilowatts, ExternalDemandKilowatts);
		return Result;
	}

	const double NextChargeKilowattHours =
		static_cast<double>(Result.Snapshot.BatteryChargeKilowattHours) + EnergyDeltaKilowattHours;
	Result.EnergyDeltaKilowattHours = static_cast<float>(EnergyDeltaKilowattHours);
	Result.Snapshot = MakeSnapshot(
		Config,
		static_cast<float>(NextChargeKilowattHours),
		GenerationKilowatts,
		BaselineDemandKilowatts,
		ExternalDemandKilowatts);

	return Result;
}
