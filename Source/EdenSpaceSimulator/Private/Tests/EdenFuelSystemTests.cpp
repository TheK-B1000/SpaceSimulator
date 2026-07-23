// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenFuelConfigDataAsset.h"
#include "Systems/EdenFuelModel.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "EdenFuelSystemTestListener.h"

#include "Misc/AutomationTest.h"

#include <limits>

void UEdenFuelSystemTestListener::HandleFuelStateChanged(EEdenFuelState PreviousState, EEdenFuelState NewState)
{
	PreviousStates.Add(PreviousState);
	NewStates.Add(NewState);
}

void UEdenFuelSystemTestListener::HandleFuelDepleted()
{
	++DepletedEventCount;
}

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenFuelSystemTests
{
constexpr double Tolerance = 0.001;

FEdenFuelConfig MakeValidConfig()
{
	FEdenFuelConfig Config;
	Config.CapacityKilograms = 100.0f;
	Config.ConsumptionRateKilogramsPerSecond = 10.0f;
	Config.InitialFuelFraction = 1.0f;
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

UEdenFuelSystemComponent* MakeInitializedComponent(const FEdenFuelConfig& Config)
{
	UEdenFuelSystemComponent* FuelComponent = NewObject<UEdenFuelSystemComponent>();
	FuelComponent->InitializeFuelSimulation(Config);
	return FuelComponent;
}

UEdenFuelSystemTestListener* BindListener(UEdenFuelSystemComponent* FuelComponent)
{
	UEdenFuelSystemTestListener* Listener = NewObject<UEdenFuelSystemTestListener>();
	FuelComponent->OnFuelStateChanged.AddDynamic(Listener, &UEdenFuelSystemTestListener::HandleFuelStateChanged);
	FuelComponent->OnFuelDepleted.AddDynamic(Listener, &UEdenFuelSystemTestListener::HandleFuelDepleted);
	return Listener;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelConfigValidationAcceptsValidConfigTest,
	"Eden.Unit.Systems.Fuel.ConfigValidationAcceptsValidConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelConfigValidationAcceptsValidConfigTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FString> ValidationErrors;
	TestTrue(TEXT("Valid fuel config passes"), FEdenFuelModel::ValidateConfig(EdenFuelSystemTests::MakeValidConfig(), &ValidationErrors));
	TestEqual(TEXT("Valid fuel config has no validation errors"), ValidationErrors.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelConfigRejectsInvalidCapacityTest,
	"Eden.Unit.Systems.Fuel.ConfigRejectsInvalidCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelConfigRejectsInvalidCapacityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float InvalidCapacities[] = {
		0.0f,
		-1.0f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()
	};

	for (const float CapacityKilograms : InvalidCapacities)
	{
		FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
		Config.CapacityKilograms = CapacityKilograms;
		TestFalse(TEXT("Invalid capacity is rejected"), FEdenFuelModel::ValidateConfig(Config));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelConfigRejectsInvalidConsumptionRateTest,
	"Eden.Unit.Systems.Fuel.ConfigRejectsInvalidConsumptionRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelConfigRejectsInvalidConsumptionRateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float InvalidRates[] = {
		-1.0f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()
	};

	for (const float ConsumptionRateKilogramsPerSecond : InvalidRates)
	{
		FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
		Config.ConsumptionRateKilogramsPerSecond = ConsumptionRateKilogramsPerSecond;
		TestFalse(TEXT("Invalid consumption rate is rejected"), FEdenFuelModel::ValidateConfig(Config));
	}

	FEdenFuelConfig ZeroRateConfig = EdenFuelSystemTests::MakeValidConfig();
	ZeroRateConfig.ConsumptionRateKilogramsPerSecond = 0.0f;
	TestTrue(TEXT("Zero consumption rate is valid"), FEdenFuelModel::ValidateConfig(ZeroRateConfig));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelConfigRejectsInvalidInitialFractionTest,
	"Eden.Unit.Systems.Fuel.ConfigRejectsInvalidInitialFraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelConfigRejectsInvalidInitialFractionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float InvalidFractions[] = {
		-0.01f,
		1.01f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()
	};

	for (const float InitialFuelFraction : InvalidFractions)
	{
		FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
		Config.InitialFuelFraction = InitialFuelFraction;
		TestFalse(TEXT("Invalid initial fuel fraction is rejected"), FEdenFuelModel::ValidateConfig(Config));
	}

	for (const float InitialFuelFraction : {0.0f, 1.0f})
	{
		FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
		Config.InitialFuelFraction = InitialFuelFraction;
		TestTrue(TEXT("Initial fuel fraction bounds are inclusive"), FEdenFuelModel::ValidateConfig(Config));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelConfigRejectsInvalidThresholdOrderingTest,
	"Eden.Unit.Systems.Fuel.ConfigRejectsInvalidThresholdOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelConfigRejectsInvalidThresholdOrderingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFuelConfig EqualThresholdConfig = EdenFuelSystemTests::MakeValidConfig();
	EqualThresholdConfig.CriticalThresholdFraction = 0.25f;
	EqualThresholdConfig.WarningThresholdFraction = 0.25f;
	TestFalse(TEXT("Critical must be strictly below warning"), FEdenFuelModel::ValidateConfig(EqualThresholdConfig));

	FEdenFuelConfig ReversedThresholdConfig = EdenFuelSystemTests::MakeValidConfig();
	ReversedThresholdConfig.CriticalThresholdFraction = 0.5f;
	ReversedThresholdConfig.WarningThresholdFraction = 0.25f;
	TestFalse(TEXT("Critical above warning is rejected"), FEdenFuelModel::ValidateConfig(ReversedThresholdConfig));

	FEdenFuelConfig OutOfRangeThresholdConfig = EdenFuelSystemTests::MakeValidConfig();
	OutOfRangeThresholdConfig.CriticalThresholdFraction = -0.01f;
	TestFalse(TEXT("Out-of-range critical threshold is rejected"), FEdenFuelModel::ValidateConfig(OutOfRangeThresholdConfig));

	FEdenFuelConfig InclusiveBoundaryConfig = EdenFuelSystemTests::MakeValidConfig();
	InclusiveBoundaryConfig.CriticalThresholdFraction = 0.0f;
	InclusiveBoundaryConfig.WarningThresholdFraction = 1.0f;
	TestTrue(TEXT("0 <= critical < warning <= 1 is accepted"), FEdenFuelModel::ValidateConfig(InclusiveBoundaryConfig));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelInitialSnapshotUsesConfiguredFractionTest,
	"Eden.Unit.Systems.Fuel.InitialSnapshotUsesConfiguredFraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelInitialSnapshotUsesConfiguredFractionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
	Config.CapacityKilograms = 80.0f;
	Config.InitialFuelFraction = 0.5f;

	const FEdenFuelStateSnapshot Snapshot = FEdenFuelModel::MakeInitialSnapshot(Config);

	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Initial quantity uses configured fraction"), Snapshot.FuelQuantityKilograms, 40.0f);
	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Initial fraction is reported"), Snapshot.FuelFraction, 0.5f);
	TestEqual(TEXT("Initial state is derived from initial fraction"), Snapshot.FuelState, EEdenFuelState::Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelZeroDemandDoesNotConsumeTest,
	"Eden.Unit.Systems.Fuel.ZeroDemandDoesNotConsume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelZeroDemandDoesNotConsumeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
	const FEdenFuelStateSnapshot InitialSnapshot = FEdenFuelModel::MakeInitialSnapshot(Config);
	const FEdenFuelStepResult Result = FEdenFuelModel::Step(Config, InitialSnapshot, 0.0f, 10.0f);

	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Zero demand consumes no fuel"), Result.Snapshot.FuelQuantityKilograms, 100.0f);
	TestEqual(TEXT("Zero demand remains normal"), Result.Snapshot.FuelState, EEdenFuelState::Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelConsumesAndClampsTest,
	"Eden.Unit.Systems.Fuel.ConsumesAndClamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelConsumesAndClampsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
	const FEdenFuelStateSnapshot InitialSnapshot = FEdenFuelModel::MakeInitialSnapshot(Config);
	const FEdenFuelStepResult HalfDemandResult = FEdenFuelModel::Step(Config, InitialSnapshot, 0.5f, 2.0f);

	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Fuel consumption scales by normalized demand"), HalfDemandResult.FuelConsumedKilograms, 10.0f);
	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Fuel quantity decreases"), HalfDemandResult.Snapshot.FuelQuantityKilograms, 90.0f);

	bool bQuantityWasSanitized = false;
	const float ClampedQuantity = FEdenFuelModel::ClampFuelQuantityKilograms(200.0f, Config.CapacityKilograms, &bQuantityWasSanitized);
	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Fuel quantity clamps to capacity"), ClampedQuantity, 100.0f);
	TestTrue(TEXT("Fuel quantity reports clamping"), bQuantityWasSanitized);

	const float ClampedNegativeQuantity = FEdenFuelModel::ClampFuelQuantityKilograms(-25.0f, Config.CapacityKilograms, &bQuantityWasSanitized);
	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Fuel quantity clamps to zero"), ClampedNegativeQuantity, 0.0f);
	TestTrue(TEXT("Negative fuel quantity reports clamping"), bQuantityWasSanitized);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelComponentConsumesOnAdvanceTest,
	"Eden.Unit.Systems.Fuel.ComponentConsumesOnAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelComponentConsumesOnAdvanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenFuelSystemComponent* FuelComponent = EdenFuelSystemTests::MakeInitializedComponent(EdenFuelSystemTests::MakeValidConfig());

	FuelComponent->SetConsumptionDemandNormalized(0.5f);
	FuelComponent->AdvanceSimulation(2.0f);

	const FEdenFuelStateSnapshot Snapshot = FuelComponent->GetFuelStateSnapshot();
	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Component consumes fuel using current demand"), Snapshot.FuelQuantityKilograms, 90.0f);
	TestEqual(TEXT("Component remains normal after small consumption"), Snapshot.FuelState, EEdenFuelState::Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelDerivesWarningCriticalAndDepletedStatesTest,
	"Eden.Unit.Systems.Fuel.DerivesWarningCriticalAndDepletedStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelDerivesWarningCriticalAndDepletedStatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();

	TestEqual(TEXT("Normal above warning"), FEdenFuelModel::DeriveFuelState(Config, 26.0f), EEdenFuelState::Normal);
	TestEqual(TEXT("Warning at warning threshold"), FEdenFuelModel::DeriveFuelState(Config, 25.0f), EEdenFuelState::Warning);
	TestEqual(TEXT("Critical at critical threshold"), FEdenFuelModel::DeriveFuelState(Config, 10.0f), EEdenFuelState::Critical);
	TestEqual(TEXT("Depleted at zero quantity"), FEdenFuelModel::DeriveFuelState(Config, 0.0f), EEdenFuelState::Depleted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelMultiThresholdCrossingEmitsSingleTransitionTest,
	"Eden.Unit.Systems.Fuel.MultiThresholdCrossingEmitsSingleTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelMultiThresholdCrossingEmitsSingleTransitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
	Config.ConsumptionRateKilogramsPerSecond = 1000.0f;
	UEdenFuelSystemComponent* FuelComponent = EdenFuelSystemTests::MakeInitializedComponent(Config);
	UEdenFuelSystemTestListener* Listener = EdenFuelSystemTests::BindListener(FuelComponent);

	FuelComponent->SetConsumptionDemandNormalized(1.0f);
	FuelComponent->AdvanceSimulation(0.2f);

	TestEqual(TEXT("One final transition is emitted"), Listener->NewStates.Num(), 1);
	TestEqual(TEXT("Transition starts at normal"), Listener->PreviousStates[0], EEdenFuelState::Normal);
	TestEqual(TEXT("Transition goes directly to depleted"), Listener->NewStates[0], EEdenFuelState::Depleted);
	TestEqual(TEXT("Depleted event emits once"), Listener->DepletedEventCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelDepletedEventOnlyWhenEnteringDepletedTest,
	"Eden.Unit.Systems.Fuel.DepletedEventOnlyWhenEnteringDepleted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelDepletedEventOnlyWhenEnteringDepletedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
	Config.ConsumptionRateKilogramsPerSecond = 1000.0f;
	UEdenFuelSystemComponent* FuelComponent = EdenFuelSystemTests::MakeInitializedComponent(Config);
	UEdenFuelSystemTestListener* Listener = EdenFuelSystemTests::BindListener(FuelComponent);

	FuelComponent->SetConsumptionDemandNormalized(1.0f);
	FuelComponent->AdvanceSimulation(1.0f);
	FuelComponent->AdvanceSimulation(1.0f);

	TestEqual(TEXT("Depleted event is emitted only on entry"), Listener->DepletedEventCount, 1);
	TestEqual(TEXT("Only one state transition is emitted"), Listener->NewStates.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelSanitizesNonFiniteAndOutOfRangeDemandTest,
	"Eden.Unit.Systems.Fuel.SanitizesNonFiniteAndOutOfRangeDemand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelSanitizesNonFiniteAndOutOfRangeDemandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bDemandWasSanitized = false;
	TestEqual(
		TEXT("NaN demand sanitizes to zero"),
		FEdenFuelModel::SanitizeDemandNormalized(std::numeric_limits<float>::quiet_NaN(), &bDemandWasSanitized),
		0.0f);
	TestTrue(TEXT("NaN demand reports sanitization"), bDemandWasSanitized);

	TestEqual(TEXT("Demand above one clamps"), FEdenFuelModel::SanitizeDemandNormalized(5.0f, &bDemandWasSanitized), 1.0f);
	TestTrue(TEXT("Out-of-range demand reports sanitization"), bDemandWasSanitized);

	UEdenFuelSystemComponent* FuelComponent = EdenFuelSystemTests::MakeInitializedComponent(EdenFuelSystemTests::MakeValidConfig());
	TestFalse(TEXT("Component reports sanitized non-finite demand"), FuelComponent->SetConsumptionDemandNormalized(std::numeric_limits<float>::infinity()));
	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Component stores safe zero demand"), FuelComponent->GetConsumptionDemandNormalized(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelInvalidDeltaTimeDoesNotMutateTest,
	"Eden.Unit.Systems.Fuel.InvalidDeltaTimeDoesNotMutate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelInvalidDeltaTimeDoesNotMutateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
	const FEdenFuelStateSnapshot InitialSnapshot = FEdenFuelModel::MakeInitialSnapshot(Config);
	const float InvalidDeltaTimes[] = {
		0.0f,
		-0.1f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()
	};

	for (const float DeltaTimeSeconds : InvalidDeltaTimes)
	{
		const FEdenFuelStepResult Result = FEdenFuelModel::Step(Config, InitialSnapshot, 1.0f, DeltaTimeSeconds);
		TestFalse(TEXT("Invalid DeltaTime is reported"), Result.bDeltaTimeWasValid);
		EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Invalid DeltaTime keeps fuel quantity"), Result.Snapshot.FuelQuantityKilograms, InitialSnapshot.FuelQuantityKilograms);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelExcessiveConsumptionDepletesSafelyTest,
	"Eden.Unit.Systems.Fuel.ExcessiveConsumptionDepletesSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelExcessiveConsumptionDepletesSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
	Config.ConsumptionRateKilogramsPerSecond = 1000000.0f;
	const FEdenFuelStepResult Result = FEdenFuelModel::Step(Config, FEdenFuelModel::MakeInitialSnapshot(Config), 1.0f, 1000000.0f);

	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Excessive consumption clamps quantity to zero"), Result.Snapshot.FuelQuantityKilograms, 0.0f);
	TestEqual(TEXT("Excessive consumption derives depleted state"), Result.Snapshot.FuelState, EEdenFuelState::Depleted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelResetRestoresConfiguredInitialFuelTest,
	"Eden.Unit.Systems.Fuel.ResetRestoresConfiguredInitialFuel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelResetRestoresConfiguredInitialFuelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFuelConfig Config = EdenFuelSystemTests::MakeValidConfig();
	Config.InitialFuelFraction = 0.6f;
	UEdenFuelSystemComponent* FuelComponent = EdenFuelSystemTests::MakeInitializedComponent(Config);

	FuelComponent->SetFuelQuantityKilograms(10.0f);
	TestTrue(TEXT("Fuel reset succeeds"), FuelComponent->ResetFuelState());

	const FEdenFuelStateSnapshot Snapshot = FuelComponent->GetFuelStateSnapshot();
	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Reset restores configured quantity"), Snapshot.FuelQuantityKilograms, 60.0f);
	EdenFuelSystemTests::TestFloatNearlyEqual(*this, TEXT("Reset restores configured fraction"), Snapshot.FuelFraction, 0.6f);
	TestEqual(TEXT("Reset clears demand"), FuelComponent->GetConsumptionDemandNormalized(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelRecoveryFromDepletedTransitionsSafelyTest,
	"Eden.Unit.Systems.Fuel.RecoveryFromDepletedTransitionsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelRecoveryFromDepletedTransitionsSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenFuelSystemComponent* FuelComponent = EdenFuelSystemTests::MakeInitializedComponent(EdenFuelSystemTests::MakeValidConfig());
	UEdenFuelSystemTestListener* Listener = EdenFuelSystemTests::BindListener(FuelComponent);

	FuelComponent->SetFuelQuantityKilograms(0.0f);
	FuelComponent->SetFuelQuantityKilograms(50.0f);

	TestEqual(TEXT("Two real transitions are emitted"), Listener->NewStates.Num(), 2);
	TestEqual(TEXT("First transition enters depleted"), Listener->NewStates[0], EEdenFuelState::Depleted);
	TestEqual(TEXT("Second transition recovers to normal"), Listener->NewStates[1], EEdenFuelState::Normal);
	TestEqual(TEXT("Depleted event only emits on entering depleted"), Listener->DepletedEventCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelMissingConfigDataAssetDisablesSimulationSafelyTest,
	"Eden.Unit.Systems.Fuel.MissingConfigDataAssetDisablesSimulationSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelMissingConfigDataAssetDisablesSimulationSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenFuelSystemComponent* FuelComponent = NewObject<UEdenFuelSystemComponent>();

	TestFalse(TEXT("Reset without config fails safely"), FuelComponent->ResetFuelState());
	TestFalse(TEXT("Fuel simulation remains disabled"), FuelComponent->IsFuelSimulationEnabled());
	TestFalse(TEXT("Register without world/config is safe"), FuelComponent->RegisterWithSimulationClock());
	TestFalse(TEXT("Unregister without clock is safe"), FuelComponent->UnregisterFromSimulationClock());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFuelInvalidExplicitConfigDisablesSimulationSafelyTest,
	"Eden.Unit.Systems.Fuel.InvalidExplicitConfigDisablesSimulationSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFuelInvalidExplicitConfigDisablesSimulationSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFuelConfig InvalidConfig = EdenFuelSystemTests::MakeValidConfig();
	InvalidConfig.CriticalThresholdFraction = 0.5f;
	InvalidConfig.WarningThresholdFraction = 0.25f;

	UEdenFuelSystemComponent* FuelComponent = NewObject<UEdenFuelSystemComponent>();

	AddExpectedError(TEXT("invalid fuel configuration"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid explicit config is rejected"), FuelComponent->InitializeFuelSimulation(InvalidConfig));
	TestFalse(TEXT("Fuel simulation is disabled"), FuelComponent->IsFuelSimulationEnabled());
	TestEqual(TEXT("Disabled fuel state is depleted"), FuelComponent->GetFuelStateSnapshot().FuelState, EEdenFuelState::Depleted);

	return true;
}

#endif
