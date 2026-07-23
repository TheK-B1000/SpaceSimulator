// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenFuelModel.h"

namespace EdenFuelModel
{
void AddValidationError(TArray<FString>* OutErrors, const FString& ErrorMessage)
{
	if (OutErrors)
	{
		OutErrors->Add(ErrorMessage);
	}
}
}

bool FEdenFuelModel::ValidateConfig(const FEdenFuelConfig& Config, TArray<FString>* OutErrors)
{
	bool bIsValid = true;

	if (!FMath::IsFinite(Config.CapacityKilograms) || Config.CapacityKilograms <= 0.0f)
	{
		bIsValid = false;
		EdenFuelModel::AddValidationError(
			OutErrors,
			FString::Printf(TEXT("CapacityKilograms must be positive and finite. Value=%f"), Config.CapacityKilograms));
	}

	if (!FMath::IsFinite(Config.ConsumptionRateKilogramsPerSecond) || Config.ConsumptionRateKilogramsPerSecond < 0.0f)
	{
		bIsValid = false;
		EdenFuelModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("ConsumptionRateKilogramsPerSecond must be nonnegative and finite. Value=%f"),
				Config.ConsumptionRateKilogramsPerSecond));
	}

	if (!FMath::IsFinite(Config.InitialFuelFraction)
		|| Config.InitialFuelFraction < 0.0f
		|| Config.InitialFuelFraction > 1.0f)
	{
		bIsValid = false;
		EdenFuelModel::AddValidationError(
			OutErrors,
			FString::Printf(TEXT("InitialFuelFraction must be within [0, 1]. Value=%f"), Config.InitialFuelFraction));
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
		EdenFuelModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("CriticalThresholdFraction and WarningThresholdFraction must be finite values within [0, 1]. Critical=%f Warning=%f"),
				Config.CriticalThresholdFraction,
				Config.WarningThresholdFraction));
	}
	else if (!(Config.CriticalThresholdFraction < Config.WarningThresholdFraction))
	{
		bIsValid = false;
		EdenFuelModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("Fuel thresholds must satisfy 0 <= CriticalThresholdFraction < WarningThresholdFraction <= 1. Critical=%f Warning=%f"),
				Config.CriticalThresholdFraction,
				Config.WarningThresholdFraction));
	}

	return bIsValid;
}

bool FEdenFuelModel::IsValidDeltaTime(float DeltaTimeSeconds)
{
	return FMath::IsFinite(DeltaTimeSeconds) && DeltaTimeSeconds > 0.0f;
}

float FEdenFuelModel::SanitizeDemandNormalized(float DemandNormalized, bool* bOutWasSanitized)
{
	bool bWasSanitized = false;
	float SanitizedDemand = DemandNormalized;

	if (!FMath::IsFinite(SanitizedDemand))
	{
		SanitizedDemand = 0.0f;
		bWasSanitized = true;
	}
	else
	{
		const float ClampedDemand = FMath::Clamp(SanitizedDemand, 0.0f, 1.0f);
		bWasSanitized = !FMath::IsNearlyEqual(ClampedDemand, SanitizedDemand);
		SanitizedDemand = ClampedDemand;
	}

	if (bOutWasSanitized)
	{
		*bOutWasSanitized = bWasSanitized;
	}

	return SanitizedDemand;
}

float FEdenFuelModel::ClampFuelQuantityKilograms(
	float FuelQuantityKilograms,
	float CapacityKilograms,
	bool* bOutWasSanitized)
{
	bool bWasSanitized = false;
	float SanitizedQuantity = FuelQuantityKilograms;

	if (!FMath::IsFinite(SanitizedQuantity))
	{
		SanitizedQuantity = 0.0f;
		bWasSanitized = true;
	}

	const float SafeCapacity = FMath::IsFinite(CapacityKilograms) && CapacityKilograms > 0.0f
		? CapacityKilograms
		: 0.0f;
	const float ClampedQuantity = FMath::Clamp(SanitizedQuantity, 0.0f, SafeCapacity);
	bWasSanitized = bWasSanitized || !FMath::IsNearlyEqual(ClampedQuantity, FuelQuantityKilograms);

	if (bOutWasSanitized)
	{
		*bOutWasSanitized = bWasSanitized;
	}

	return ClampedQuantity;
}

