// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementModel.h"
#include "Tests/EdenFlightSimulationHarness.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenFlightPropellantTests
{
constexpr double FixedStep = 1.0 / 120.0;
constexpr double G0 = FEdenFlightMovementModel::StandardGravityMetersPerSecondSquared;

bool TestRelative(FAutomationTestBase& Test, const FString& What, double Actual, double Expected, double Tolerance)
{
	const double Scale = FMath::Max(FMath::Abs(Expected), 1.0e-300);
	const bool bMatches = FMath::Abs(Actual - Expected) <= Tolerance * Scale;
	Test.TestTrue(FString::Printf(TEXT("%s. Actual=%.15g Expected=%.15g"), *What, Actual, Expected), bMatches);
	return bMatches;
}

FEdenFlightInputCommand MakeCommand(const FVector& Translation, const FVector& PitchYawRoll, bool bAssist)
{
	FEdenFlightInputCommand Command;
	Command.TranslationInput = Translation;
	Command.RotationInput = PitchYawRoll;
	Command.bStabilizationEnabled = bAssist;
	return Command;
}

FEdenFlightStepInput MakeMeteredInput(const FEdenFlightInputCommand& Command, double PropellantKilograms, double PowerFraction = 1.0)
{
	FEdenFlightStepInput Input;
	Input.Command = Command;
	Input.DryMassKilograms = FEdenFlightPropulsionSettings().DryMassKilograms;
	Input.bPropellantMetered = true;
	Input.PropellantMassKilograms = PropellantKilograms;
	Input.PowerThrustFraction = PowerFraction;
	return Input;
}

FEdenFlightStepOutput Step(const FEdenFlightStepInput& Input)
{
	return FEdenFlightMovementModel::StepFlight(Input, FEdenFlightPropulsionSettings(), FEdenFlightAttitudeSettings(), FixedStep);
}

double IspG0()
{
	return static_cast<double>(FEdenFlightPropulsionSettings().SpecificImpulseSeconds) * G0;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantPowerLimitedDeliveryUsesDeliveredThrustTest,
	"Eden.Unit.Flight.Propellant.PowerLimitedDeliveryUsesDeliveredThrust",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantPowerLimitedDeliveryUsesDeliveredThrustTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;

	// The ship asks for 100 kN; limited power lets it deliver 40 kN. Physics and propellant use 40 kN.
	const FEdenFlightStepOutput Output = Step(MakeMeteredInput(MakeCommand(FVector(1.0, 0.0, 0.0), FVector::ZeroVector, false), 100.0, 0.4));

	const double Delivered = 40000.0;
	const double Consumed = Delivered * FixedStep / IspG0();
	const double MassDuringBurn = 10100.0 - 0.5 * Consumed;
	TestTrue(TEXT("Requested force is the full 100 kN"), Output.RequestedBodyForceNewtons.Equals(FVector(100000.0, 0.0, 0.0), 0.0));
	TestTrue(TEXT("Delivered force is the power-limited 40 kN"), Output.AppliedBodyForceNewtons.Equals(FVector(Delivered, 0.0, 0.0), 1.0e-9));
	TestRelative(*this, TEXT("Requested thruster output 100 kN"), Output.RequestedThrusterOutputNewtons, 100000.0, 1.0e-15);
	TestRelative(*this, TEXT("Delivered thruster output 40 kN"), Output.DeliveredThrusterOutputNewtons, Delivered, 1.0e-15);
	TestRelative(*this, TEXT("Propellant drawn from 40 kN, not 100 kN: F dt / (Isp g0)"), Output.PropellantConsumedKilograms, Consumed, 1.0e-12);
	TestRelative(
		*this,
		TEXT("Acceleration from 40 kN over current mass (dry + propellant, midpoint)"),
		Output.Translation.AccelerationWorldMetersPerSecondSquared.X,
		Delivered / MassDuringBurn,
		1.0e-12);
	TestEqual(TEXT("Demand reports the delivered fraction"), Output.PropulsionDemandNormalized, 0.4f);
	TestTrue(TEXT("Step is flagged power-limited"), Output.bPowerLimited);
	TestFalse(TEXT("Step is not propellant-limited"), Output.bPropellantLimited);

	const FEdenFlightStepOutput NoPower = Step(MakeMeteredInput(MakeCommand(FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false), 100.0, 0.0));
	TestTrue(TEXT("Zero power delivers no force"), NoPower.AppliedBodyForceNewtons.IsZero());
	TestTrue(TEXT("Zero power delivers no torque"), NoPower.AppliedBodyTorqueNewtonMeters.IsZero());
	TestEqual(TEXT("Zero power burns no propellant"), NoPower.PropellantConsumedKilograms, 0.0);
	TestTrue(TEXT("Zero power still records what was requested"), NoPower.RequestedThrusterOutputNewtons > 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantMassFlowMatchesSpecificImpulseTest,
	"Eden.Unit.Flight.Propellant.MassFlowMatchesSpecificImpulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantMassFlowMatchesSpecificImpulseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;
	const FEdenFlightPropulsionSettings Defaults;
	TestRelative(
		*this,
		TEXT("Default Isp keeps the pre-0008 balance: 1 kg/s at 100 kN"),
		FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(100000.0, Defaults),
		1.0,
		1.0e-6);

	FEdenFlightPropulsionSettings Chemical;
	Chemical.SpecificImpulseSeconds = 300.0f;
	TestRelative(
		*this,
		TEXT("Isp 300 s: mdot = F / (Isp g0)"),
		FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(100000.0, Chemical),
		100000.0 / (300.0 * G0),
		1.0e-15);
	TestEqual(TEXT("No thrust, no flow"), FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(0.0, Defaults), 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantTsiolkovskyRocketEquationTest,
	"Eden.Unit.Flight.Propellant.TsiolkovskyRocketEquation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantTsiolkovskyRocketEquationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;

	// Variable mass: 50 s full burn from 10 100 kg (100 kg propellant) burns ~50 kg.
	EdenFlightHarness::FRunConfig Config;
	Config.DurationSeconds = 50.0;
	Config.Script = {{0.0, MakeCommand(FVector(1.0, 0.0, 0.0), FVector::ZeroVector, false)}};
	Config.bPropellantMetered = true;
	Config.Initial.PropellantKilograms = 100.0;
	const EdenFlightHarness::FRunResult Result = EdenFlightHarness::Run(Config);

	const double InitialMass = Config.Propulsion.DryMassKilograms + 100.0;
	const double FinalMass = Config.Propulsion.DryMassKilograms + Result.Final.PropellantKilograms;
	TestRelative(*this, TEXT("Propellant burned at mdot = F / (Isp g0)"), Result.PropellantConsumedKilograms, 50.0 * 100000.0 / IspG0(), 1.0e-9);
	TestRelative(*this, TEXT("Tank ledger matches propellant consumed"), 100.0 - Result.Final.PropellantKilograms, Result.PropellantConsumedKilograms, 1.0e-12);
	TestRelative(
		*this,
		TEXT("Delta-v follows Tsiolkovsky: Isp g0 ln(m0 / m1)"),
		Result.Final.Translation.VelocityWorldMetersPerSecond.X,
		IspG0() * FMath::Loge(InitialMass / FinalMass),
		1.0e-9);
	TestTrue(
		TEXT("Lighter ship gains more speed than a constant-mass ship would"),
		Result.Final.Translation.VelocityWorldMetersPerSecond.X > 100000.0 * 50.0 / InitialMass);
	TestEqual(TEXT("No step was propellant-limited"), Result.PropellantLimitedSteps, static_cast<int64>(0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantMidStepDepletionPartialBurnTest,
	"Eden.Unit.Flight.Propellant.MidStepDepletionPartialBurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantMidStepDepletionPartialBurnTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;
	const double Required = 100000.0 * FixedStep / IspG0();
	const double Stored = 0.3 * Required;

	FEdenFlightStepInput Input = MakeMeteredInput(MakeCommand(FVector(1.0, 0.0, 0.0), FVector::ZeroVector, false), Stored);
	Input.Translation.VelocityWorldMetersPerSecond = FVector(20.0, 0.0, 0.0);
	const FEdenFlightStepOutput Output = Step(Input);

	const double BurnSeconds = 0.3 * FixedStep;
	const double Mass = 10000.0 + Stored;
	const double Acceleration = 100000.0 / (Mass - 0.5 * Stored);
	const double BurnVelocity = 20.0 + Acceleration * BurnSeconds;
	const double Displacement = 20.0 * BurnSeconds + 0.5 * Acceleration * BurnSeconds * BurnSeconds + BurnVelocity * (FixedStep - BurnSeconds);

	TestRelative(*this, TEXT("Burn fraction is the propellant fraction"), Output.PropellantBurnFraction, 0.3, 1.0e-12);
	TestEqual(TEXT("Exactly the stored propellant is consumed"), Output.PropellantConsumedKilograms, Stored);
	TestRelative(*this, TEXT("Velocity gains only the burned part of the step"), Output.Translation.State.VelocityWorldMetersPerSecond.X, BurnVelocity, 1.0e-13);
	TestRelative(*this, TEXT("Position is burn then coast"), Output.Translation.DisplacementWorldMeters.X, Displacement, 1.0e-12);
	TestRelative(*this, TEXT("Delivered output is 30% of 100 kN"), Output.DeliveredThrusterOutputNewtons, 30000.0, 1.0e-12);
	TestTrue(TEXT("Step is flagged propellant-limited"), Output.bPropellantLimited);
	TestRelative(*this, TEXT("Mass at end is dry mass"), Output.MassAtEndKilograms, 10000.0, 1.0e-15);

	// Next step: the tank is empty, so the ship coasts; no negative propellant, no free acceleration.
	FEdenFlightStepInput Empty = Input;
	Empty.Translation = Output.Translation.State;
	Empty.PropellantMassKilograms = Stored - Output.PropellantConsumedKilograms;
	TestEqual(TEXT("Tank is exactly empty, never negative"), Empty.PropellantMassKilograms, 0.0);
	const FEdenFlightStepOutput Coast = Step(Empty);
	TestTrue(TEXT("Empty tank: velocity unchanged bit for bit"), Coast.Translation.State.VelocityWorldMetersPerSecond == Empty.Translation.VelocityWorldMetersPerSecond);
	TestEqual(TEXT("Empty tank: nothing consumed"), Coast.PropellantConsumedKilograms, 0.0);
	TestEqual(TEXT("Empty tank: zero demand"), Coast.PropulsionDemandNormalized, 0.0f);
	TestTrue(TEXT("Empty tank: still flagged propellant-limited"), Coast.bPropellantLimited);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantEmptyTankNoThrustOrTorqueTest,
	"Eden.Unit.Flight.Propellant.EmptyTankNoThrustOrTorque",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantEmptyTankNoThrustOrTorqueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;

	// Assist asks for everything; with no propellant, nothing can be delivered and momentum is conserved.
	FEdenFlightStepInput Input = MakeMeteredInput(MakeCommand(FVector(1.0, 0.5, 0.0), FVector(0.3, 1.0, -0.5), true), 0.0);
	Input.Translation.VelocityWorldMetersPerSecond = FVector(15.0, -4.0, 2.0);
	Input.Rotation.AngularVelocityBodyRadiansPerSecond = FVector(0.0, 0.0, 0.4);
	const FEdenFlightStepOutput Output = Step(Input);

	TestTrue(TEXT("Controllers still request thrust"), Output.RequestedThrusterOutputNewtons > 0.0);
	TestTrue(TEXT("No force is delivered"), Output.AppliedBodyForceNewtons.IsZero());
	TestTrue(TEXT("No torque is delivered"), Output.AppliedBodyTorqueNewtonMeters.IsZero());
	TestTrue(TEXT("Linear momentum conserved bit for bit"), Output.Translation.State.VelocityWorldMetersPerSecond == Input.Translation.VelocityWorldMetersPerSecond);
	TestTrue(
		TEXT("Principal-axis spin conserved bit for bit"),
		Output.Rotation.State.AngularVelocityBodyRadiansPerSecond == Input.Rotation.AngularVelocityBodyRadiansPerSecond);
	TestEqual(TEXT("No propellant consumed"), Output.PropellantConsumedKilograms, 0.0);
	TestEqual(TEXT("Zero demand"), Output.PropulsionDemandNormalized, 0.0f);
	TestTrue(TEXT("Flagged propellant-limited"), Output.bPropellantLimited);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantRcsTorqueConsumesPropellantTest,
	"Eden.Unit.Flight.Propellant.RcsTorqueConsumesPropellant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantRcsTorqueConsumesPropellantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;
	const FEdenFlightAttitudeSettings Attitude;

	const FEdenFlightStepOutput Output = Step(MakeMeteredInput(MakeCommand(FVector::ZeroVector, FVector(0.0, 1.0, 0.0), false), 100.0));
	const double EquivalentThrust = Attitude.YawTorqueNewtonMeters / Attitude.ThrusterLeverArmsMeters.Z;
	const double Consumed = EquivalentThrust * FixedStep / IspG0();
	TestRelative(*this, TEXT("Yaw RCS draws propellant at (tau / r) / (Isp g0)"), Output.PropellantConsumedKilograms, Consumed, 1.0e-12);

	const double InertiaScale = (10100.0 - 0.5 * Consumed) / 10000.0;
	TestRelative(
		*this,
		TEXT("Angular acceleration uses inertia at current mass"),
		Output.Rotation.State.AngularVelocityBodyRadiansPerSecond.Z,
		Attitude.YawTorqueNewtonMeters / (Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared.Z * InertiaScale) * FixedStep,
		1.0e-12);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantZeroInputConservesMomentumWhenMeteredTest,
	"Eden.Unit.Flight.Propellant.ZeroInputConservesMomentumWhenMetered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantZeroInputConservesMomentumWhenMeteredTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;

	FEdenFlightStepInput Input = MakeMeteredInput(MakeCommand(FVector::ZeroVector, FVector::ZeroVector, false), 50.0);
	Input.Translation.VelocityWorldMetersPerSecond = FVector(30.0, 10.0, -5.0);
	Input.Rotation.AngularVelocityBodyRadiansPerSecond = FVector(0.0, 0.25, 0.0);
	FEdenFlightStepInput State = Input;
	double Consumed = 0.0;
	for (int32 StepIndex = 0; StepIndex < 1200; ++StepIndex)
	{
		const FEdenFlightStepOutput Output = Step(State);
		Consumed += Output.PropellantConsumedKilograms;
		State.Translation = Output.Translation.State;
		State.Rotation = Output.Rotation.State;
	}

	TestEqual(TEXT("Coasting burns no propellant"), Consumed, 0.0);
	TestTrue(TEXT("Linear momentum conserved bit for bit over 10 s"), State.Translation.VelocityWorldMetersPerSecond == Input.Translation.VelocityWorldMetersPerSecond);
	TestTrue(
		TEXT("Angular velocity conserved bit for bit over 10 s"),
		State.Rotation.AngularVelocityBodyRadiansPerSecond == Input.Rotation.AngularVelocityBodyRadiansPerSecond);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantUnmeteredMatchesCheckpointCTest,
	"Eden.Unit.Flight.Propellant.UnmeteredMatchesCheckpointC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantUnmeteredMatchesCheckpointCTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;
	const FEdenFlightPropulsionSettings Propulsion;
	const FEdenFlightAttitudeSettings Attitude;

	// Without a propellant supply the step must reduce exactly to the Checkpoint C dynamics.
	FEdenFlightStepInput Input;
	Input.Command = MakeCommand(FVector(0.7, -0.3, 0.2), FVector(0.4, -0.6, 0.5), false);
	Input.DryMassKilograms = Propulsion.DryMassKilograms;
	Input.Translation.VelocityWorldMetersPerSecond = FVector(3.0, 1.0, -2.0);
	Input.Rotation.Orientation = FRotator(10.0, 20.0, 30.0).Quaternion();
	Input.Rotation.AngularVelocityBodyRadiansPerSecond = FVector(0.1, -0.2, 0.3);
	const FEdenFlightStepOutput Output = FEdenFlightMovementModel::StepFlight(Input, Propulsion, Attitude, FixedStep);

	const FEdenFlightTranslationStepResult Translation = FEdenFlightMovementModel::IntegrateTranslation(
		Input.Translation,
		Output.AppliedBodyForceNewtons,
		Propulsion.DryMassKilograms,
		Input.Rotation.Orientation.GetNormalized(),
		FixedStep);
	const FEdenFlightRotationStepResult Rotation = FEdenFlightMovementModel::IntegrateRotation(
		Input.Rotation,
		Output.AppliedBodyTorqueNewtonMeters,
		Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared,
		FixedStep);

	TestTrue(TEXT("Unmetered: delivered equals requested"), Output.AppliedBodyForceNewtons == Output.RequestedBodyForceNewtons);
	TestTrue(TEXT("Unmetered translation is pure F = m a at dry mass"), Output.Translation.State.VelocityWorldMetersPerSecond == Translation.State.VelocityWorldMetersPerSecond);
	TestTrue(TEXT("Unmetered rotation is pure rigid-body dynamics at dry inertia"), Output.Rotation.State.Orientation == Rotation.State.Orientation);
	TestEqual(TEXT("Unmetered consumes nothing"), Output.PropellantConsumedKilograms, 0.0);
	TestFalse(TEXT("Unmetered is never propellant-limited"), Output.bPropellantLimited);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantFrameRateIndependentWithDepletionTest,
	"Eden.Unit.Flight.Propellant.FrameRateIndependentWithDepletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantFrameRateIndependentWithDepletionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightPropellantTests;

	// 12 kg at ~1.2 kg/s (forward burn + yaw RCS) runs dry around t = 10 s; the rest of the minute is free flight.
	const TArray<EdenFlightHarness::FScriptedSegment> Profile = {
		{0.0, MakeCommand(FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false)},
		{40.0, MakeCommand(FVector::ZeroVector, FVector::ZeroVector, true)},
	};

	TOptional<EdenFlightHarness::FRunResult> Reference;
	FString ReferenceName;
	for (const EdenFlightHarness::FFrameSource& Source : EdenFlightHarness::StandardFrameSources())
	{
		EdenFlightHarness::FRunConfig Config;
		Config.DurationSeconds = 60.0;
		Config.Script = Profile;
		Config.Frames = Source;
		Config.bPropellantMetered = true;
		Config.Initial.PropellantKilograms = 12.0;
		const EdenFlightHarness::FRunResult Result = EdenFlightHarness::Run(Config);

		TestEqual(FString::Printf(TEXT("%s: tank ends exactly empty"), *Source.Name), Result.Final.PropellantKilograms, 0.0);
		TestTrue(FString::Printf(TEXT("%s: propellant never negative"), *Source.Name), Result.MinPropellantKilograms >= 0.0);
		TestRelative(*this, FString::Printf(TEXT("%s: consumed the whole tank (sum of ~1200 draws)"), *Source.Name), Result.PropellantConsumedKilograms, 12.0, 1.0e-10);

		if (!Reference.IsSet())
		{
			Reference = Result;
			ReferenceName = Source.Name;
			continue;
		}

		const FString Pair = FString::Printf(TEXT("%s vs %s"), *Source.Name, *ReferenceName);
		TestTrue(
			FString::Printf(TEXT("%s: velocity bit-identical"), *Pair),
			Reference->Final.Translation.VelocityWorldMetersPerSecond == Result.Final.Translation.VelocityWorldMetersPerSecond);
		TestTrue(
			FString::Printf(TEXT("%s: position bit-identical"), *Pair),
			Reference->Final.Translation.PositionWorldMeters == Result.Final.Translation.PositionWorldMeters);
		TestTrue(
			FString::Printf(TEXT("%s: orientation bit-identical"), *Pair),
			Reference->Final.Rotation.Orientation == Result.Final.Rotation.Orientation);
		TestTrue(
			FString::Printf(TEXT("%s: angular velocity bit-identical"), *Pair),
			Reference->Final.Rotation.AngularVelocityBodyRadiansPerSecond == Result.Final.Rotation.AngularVelocityBodyRadiansPerSecond);
		TestEqual(FString::Printf(TEXT("%s: same propellant-limited step count"), *Pair), Result.PropellantLimitedSteps, Reference->PropellantLimitedSteps);
	}

	if (Reference.IsSet())
	{
		AddInfo(FString::Printf(
			TEXT("Depletion profile final: velocity %s m/s, |w| %.6f rad/s, propellant-limited steps %lld"),
			*Reference->Final.Translation.VelocityWorldMetersPerSecond.ToString(),
			Reference->Final.Rotation.AngularVelocityBodyRadiansPerSecond.Size(),
			Reference->PropellantLimitedSteps));
		TestTrue(
			TEXT("With the tank dry, assist cannot stop the spin it could not pay for"),
			Reference->Final.Rotation.AngularVelocityBodyRadiansPerSecond.Size() > 0.1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightPropellantPowerFractionsAndSettingsTest,
	"Eden.Unit.Flight.Propellant.PowerFractionsAndSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightPropellantPowerFractionsAndSettingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFlightPropulsionSettings Settings;
	TestEqual(TEXT("Normal power: full thrust"), FEdenFlightMovementModel::CalculatePowerThrustFraction(EEdenPowerState::Normal, Settings), 1.0);
	TestEqual(TEXT("Warning power: full thrust"), FEdenFlightMovementModel::CalculatePowerThrustFraction(EEdenPowerState::Warning, Settings), 1.0);
	TestEqual(
		TEXT("Critical power: CriticalPowerThrustFraction"),
		FEdenFlightMovementModel::CalculatePowerThrustFraction(EEdenPowerState::Critical, Settings),
		static_cast<double>(Settings.CriticalPowerThrustFraction));
	TestEqual(
		TEXT("Depleted power: DepletedPowerThrustFraction"),
		FEdenFlightMovementModel::CalculatePowerThrustFraction(EEdenPowerState::Depleted, Settings),
		static_cast<double>(Settings.DepletedPowerThrustFraction));

	FEdenFlightPropulsionSettings BadIsp;
	BadIsp.SpecificImpulseSeconds = 0.0f;
	TestFalse(TEXT("Zero Isp is rejected"), FEdenFlightMovementModel::ValidatePropulsionSettings(BadIsp, 1.0 / 120.0));
	FEdenFlightPropulsionSettings BadFraction;
	BadFraction.CriticalPowerThrustFraction = 1.5f;
	TestFalse(TEXT("Power fraction above 1 is rejected"), FEdenFlightMovementModel::ValidatePropulsionSettings(BadFraction, 1.0 / 120.0));
	TestEqual(TEXT("Inertia scale is exactly 1 at dry mass"), FEdenFlightMovementModel::CalculateInertiaScale(10000.0, 10000.0), 1.0);

	return true;
}

#endif
