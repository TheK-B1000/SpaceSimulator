// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenPowerConfigDataAsset.h"
#include "Systems/EdenPowerModel.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "EdenPowerThermalSystemTestListener.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenPowerSystemTests
{
constexpr double Tolerance = 0.001;

FEdenPowerConfig MakeValidConfig()
{
	FEdenPowerConfig Config;
	Config.BatteryCapacityKilowattHours = 10.0f;
	Config.GenerationKilowatts = 1.0f;
	Config.BaselineDemandKilowatts = 2.0f;
	Config.InitialChargeFraction = 1.0f;
	Config.WarningThresholdFraction = 0.25f;
	Config.CriticalThresholdFraction = 0.1f;
	return Config;
}

bool TestFloatNearlyEqual(FAutomationTestBase& Test, const TCHAR* What, float Actual, float Expected)
{
	return Test.TestTrue(
		FString::Printf(TEXT("%s. Actual=%f Expected=%f"), What, Actual, Expected),
		FMath::IsNearlyEqual(Actual, Expected, Tolerance));
}

UEdenPowerSystemComponent* MakeInitializedComponent(const FEdenPowerConfig& Config)
{
	UEdenPowerSystemComponent* PowerComponent = NewObject<UEdenPowerSystemComponent>();
	PowerComponent->InitializePowerSimulation(Config);
	return PowerComponent;
}

UEdenPowerSystemTestListener* BindListener(UEdenPowerSystemComponent* PowerComponent)
{
	UEdenPowerSystemTestListener* Listener = NewObject<UEdenPowerSystemTestListener>();
	PowerComponent->OnPowerStateChanged.AddDynamic(Listener, &UEdenPowerSystemTestListener::HandlePowerStateChanged);
	PowerComponent->OnPowerDepleted.AddDynamic(Listener, &UEdenPowerSystemTestListener::HandlePowerDepleted);
	return Listener;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerConfigValidationAcceptsValidConfigTest,
	"Eden.Unit.Systems.Power.ConfigValidationAcceptsValidConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerConfigValidationAcceptsValidConfigTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FString> ValidationErrors;
	TestTrue(TEXT("Valid power config passes"), FEdenPowerModel::ValidateConfig(EdenPowerSystemTests::MakeValidConfig(), &ValidationErrors));
	TestEqual(TEXT("Valid power config has no validation errors"), ValidationErrors.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerConfigRejectsInvalidCapacityGenerationAndDemandTest,
	"Eden.Unit.Systems.Power.ConfigRejectsInvalidCapacityGenerationAndDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerConfigRejectsInvalidCapacityGenerationAndDemandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float InvalidCapacities[] = {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
	for (const float BatteryCapacityKilowattHours : InvalidCapacities)
	{
		FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
		Config.BatteryCapacityKilowattHours = BatteryCapacityKilowattHours;
		TestFalse(TEXT("Invalid battery capacity is rejected"), FEdenPowerModel::ValidateConfig(Config));
	}

	const float InvalidNonnegativeValues[] = {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
	for (const float InvalidValue : InvalidNonnegativeValues)
	{
		FEdenPowerConfig GenerationConfig = EdenPowerSystemTests::MakeValidConfig();
		GenerationConfig.GenerationKilowatts = InvalidValue;
		TestFalse(TEXT("Invalid generation is rejected"), FEdenPowerModel::ValidateConfig(GenerationConfig));

		FEdenPowerConfig DemandConfig = EdenPowerSystemTests::MakeValidConfig();
		DemandConfig.BaselineDemandKilowatts = InvalidValue;
		TestFalse(TEXT("Invalid demand is rejected"), FEdenPowerModel::ValidateConfig(DemandConfig));
	}

	FEdenPowerConfig ZeroGenerationAndDemandConfig = EdenPowerSystemTests::MakeValidConfig();
	ZeroGenerationAndDemandConfig.GenerationKilowatts = 0.0f;
	ZeroGenerationAndDemandConfig.BaselineDemandKilowatts = 0.0f;
	TestTrue(TEXT("Zero generation and demand are valid"), FEdenPowerModel::ValidateConfig(ZeroGenerationAndDemandConfig));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerConfigRejectsInvalidInitialChargeFractionTest,
	"Eden.Unit.Systems.Power.ConfigRejectsInvalidInitialChargeFraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerConfigRejectsInvalidInitialChargeFractionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float InvalidFractions[] = {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
	for (const float InitialChargeFraction : InvalidFractions)
	{
		FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
		Config.InitialChargeFraction = InitialChargeFraction;
		TestFalse(TEXT("Invalid initial charge fraction is rejected"), FEdenPowerModel::ValidateConfig(Config));
	}

	for (const float InitialChargeFraction : {0.0f, 1.0f})
	{
		FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
		Config.InitialChargeFraction = InitialChargeFraction;
		TestTrue(TEXT("Initial charge fraction bounds are inclusive"), FEdenPowerModel::ValidateConfig(Config));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerConfigRejectsInvalidThresholdOrderingTest,
	"Eden.Unit.Systems.Power.ConfigRejectsInvalidThresholdOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerConfigRejectsInvalidThresholdOrderingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenPowerConfig EqualThresholdConfig = EdenPowerSystemTests::MakeValidConfig();
	EqualThresholdConfig.CriticalThresholdFraction = 0.25f;
	EqualThresholdConfig.WarningThresholdFraction = 0.25f;
	TestFalse(TEXT("Critical must be strictly below warning"), FEdenPowerModel::ValidateConfig(EqualThresholdConfig));

	FEdenPowerConfig ReversedThresholdConfig = EdenPowerSystemTests::MakeValidConfig();
	ReversedThresholdConfig.CriticalThresholdFraction = 0.5f;
	ReversedThresholdConfig.WarningThresholdFraction = 0.25f;
	TestFalse(TEXT("Critical above warning is rejected"), FEdenPowerModel::ValidateConfig(ReversedThresholdConfig));

	FEdenPowerConfig InclusiveBoundaryConfig = EdenPowerSystemTests::MakeValidConfig();
	InclusiveBoundaryConfig.CriticalThresholdFraction = 0.0f;
	InclusiveBoundaryConfig.WarningThresholdFraction = 1.0f;
	TestTrue(TEXT("0 <= critical < warning <= 1 is accepted"), FEdenPowerModel::ValidateConfig(InclusiveBoundaryConfig));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerInitialSnapshotUsesConfiguredChargeTest,
	"Eden.Unit.Systems.Power.InitialSnapshotUsesConfiguredCharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerInitialSnapshotUsesConfiguredChargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	Config.BatteryCapacityKilowattHours = 8.0f;
	Config.InitialChargeFraction = 0.5f;

	const FEdenPowerStateSnapshot Snapshot = FEdenPowerModel::MakeInitialSnapshot(Config);

	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Initial charge uses configured fraction"), Snapshot.BatteryChargeKilowattHours, 4.0f);
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Initial charge fraction is reported"), Snapshot.ChargeFraction, 0.5f);
	TestEqual(TEXT("Initial state is derived from charge fraction"), Snapshot.PowerState, EEdenPowerState::Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerConvertsKilowattsToKilowattHoursTest,
	"Eden.Unit.Systems.Power.ConvertsKilowattsToKilowattHours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerConvertsKilowattsToKilowattHoursTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	const FEdenPowerStepResult Result = FEdenPowerModel::Step(Config, FEdenPowerModel::MakeInitialSnapshot(Config), 3600.0f);

	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Net -1 kW over one hour is -1 kWh"), Result.EnergyDeltaKilowattHours, -1.0f);
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Battery charge is reduced by kWh delta"), Result.Snapshot.BatteryChargeKilowattHours, 9.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerClampsBatteryChargeTest,
	"Eden.Unit.Systems.Power.ClampsBatteryCharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerClampsBatteryChargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	bool bChargeWasSanitized = false;

	EdenPowerSystemTests::TestFloatNearlyEqual(
		*this,
		TEXT("Battery charge clamps to capacity"),
		FEdenPowerModel::ClampBatteryChargeKilowattHours(20.0f, Config.BatteryCapacityKilowattHours, &bChargeWasSanitized),
		10.0f);
	TestTrue(TEXT("Over-capacity charge reports clamping"), bChargeWasSanitized);

	EdenPowerSystemTests::TestFloatNearlyEqual(
		*this,
		TEXT("Battery charge clamps to zero"),
		FEdenPowerModel::ClampBatteryChargeKilowattHours(-1.0f, Config.BatteryCapacityKilowattHours, &bChargeWasSanitized),
		0.0f);
	TestTrue(TEXT("Negative charge reports clamping"), bChargeWasSanitized);

	FEdenPowerStateSnapshot Snapshot = FEdenPowerModel::MakeSnapshot(Config, 9.5f, 10.0f, 0.0f);
	Snapshot = FEdenPowerModel::Step(Config, Snapshot, 3600.0f).Snapshot;
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Charging clamps at capacity"), Snapshot.BatteryChargeKilowattHours, 10.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerDerivesWarningCriticalAndDepletedStatesTest,
	"Eden.Unit.Systems.Power.DerivesWarningCriticalAndDepletedStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerDerivesWarningCriticalAndDepletedStatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();

	TestEqual(TEXT("Normal above warning"), FEdenPowerModel::DerivePowerState(Config, 2.6f), EEdenPowerState::Normal);
	TestEqual(TEXT("Warning at warning threshold"), FEdenPowerModel::DerivePowerState(Config, 2.5f), EEdenPowerState::Warning);
	TestEqual(TEXT("Critical at critical threshold"), FEdenPowerModel::DerivePowerState(Config, 1.0f), EEdenPowerState::Critical);
	TestEqual(TEXT("Depleted at zero charge"), FEdenPowerModel::DerivePowerState(Config, 0.0f), EEdenPowerState::Depleted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerInvalidDeltaTimeDoesNotMutateTest,
	"Eden.Unit.Systems.Power.InvalidDeltaTimeDoesNotMutate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerInvalidDeltaTimeDoesNotMutateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	const FEdenPowerStateSnapshot InitialSnapshot = FEdenPowerModel::MakeInitialSnapshot(Config);
	const float InvalidDeltaTimes[] = {0.0f, -0.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};

	for (const float DeltaTimeSeconds : InvalidDeltaTimes)
	{
		const FEdenPowerStepResult Result = FEdenPowerModel::Step(Config, InitialSnapshot, DeltaTimeSeconds);
		TestFalse(TEXT("Invalid DeltaTime is reported"), Result.bDeltaTimeWasValid);
		EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Invalid DeltaTime keeps battery charge"), Result.Snapshot.BatteryChargeKilowattHours, InitialSnapshot.BatteryChargeKilowattHours);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerSanitizesNonFiniteGenerationAndDemandTest,
	"Eden.Unit.Systems.Power.SanitizesNonFiniteGenerationAndDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerSanitizesNonFiniteGenerationAndDemandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bValueWasSanitized = false;
	TestEqual(
		TEXT("Infinity generation sanitizes to zero"),
		FEdenPowerModel::SanitizeNonnegativeKilowatts(std::numeric_limits<float>::infinity(), &bValueWasSanitized),
		0.0f);
	TestTrue(TEXT("Infinity reports sanitization"), bValueWasSanitized);

	TestEqual(TEXT("Negative demand sanitizes to zero"), FEdenPowerModel::SanitizeNonnegativeKilowatts(-5.0f, &bValueWasSanitized), 0.0f);
	TestTrue(TEXT("Negative demand reports sanitization"), bValueWasSanitized);

	UEdenPowerSystemComponent* PowerComponent = EdenPowerSystemTests::MakeInitializedComponent(EdenPowerSystemTests::MakeValidConfig());
	TestFalse(TEXT("Component reports sanitized generation"), PowerComponent->SetGenerationKilowatts(std::numeric_limits<float>::quiet_NaN()));
	TestFalse(TEXT("Component reports sanitized demand"), PowerComponent->SetBaselineDemandKilowatts(-2.0f));
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Generation stores safe zero"), PowerComponent->GetPowerStateSnapshot().GenerationKilowatts, 0.0f);
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Demand stores safe zero"), PowerComponent->GetPowerStateSnapshot().BaselineDemandKilowatts, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerMultiThresholdCrossingEmitsSingleTransitionTest,
	"Eden.Unit.Systems.Power.MultiThresholdCrossingEmitsSingleTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerMultiThresholdCrossingEmitsSingleTransitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	Config.GenerationKilowatts = 0.0f;
	Config.BaselineDemandKilowatts = 1000.0f;
	UEdenPowerSystemComponent* PowerComponent = EdenPowerSystemTests::MakeInitializedComponent(Config);
	UEdenPowerSystemTestListener* Listener = EdenPowerSystemTests::BindListener(PowerComponent);

	PowerComponent->AdvanceSimulation(3600.0f);

	TestEqual(TEXT("One final transition is emitted"), Listener->NewStates.Num(), 1);
	TestEqual(TEXT("Transition starts at normal"), Listener->PreviousStates[0], EEdenPowerState::Normal);
	TestEqual(TEXT("Transition goes directly to depleted"), Listener->NewStates[0], EEdenPowerState::Depleted);
	TestEqual(TEXT("Depleted event emits once"), Listener->DepletedEventCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerDepletedEventOnlyWhenEnteringDepletedTest,
	"Eden.Unit.Systems.Power.DepletedEventOnlyWhenEnteringDepleted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerDepletedEventOnlyWhenEnteringDepletedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	Config.GenerationKilowatts = 0.0f;
	Config.BaselineDemandKilowatts = 1000.0f;
	UEdenPowerSystemComponent* PowerComponent = EdenPowerSystemTests::MakeInitializedComponent(Config);
	UEdenPowerSystemTestListener* Listener = EdenPowerSystemTests::BindListener(PowerComponent);

	PowerComponent->AdvanceSimulation(3600.0f);
	PowerComponent->AdvanceSimulation(3600.0f);

	TestEqual(TEXT("Depleted event is emitted only on entry"), Listener->DepletedEventCount, 1);
	TestEqual(TEXT("Only one state transition is emitted"), Listener->NewStates.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerRecoveryFromDepletedTransitionsSafelyTest,
	"Eden.Unit.Systems.Power.RecoveryFromDepletedTransitionsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerRecoveryFromDepletedTransitionsSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenPowerSystemComponent* PowerComponent = EdenPowerSystemTests::MakeInitializedComponent(EdenPowerSystemTests::MakeValidConfig());
	UEdenPowerSystemTestListener* Listener = EdenPowerSystemTests::BindListener(PowerComponent);

	PowerComponent->SetBatteryChargeKilowattHours(0.0f);
	PowerComponent->SetBatteryChargeKilowattHours(5.0f);

	TestEqual(TEXT("Two real transitions are emitted"), Listener->NewStates.Num(), 2);
	TestEqual(TEXT("First transition enters depleted"), Listener->NewStates[0], EEdenPowerState::Depleted);
	TestEqual(TEXT("Second transition recovers to normal"), Listener->NewStates[1], EEdenPowerState::Normal);
	TestEqual(TEXT("Depleted event only emits on entering depleted"), Listener->DepletedEventCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerResetRestoresConfiguredInitialChargeTest,
	"Eden.Unit.Systems.Power.ResetRestoresConfiguredInitialCharge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerResetRestoresConfiguredInitialChargeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	Config.InitialChargeFraction = 0.6f;
	UEdenPowerSystemComponent* PowerComponent = EdenPowerSystemTests::MakeInitializedComponent(Config);

	PowerComponent->SetBatteryChargeKilowattHours(1.0f);
	PowerComponent->SetGenerationKilowatts(10.0f);
	PowerComponent->SetBaselineDemandKilowatts(0.0f);
	TestTrue(TEXT("Power reset succeeds"), PowerComponent->ResetPowerState());

	const FEdenPowerStateSnapshot Snapshot = PowerComponent->GetPowerStateSnapshot();
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Reset restores configured charge"), Snapshot.BatteryChargeKilowattHours, 6.0f);
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Reset restores configured generation"), Snapshot.GenerationKilowatts, Config.GenerationKilowatts);
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Reset restores configured demand"), Snapshot.BaselineDemandKilowatts, Config.BaselineDemandKilowatts);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerEquivalentSimulatedTimeMatchesTest,
	"Eden.Unit.Systems.Power.EquivalentSimulatedTimeMatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerEquivalentSimulatedTimeMatchesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	Config.GenerationKilowatts = 0.0f;
	Config.BaselineDemandKilowatts = 1.0f;

	const FEdenPowerStateSnapshot InitialSnapshot = FEdenPowerModel::MakeInitialSnapshot(Config);
	const FEdenPowerStateSnapshot SingleStepSnapshot = FEdenPowerModel::Step(Config, InitialSnapshot, 3600.0f).Snapshot;

	FEdenPowerStateSnapshot PartitionedSnapshot = InitialSnapshot;
	for (int32 StepIndex = 0; StepIndex < 4; ++StepIndex)
	{
		PartitionedSnapshot = FEdenPowerModel::Step(Config, PartitionedSnapshot, 900.0f).Snapshot;
	}

	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Equivalent partitions produce same charge"), PartitionedSnapshot.BatteryChargeKilowattHours, SingleStepSnapshot.BatteryChargeKilowattHours);
	TestEqual(TEXT("Equivalent partitions produce same state"), PartitionedSnapshot.PowerState, SingleStepSnapshot.PowerState);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerMissingAndInvalidConfigDisableSimulationSafelyTest,
	"Eden.Unit.Systems.Power.MissingAndInvalidConfigDisableSimulationSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenPowerMissingAndInvalidConfigDisableSimulationSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenPowerSystemComponent* MissingConfigComponent = NewObject<UEdenPowerSystemComponent>();
	TestFalse(TEXT("Reset without config fails safely"), MissingConfigComponent->ResetPowerState());
	TestFalse(TEXT("Power simulation remains disabled"), MissingConfigComponent->IsPowerSimulationEnabled());
	TestFalse(TEXT("Register without world/config is safe"), MissingConfigComponent->RegisterWithSimulationClock());

	FEdenPowerConfig InvalidConfig = EdenPowerSystemTests::MakeValidConfig();
	InvalidConfig.CriticalThresholdFraction = 0.5f;
	InvalidConfig.WarningThresholdFraction = 0.25f;
	UEdenPowerSystemComponent* InvalidConfigComponent = NewObject<UEdenPowerSystemComponent>();

	AddExpectedError(TEXT("invalid power configuration"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid explicit config is rejected"), InvalidConfigComponent->InitializePowerSimulation(InvalidConfig));
	TestFalse(TEXT("Invalid explicit config disables simulation"), InvalidConfigComponent->IsPowerSimulationEnabled());
	TestEqual(TEXT("Disabled power state is depleted"), InvalidConfigComponent->GetPowerStateSnapshot().PowerState, EEdenPowerState::Depleted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerExternalDemandIncreasesTotalDemandTest,
	"Eden.Unit.Systems.Power.ExternalDemandIncreasesTotalDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenPowerExternalDemandIncreasesTotalDemandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	Config.GenerationKilowatts = 10.0f;
	Config.BaselineDemandKilowatts = 2.0f;
	Config.BatteryCapacityKilowattHours = 10.0f;
	Config.InitialChargeFraction = 0.5f; // 5 kWh

	UEdenPowerSystemComponent* PowerComponent = EdenPowerSystemTests::MakeInitializedComponent(Config);
	TestTrue(TEXT("Set external demand succeeds"), PowerComponent->SetExternalDemandKilowatts(3.0f));

	FEdenPowerStateSnapshot Snapshot = PowerComponent->GetPowerStateSnapshot();
	TestEqual(TEXT("External demand recorded in snapshot"), Snapshot.ExternalDemandKilowatts, 3.0f);
	// Net power = Generation (10) - Baseline (2) - External (3) = 5 kW
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Net power reflects baseline plus external demand"), Snapshot.NetPowerKilowatts, 5.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerClearExternalDemandResetsToZeroTest,
	"Eden.Unit.Systems.Power.ClearExternalDemandResetsToZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenPowerClearExternalDemandResetsToZeroTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	Config.GenerationKilowatts = 10.0f;
	Config.BaselineDemandKilowatts = 2.0f;

	UEdenPowerSystemComponent* PowerComponent = EdenPowerSystemTests::MakeInitializedComponent(Config);
	PowerComponent->SetExternalDemandKilowatts(8.0f);
	TestEqual(TEXT("External demand active"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 8.0f);

	TestTrue(TEXT("Clear external demand succeeds"), PowerComponent->ClearExternalDemand());
	FEdenPowerStateSnapshot Snapshot = PowerComponent->GetPowerStateSnapshot();
	TestEqual(TEXT("External demand cleared to zero"), Snapshot.ExternalDemandKilowatts, 0.0f);
	EdenPowerSystemTests::TestFloatNearlyEqual(*this, TEXT("Net power restored to generation minus baseline"), Snapshot.NetPowerKilowatts, 8.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenPowerExternalDemandSanitizesNegativeAndNaNTest,
	"Eden.Unit.Systems.Power.ExternalDemandSanitizesNegativeAndNaN",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEdenPowerExternalDemandSanitizesNegativeAndNaNTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenPowerConfig Config = EdenPowerSystemTests::MakeValidConfig();
	UEdenPowerSystemComponent* PowerComponent = EdenPowerSystemTests::MakeInitializedComponent(Config);

	AddExpectedError(TEXT("sanitized requested external demand"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Negative external demand is sanitized"), PowerComponent->SetExternalDemandKilowatts(-5.0f));
	TestEqual(TEXT("Negative external demand clamped to zero"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 0.0f);

	TestFalse(TEXT("NaN external demand is sanitized"), PowerComponent->SetExternalDemandKilowatts(std::numeric_limits<float>::quiet_NaN()));
	TestEqual(TEXT("NaN external demand clamped to zero"), PowerComponent->GetPowerStateSnapshot().ExternalDemandKilowatts, 0.0f);

	return true;
}

#endif
