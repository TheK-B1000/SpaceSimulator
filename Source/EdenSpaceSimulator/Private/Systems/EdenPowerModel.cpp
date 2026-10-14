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

	if (!FMath::IsFinite(Config.BatteryMaxDischargeKilowatts) || Config.BatteryMaxDischargeKilowatts < 0.0f)
	{
		bIsValid = false;
		EdenPowerModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("BatteryMaxDischargeKilowatts must be nonnegative and finite (0 = unlimited). Value=%f"),
				Config.BatteryMaxDischargeKilowatts));
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


float FEdenPowerModel::SanitizeFiniteKilowatts(float Kilowatts, bool* bOutWasSanitized)
{
	bool bWasSanitized = false;
	float SanitizedKilowatts = Kilowatts;

	if (!FMath::IsFinite(SanitizedKilowatts))
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
	float ExternalDemandKilowatts,
	float OperatorDemandKilowatts)
{
	FEdenPowerStateSnapshot Snapshot;

	if (!ValidateConfig(Config))
	{
		return Snapshot;
	}

	bool bGenerationWasSanitized = false;
	bool bBaselineDemandWasSanitized = false;
	bool bExternalDemandWasSanitized = false;
	bool bOperatorDemandWasSanitized = false;
	Snapshot.GenerationKilowatts = SanitizeNonnegativeKilowatts(GenerationKilowatts, &bGenerationWasSanitized);
	Snapshot.BaselineDemandKilowatts = SanitizeNonnegativeKilowatts(BaselineDemandKilowatts, &bBaselineDemandWasSanitized);
	Snapshot.ExternalDemandKilowatts = SanitizeNonnegativeKilowatts(ExternalDemandKilowatts, &bExternalDemandWasSanitized);
	Snapshot.OperatorDemandKilowatts = SanitizeFiniteKilowatts(OperatorDemandKilowatts, &bOperatorDemandWasSanitized);
	const double RawTotalDemand =
		static_cast<double>(Snapshot.BaselineDemandKilowatts)
		+ static_cast<double>(Snapshot.ExternalDemandKilowatts)
		+ static_cast<double>(Snapshot.OperatorDemandKilowatts);
	Snapshot.TotalDemandKilowatts = static_cast<float>(FMath::Max(0.0, RawTotalDemand));
	Snapshot.NetPowerKilowatts = Snapshot.GenerationKilowatts - Snapshot.TotalDemandKilowatts;
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

	return WithBusAllocation(
		MakeSnapshot(
			Config,
			Config.BatteryCapacityKilowattHours * Config.InitialChargeFraction,
			Config.GenerationKilowatts,
			Config.BaselineDemandKilowatts,
			0.0f,
			0.0f),
		0.0f,
		Config.BatteryMaxDischargeKilowatts);
}

FEdenPowerBusAllocation FEdenPowerModel::AllocateBus(
	const FEdenPowerStateSnapshot& Snapshot,
	float ThermalControlDemandKilowatts,
	float BatteryMaxDischargeKilowatts,
	double AvailableDischargeKilowatts)
{
	FEdenPowerBusAllocation Bus;

	const double Generation = FMath::Max(0.0, static_cast<double>(Snapshot.GenerationKilowatts));
	const double Baseline = FMath::Max(0.0, static_cast<double>(Snapshot.BaselineDemandKilowatts));
	const double External = FMath::Max(0.0, static_cast<double>(Snapshot.ExternalDemandKilowatts));
	const double Operator = FMath::IsFinite(Snapshot.OperatorDemandKilowatts) ? static_cast<double>(Snapshot.OperatorDemandKilowatts) : 0.0;
	const double Cooling = FMath::IsFinite(ThermalControlDemandKilowatts) ? FMath::Max(0.0, static_cast<double>(ThermalControlDemandKilowatts)) : 0.0;

	// Operator demand = cooling + other operator load - load-shed reduction. Shed removes auxiliary load
	// first; only a reduction larger than all auxiliary load comes off the essential baseline.
	const double ShedReduction = FMath::Max(0.0, Cooling - Operator);
	const double OtherOperatorLoad = FMath::Max(0.0, Operator - Cooling);
	const double AuxiliaryBeforeShed = External + OtherOperatorLoad;
	Bus.AuxiliaryRequestedKilowatts = FMath::Max(0.0, AuxiliaryBeforeShed - ShedReduction);
	Bus.EssentialRequestedKilowatts = FMath::Max(0.0, Baseline - FMath::Max(0.0, ShedReduction - AuxiliaryBeforeShed));
	Bus.CoolingRequestedKilowatts = Cooling;

	const double Limit = FMath::IsFinite(BatteryMaxDischargeKilowatts) ? static_cast<double>(BatteryMaxDischargeKilowatts) : 0.0;
	if (Limit <= 0.0)
	{
		Bus.bLimited = false;
		Bus.EssentialServedKilowatts = Bus.EssentialRequestedKilowatts;
		Bus.CoolingServedKilowatts = Bus.CoolingRequestedKilowatts;
		Bus.AuxiliaryServedKilowatts = Bus.AuxiliaryRequestedKilowatts;
		// Unlimited: delivered is the legacy total so pre-0011 energy accounting is unchanged.
		Bus.DeliveredKilowatts = FMath::Max(0.0, Baseline + External + Operator);
		Bus.CoolingAuthority = 1.0;
		return Bus;
	}

	Bus.bLimited = true;
	double Discharge = Snapshot.BatteryChargeKilowattHours > 0.0f ? Limit : 0.0;
	if (AvailableDischargeKilowatts >= 0.0)
	{
		Discharge = FMath::Min(Discharge, AvailableDischargeKilowatts);
	}
	Bus.CapacityKilowatts = Generation + Discharge;

	double Remaining = Bus.CapacityKilowatts;
	Bus.EssentialServedKilowatts = FMath::Min(Bus.EssentialRequestedKilowatts, Remaining);
	Remaining -= Bus.EssentialServedKilowatts;
	Bus.CoolingServedKilowatts = FMath::Min(Bus.CoolingRequestedKilowatts, Remaining);
	Remaining -= Bus.CoolingServedKilowatts;
	Bus.AuxiliaryServedKilowatts = FMath::Min(Bus.AuxiliaryRequestedKilowatts, Remaining);
	Bus.DeliveredKilowatts = Bus.EssentialServedKilowatts + Bus.CoolingServedKilowatts + Bus.AuxiliaryServedKilowatts;
	Bus.CoolingAuthority = Bus.CoolingRequestedKilowatts > 0.0 ? Bus.CoolingServedKilowatts / Bus.CoolingRequestedKilowatts : 1.0;
	return Bus;
}

