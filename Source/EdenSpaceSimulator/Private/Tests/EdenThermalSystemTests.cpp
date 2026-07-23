// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/EdenThermalConfigDataAsset.h"
#include "Systems/EdenThermalModel.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "EdenPowerThermalSystemTestListener.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenThermalSystemTests
{
constexpr double Tolerance = 0.001;

FEdenThermalConfig MakeValidConfig()
{
	FEdenThermalConfig Config;
	Config.AbsoluteMinTemperatureCelsius = -50.0f;
	Config.AmbientTemperatureCelsius = 20.0f;
	Config.WarningTemperatureCelsius = 70.0f;
	Config.CriticalTemperatureCelsius = 100.0f;
	Config.AbsoluteMaxTemperatureCelsius = 120.0f;
	Config.InitialTemperatureCelsius = 20.0f;
	Config.HeatGenerationDegreesCelsiusPerSecond = 10.0f;
	Config.DissipationDegreesCelsiusPerSecond = 2.0f;
	return Config;
}

bool TestFloatNearlyEqual(FAutomationTestBase& Test, const TCHAR* What, float Actual, float Expected)
{
	return Test.TestTrue(
		FString::Printf(TEXT("%s. Actual=%f Expected=%f"), What, Actual, Expected),
		FMath::IsNearlyEqual(Actual, Expected, Tolerance));
}

UEdenThermalSystemComponent* MakeInitializedComponent(const FEdenThermalConfig& Config)
{
	UEdenThermalSystemComponent* ThermalComponent = NewObject<UEdenThermalSystemComponent>();
	ThermalComponent->InitializeThermalSimulation(Config);
	return ThermalComponent;
}

UEdenThermalSystemTestListener* BindListener(UEdenThermalSystemComponent* ThermalComponent)
{
	UEdenThermalSystemTestListener* Listener = NewObject<UEdenThermalSystemTestListener>();
	ThermalComponent->OnThermalStateChanged.AddDynamic(Listener, &UEdenThermalSystemTestListener::HandleThermalStateChanged);
	ThermalComponent->OnThermalOverheated.AddDynamic(Listener, &UEdenThermalSystemTestListener::HandleThermalOverheated);
	return Listener;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalConfigValidationAcceptsValidConfigTest,
	"Eden.Unit.Systems.Thermal.ConfigValidationAcceptsValidConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalConfigValidationAcceptsValidConfigTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FString> ValidationErrors;
	TestTrue(TEXT("Valid thermal config passes"), FEdenThermalModel::ValidateConfig(EdenThermalSystemTests::MakeValidConfig(), &ValidationErrors));
	TestEqual(TEXT("Valid thermal config has no validation errors"), ValidationErrors.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalConfigRejectsInvalidThresholdOrderingTest,
	"Eden.Unit.Systems.Thermal.ConfigRejectsInvalidThresholdOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalConfigRejectsInvalidThresholdOrderingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenThermalConfig AmbientAtWarningConfig = EdenThermalSystemTests::MakeValidConfig();
	AmbientAtWarningConfig.AmbientTemperatureCelsius = 70.0f;
	TestFalse(TEXT("Ambient must be below warning"), FEdenThermalModel::ValidateConfig(AmbientAtWarningConfig));

	FEdenThermalConfig WarningAtCriticalConfig = EdenThermalSystemTests::MakeValidConfig();
	WarningAtCriticalConfig.WarningTemperatureCelsius = 100.0f;
	TestFalse(TEXT("Warning must be below critical"), FEdenThermalModel::ValidateConfig(WarningAtCriticalConfig));

	FEdenThermalConfig CriticalAboveMaxConfig = EdenThermalSystemTests::MakeValidConfig();
	CriticalAboveMaxConfig.CriticalTemperatureCelsius = 130.0f;
	TestFalse(TEXT("Critical must be at or below absolute max"), FEdenThermalModel::ValidateConfig(CriticalAboveMaxConfig));

	FEdenThermalConfig BoundaryConfig = EdenThermalSystemTests::MakeValidConfig();
	BoundaryConfig.AbsoluteMinTemperatureCelsius = BoundaryConfig.AmbientTemperatureCelsius;
	BoundaryConfig.AbsoluteMaxTemperatureCelsius = BoundaryConfig.CriticalTemperatureCelsius;
	TestTrue(TEXT("AbsoluteMin <= Ambient and Critical <= AbsoluteMax are inclusive"), FEdenThermalModel::ValidateConfig(BoundaryConfig));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalConfigRejectsInvalidInitialTemperatureAndRatesTest,
	"Eden.Unit.Systems.Thermal.ConfigRejectsInvalidInitialTemperatureAndRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalConfigRejectsInvalidInitialTemperatureAndRatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float InvalidTemperatures[] = {-60.0f, 130.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
	for (const float InitialTemperatureCelsius : InvalidTemperatures)
	{
		FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
		Config.InitialTemperatureCelsius = InitialTemperatureCelsius;
		TestFalse(TEXT("Invalid initial temperature is rejected"), FEdenThermalModel::ValidateConfig(Config));
	}

	const float InvalidRates[] = {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
	for (const float InvalidRate : InvalidRates)
	{
		FEdenThermalConfig HeatConfig = EdenThermalSystemTests::MakeValidConfig();
		HeatConfig.HeatGenerationDegreesCelsiusPerSecond = InvalidRate;
		TestFalse(TEXT("Invalid heat generation rate is rejected"), FEdenThermalModel::ValidateConfig(HeatConfig));

		FEdenThermalConfig DissipationConfig = EdenThermalSystemTests::MakeValidConfig();
		DissipationConfig.DissipationDegreesCelsiusPerSecond = InvalidRate;
		TestFalse(TEXT("Invalid dissipation rate is rejected"), FEdenThermalModel::ValidateConfig(DissipationConfig));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalInitialSnapshotUsesConfiguredTemperatureTest,
	"Eden.Unit.Systems.Thermal.InitialSnapshotUsesConfiguredTemperature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalInitialSnapshotUsesConfiguredTemperatureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	Config.InitialTemperatureCelsius = 30.0f;

	const FEdenThermalStateSnapshot Snapshot = FEdenThermalModel::MakeInitialSnapshot(Config);

	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Initial temperature uses config"), Snapshot.TemperatureCelsius, 30.0f);
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Initial heat rate uses config"), Snapshot.HeatGenerationDegreesCelsiusPerSecond, Config.HeatGenerationDegreesCelsiusPerSecond);
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Initial dissipation uses config"), Snapshot.DissipationDegreesCelsiusPerSecond, Config.DissipationDegreesCelsiusPerSecond);
	TestEqual(TEXT("Initial state is normal"), Snapshot.ThermalState, EEdenThermalState::Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalHeatGenerationIncreasesTemperatureTest,
	"Eden.Unit.Systems.Thermal.HeatGenerationIncreasesTemperature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalHeatGenerationIncreasesTemperatureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	Config.HeatGenerationDegreesCelsiusPerSecond = 10.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.0f;

	const FEdenThermalStepResult Result = FEdenThermalModel::Step(Config, FEdenThermalModel::MakeInitialSnapshot(Config), 3.0f);

	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Heat generation increases temperature"), Result.Snapshot.TemperatureCelsius, 50.0f);
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Temperature delta is reported"), Result.TemperatureDeltaCelsius, 30.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalDissipationMovesTowardAmbientWithoutCrossingTest,
	"Eden.Unit.Systems.Thermal.DissipationMovesTowardAmbientWithoutCrossing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalDissipationMovesTowardAmbientWithoutCrossingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	Config.HeatGenerationDegreesCelsiusPerSecond = 0.0f;
	Config.DissipationDegreesCelsiusPerSecond = 100.0f;

	FEdenThermalStateSnapshot AboveAmbientSnapshot = FEdenThermalModel::MakeSnapshot(Config, 30.0f, 0.0f, 100.0f);
	AboveAmbientSnapshot = FEdenThermalModel::Step(Config, AboveAmbientSnapshot, 1.0f).Snapshot;
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Dissipation does not cross below ambient"), AboveAmbientSnapshot.TemperatureCelsius, 20.0f);

	FEdenThermalStateSnapshot BelowAmbientSnapshot = FEdenThermalModel::MakeSnapshot(Config, 10.0f, 0.0f, 100.0f);
	BelowAmbientSnapshot = FEdenThermalModel::Step(Config, BelowAmbientSnapshot, 1.0f).Snapshot;
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Dissipation does not cross above ambient"), BelowAmbientSnapshot.TemperatureCelsius, 20.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalClampsTemperatureTest,
	"Eden.Unit.Systems.Thermal.ClampsTemperature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalClampsTemperatureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	bool bTemperatureWasSanitized = false;

	EdenThermalSystemTests::TestFloatNearlyEqual(
		*this,
		TEXT("Temperature clamps to absolute max"),
		FEdenThermalModel::ClampTemperatureCelsius(200.0f, Config, &bTemperatureWasSanitized),
		120.0f);
	TestTrue(TEXT("High temperature reports clamping"), bTemperatureWasSanitized);

	EdenThermalSystemTests::TestFloatNearlyEqual(
		*this,
		TEXT("Temperature clamps to absolute min"),
		FEdenThermalModel::ClampTemperatureCelsius(-200.0f, Config, &bTemperatureWasSanitized),
		-50.0f);
	TestTrue(TEXT("Low temperature reports clamping"), bTemperatureWasSanitized);

	EdenThermalSystemTests::TestFloatNearlyEqual(
		*this,
		TEXT("NaN temperature sanitizes to ambient"),
		FEdenThermalModel::ClampTemperatureCelsius(std::numeric_limits<float>::quiet_NaN(), Config, &bTemperatureWasSanitized),
		20.0f);
	TestTrue(TEXT("NaN temperature reports sanitization"), bTemperatureWasSanitized);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalDerivesWarningCriticalAndOverheatedStatesTest,
	"Eden.Unit.Systems.Thermal.DerivesWarningCriticalAndOverheatedStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalDerivesWarningCriticalAndOverheatedStatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();

	TestEqual(TEXT("Normal below warning"), FEdenThermalModel::DeriveThermalState(Config, 69.0f), EEdenThermalState::Normal);
	TestEqual(TEXT("Warning at warning threshold"), FEdenThermalModel::DeriveThermalState(Config, 70.0f), EEdenThermalState::Warning);
	TestEqual(TEXT("Critical at critical threshold"), FEdenThermalModel::DeriveThermalState(Config, 100.0f), EEdenThermalState::Critical);
	TestEqual(TEXT("Overheated at absolute max"), FEdenThermalModel::DeriveThermalState(Config, 120.0f), EEdenThermalState::Overheated);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalInvalidDeltaTimeDoesNotMutateTest,
	"Eden.Unit.Systems.Thermal.InvalidDeltaTimeDoesNotMutate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalInvalidDeltaTimeDoesNotMutateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	const FEdenThermalStateSnapshot InitialSnapshot = FEdenThermalModel::MakeInitialSnapshot(Config);
	const float InvalidDeltaTimes[] = {0.0f, -0.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};

	for (const float DeltaTimeSeconds : InvalidDeltaTimes)
	{
		const FEdenThermalStepResult Result = FEdenThermalModel::Step(Config, InitialSnapshot, DeltaTimeSeconds);
		TestFalse(TEXT("Invalid DeltaTime is reported"), Result.bDeltaTimeWasValid);
		EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Invalid DeltaTime keeps temperature"), Result.Snapshot.TemperatureCelsius, InitialSnapshot.TemperatureCelsius);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalSanitizesNonFiniteRatesTest,
	"Eden.Unit.Systems.Thermal.SanitizesNonFiniteRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalSanitizesNonFiniteRatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bRateWasSanitized = false;
	TestEqual(
		TEXT("Infinity heat rate sanitizes to zero"),
		FEdenThermalModel::SanitizeNonnegativeDegreesCelsiusPerSecond(std::numeric_limits<float>::infinity(), &bRateWasSanitized),
		0.0f);
	TestTrue(TEXT("Infinity heat rate reports sanitization"), bRateWasSanitized);

	TestEqual(
		TEXT("Negative dissipation sanitizes to zero"),
		FEdenThermalModel::SanitizeNonnegativeDegreesCelsiusPerSecond(-5.0f, &bRateWasSanitized),
		0.0f);
	TestTrue(TEXT("Negative dissipation reports sanitization"), bRateWasSanitized);

	UEdenThermalSystemComponent* ThermalComponent =
		EdenThermalSystemTests::MakeInitializedComponent(EdenThermalSystemTests::MakeValidConfig());
	TestFalse(TEXT("Component reports sanitized heat"), ThermalComponent->SetHeatGenerationDegreesCelsiusPerSecond(std::numeric_limits<float>::quiet_NaN()));
	TestFalse(TEXT("Component reports sanitized dissipation"), ThermalComponent->SetDissipationDegreesCelsiusPerSecond(-1.0f));
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Heat generation stores safe zero"), ThermalComponent->GetThermalStateSnapshot().HeatGenerationDegreesCelsiusPerSecond, 0.0f);
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Dissipation stores safe zero"), ThermalComponent->GetThermalStateSnapshot().DissipationDegreesCelsiusPerSecond, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalMultiThresholdCrossingEmitsSingleTransitionTest,
	"Eden.Unit.Systems.Thermal.MultiThresholdCrossingEmitsSingleTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalMultiThresholdCrossingEmitsSingleTransitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	Config.HeatGenerationDegreesCelsiusPerSecond = 1000.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.0f;
	UEdenThermalSystemComponent* ThermalComponent = EdenThermalSystemTests::MakeInitializedComponent(Config);
	UEdenThermalSystemTestListener* Listener = EdenThermalSystemTests::BindListener(ThermalComponent);

	ThermalComponent->AdvanceSimulation(1.0f);

	TestEqual(TEXT("One final transition is emitted"), Listener->NewStates.Num(), 1);
	TestEqual(TEXT("Transition starts at normal"), Listener->PreviousStates[0], EEdenThermalState::Normal);
	TestEqual(TEXT("Transition goes directly to overheated"), Listener->NewStates[0], EEdenThermalState::Overheated);
	TestEqual(TEXT("Overheated event emits once"), Listener->OverheatedEventCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalOverheatedEventOnlyWhenEnteringOverheatedTest,
	"Eden.Unit.Systems.Thermal.OverheatedEventOnlyWhenEnteringOverheated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalOverheatedEventOnlyWhenEnteringOverheatedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	Config.HeatGenerationDegreesCelsiusPerSecond = 1000.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.0f;
	UEdenThermalSystemComponent* ThermalComponent = EdenThermalSystemTests::MakeInitializedComponent(Config);
	UEdenThermalSystemTestListener* Listener = EdenThermalSystemTests::BindListener(ThermalComponent);

	ThermalComponent->AdvanceSimulation(1.0f);
	ThermalComponent->AdvanceSimulation(1.0f);

	TestEqual(TEXT("Overheated event is emitted only on entry"), Listener->OverheatedEventCount, 1);
	TestEqual(TEXT("Only one state transition is emitted"), Listener->NewStates.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalRecoveryFromOverheatedTransitionsSafelyTest,
	"Eden.Unit.Systems.Thermal.RecoveryFromOverheatedTransitionsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalRecoveryFromOverheatedTransitionsSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenThermalSystemComponent* ThermalComponent =
		EdenThermalSystemTests::MakeInitializedComponent(EdenThermalSystemTests::MakeValidConfig());
	UEdenThermalSystemTestListener* Listener = EdenThermalSystemTests::BindListener(ThermalComponent);

	ThermalComponent->SetTemperatureCelsius(120.0f);
	ThermalComponent->SetTemperatureCelsius(20.0f);

	TestEqual(TEXT("Two real transitions are emitted"), Listener->NewStates.Num(), 2);
	TestEqual(TEXT("First transition enters overheated"), Listener->NewStates[0], EEdenThermalState::Overheated);
	TestEqual(TEXT("Second transition recovers to normal"), Listener->NewStates[1], EEdenThermalState::Normal);
	TestEqual(TEXT("Overheated event only emits on entering overheated"), Listener->OverheatedEventCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalResetRestoresConfiguredInitialTemperatureTest,
	"Eden.Unit.Systems.Thermal.ResetRestoresConfiguredInitialTemperature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalResetRestoresConfiguredInitialTemperatureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	Config.InitialTemperatureCelsius = 30.0f;
	UEdenThermalSystemComponent* ThermalComponent = EdenThermalSystemTests::MakeInitializedComponent(Config);

	ThermalComponent->SetTemperatureCelsius(100.0f);
	ThermalComponent->SetHeatGenerationDegreesCelsiusPerSecond(0.0f);
	ThermalComponent->SetDissipationDegreesCelsiusPerSecond(0.0f);
	TestTrue(TEXT("Thermal reset succeeds"), ThermalComponent->ResetThermalState());

	const FEdenThermalStateSnapshot Snapshot = ThermalComponent->GetThermalStateSnapshot();
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Reset restores configured temperature"), Snapshot.TemperatureCelsius, 30.0f);
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Reset restores configured heat"), Snapshot.HeatGenerationDegreesCelsiusPerSecond, Config.HeatGenerationDegreesCelsiusPerSecond);
	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Reset restores configured dissipation"), Snapshot.DissipationDegreesCelsiusPerSecond, Config.DissipationDegreesCelsiusPerSecond);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalEquivalentSimulatedTimeMatchesTest,
	"Eden.Unit.Systems.Thermal.EquivalentSimulatedTimeMatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalEquivalentSimulatedTimeMatchesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenThermalConfig Config = EdenThermalSystemTests::MakeValidConfig();
	Config.HeatGenerationDegreesCelsiusPerSecond = 3.0f;
	Config.DissipationDegreesCelsiusPerSecond = 0.0f;

	const FEdenThermalStateSnapshot InitialSnapshot = FEdenThermalModel::MakeInitialSnapshot(Config);
	const FEdenThermalStateSnapshot SingleStepSnapshot = FEdenThermalModel::Step(Config, InitialSnapshot, 10.0f).Snapshot;

	FEdenThermalStateSnapshot PartitionedSnapshot = InitialSnapshot;
	for (int32 StepIndex = 0; StepIndex < 4; ++StepIndex)
	{
		PartitionedSnapshot = FEdenThermalModel::Step(Config, PartitionedSnapshot, 2.5f).Snapshot;
	}

	EdenThermalSystemTests::TestFloatNearlyEqual(*this, TEXT("Equivalent partitions produce same temperature"), PartitionedSnapshot.TemperatureCelsius, SingleStepSnapshot.TemperatureCelsius);
	TestEqual(TEXT("Equivalent partitions produce same state"), PartitionedSnapshot.ThermalState, SingleStepSnapshot.ThermalState);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenThermalMissingAndInvalidConfigDisableSimulationSafelyTest,
	"Eden.Unit.Systems.Thermal.MissingAndInvalidConfigDisableSimulationSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenThermalMissingAndInvalidConfigDisableSimulationSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenThermalSystemComponent* MissingConfigComponent = NewObject<UEdenThermalSystemComponent>();
	TestFalse(TEXT("Reset without config fails safely"), MissingConfigComponent->ResetThermalState());
	TestFalse(TEXT("Thermal simulation remains disabled"), MissingConfigComponent->IsThermalSimulationEnabled());
	TestFalse(TEXT("Register without world/config is safe"), MissingConfigComponent->RegisterWithSimulationClock());

	FEdenThermalConfig InvalidConfig = EdenThermalSystemTests::MakeValidConfig();
	InvalidConfig.WarningTemperatureCelsius = InvalidConfig.CriticalTemperatureCelsius;
	UEdenThermalSystemComponent* InvalidConfigComponent = NewObject<UEdenThermalSystemComponent>();

	AddExpectedError(TEXT("invalid thermal configuration"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid explicit config is rejected"), InvalidConfigComponent->InitializeThermalSimulation(InvalidConfig));
	TestFalse(TEXT("Invalid explicit config disables simulation"), InvalidConfigComponent->IsThermalSimulationEnabled());
	TestEqual(TEXT("Disabled thermal state is overheated"), InvalidConfigComponent->GetThermalStateSnapshot().ThermalState, EEdenThermalState::Overheated);

	return true;
}

#endif
