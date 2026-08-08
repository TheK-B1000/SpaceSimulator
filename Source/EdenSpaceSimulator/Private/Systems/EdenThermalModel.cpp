// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenThermalModel.h"

namespace EdenThermalModel
{
void AddValidationError(TArray<FString>* OutErrors, const FString& ErrorMessage)
{
	if (OutErrors)
	{
		OutErrors->Add(ErrorMessage);
	}
}

bool AreThermalBoundsFinite(const FEdenThermalConfig& Config)
{
	return FMath::IsFinite(Config.AbsoluteMinTemperatureCelsius)
		&& FMath::IsFinite(Config.AmbientTemperatureCelsius)
		&& FMath::IsFinite(Config.WarningTemperatureCelsius)
		&& FMath::IsFinite(Config.CriticalTemperatureCelsius)
		&& FMath::IsFinite(Config.AbsoluteMaxTemperatureCelsius);
}
}

bool FEdenThermalModel::ValidateConfig(const FEdenThermalConfig& Config, TArray<FString>* OutErrors)
{
	bool bIsValid = true;

	if (!EdenThermalModel::AreThermalBoundsFinite(Config))
	{
		bIsValid = false;
		EdenThermalModel::AddValidationError(
			OutErrors,
			TEXT("Thermal absolute bounds, ambient, warning, and critical temperatures must all be finite."));
	}
	else if (!(Config.AbsoluteMinTemperatureCelsius <= Config.AmbientTemperatureCelsius
		&& Config.AmbientTemperatureCelsius < Config.WarningTemperatureCelsius
		&& Config.WarningTemperatureCelsius < Config.CriticalTemperatureCelsius
		&& Config.CriticalTemperatureCelsius <= Config.AbsoluteMaxTemperatureCelsius))
	{
		bIsValid = false;
		EdenThermalModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("Thermal thresholds must satisfy AbsoluteMin <= Ambient < Warning < Critical <= AbsoluteMax. Min=%f Ambient=%f Warning=%f Critical=%f Max=%f"),
				Config.AbsoluteMinTemperatureCelsius,
				Config.AmbientTemperatureCelsius,
				Config.WarningTemperatureCelsius,
				Config.CriticalTemperatureCelsius,
				Config.AbsoluteMaxTemperatureCelsius));
	}

	if (!FMath::IsFinite(Config.InitialTemperatureCelsius)
		|| Config.InitialTemperatureCelsius < Config.AbsoluteMinTemperatureCelsius
		|| Config.InitialTemperatureCelsius > Config.AbsoluteMaxTemperatureCelsius)
	{
		bIsValid = false;
		EdenThermalModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("InitialTemperatureCelsius must be finite and inside absolute bounds. Value=%f Min=%f Max=%f"),
				Config.InitialTemperatureCelsius,
				Config.AbsoluteMinTemperatureCelsius,
				Config.AbsoluteMaxTemperatureCelsius));
	}

	if (!FMath::IsFinite(Config.HeatGenerationDegreesCelsiusPerSecond)
		|| Config.HeatGenerationDegreesCelsiusPerSecond < 0.0f)
	{
		bIsValid = false;
		EdenThermalModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("HeatGenerationDegreesCelsiusPerSecond must be nonnegative and finite. Value=%f"),
				Config.HeatGenerationDegreesCelsiusPerSecond));
	}

	if (!FMath::IsFinite(Config.DissipationDegreesCelsiusPerSecond)
		|| Config.DissipationDegreesCelsiusPerSecond < 0.0f)
	{
		bIsValid = false;
		EdenThermalModel::AddValidationError(
			OutErrors,
			FString::Printf(
				TEXT("DissipationDegreesCelsiusPerSecond must be nonnegative and finite. Value=%f"),
				Config.DissipationDegreesCelsiusPerSecond));
	}

	return bIsValid;
}

bool FEdenThermalModel::IsValidDeltaTime(float DeltaTimeSeconds)
{
	return FMath::IsFinite(DeltaTimeSeconds) && DeltaTimeSeconds > 0.0f;
}

float FEdenThermalModel::SanitizeNonnegativeDegreesCelsiusPerSecond(float Rate, bool* bOutWasSanitized)
{
	bool bWasSanitized = false;
	float SanitizedRate = Rate;

	if (!FMath::IsFinite(SanitizedRate))
	{
		SanitizedRate = 0.0f;
		bWasSanitized = true;
	}
	else if (SanitizedRate < 0.0f)
	{
		SanitizedRate = 0.0f;
		bWasSanitized = true;
	}

	if (bOutWasSanitized)
	{
		*bOutWasSanitized = bWasSanitized;
	}

	return SanitizedRate;
}


float FEdenThermalModel::SanitizeFiniteDegreesCelsiusPerSecond(float Rate, bool* bOutWasSanitized)
{
	bool bWasSanitized = false;
	float SanitizedRate = Rate;

	if (!FMath::IsFinite(SanitizedRate))
	{
		SanitizedRate = 0.0f;
		bWasSanitized = true;
	}

	if (bOutWasSanitized)
	{
		*bOutWasSanitized = bWasSanitized;
	}

	return SanitizedRate;
}

