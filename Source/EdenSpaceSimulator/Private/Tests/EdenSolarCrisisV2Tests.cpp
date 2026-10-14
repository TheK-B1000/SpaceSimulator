// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorControlModel.h"
#include "Systems/EdenPowerModel.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalModel.h"
#include "Systems/EdenThermalSystemComponent.h"

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

namespace EdenSolarCrisisV2Tests
{
/** Sandbox power configuration (DA_EdenPowerConfig values) with the v2 bus limit. */
FEdenPowerConfig MakeSandboxPowerConfig(float DischargeLimit = 6.0f)
{
	FEdenPowerConfig Config;
	Config.BatteryCapacityKilowattHours = 20.0f;
	Config.GenerationKilowatts = 2.0f;
	Config.BaselineDemandKilowatts = 1.0f;
	Config.InitialChargeFraction = 1.0f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	Config.BatteryMaxDischargeKilowatts = DischargeLimit;
	return Config;
}

FEdenPowerStateSnapshot MakePowerState(float ChargeKilowattHours, float External, float Operator)
{
	const FEdenPowerConfig Config = MakeSandboxPowerConfig();
	return FEdenPowerModel::MakeSnapshot(Config, ChargeKilowattHours, 2.0f, 1.0f, External, Operator);
}

bool Near(double A, double B, double Tolerance = 1.0e-4)
{
	return FMath::IsNearlyEqual(A, B, Tolerance);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2UnlimitedBusMatchesLegacyTest,
	"Eden.Unit.SolarCrisisV2.B.UnlimitedBusMatchesLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2UnlimitedBusMatchesLegacyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenSolarCrisisV2Tests;

	// Default bus (limit 0): every load is served and energy follows the pre-0011 formula exactly.
	const FEdenPowerConfig Config = MakeSandboxPowerConfig(0.0f);
	struct FCase { float Charge; float External; float Operator; float Cooling; };
	const FCase Cases[] = {{20.0f, 0.0f, 0.5f, 0.5f}, {20.0f, 5.0f, 4.5f, 4.5f}, {0.0f, 5.0f, 2.5f, 4.5f}, {3.0f, 0.0f, -1.5f, 0.5f}, {1.0f, 2.0f, 6.0f, 0.0f}};
	for (const FCase& Case : Cases)
	{
		FEdenPowerStateSnapshot State = FEdenPowerModel::WithBusAllocation(
			FEdenPowerModel::MakeSnapshot(Config, Case.Charge, 2.0f, 1.0f, Case.External, Case.Operator), Case.Cooling, 0.0f);
		const double LegacyTotal = FMath::Max(0.0, 1.0 + Case.External + Case.Operator);
		const double LegacyNet = 2.0 - LegacyTotal;
		const FEdenPowerStepResult Step = FEdenPowerModel::Step(Config, State, 0.1f);
		const double LegacyCharge = FMath::Clamp(static_cast<double>(Case.Charge) + LegacyNet * 0.1 / 3600.0, 0.0, 20.0);
		TestFalse(TEXT("Unlimited bus is not limited"), State.bBusLimited);
		TestTrue(TEXT("Unlimited: cooling authority 1"), State.CoolingAuthority == 1.0f);
		TestTrue(TEXT("Unlimited: nothing curtailed"), State.AuxiliaryCurtailedKilowatts == 0.0f);
		TestTrue(TEXT("Unlimited: delivered = total demand"), State.DeliveredDemandKilowatts == State.TotalDemandKilowatts);
		TestTrue(FString::Printf(TEXT("Unlimited: net power %.4f = legacy %.4f"), State.NetPowerKilowatts, LegacyNet), Near(State.NetPowerKilowatts, LegacyNet));
		TestTrue(FString::Printf(TEXT("Unlimited: charge %.7f = legacy %.7f"), Step.Snapshot.BatteryChargeKilowattHours, LegacyCharge),
			Near(Step.Snapshot.BatteryChargeKilowattHours, LegacyCharge, 1.0e-6));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2LimitedBusPriorityOrderTest,
	"Eden.Unit.SolarCrisisV2.B.LimitedBusPriorityOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2LimitedBusPriorityOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenSolarCrisisV2Tests;

	struct FCase
	{
		const TCHAR* Name;
		float Charge, External, Operator, Cooling;
		double Capacity, Essential, CoolingServed, AuxRequested, AuxServed, Authority, Delivered;
	};
	// Sandbox: 2 kW generation, 6 kW discharge, 1 kW essential; the flare adds 5 kW auxiliary.
	const FCase Cases[] = {
		{TEXT("Boost in flare"), 20.0f, 5.0f, 1.5f, 1.5f, 8.0, 1.0, 1.5, 5.0, 5.0, 1.0, 7.5},
		{TEXT("Emergency in flare"), 20.0f, 5.0f, 4.5f, 4.5f, 8.0, 1.0, 4.5, 5.0, 2.5, 1.0, 8.0},
		{TEXT("Emergency + load shed"), 20.0f, 5.0f, 2.5f, 4.5f, 8.0, 1.0, 4.5, 3.0, 2.5, 1.0, 8.0},
		{TEXT("Boost, battery empty"), 0.0f, 5.0f, 1.5f, 1.5f, 2.0, 1.0, 1.0, 5.0, 0.0, 1.0 / 1.5, 2.0},
		{TEXT("Emergency, battery empty"), 0.0f, 5.0f, 4.5f, 4.5f, 2.0, 1.0, 1.0, 5.0, 0.0, 1.0 / 4.5, 2.0},
		{TEXT("Nominal + shed, no aux"), 20.0f, 0.0f, -1.5f, 0.5f, 8.0, 0.0, 0.5, 0.0, 0.0, 1.0, 0.5},
	};
	for (const FCase& Case : Cases)
	{
		const FEdenPowerStateSnapshot S = FEdenPowerModel::WithBusAllocation(MakePowerState(Case.Charge, Case.External, Case.Operator), Case.Cooling, 6.0f);
		const FString N = Case.Name;
		TestTrue(N + TEXT(": limited"), S.bBusLimited);
		TestTrue(FString::Printf(TEXT("%s: capacity %.3f"), *N, S.BusCapacityKilowatts), Near(S.BusCapacityKilowatts, Case.Capacity));
		TestTrue(FString::Printf(TEXT("%s: essential first %.3f"), *N, S.EssentialServedKilowatts), Near(S.EssentialServedKilowatts, Case.Essential));
		TestTrue(FString::Printf(TEXT("%s: cooling second %.3f"), *N, S.ThermalControlServedKilowatts), Near(S.ThermalControlServedKilowatts, Case.CoolingServed));
		TestTrue(FString::Printf(TEXT("%s: auxiliary requested %.3f"), *N, S.AuxiliaryRequestedKilowatts), Near(S.AuxiliaryRequestedKilowatts, Case.AuxRequested));
		TestTrue(FString::Printf(TEXT("%s: auxiliary third %.3f"), *N, S.AuxiliaryServedKilowatts), Near(S.AuxiliaryServedKilowatts, Case.AuxServed));
		TestTrue(FString::Printf(TEXT("%s: curtailed = requested - served"), *N),
			Near(S.AuxiliaryCurtailedKilowatts, Case.AuxRequested - Case.AuxServed));
		TestTrue(FString::Printf(TEXT("%s: cooling authority %.4f"), *N, S.CoolingAuthority), Near(S.CoolingAuthority, Case.Authority));
		TestTrue(FString::Printf(TEXT("%s: delivered %.3f"), *N, S.DeliveredDemandKilowatts), Near(S.DeliveredDemandKilowatts, Case.Delivered));
		TestTrue(FString::Printf(TEXT("%s: net = generation - delivered"), *N), Near(S.NetPowerKilowatts, 2.0 - Case.Delivered));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2LimitedBusEnergyAccountedOnceTest,
	"Eden.Unit.SolarCrisisV2.B.LimitedBusEnergyAccountedOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2LimitedBusEnergyAccountedOnceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenSolarCrisisV2Tests;

	const FEdenPowerConfig Config = MakeSandboxPowerConfig();
	// Emergency + flare from a full pack: 8 kW delivered, 6 kW from the battery, for 10 s.
	FEdenPowerStateSnapshot State = FEdenPowerModel::WithBusAllocation(MakePowerState(20.0f, 5.0f, 4.5f), 4.5f, 6.0f);
	double EnergyDrawn = 0.0;
	float FloatReference = 20.0f; // the charge is stored as float, exactly as before 0011
	for (int32 Step = 0; Step < 100; ++Step)
	{
		const FEdenPowerStepResult Result = FEdenPowerModel::Step(Config, State, 0.1f);
		EnergyDrawn -= Result.EnergyDeltaKilowattHours;
		FloatReference = static_cast<float>(static_cast<double>(FloatReference) + static_cast<double>(Result.EnergyDeltaKilowattHours));
		TestTrue(TEXT("Each step draws the delivered-minus-generated 6 kW once"), Near(Result.EnergyDeltaKilowattHours, -6.0 * 0.1 / 3600.0, 1.0e-9));
		State = Result.Snapshot;
	}
	TestTrue(FString::Printf(TEXT("Energy drawn = 6 kW x 10 s = 0.016667 kWh (%.7f)"), EnergyDrawn), Near(EnergyDrawn, 6.0 * 10.0 / 3600.0, 1.0e-7));
	TestTrue(FString::Printf(TEXT("Battery = float accumulation of those deltas (%.6f)"), State.BatteryChargeKilowattHours),
		Near(State.BatteryChargeKilowattHours, FloatReference, 1.0e-6));
	TestTrue(TEXT("Bus inputs survive stepping"), State.ThermalControlDemandKilowatts == 4.5f && State.BatteryMaxDischargeKilowatts == 6.0f);

	// Nearly empty: the battery can supply only what it holds this step (no negative energy, no clamp loss).
	const float Tiny = 0.0001f; // 0.36 kJ: at most 3.6 kW over 0.1 s
	FEdenPowerStateSnapshot Last = FEdenPowerModel::WithBusAllocation(MakePowerState(Tiny, 5.0f, 4.5f), 4.5f, 6.0f);
	const FEdenPowerStepResult Drain = FEdenPowerModel::Step(Config, Last, 0.1f);
	TestTrue(FString::Printf(TEXT("Last step drains the battery exactly (%.9f kWh left)"), Drain.Snapshot.BatteryChargeKilowattHours),
		FMath::IsNearlyZero(Drain.Snapshot.BatteryChargeKilowattHours, 1.0e-8f));
	TestTrue(FString::Printf(TEXT("Energy delta equals what the battery held (%.9f)"), Drain.EnergyDeltaKilowattHours),
		Near(Drain.EnergyDeltaKilowattHours, -Tiny, 1.0e-8));
	TestTrue(TEXT("Empty battery: capacity falls to generation"), Near(Drain.Snapshot.BusCapacityKilowatts, 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2CoolingAuthorityScalesActiveCoolingTest,
	"Eden.Unit.SolarCrisisV2.B.CoolingAuthorityScalesOnlyActiveCooling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2CoolingAuthorityScalesActiveCoolingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenSolarCrisisV2Tests;

	const FEdenThermalConfig Config = MakeSandboxThermalConfig();
	// Emergency (+2.0 active) with load shed (-0.4): operator dissipation 1.6.
	const FEdenThermalStateSnapshot Base = FEdenThermalModel::MakeSnapshot(Config, 60.0f, 1.0f, 0.5f, 0.0f, 1.6f);
	auto Effective = [&](float Authority) { return FEdenThermalModel::WithCooling(Base, 2.0f, Authority).EffectiveDissipationDegreesCelsiusPerSecond; };
	TestTrue(TEXT("Full power: 0.5 + 1.6 = 2.1"), Near(Effective(1.0f), 2.1));
	TestTrue(TEXT("Half power: only active cooling halves (2.1 - 1.0 = 1.1)"), Near(Effective(0.5f), 1.1));
	TestTrue(TEXT("No power: passive minus the shed penalty remains (0.1)"), Near(Effective(0.0f), 0.1));
	TestTrue(TEXT("Authority is clamped to [0, 1]"), Near(Effective(7.0f), 2.1) && Near(Effective(-3.0f), 0.1));

	// Step honours the authority carried in the snapshot.
	const FEdenThermalStateSnapshot HalfPowered = FEdenThermalModel::WithCooling(Base, 2.0f, 0.5f);
	const FEdenThermalStepResult Next = FEdenThermalModel::Step(Config, HalfPowered, 0.1f);
	TestTrue(FString::Printf(TEXT("Step: net 1.0 - 1.1 = -0.1 C/s (delta %.4f)"), Next.TemperatureDeltaCelsius), Near(Next.TemperatureDeltaCelsius, -0.01, 1.0e-4));
	TestTrue(TEXT("Step carries the cooling inputs"), Next.Snapshot.CoolingAuthority == 0.5f && Next.Snapshot.ActiveCoolingDegreesCelsiusPerSecond == 2.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2CoolingFollowsActualPowerTest,
	"Eden.Integration.SolarCrisisV2.B.CoolingFollowsActualPowerAvailability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2CoolingFollowsActualPowerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenSolarCrisisV2Tests;

	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>();
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>();
	if (!TestTrue(TEXT("Systems initialize"), Power->InitializePowerSimulation(MakeSandboxPowerConfig()) && Thermal->InitializeThermalSimulation(MakeSandboxThermalConfig())))
	{
		return false;
	}
	Thermal->SetCoolingPowerSource(Power);

	// Emergency cooling during the 4.25 C/s flare with its 5 kW auxiliary load.
	Power->SetOperatorDemandBreakdown(4.5f, 4.5f);
	Power->SetExternalDemandKilowatts(5.0f);
	Thermal->SetOperatorCooling(2.0f, 2.0f);
	Thermal->SetExternalHeatingRateDegreesCelsiusPerSecond(4.25f);
	Thermal->SetTemperatureCelsius(60.0f);

	auto RunOneSecond = [&]()
	{
		const float Start = Thermal->GetThermalStateSnapshot().TemperatureCelsius;
		for (int32 Step = 0; Step < 10; ++Step)
		{
			Power->AdvanceSimulation(0.1f);
			Thermal->AdvanceSimulation(0.1f);
		}
		return Thermal->GetThermalStateSnapshot().TemperatureCelsius - Start;
	};

	const float Powered = RunOneSecond();
	TestTrue(FString::Printf(TEXT("Charged battery: full Emergency cooling, +2.75 C/s (%.3f)"), Powered), Near(Powered, 2.75, 1.0e-3));
	TestTrue(TEXT("Charged battery: auxiliary curtailed 2.5 kW, reported"), Near(Power->GetPowerStateSnapshot().AuxiliaryCurtailedKilowatts, 2.5));

	// Battery dies: only 2 kW generation; essential 1 kW leaves 1 kW of the 4.5 kW cooling demand.
	Power->SetBatteryChargeKilowattHours(0.0f);
	const float Unpowered = RunOneSecond();
	const double Expected = 1.0 + 4.25 - (0.5 + 2.0 * (1.0 / 4.5));
	TestTrue(FString::Printf(TEXT("Dead battery: cooling authority 22%% (%.3f)"), Thermal->GetThermalStateSnapshot().CoolingAuthority),
		Near(Thermal->GetThermalStateSnapshot().CoolingAuthority, 1.0 / 4.5, 1.0e-4));
	TestTrue(FString::Printf(TEXT("Dead battery: the ship actually heats faster, +%.3f C/s (expected %.3f)"), Unpowered, Expected),
		Near(Unpowered, Expected, 2.0e-3));
	TestTrue(TEXT("Dead battery: all auxiliary load curtailed"), Near(Power->GetPowerStateSnapshot().AuxiliaryCurtailedKilowatts, 5.0));
	return true;
}

#endif