FEdenPowerStateSnapshot FEdenPowerModel::WithBusAllocation(
	const FEdenPowerStateSnapshot& InSnapshot,
	float ThermalControlDemandKilowatts,
	float BatteryMaxDischargeKilowatts)
{
	FEdenPowerStateSnapshot Snapshot = InSnapshot;
	Snapshot.ThermalControlDemandKilowatts = FMath::IsFinite(ThermalControlDemandKilowatts) ? FMath::Max(0.0f, ThermalControlDemandKilowatts) : 0.0f;
	Snapshot.BatteryMaxDischargeKilowatts = FMath::IsFinite(BatteryMaxDischargeKilowatts) ? FMath::Max(0.0f, BatteryMaxDischargeKilowatts) : 0.0f;

	const FEdenPowerBusAllocation Bus = AllocateBus(Snapshot, Snapshot.ThermalControlDemandKilowatts, Snapshot.BatteryMaxDischargeKilowatts);
	Snapshot.bBusLimited = Bus.bLimited;
	Snapshot.BusCapacityKilowatts = static_cast<float>(Bus.CapacityKilowatts);
	Snapshot.EssentialRequestedKilowatts = static_cast<float>(Bus.EssentialRequestedKilowatts);
	Snapshot.EssentialServedKilowatts = static_cast<float>(Bus.EssentialServedKilowatts);
	Snapshot.ThermalControlServedKilowatts = static_cast<float>(Bus.CoolingServedKilowatts);
	Snapshot.AuxiliaryRequestedKilowatts = static_cast<float>(Bus.AuxiliaryRequestedKilowatts);
	Snapshot.AuxiliaryServedKilowatts = static_cast<float>(Bus.AuxiliaryServedKilowatts);
	Snapshot.AuxiliaryCurtailedKilowatts = static_cast<float>(FMath::Max(0.0, Bus.AuxiliaryRequestedKilowatts - Bus.AuxiliaryServedKilowatts));
	Snapshot.CoolingAuthority = static_cast<float>(Bus.CoolingAuthority);
	if (Bus.bLimited)
	{
		Snapshot.DeliveredDemandKilowatts = static_cast<float>(Bus.DeliveredKilowatts);
		Snapshot.NetPowerKilowatts = static_cast<float>(static_cast<double>(Snapshot.GenerationKilowatts) - Bus.DeliveredKilowatts);
	}
	else
	{
		Snapshot.DeliveredDemandKilowatts = Snapshot.TotalDemandKilowatts;
	}
	Snapshot.BatteryDischargeKilowatts = FMath::Max(0.0f, -Snapshot.NetPowerKilowatts);
	return Snapshot;
}