float FEdenThermalModel::ClampTemperatureCelsius(
	float TemperatureCelsius,
	const FEdenThermalConfig& Config,
	bool* bOutWasSanitized)
{
	bool bWasSanitized = false;
	float SanitizedTemperatureCelsius = TemperatureCelsius;

	if (!FMath::IsFinite(SanitizedTemperatureCelsius))
	{
		SanitizedTemperatureCelsius = Config.AmbientTemperatureCelsius;
		bWasSanitized = true;
	}

	const float ClampedTemperatureCelsius = FMath::Clamp(
		SanitizedTemperatureCelsius,
		Config.AbsoluteMinTemperatureCelsius,
		Config.AbsoluteMaxTemperatureCelsius);
	bWasSanitized = bWasSanitized || !FMath::IsNearlyEqual(ClampedTemperatureCelsius, TemperatureCelsius);

	if (bOutWasSanitized)
	{
		*bOutWasSanitized = bWasSanitized;
	}

	return ClampedTemperatureCelsius;
}

EEdenThermalState FEdenThermalModel::DeriveThermalState(const FEdenThermalConfig& Config, float TemperatureCelsius)
{
	if (!ValidateConfig(Config))
	{
		return EEdenThermalState::Overheated;
	}

	const float SafeTemperatureCelsius = ClampTemperatureCelsius(TemperatureCelsius, Config);
	if (SafeTemperatureCelsius >= Config.AbsoluteMaxTemperatureCelsius)
	{
		return EEdenThermalState::Overheated;
	}

	if (SafeTemperatureCelsius >= Config.CriticalTemperatureCelsius)
	{
		return EEdenThermalState::Critical;
	}

	if (SafeTemperatureCelsius >= Config.WarningTemperatureCelsius)
	{
		return EEdenThermalState::Warning;
	}

	return EEdenThermalState::Normal;
}

FEdenThermalStateSnapshot FEdenThermalModel::MakeSnapshot(
	const FEdenThermalConfig& Config,
	float TemperatureCelsius,
	float HeatGenerationDegreesCelsiusPerSecond,
	float DissipationDegreesCelsiusPerSecond,
	float ExternalHeatingRateDegreesCelsiusPerSecond,
	float OperatorDissipationDegreesCelsiusPerSecond)
{
	FEdenThermalStateSnapshot Snapshot;

	if (!ValidateConfig(Config))
	{
		return Snapshot;
	}

	Snapshot.TemperatureCelsius = ClampTemperatureCelsius(TemperatureCelsius, Config);
	Snapshot.HeatGenerationDegreesCelsiusPerSecond =
		SanitizeNonnegativeDegreesCelsiusPerSecond(HeatGenerationDegreesCelsiusPerSecond);
	Snapshot.DissipationDegreesCelsiusPerSecond =
		SanitizeNonnegativeDegreesCelsiusPerSecond(DissipationDegreesCelsiusPerSecond);
	Snapshot.OperatorDissipationDegreesCelsiusPerSecond =
		SanitizeFiniteDegreesCelsiusPerSecond(OperatorDissipationDegreesCelsiusPerSecond);
	const double RawEffectiveDissipation =
		static_cast<double>(Snapshot.DissipationDegreesCelsiusPerSecond)
		+ static_cast<double>(Snapshot.OperatorDissipationDegreesCelsiusPerSecond);
	Snapshot.EffectiveDissipationDegreesCelsiusPerSecond =
		static_cast<float>(FMath::Max(0.0, RawEffectiveDissipation));
	Snapshot.ExternalHeatingRateDegreesCelsiusPerSecond =
		SanitizeNonnegativeDegreesCelsiusPerSecond(ExternalHeatingRateDegreesCelsiusPerSecond);
	Snapshot.ThermalState = DeriveThermalState(Config, Snapshot.TemperatureCelsius);

	return Snapshot;
}

FEdenThermalStateSnapshot FEdenThermalModel::MakeInitialSnapshot(const FEdenThermalConfig& Config)
{
	if (!ValidateConfig(Config))
	{
		return FEdenThermalStateSnapshot();
	}

	return MakeSnapshot(
		Config,
		Config.InitialTemperatureCelsius,
		Config.HeatGenerationDegreesCelsiusPerSecond,
		Config.DissipationDegreesCelsiusPerSecond,
		0.0f,
		0.0f);
}

