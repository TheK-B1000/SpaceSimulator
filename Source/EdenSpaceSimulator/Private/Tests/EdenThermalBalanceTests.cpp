// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenThermalModel.h"
#include "Systems/EdenThermalSystemComponent.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenThermalBalanceTests
{
constexpr float StepSeconds = 0.1f;
constexpr double RateTolerance = 1.0e-3;

/** The sandbox thermal configuration Solar Crisis v1 was balanced against (ExecPlan 0004, Checkpoint G). */
FEdenThermalConfig MakeSandboxConfig()
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

FEdenThermalStateSnapshot MakeState(const FEdenThermalConfig& Config, float Temperature, float External, float Operator)
{
	return FEdenThermalModel::MakeSnapshot(
		Config,
		Temperature,
		Config.HeatGenerationDegreesCelsiusPerSecond,
		Config.DissipationDegreesCelsiusPerSecond,
		External,
		Operator);
}

bool TestRate(FAutomationTestBase& Test, const TCHAR* What, double Actual, double Expected)
{
	return Test.TestTrue(
		FString::Printf(TEXT("%s. Actual=%+.4f Expected=%+.4f C/s"), What, Actual, Expected),
		FMath::IsNearlyEqual(Actual, Expected, RateTolerance));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalBalanceReportsAllTermsTest,
	"Eden.Unit.Systems.Thermal.BalanceReportsAllTerms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalBalanceReportsAllTermsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenThermalBalanceTests;

	const FEdenThermalConfig Config = MakeSandboxConfig();
	const FEdenThermalBalance Balance = FEdenThermalModel::ComputeThermalBalance(Config, MakeState(Config, 50.0f, 2.5f, 1.0f), StepSeconds);

	TestTrue(TEXT("Balance is valid"), Balance.bIsValid);
	TestRate(*this, TEXT("Base heating"), Balance.BaseHeatingDegreesCelsiusPerSecond, 1.0);
	TestRate(*this, TEXT("External heating"), Balance.ExternalHeatingDegreesCelsiusPerSecond, 2.5);
	TestRate(*this, TEXT("Total heating"), Balance.TotalHeatingDegreesCelsiusPerSecond, 3.5);
	TestRate(*this, TEXT("Base dissipation"), Balance.BaseDissipationDegreesCelsiusPerSecond, 0.5);
	TestRate(*this, TEXT("Operator cooling"), Balance.OperatorDissipationDegreesCelsiusPerSecond, 1.0);
	TestRate(*this, TEXT("Effective dissipation"), Balance.EffectiveDissipationDegreesCelsiusPerSecond, 1.5);
	TestRate(*this, TEXT("Unconstrained net"), Balance.UnconstrainedNetDegreesCelsiusPerSecond, 2.0);
	TestRate(*this, TEXT("Realized net"), Balance.RealizedNetDegreesCelsiusPerSecond, 2.0);
	TestTrue(TEXT("No bound applies mid-range"), Balance.Limit == EEdenThermalRateLimit::None);

	const FString Text = FEdenThermalModel::DescribeThermalBalance(Balance);
	AddInfo(Text);
	for (const TCHAR* Expected : {
			TEXT("Temperature=50.000 C"),
			TEXT("heating base 1.000 + external 2.500 = 3.500 C/s"),
			TEXT("cooling base 0.500 + operator +1.000 = effective 1.500 C/s"),
			TEXT("net +2.000 C/s (realized +2.000 C/s)"),
			TEXT("limit: none")})
	{
		TestTrue(FString::Printf(TEXT("Log line contains '%s'"), Expected), Text.Contains(Expected));
	}

	const FEdenThermalBalance Invalid = FEdenThermalModel::ComputeThermalBalance(Config, MakeState(Config, 50.0f, 0.0f, 0.0f), 0.0f);
	TestFalse(TEXT("An invalid step gives an invalid balance"), Invalid.bIsValid);
	TestTrue(TEXT("Invalid balance says so"), FEdenThermalModel::DescribeThermalBalance(Invalid).Contains(TEXT("unavailable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalBalanceMatchesSolarCrisisObservationsTest,
	"Eden.Unit.Systems.Thermal.BalanceMatchesSolarCrisisObservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalBalanceMatchesSolarCrisisObservationsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenThermalBalanceTests;

	// Rates observed in the 2026-10-14 PIE run; the old log printed only 1.0 / 0.5 for all of them.
	const FEdenThermalConfig Config = MakeSandboxConfig();
	TestRate(*this, TEXT("Nominal, no flare: net heating"),
		FEdenThermalModel::ComputeThermalBalance(Config, MakeState(Config, 60.0f, 0.0f, 0.0f), StepSeconds).RealizedNetDegreesCelsiusPerSecond, 0.5);
	TestRate(*this, TEXT("During the 2.5 C/s flare"),
		FEdenThermalModel::ComputeThermalBalance(Config, MakeState(Config, 60.0f, 2.5f, 0.0f), StepSeconds).RealizedNetDegreesCelsiusPerSecond, 3.0);
	TestRate(*this, TEXT("Boost (+1.0 C/s) after the flare: 120 -> 100 C in 40 s"),
		FEdenThermalModel::ComputeThermalBalance(Config, MakeState(Config, 110.0f, 0.0f, 1.0f), StepSeconds).RealizedNetDegreesCelsiusPerSecond, -0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalBalanceReportsSaturationTest,
	"Eden.Unit.Systems.Thermal.BalanceReportsSaturation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalBalanceReportsSaturationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenThermalBalanceTests;

	const FEdenThermalConfig Config = MakeSandboxConfig();

	const FEdenThermalBalance AtCap = FEdenThermalModel::ComputeThermalBalance(Config, MakeState(Config, 120.0f, 0.0f, 0.0f), StepSeconds);
	TestRate(*this, TEXT("Net heating is still reported at the cap"), AtCap.UnconstrainedNetDegreesCelsiusPerSecond, 0.5);
	TestRate(*this, TEXT("But the temperature cannot rise"), AtCap.RealizedNetDegreesCelsiusPerSecond, 0.0);
	TestTrue(TEXT("Limit is the absolute maximum"), AtCap.Limit == EEdenThermalRateLimit::AbsoluteMax);
	TestTrue(TEXT("Log says the heating is saturated"), FEdenThermalModel::DescribeThermalBalance(AtCap).Contains(TEXT("held at absolute max")));

	// Dissipation only pulls toward ambient: at ambient with more cooling than heating, nothing moves.
	FEdenThermalConfig CoolConfig = Config;
	CoolConfig.HeatGenerationDegreesCelsiusPerSecond = 0.5f;
	CoolConfig.DissipationDegreesCelsiusPerSecond = 1.0f;
	const FEdenThermalBalance AtAmbient = FEdenThermalModel::ComputeThermalBalance(CoolConfig, MakeState(CoolConfig, 20.0f, 0.0f, 0.0f), StepSeconds);
	TestRate(*this, TEXT("Unconstrained net is cooling"), AtAmbient.UnconstrainedNetDegreesCelsiusPerSecond, -0.5);
	TestRate(*this, TEXT("Realized net is zero at ambient"), AtAmbient.RealizedNetDegreesCelsiusPerSecond, 0.0);
	TestTrue(TEXT("Limit is the ambient floor"), AtAmbient.Limit == EEdenThermalRateLimit::Ambient);

	const FEdenThermalBalance Balanced = FEdenThermalModel::ComputeThermalBalance(Config, MakeState(Config, 60.0f, 0.0f, 0.5f), StepSeconds);
	TestRate(*this, TEXT("Balanced heating and cooling holds temperature"), Balanced.RealizedNetDegreesCelsiusPerSecond, 0.0);
	TestTrue(TEXT("A balanced state is not reported as a bound"), Balanced.Limit == EEdenThermalRateLimit::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalBalanceDiagnosticsDoNotAlterSimulationTest,
	"Eden.Integration.Systems.Thermal.BalanceDiagnosticsDoNotAlterSimulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalBalanceDiagnosticsDoNotAlterSimulationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenThermalBalanceTests;

	const FEdenThermalConfig Config = MakeSandboxConfig();
	UEdenThermalSystemComponent* Observed = NewObject<UEdenThermalSystemComponent>();
	UEdenThermalSystemComponent* Reference = NewObject<UEdenThermalSystemComponent>();
	if (!TestTrue(TEXT("Both components initialize"), Observed->InitializeThermalSimulation(Config) && Reference->InitializeThermalSimulation(Config)))
	{
		return false;
	}

	// Solar Crisis v1 shape plus Boost, long enough to reach the cap: same inputs to both, diagnostics on one.
	bool bIdentical = true;
	for (int32 Step = 0; Step < 2400; ++Step)
	{
		const double Time = Step * StepSeconds;
		for (UEdenThermalSystemComponent* Component : {Observed, Reference})
		{
			if (Step == 100) { Component->SetExternalHeatingRateDegreesCelsiusPerSecond(2.5f); }
			if (Step == 300) { Component->ClearExternalHeatingRate(); }
			if (Step == 2000) { Component->SetOperatorDissipationDegreesCelsiusPerSecond(1.0f); }
			Component->AdvanceSimulation(StepSeconds);
		}

		const FEdenThermalBalance Balance = Observed->GetThermalBalance();
		(void)FEdenThermalModel::DescribeThermalBalance(Balance);

		const FEdenThermalStateSnapshot A = Observed->GetThermalStateSnapshot();
		const FEdenThermalStateSnapshot B = Reference->GetThermalStateSnapshot();
		if (A.TemperatureCelsius != B.TemperatureCelsius || A.ThermalState != B.ThermalState
			|| A.ExternalHeatingRateDegreesCelsiusPerSecond != B.ExternalHeatingRateDegreesCelsiusPerSecond
			|| A.EffectiveDissipationDegreesCelsiusPerSecond != B.EffectiveDissipationDegreesCelsiusPerSecond)
		{
			AddError(FString::Printf(TEXT("Diverged at t=%.1f s: %.6f vs %.6f C"), Time, A.TemperatureCelsius, B.TemperatureCelsius));
			bIdentical = false;
			break;
		}
	}
	TestTrue(TEXT("Computing and logging the balance never changes the thermal trajectory"), bIdentical);

	const FEdenThermalBalance Final = Observed->GetThermalBalance();
	TestRate(*this, TEXT("Balance uses the clock's fixed step and reports Boost cooling"), Final.RealizedNetDegreesCelsiusPerSecond, -0.5);
	TestTrue(TEXT("Balance step matches the last fixed step"), FMath::IsNearlyEqual(Final.StepSeconds, static_cast<double>(StepSeconds), 1.0e-9));
	return true;
}

#endif
