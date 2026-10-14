// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorControlModel.h"
#include "Systems/EdenThermalModel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// ExecPlan 0011 (Solar Crisis v2) checkpoint tests.

namespace EdenSolarCrisisV2Tests
{
/** Sandbox spacecraft thermal configuration (DA_EdenThermalConfig values). */
FEdenThermalConfig MakeSandboxThermalConfig()
{
	FEdenThermalConfig Config;
	Config.AbsoluteMinTemperatureCelsius = -50.0f;
	Config.AmbientTemperatureCelsius = 20.0f;
	Config.WarningTemperatureCelsius = 70.0f;
	Config.CriticalTemperatureCelsius = 100.0f;
	Config.AbsoluteMaxTemperatureCelsius = 120.0f;
	Config.InitialTemperatureCelsius = 20.0f;
	Config.HeatGenerationDegreesCelsiusPerSecond = 1.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.5f;
	return Config;
}

double NetRateFor(const FEdenOperatorResolvedModifiers& Modifiers, float Temperature, float ExternalHeating = 0.0f)
{
	const FEdenThermalConfig Config = MakeSandboxThermalConfig();
	const FEdenThermalStateSnapshot State = FEdenThermalModel::MakeSnapshot(
		Config,
		Temperature,
		Config.HeatGenerationDegreesCelsiusPerSecond,
		Config.DissipationDegreesCelsiusPerSecond,
		ExternalHeating,
		Modifiers.OperatorDissipationDegreesCelsiusPerSecond);
	return FEdenThermalModel::ComputeThermalBalance(Config, State, 0.1f).RealizedNetDegreesCelsiusPerSecond;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2BaselineA2ModesTest,
	"Eden.Unit.SolarCrisisV2.A.BaselineA2Modes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2BaselineA2ModesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenSolarCrisisV2Tests;

	const FEdenOperatorControlConfig Config;
	struct FCase
	{
		EEdenThermalControlMode Mode;
		float ExpectedActiveCooling;
		float ExpectedCoolingKilowatts;
		double ExpectedNetAt60C;
	};
	// Approved A2 table: Off +0.5, Nominal 0.0, Boost -0.5, Emergency -1.5 C/s with no external heat.
	const FCase Cases[] = {
		{EEdenThermalControlMode::Off, 0.0f, 0.0f, +0.5},
		{EEdenThermalControlMode::Nominal, 0.5f, 0.5f, 0.0},
		{EEdenThermalControlMode::Boost, 1.0f, 1.5f, -0.5},
		{EEdenThermalControlMode::Emergency, 2.0f, 4.5f, -1.5},
	};

	for (const FCase& Case : Cases)
	{
		FEdenOperatorIntent Intent;
		Intent.ThermalMode = Case.Mode;
		const FEdenOperatorResolvedModifiers Modifiers = FEdenOperatorControlModel::ResolveIntent(Intent, Config);
		const FString Mode = UEnum::GetValueAsString(Case.Mode);
		TestTrue(FString::Printf(TEXT("%s active cooling replaces, not stacks"), *Mode),
			FMath::IsNearlyEqual(Modifiers.ActiveCoolingDegreesCelsiusPerSecond, Case.ExpectedActiveCooling));
		TestTrue(FString::Printf(TEXT("%s operator dissipation is its active cooling"), *Mode),
			FMath::IsNearlyEqual(Modifiers.OperatorDissipationDegreesCelsiusPerSecond, Case.ExpectedActiveCooling));
		TestTrue(FString::Printf(TEXT("%s cooling power"), *Mode),
			FMath::IsNearlyEqual(Modifiers.ThermalControlDemandKilowatts, Case.ExpectedCoolingKilowatts)
				&& FMath::IsNearlyEqual(Modifiers.OperatorDemandKilowatts, Case.ExpectedCoolingKilowatts));
		const double Net = NetRateFor(Modifiers, 60.0f);
		TestTrue(FString::Printf(TEXT("%s net rate %+.3f C/s matches the A2 table (%+.1f)"), *Mode, Net, Case.ExpectedNetAt60C),
			FMath::IsNearlyEqual(Net, Case.ExpectedNetAt60C, 1.0e-3));
	}

	// Load shed keeps v1 semantics on top of A2: -0.4 C/s cooling, -2 kW, so Nominal + shed heats at +0.4 C/s.
	FEdenOperatorIntent Shed;
	Shed.LoadShedMode = EEdenLoadShedMode::Shed;
	const FEdenOperatorResolvedModifiers ShedModifiers = FEdenOperatorControlModel::ResolveIntent(Shed, Config);
	TestTrue(TEXT("Nominal + load shed heats (shed is not thermal relief)"), FMath::IsNearlyEqual(NetRateFor(ShedModifiers, 60.0f), 0.4, 1.0e-3));
	TestTrue(TEXT("Load shed keeps its active-cooling share (penalty is separate)"),
		FMath::IsNearlyEqual(ShedModifiers.ActiveCoolingDegreesCelsiusPerSecond, Config.NominalDissipationDegreesCelsiusPerSecond));
	TestTrue(TEXT("Load shed net demand"), FMath::IsNearlyEqual(ShedModifiers.OperatorDemandKilowatts, 0.5f - 2.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2IdleNominalIsStableTest,
	"Eden.Unit.SolarCrisisV2.A.IdleNominalIsStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2IdleNominalIsStableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenSolarCrisisV2Tests;

	const FEdenThermalConfig Thermal = MakeSandboxThermalConfig();
	const FEdenOperatorControlConfig Operator;
	auto Simulate = [&](EEdenThermalControlMode Mode, float Seconds)
	{
		FEdenOperatorIntent Intent;
		Intent.ThermalMode = Mode;
		const float OperatorDissipation = FEdenOperatorControlModel::ResolveIntent(Intent, Operator).OperatorDissipationDegreesCelsiusPerSecond;
		FEdenThermalStateSnapshot State = FEdenThermalModel::MakeSnapshot(
			Thermal, 20.0f, Thermal.HeatGenerationDegreesCelsiusPerSecond, Thermal.DissipationDegreesCelsiusPerSecond, 0.0f, OperatorDissipation);
		for (int32 Step = 0; Step < FMath::RoundToInt(Seconds / 0.1f); ++Step)
		{
			State = FEdenThermalModel::Step(Thermal, State, 0.1f).Snapshot;
		}
		return State.TemperatureCelsius;
	};

	TestTrue(TEXT("10 min idle in Nominal holds 20 C"), FMath::IsNearlyEqual(Simulate(EEdenThermalControlMode::Nominal, 600.0f), 20.0f, 0.01f));
	TestTrue(TEXT("Off drifts up at +0.5 C/s (60 s -> 50 C)"), FMath::IsNearlyEqual(Simulate(EEdenThermalControlMode::Off, 60.0f), 50.0f, 0.05f));
	return true;
}

#endif