FEdenPowerStepResult FEdenPowerModel::Step(
	const FEdenPowerConfig& Config,
	const FEdenPowerStateSnapshot& CurrentSnapshot,
	float DeltaTimeSeconds)
{
	FEdenPowerStepResult Result;
	Result.Snapshot = WithBusAllocation(
		MakeSnapshot(
		Config,
		CurrentSnapshot.BatteryChargeKilowattHours,
		CurrentSnapshot.GenerationKilowatts,
		CurrentSnapshot.BaselineDemandKilowatts,
		CurrentSnapshot.ExternalDemandKilowatts,
		CurrentSnapshot.OperatorDemandKilowatts),
		CurrentSnapshot.ThermalControlDemandKilowatts,
		CurrentSnapshot.BatteryMaxDischargeKilowatts);

	if (!ValidateConfig(Config))
	{
		Result.bConfigWasValid = false;
		return Result;
	}

	bool bGenerationWasSanitized = false;
	bool bBaselineDemandWasSanitized = false;
	bool bExternalDemandWasSanitized = false;
	bool bOperatorDemandWasSanitized = false;
	const float GenerationKilowatts =
		SanitizeNonnegativeKilowatts(CurrentSnapshot.GenerationKilowatts, &bGenerationWasSanitized);
	const float BaselineDemandKilowatts =
		SanitizeNonnegativeKilowatts(CurrentSnapshot.BaselineDemandKilowatts, &bBaselineDemandWasSanitized);
	const float ExternalDemandKilowatts =
		SanitizeNonnegativeKilowatts(CurrentSnapshot.ExternalDemandKilowatts, &bExternalDemandWasSanitized);
	const float OperatorDemandKilowatts =
		SanitizeFiniteKilowatts(CurrentSnapshot.OperatorDemandKilowatts, &bOperatorDemandWasSanitized);
	Result.bGenerationWasSanitized = bGenerationWasSanitized;
	Result.bBaselineDemandWasSanitized = bBaselineDemandWasSanitized;
	Result.bExternalDemandWasSanitized = bExternalDemandWasSanitized;
	Result.bOperatorDemandWasSanitized = bOperatorDemandWasSanitized;

	if (!IsValidDeltaTime(DeltaTimeSeconds))
	{
		Result.bDeltaTimeWasValid = false;
		Result.Snapshot = WithBusAllocation(
		MakeSnapshot(
			Config,
			CurrentSnapshot.BatteryChargeKilowattHours,
			GenerationKilowatts,
			BaselineDemandKilowatts,
			ExternalDemandKilowatts,
			OperatorDemandKilowatts),
		CurrentSnapshot.ThermalControlDemandKilowatts,
		CurrentSnapshot.BatteryMaxDischargeKilowatts);
		return Result;
	}

	const double RawTotalDemandKilowatts =
		static_cast<double>(BaselineDemandKilowatts)
		+ static_cast<double>(ExternalDemandKilowatts)
		+ static_cast<double>(OperatorDemandKilowatts);
	const double TotalDemandKilowatts = FMath::Max(0.0, RawTotalDemandKilowatts);
	double NetPowerKilowatts =
		static_cast<double>(GenerationKilowatts) - TotalDemandKilowatts;
	if (CurrentSnapshot.BatteryMaxDischargeKilowatts > 0.0f)
	{
		// Limited bus: only powered load draws energy, and the battery cannot supply more energy this
		// step than it holds. Each kWh is accounted exactly once, here.
		const double AvailableDischargeKilowatts =
			static_cast<double>(Result.Snapshot.BatteryChargeKilowattHours) * 3600.0 / static_cast<double>(DeltaTimeSeconds);
		const FEdenPowerBusAllocation Bus = AllocateBus(
			Result.Snapshot,
			CurrentSnapshot.ThermalControlDemandKilowatts,
			CurrentSnapshot.BatteryMaxDischargeKilowatts,
			AvailableDischargeKilowatts);
		NetPowerKilowatts = static_cast<double>(GenerationKilowatts) - Bus.DeliveredKilowatts;
	}
	const double EnergyDeltaKilowattHours =
		NetPowerKilowatts * (static_cast<double>(DeltaTimeSeconds) / 3600.0);

	if (!FMath::IsFinite(EnergyDeltaKilowattHours))
	{
		Result.EnergyDeltaKilowattHours = 0.0f;
		Result.Snapshot = WithBusAllocation(
		MakeSnapshot(
			Config,
			0.0f,
			GenerationKilowatts,
			BaselineDemandKilowatts,
			ExternalDemandKilowatts,
			OperatorDemandKilowatts),
		CurrentSnapshot.ThermalControlDemandKilowatts,
		CurrentSnapshot.BatteryMaxDischargeKilowatts);
		return Result;
	}

	const double NextChargeKilowattHours =
		static_cast<double>(Result.Snapshot.BatteryChargeKilowattHours) + EnergyDeltaKilowattHours;
	Result.EnergyDeltaKilowattHours = static_cast<float>(EnergyDeltaKilowattHours);
	Result.Snapshot = WithBusAllocation(
		MakeSnapshot(
		Config,
		static_cast<float>(NextChargeKilowattHours),
		GenerationKilowatts,
		BaselineDemandKilowatts,
		ExternalDemandKilowatts,
		OperatorDemandKilowatts),
		CurrentSnapshot.ThermalControlDemandKilowatts,
		CurrentSnapshot.BatteryMaxDischargeKilowatts);

	return Result;
}