EEdenFuelState FEdenFuelModel::DeriveFuelState(const FEdenFuelConfig& Config, float FuelQuantityKilograms)
{
	if (!ValidateConfig(Config))
	{
		return EEdenFuelState::Depleted;
	}

	const float SafeQuantityKilograms = ClampFuelQuantityKilograms(FuelQuantityKilograms, Config.CapacityKilograms);
	if (SafeQuantityKilograms <= 0.0f)
	{
		return EEdenFuelState::Depleted;
	}

	const float FuelFraction = SafeQuantityKilograms / Config.CapacityKilograms;
	if (FuelFraction <= Config.CriticalThresholdFraction)
	{
		return EEdenFuelState::Critical;
	}

	if (FuelFraction <= Config.WarningThresholdFraction)
	{
		return EEdenFuelState::Warning;
	}

	return EEdenFuelState::Normal;
}

FEdenFuelStateSnapshot FEdenFuelModel::MakeSnapshot(const FEdenFuelConfig& Config, float FuelQuantityKilograms)
{
	FEdenFuelStateSnapshot Snapshot;

	if (!ValidateConfig(Config))
	{
		return Snapshot;
	}

	Snapshot.FuelQuantityKilograms = ClampFuelQuantityKilograms(FuelQuantityKilograms, Config.CapacityKilograms);
	Snapshot.FuelFraction = Config.CapacityKilograms > 0.0f
		? Snapshot.FuelQuantityKilograms / Config.CapacityKilograms
		: 0.0f;
	Snapshot.FuelState = DeriveFuelState(Config, Snapshot.FuelQuantityKilograms);

	return Snapshot;
}

FEdenFuelStateSnapshot FEdenFuelModel::MakeInitialSnapshot(const FEdenFuelConfig& Config)
{
	if (!ValidateConfig(Config))
	{
		return FEdenFuelStateSnapshot();
	}

	return MakeSnapshot(Config, Config.CapacityKilograms * Config.InitialFuelFraction);
}

FEdenFuelStepResult FEdenFuelModel::Step(
	const FEdenFuelConfig& Config,
	const FEdenFuelStateSnapshot& CurrentSnapshot,
	float DemandNormalized,
	float DeltaTimeSeconds)
{
	FEdenFuelStepResult Result;
	Result.Snapshot = MakeSnapshot(Config, CurrentSnapshot.FuelQuantityKilograms);

	if (!ValidateConfig(Config))
	{
		Result.bConfigWasValid = false;
		return Result;
	}

	bool bDemandWasSanitized = false;
	Result.SanitizedDemandNormalized = SanitizeDemandNormalized(DemandNormalized, &bDemandWasSanitized);
	Result.bDemandWasSanitized = bDemandWasSanitized;

	if (!IsValidDeltaTime(DeltaTimeSeconds))
	{
		Result.bDeltaTimeWasValid = false;
		return Result;
	}

	const double CurrentQuantityKilograms = static_cast<double>(Result.Snapshot.FuelQuantityKilograms);
	const double ConsumptionKilograms =
		static_cast<double>(Config.ConsumptionRateKilogramsPerSecond)
		* static_cast<double>(Result.SanitizedDemandNormalized)
		* static_cast<double>(DeltaTimeSeconds);

	if (!FMath::IsFinite(ConsumptionKilograms) || ConsumptionKilograms >= CurrentQuantityKilograms)
	{
		Result.FuelConsumedKilograms = Result.Snapshot.FuelQuantityKilograms;
		Result.Snapshot = MakeSnapshot(Config, 0.0f);
		return Result;
	}

	const double NextQuantityKilograms = CurrentQuantityKilograms - FMath::Max(0.0, ConsumptionKilograms);
	Result.FuelConsumedKilograms = static_cast<float>(CurrentQuantityKilograms - NextQuantityKilograms);
	Result.Snapshot = MakeSnapshot(Config, static_cast<float>(NextQuantityKilograms));

	return Result;
}