FEdenThermalStepResult FEdenThermalModel::Step(
	const FEdenThermalConfig& Config,
	const FEdenThermalStateSnapshot& CurrentSnapshot,
	float DeltaTimeSeconds)
{
	FEdenThermalStepResult Result;

	if (!ValidateConfig(Config))
	{
		Result.bConfigWasValid = false;
		return Result;
	}

	bool bTemperatureWasSanitized = false;
	Result.Snapshot = MakeSnapshot(
		Config,
		ClampTemperatureCelsius(CurrentSnapshot.TemperatureCelsius, Config, &bTemperatureWasSanitized),
		CurrentSnapshot.HeatGenerationDegreesCelsiusPerSecond,
		CurrentSnapshot.DissipationDegreesCelsiusPerSecond,
		CurrentSnapshot.ExternalHeatingRateDegreesCelsiusPerSecond,
		CurrentSnapshot.OperatorDissipationDegreesCelsiusPerSecond);
	Result.bTemperatureWasSanitized = bTemperatureWasSanitized;

	bool bHeatGenerationWasSanitized = false;
	bool bDissipationWasSanitized = false;
	bool bOperatorDissipationWasSanitized = false;
	bool bExternalHeatingRateWasSanitized = false;
	const float HeatGenerationDegreesCelsiusPerSecond = SanitizeNonnegativeDegreesCelsiusPerSecond(
		CurrentSnapshot.HeatGenerationDegreesCelsiusPerSecond,
		&bHeatGenerationWasSanitized);
	const float DissipationDegreesCelsiusPerSecond = SanitizeNonnegativeDegreesCelsiusPerSecond(
		CurrentSnapshot.DissipationDegreesCelsiusPerSecond,
		&bDissipationWasSanitized);
	const float OperatorDissipationDegreesCelsiusPerSecond = SanitizeFiniteDegreesCelsiusPerSecond(
		CurrentSnapshot.OperatorDissipationDegreesCelsiusPerSecond,
		&bOperatorDissipationWasSanitized);
	const float ExternalHeatingRateDegreesCelsiusPerSecond = SanitizeNonnegativeDegreesCelsiusPerSecond(
		CurrentSnapshot.ExternalHeatingRateDegreesCelsiusPerSecond,
		&bExternalHeatingRateWasSanitized);
	Result.bHeatGenerationWasSanitized = bHeatGenerationWasSanitized;
	Result.bDissipationWasSanitized = bDissipationWasSanitized;
	Result.bOperatorDissipationWasSanitized = bOperatorDissipationWasSanitized;
	Result.bExternalHeatingRateWasSanitized = bExternalHeatingRateWasSanitized;

	const double RawEffectiveDissipation =
		static_cast<double>(DissipationDegreesCelsiusPerSecond)
		+ static_cast<double>(OperatorDissipationDegreesCelsiusPerSecond);
	const float EffectiveDissipationDegreesCelsiusPerSecond =
		static_cast<float>(FMath::Max(0.0, RawEffectiveDissipation));

	if (!IsValidDeltaTime(DeltaTimeSeconds))
	{
		Result.bDeltaTimeWasValid = false;
		Result.Snapshot = MakeSnapshot(
			Config,
			Result.Snapshot.TemperatureCelsius,
			HeatGenerationDegreesCelsiusPerSecond,
			DissipationDegreesCelsiusPerSecond,
			ExternalHeatingRateDegreesCelsiusPerSecond,
			OperatorDissipationDegreesCelsiusPerSecond);
		return Result;
	}

	const double TotalHeatGenerationDegreesCelsiusPerSecond =
		static_cast<double>(HeatGenerationDegreesCelsiusPerSecond) +
		static_cast<double>(ExternalHeatingRateDegreesCelsiusPerSecond);

	const double CurrentTemperatureCelsius = static_cast<double>(Result.Snapshot.TemperatureCelsius);
	const double HeatDeltaCelsius =
		TotalHeatGenerationDegreesCelsiusPerSecond * static_cast<double>(DeltaTimeSeconds);
	double NextTemperatureCelsius = CurrentTemperatureCelsius + HeatDeltaCelsius;

	const double AmbientTemperatureCelsius = static_cast<double>(Config.AmbientTemperatureCelsius);
	const double DissipationDeltaCelsius =
		static_cast<double>(EffectiveDissipationDegreesCelsiusPerSecond) * static_cast<double>(DeltaTimeSeconds);

	if (NextTemperatureCelsius > AmbientTemperatureCelsius)
	{
		NextTemperatureCelsius = FMath::Max(AmbientTemperatureCelsius, NextTemperatureCelsius - DissipationDeltaCelsius);
	}
	else if (NextTemperatureCelsius < AmbientTemperatureCelsius)
	{
		NextTemperatureCelsius = FMath::Min(AmbientTemperatureCelsius, NextTemperatureCelsius + DissipationDeltaCelsius);
	}

	const float ClampedTemperatureCelsius =
		ClampTemperatureCelsius(static_cast<float>(NextTemperatureCelsius), Config);
	Result.TemperatureDeltaCelsius = ClampedTemperatureCelsius - Result.Snapshot.TemperatureCelsius;
	Result.Snapshot = MakeSnapshot(
		Config,
		ClampedTemperatureCelsius,
		HeatGenerationDegreesCelsiusPerSecond,
		DissipationDegreesCelsiusPerSecond,
		ExternalHeatingRateDegreesCelsiusPerSecond,
		OperatorDissipationDegreesCelsiusPerSecond);

	return Result;
}
