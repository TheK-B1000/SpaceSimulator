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

FEdenThermalLimitsSnapshot FEdenThermalModel::MakeLimitsSnapshot(const FEdenThermalConfig& Config)
{
	FEdenThermalLimitsSnapshot Limits;
	if (!ValidateConfig(Config))
	{
		return Limits;
	}

	Limits.bIsValid = true;
	Limits.AmbientTemperatureCelsius = Config.AmbientTemperatureCelsius;
	Limits.WarningTemperatureCelsius = Config.WarningTemperatureCelsius;
	Limits.CriticalTemperatureCelsius = Config.CriticalTemperatureCelsius;
	Limits.AbsoluteMaxTemperatureCelsius = Config.AbsoluteMaxTemperatureCelsius;
	return Limits;
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
	Result.Snapshot = WithCooling(
		MakeSnapshot(
		Config,
		ClampTemperatureCelsius(CurrentSnapshot.TemperatureCelsius, Config, &bTemperatureWasSanitized),
		CurrentSnapshot.HeatGenerationDegreesCelsiusPerSecond,
		CurrentSnapshot.DissipationDegreesCelsiusPerSecond,
		CurrentSnapshot.ExternalHeatingRateDegreesCelsiusPerSecond,
		CurrentSnapshot.OperatorDissipationDegreesCelsiusPerSecond),
		CurrentSnapshot.ActiveCoolingDegreesCelsiusPerSecond,
		CurrentSnapshot.CoolingAuthority);
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

	const FEdenThermalStateSnapshot CoolingInputs = WithCooling(
		Result.Snapshot,
		CurrentSnapshot.ActiveCoolingDegreesCelsiusPerSecond,
		CurrentSnapshot.CoolingAuthority);
	const float EffectiveDissipationDegreesCelsiusPerSecond = CoolingInputs.EffectiveDissipationDegreesCelsiusPerSecond;

	if (!IsValidDeltaTime(DeltaTimeSeconds))
	{
		Result.bDeltaTimeWasValid = false;
		Result.Snapshot = WithCooling(
		MakeSnapshot(
			Config,
			Result.Snapshot.TemperatureCelsius,
			HeatGenerationDegreesCelsiusPerSecond,
			DissipationDegreesCelsiusPerSecond,
			ExternalHeatingRateDegreesCelsiusPerSecond,
			OperatorDissipationDegreesCelsiusPerSecond),
		CurrentSnapshot.ActiveCoolingDegreesCelsiusPerSecond,
		CurrentSnapshot.CoolingAuthority);
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
	Result.Snapshot = WithCooling(
		MakeSnapshot(
		Config,
		ClampedTemperatureCelsius,
		HeatGenerationDegreesCelsiusPerSecond,
		DissipationDegreesCelsiusPerSecond,
		ExternalHeatingRateDegreesCelsiusPerSecond,
		OperatorDissipationDegreesCelsiusPerSecond),
		CurrentSnapshot.ActiveCoolingDegreesCelsiusPerSecond,
		CurrentSnapshot.CoolingAuthority);

	return Result;
}

FEdenThermalStateSnapshot FEdenThermalModel::WithCooling(
	const FEdenThermalStateSnapshot& InSnapshot,
	float ActiveCoolingDegreesCelsiusPerSecond,
	float CoolingAuthority)
{
	FEdenThermalStateSnapshot Snapshot = InSnapshot;
	Snapshot.ActiveCoolingDegreesCelsiusPerSecond = FMath::IsFinite(ActiveCoolingDegreesCelsiusPerSecond)
		? FMath::Max(0.0f, ActiveCoolingDegreesCelsiusPerSecond)
		: 0.0f;
	Snapshot.CoolingAuthority = FMath::IsFinite(CoolingAuthority) ? FMath::Clamp(CoolingAuthority, 0.0f, 1.0f) : 1.0f;
	const double Unpowered =
		static_cast<double>(Snapshot.ActiveCoolingDegreesCelsiusPerSecond) * (1.0 - static_cast<double>(Snapshot.CoolingAuthority));
	const double RawEffective =
		static_cast<double>(Snapshot.DissipationDegreesCelsiusPerSecond)
		+ static_cast<double>(Snapshot.OperatorDissipationDegreesCelsiusPerSecond)
		- Unpowered;
	Snapshot.EffectiveDissipationDegreesCelsiusPerSecond = static_cast<float>(FMath::Max(0.0, RawEffective));
	return Snapshot;
}

FEdenThermalBalance FEdenThermalModel::ComputeThermalBalance(
	const FEdenThermalConfig& Config,
	const FEdenThermalStateSnapshot& Snapshot,
	float StepSeconds)
{
	FEdenThermalBalance Balance;
	if (!ValidateConfig(Config) || !IsValidDeltaTime(StepSeconds))
	{
		return Balance;
	}

	// Same sanitation as Step(), so the terms are exactly what the simulation integrates.
	const FEdenThermalStateSnapshot Sanitized = WithCooling(
		MakeSnapshot(
			Config,
			Snapshot.TemperatureCelsius,
			Snapshot.HeatGenerationDegreesCelsiusPerSecond,
			Snapshot.DissipationDegreesCelsiusPerSecond,
			Snapshot.ExternalHeatingRateDegreesCelsiusPerSecond,
			Snapshot.OperatorDissipationDegreesCelsiusPerSecond),
		Snapshot.ActiveCoolingDegreesCelsiusPerSecond,
		Snapshot.CoolingAuthority);

	Balance.bIsValid = true;
	Balance.StepSeconds = StepSeconds;
	Balance.TemperatureCelsius = Sanitized.TemperatureCelsius;
	Balance.BaseHeatingDegreesCelsiusPerSecond = Sanitized.HeatGenerationDegreesCelsiusPerSecond;
	Balance.ExternalHeatingDegreesCelsiusPerSecond = Sanitized.ExternalHeatingRateDegreesCelsiusPerSecond;
	Balance.TotalHeatingDegreesCelsiusPerSecond =
		Balance.BaseHeatingDegreesCelsiusPerSecond + Balance.ExternalHeatingDegreesCelsiusPerSecond;
	Balance.BaseDissipationDegreesCelsiusPerSecond = Sanitized.DissipationDegreesCelsiusPerSecond;
	Balance.OperatorDissipationDegreesCelsiusPerSecond = Sanitized.OperatorDissipationDegreesCelsiusPerSecond;
	Balance.EffectiveDissipationDegreesCelsiusPerSecond = Sanitized.EffectiveDissipationDegreesCelsiusPerSecond;
	Balance.ActiveCoolingDegreesCelsiusPerSecond = Sanitized.ActiveCoolingDegreesCelsiusPerSecond;
	Balance.CoolingAuthority = Sanitized.CoolingAuthority;
	Balance.UnconstrainedNetDegreesCelsiusPerSecond =
		Balance.TotalHeatingDegreesCelsiusPerSecond - Balance.EffectiveDissipationDegreesCelsiusPerSecond;

	// The realized rate comes from the simulation's own step, so ambient and limit bounds match exactly.
	const FEdenThermalStepResult Next = Step(Config, Snapshot, StepSeconds);
	Balance.RealizedNetDegreesCelsiusPerSecond =
		static_cast<double>(Next.TemperatureDeltaCelsius) / static_cast<double>(StepSeconds);

	// Temperatures are float: near 100 C one ulp over a 0.1 s step is ~1e-4 C/s, so compare at 1e-3.
	constexpr double RateTolerance = 1.0e-3;
	if (FMath::Abs(Balance.RealizedNetDegreesCelsiusPerSecond - Balance.UnconstrainedNetDegreesCelsiusPerSecond) > RateTolerance)
	{
		const float NextTemperature = Next.Snapshot.TemperatureCelsius;
		if (Balance.UnconstrainedNetDegreesCelsiusPerSecond > 0.0 && NextTemperature >= Config.AbsoluteMaxTemperatureCelsius)
		{
			Balance.Limit = EEdenThermalRateLimit::AbsoluteMax;
		}
		else if (Balance.UnconstrainedNetDegreesCelsiusPerSecond < 0.0 && NextTemperature <= Config.AbsoluteMinTemperatureCelsius)
		{
			Balance.Limit = EEdenThermalRateLimit::AbsoluteMin;
		}
		else
		{
			Balance.Limit = EEdenThermalRateLimit::Ambient;
		}
	}

	return Balance;
}

FString FEdenThermalModel::DescribeThermalBalance(const FEdenThermalBalance& Balance)
{
	if (!Balance.bIsValid)
	{
		return TEXT("thermal balance unavailable (invalid configuration or step)");
	}

	const TCHAR* LimitText = TEXT("none");
	switch (Balance.Limit)
	{
	case EEdenThermalRateLimit::AbsoluteMax:
		LimitText = TEXT("held at absolute max (net heating saturated)");
		break;
	case EEdenThermalRateLimit::AbsoluteMin:
		LimitText = TEXT("held at absolute min (net cooling saturated)");
		break;
	case EEdenThermalRateLimit::Ambient:
		LimitText = TEXT("dissipation limited at ambient");
		break;
	default:
		break;
	}

	return FString::Printf(
		TEXT("Temperature=%.3f C | heating base %.3f + external %.3f = %.3f C/s | cooling base %.3f + operator %+.3f = effective %.3f C/s | net %+.3f C/s (realized %+.3f C/s) | limit: %s | active cooling %.3f C/s at %.0f%% power"),
		Balance.TemperatureCelsius,
		Balance.BaseHeatingDegreesCelsiusPerSecond,
		Balance.ExternalHeatingDegreesCelsiusPerSecond,
		Balance.TotalHeatingDegreesCelsiusPerSecond,
		Balance.BaseDissipationDegreesCelsiusPerSecond,
		Balance.OperatorDissipationDegreesCelsiusPerSecond,
		Balance.EffectiveDissipationDegreesCelsiusPerSecond,
		Balance.UnconstrainedNetDegreesCelsiusPerSecond,
		Balance.RealizedNetDegreesCelsiusPerSecond,
		LimitText,
		Balance.ActiveCoolingDegreesCelsiusPerSecond,
		Balance.CoolingAuthority * 100.0);
}
