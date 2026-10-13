// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightMovementModel.h"
#include "Tests/EdenFlightSimulationHarness.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenFlightDynamicsTests
{
constexpr double RelativeTolerance = 1.0e-9;

bool TestNearlyEqualRelative(FAutomationTestBase& Test, const FString& What, double Actual, double Expected, double Tolerance = RelativeTolerance)
{
	const double Scale = FMath::Max(1.0, FMath::Abs(Expected));
	const bool bMatches = FMath::Abs(Actual - Expected) <= Tolerance * Scale;
	Test.TestTrue(FString::Printf(TEXT("%s. Actual=%.12f Expected=%.12f"), *What, Actual, Expected), bMatches);
	return bMatches;
}

bool TestVectorNear(FAutomationTestBase& Test, const FString& What, const FVector& Actual, const FVector& Expected, double Tolerance)
{
	const bool bMatches = Actual.Equals(Expected, Tolerance);
	Test.TestTrue(FString::Printf(TEXT("%s. Actual=%s Expected=%s"), *What, *Actual.ToString(), *Expected.ToString()), bMatches);
	return bMatches;
}

FEdenFlightInputCommand MakeCommand(const FVector& Translation, bool bAssist, const FVector& Rotation = FVector::ZeroVector)
{
	FEdenFlightInputCommand Command;
	Command.TranslationInput = Translation;
	Command.RotationInput = Rotation;
	Command.bStabilizationEnabled = bAssist;
	return Command;
}

EdenFlightHarness::FRunConfig MakeConfig(double DurationSeconds, const TArray<EdenFlightHarness::FScriptedSegment>& Script)
{
	EdenFlightHarness::FRunConfig Config;
	Config.DurationSeconds = DurationSeconds;
	Config.Script = Script;
	return Config;
}

FEdenFlightStepOutput SingleStep(
	const FEdenFlightInputCommand& Command,
	const FVector& VelocityWorld = FVector::ZeroVector,
	const FQuat& Rotation = FQuat::Identity,
	double Authority = 1.0,
	bool bAssistAvailable = true)
{
	FEdenFlightStepInput Input;
	Input.Command = Command;
	Input.Translation.VelocityWorldMetersPerSecond = VelocityWorld;
	Input.BodyRotation = Rotation;
	Input.MassKilograms = FEdenFlightPropulsionSettings().DryMassKilograms;
	Input.ThrustAuthority = Authority;
	Input.bAssistAvailable = bAssistAvailable;
	return FEdenFlightMovementModel::StepFlight(
		Input,
		FEdenFlightPropulsionSettings(),
		FEdenFlightMovementSettings(),
		FEdenFlightStepSettings().GetFixedStepSeconds());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsIdealizedConstantMassThrustFromRestTest,
	"Eden.Unit.Flight.Dynamics.IdealizedConstantMassThrustFromRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsIdealizedConstantMassThrustFromRestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// IDEALIZED: constant 10 000 kg mass, no propellant use, no other forces. Variable mass is tested in Checkpoint D.
	const EdenFlightHarness::FRunConfig Config = EdenFlightDynamicsTests::MakeConfig(
		60.0,
		{{0.0, EdenFlightDynamicsTests::MakeCommand(FVector(1.0, 0.0, 0.0), false)}});
	const EdenFlightHarness::FRunResult Result = EdenFlightHarness::Run(Config);

	const double Acceleration = Config.Propulsion.ForwardThrustNewtons / Config.Propulsion.DryMassKilograms;
	TestEqual(TEXT("Exactly 60 s of 120 Hz steps"), Result.Steps, static_cast<int64>(7200));
	EdenFlightDynamicsTests::TestNearlyEqualRelative(*this, TEXT("a = F / m = 10 m/s^2"), Acceleration, 10.0);
	EdenFlightDynamicsTests::TestNearlyEqualRelative(
		*this,
		TEXT("v = a t = 600 m/s"),
		Result.Final.Translation.VelocityWorldMetersPerSecond.X,
		600.0);
	EdenFlightDynamicsTests::TestNearlyEqualRelative(
		*this,
		TEXT("x = a t^2 / 2 = 18 000 m"),
		Result.Final.Translation.PositionWorldMeters.X,
		18000.0);
	EdenFlightDynamicsTests::TestVectorNear(
		*this,
		TEXT("No off-axis motion"),
		FVector(0.0, Result.Final.Translation.VelocityWorldMetersPerSecond.Y, Result.Final.Translation.VelocityWorldMetersPerSecond.Z),
		FVector::ZeroVector,
		1.0e-12);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsMomentumConservedAssistOffTest,
	"Eden.Unit.Flight.Dynamics.MomentumConservedAssistOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsMomentumConservedAssistOffTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenFlightHarness::FRunConfig Burn = EdenFlightDynamicsTests::MakeConfig(
		10.0,
		{{0.0, EdenFlightDynamicsTests::MakeCommand(FVector(1.0, 0.0, 0.0), false)}});
	const EdenFlightHarness::FRunResult AfterBurn = EdenFlightHarness::Run(Burn);
	EdenFlightDynamicsTests::TestNearlyEqualRelative(
		*this,
		TEXT("10 s burn reaches 100 m/s"),
		AfterBurn.Final.Translation.VelocityWorldMetersPerSecond.X,
		100.0);

	EdenFlightHarness::FRunConfig Coast = EdenFlightDynamicsTests::MakeConfig(
		50.0,
		{{0.0, EdenFlightDynamicsTests::MakeCommand(FVector::ZeroVector, false)}});
	Coast.Initial = AfterBurn.Final;
	const EdenFlightHarness::FRunResult AfterCoast = EdenFlightHarness::Run(Coast);

	TestTrue(
		TEXT("Velocity is bit-identical after 50 s of coasting (no drag, no cap)"),
		AfterCoast.Final.Translation.VelocityWorldMetersPerSecond == AfterBurn.Final.Translation.VelocityWorldMetersPerSecond);
	EdenFlightDynamicsTests::TestNearlyEqualRelative(
		*this,
		TEXT("Coasting distance is v t"),
		AfterCoast.Final.Translation.PositionWorldMeters.X - AfterBurn.Final.Translation.PositionWorldMeters.X,
		100.0 * 50.0);

	// Far beyond the old 12 m/s kinematic cap and the 100 m/s assist target: assist off has no speed limit.
	EdenFlightHarness::FRunConfig LongBurn = EdenFlightDynamicsTests::MakeConfig(
		120.0,
		{{0.0, EdenFlightDynamicsTests::MakeCommand(FVector(1.0, 0.0, 0.0), false)}});
	EdenFlightDynamicsTests::TestNearlyEqualRelative(
		*this,
		TEXT("Assist off has no speed cap (120 s burn -> 1200 m/s)"),
		EdenFlightHarness::Run(LongBurn).Final.Translation.VelocityWorldMetersPerSecond.X,
		1200.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsPerDirectionThrustRatingsTest,
	"Eden.Unit.Flight.Dynamics.PerDirectionThrustRatings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsPerDirectionThrustRatingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	struct FCase
	{
		const TCHAR* Name;
		FVector Input;
		FVector ExpectedAcceleration;
		double Authority;
	};

	const FCase Cases[] = {
		{TEXT("Forward 100 kN"), FVector(1.0, 0.0, 0.0), FVector(10.0, 0.0, 0.0), 1.0},
		{TEXT("Reverse 50 kN"), FVector(-1.0, 0.0, 0.0), FVector(-5.0, 0.0, 0.0), 1.0},
		{TEXT("Right 20 kN"), FVector(0.0, 1.0, 0.0), FVector(0.0, 2.0, 0.0), 1.0},
		{TEXT("Left 20 kN"), FVector(0.0, -1.0, 0.0), FVector(0.0, -2.0, 0.0), 1.0},
		{TEXT("Up 20 kN"), FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, 2.0), 1.0},
		{TEXT("Down 20 kN"), FVector(0.0, 0.0, -1.0), FVector(0.0, 0.0, -2.0), 1.0},
		{TEXT("Half authority halves forward thrust"), FVector(1.0, 0.0, 0.0), FVector(5.0, 0.0, 0.0), 0.5},
		{TEXT("Zero authority produces no thrust"), FVector(1.0, 0.0, 0.0), FVector::ZeroVector, 0.0},
		{TEXT("Half stick is half thrust"), FVector(0.5, 0.0, 0.0), FVector(5.0, 0.0, 0.0), 1.0},
	};

	for (const FCase& Case : Cases)
	{
		const FEdenFlightStepOutput Output =
			EdenFlightDynamicsTests::SingleStep(EdenFlightDynamicsTests::MakeCommand(Case.Input, false), FVector::ZeroVector, FQuat::Identity, Case.Authority);
		EdenFlightDynamicsTests::TestVectorNear(
			*this,
			Case.Name,
			Output.Translation.AccelerationWorldMetersPerSecondSquared,
			Case.ExpectedAcceleration,
			1.0e-9);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsThrustFollowsOrientationTest,
	"Eden.Unit.Flight.Dynamics.ThrustFollowsOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsThrustFollowsOrientationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FQuat Yaw90 = FRotator(0.0, 90.0, 0.0).Quaternion();
	const FEdenFlightStepOutput Output = EdenFlightDynamicsTests::SingleStep(
		EdenFlightDynamicsTests::MakeCommand(FVector(1.0, 0.0, 0.0), false),
		FVector::ZeroVector,
		Yaw90);
	EdenFlightDynamicsTests::TestVectorNear(
		*this,
		TEXT("Forward thrust after a 90 deg yaw accelerates along world +Y"),
		Output.Translation.AccelerationWorldMetersPerSecondSquared,
		FVector(0.0, 10.0, 0.0),
		1.0e-9);

	const FQuat Pitch90 = FRotator(90.0, 0.0, 0.0).Quaternion();
	const FEdenFlightStepOutput PitchedOutput = EdenFlightDynamicsTests::SingleStep(
		EdenFlightDynamicsTests::MakeCommand(FVector(1.0, 0.0, 0.0), false),
		FVector::ZeroVector,
		Pitch90);
	EdenFlightDynamicsTests::TestVectorNear(
		*this,
		TEXT("Forward thrust after a 90 deg pitch-up accelerates along world +Z"),
		PitchedOutput.Translation.AccelerationWorldMetersPerSecondSquared,
		FVector(0.0, 0.0, 10.0),
		1.0e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsAssistTracksCommandedVelocityTest,
	"Eden.Unit.Flight.Dynamics.AssistTracksCommandedVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsAssistTracksCommandedVelocityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const EdenFlightHarness::FRunConfig Config = EdenFlightDynamicsTests::MakeConfig(
		30.0,
		{{0.0, EdenFlightDynamicsTests::MakeCommand(FVector(1.0, 0.0, 0.0), true)}});
	const EdenFlightHarness::FRunResult Result = EdenFlightHarness::Run(Config);

	EdenFlightDynamicsTests::TestNearlyEqualRelative(
		*this,
		TEXT("Assist settles at full-stick target velocity (100 m/s)"),
		Result.Final.Translation.VelocityWorldMetersPerSecond.X,
		Config.Propulsion.AssistMaxSpeedMetersPerSecond,
		1.0e-6);
	TestTrue(TEXT("Assist never exceeds the thrust envelope"), Result.MaxEnvelopeExcessNewtons <= 1.0e-6);

	const double MaxDeltaV = (Config.Propulsion.ForwardThrustNewtons / Config.Propulsion.DryMassKilograms) * Config.Step.GetFixedStepSeconds();
	TestTrue(
		FString::Printf(
			TEXT("Assist changes velocity only through thrust: max |dv| per step %.9f <= F/m dt %.9f"),
			Result.MaxStepDeltaVelocityMetersPerSecond,
			MaxDeltaV),
		Result.MaxStepDeltaVelocityMetersPerSecond <= MaxDeltaV + 1.0e-12);

	// Monotone approach: run step by step and confirm the target is never overshot.
	FEdenFlightStepInput Input;
	Input.Command = EdenFlightDynamicsTests::MakeCommand(FVector(1.0, 0.0, 0.0), true);
	Input.MassKilograms = Config.Propulsion.DryMassKilograms;
	bool bOvershot = false;
	for (int32 Step = 0; Step < 3600; ++Step)
	{
		const FEdenFlightStepOutput Output =
			FEdenFlightMovementModel::StepFlight(Input, Config.Propulsion, Config.Rotation, Config.Step.GetFixedStepSeconds());
		bOvershot |= Output.Translation.State.VelocityWorldMetersPerSecond.X > Config.Propulsion.AssistMaxSpeedMetersPerSecond + 1.0e-9;
		Input.Translation = Output.Translation.State;
	}
	TestFalse(TEXT("Assist approach does not overshoot the commanded velocity"), bOvershot);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsAssistBrakingIsThrustLimitedTest,
	"Eden.Unit.Flight.Dynamics.AssistBrakingIsThrustLimited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsAssistBrakingIsThrustLimitedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFlightPropulsionSettings Propulsion;
	const FEdenFlightMovementSettings Rotation;
	const double FixedStep = FEdenFlightStepSettings().GetFixedStepSeconds();
	const double ReverseAcceleration = Propulsion.ReverseThrustNewtons / Propulsion.DryMassKilograms;
	// Saturation ends when the proportional request m * v / tau falls to the reverse rating.
	const double SaturationExitVelocity = ReverseAcceleration * Propulsion.AssistResponseTimeSeconds;
	const double ExpectedSaturationExitSeconds = (300.0 - SaturationExitVelocity) / ReverseAcceleration;

	FEdenFlightStepInput Input;
	Input.Command = EdenFlightDynamicsTests::MakeCommand(FVector::ZeroVector, true);
	Input.Translation.VelocityWorldMetersPerSecond = FVector(300.0, 0.0, 0.0);
	Input.MassKilograms = Propulsion.DryMassKilograms;

	double SaturationExitSeconds = -1.0;
	bool bExceededReverseThrust = false;
	bool bReversedDirection = false;
	bool bDemandBilled = false;
	for (int32 Step = 0; Step < 120 * 70; ++Step)
	{
		const FEdenFlightStepOutput Output = FEdenFlightMovementModel::StepFlight(Input, Propulsion, Rotation, FixedStep);
		const double AppliedForce = Output.AppliedBodyForceNewtons.X;
		bExceededReverseThrust |= AppliedForce < -Propulsion.ReverseThrustNewtons - 1.0e-6;
		bReversedDirection |= Output.Translation.State.VelocityWorldMetersPerSecond.X < -1.0e-9;
		bDemandBilled |= Output.PropulsionDemandNormalized > 0.0f;
		if (SaturationExitSeconds < 0.0 && AppliedForce > -Propulsion.ReverseThrustNewtons + 1.0e-6)
		{
			SaturationExitSeconds = Step * FixedStep;
		}
		Input.Translation = Output.Translation.State;
	}

	TestFalse(TEXT("Assist braking never exceeds reverse thrust (5 m/s^2)"), bExceededReverseThrust);
	TestFalse(TEXT("Assist braking does not overshoot into reverse motion"), bReversedDirection);
	TestTrue(TEXT("Assist braking bills propulsion demand"), bDemandBilled);
	TestTrue(
		FString::Printf(
			TEXT("Full-thrust braking lasts the analytic time. Actual=%.4f s Expected=%.4f s"),
			SaturationExitSeconds,
			ExpectedSaturationExitSeconds),
		FMath::Abs(SaturationExitSeconds - ExpectedSaturationExitSeconds) <= FixedStep + 1.0e-9);
	TestTrue(TEXT("Ship is effectively stopped after 70 s"), FMath::Abs(Input.Translation.VelocityWorldMetersPerSecond.X) < 1.0e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsAssistAvailabilityAndIdleCostTest,
	"Eden.Unit.Flight.Dynamics.AssistAvailabilityAndIdleCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsAssistAvailabilityAndIdleCostTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFlightStepOutput Idle = EdenFlightDynamicsTests::SingleStep(EdenFlightDynamicsTests::MakeCommand(FVector::ZeroVector, true));
	TestTrue(TEXT("Assist at rest with no input is active"), Idle.bAssistActive);
	TestEqual(TEXT("Assist at rest with no input uses no thrust"), Idle.PropulsionDemandNormalized, 0.0f);

	const FVector Drift(12.0, -3.0, 1.5);
	const FEdenFlightStepOutput Unavailable = EdenFlightDynamicsTests::SingleStep(
		EdenFlightDynamicsTests::MakeCommand(FVector::ZeroVector, true),
		Drift,
		FQuat::Identity,
		1.0,
		false);
	TestFalse(TEXT("Requested assist is inactive when unavailable (load shed)"), Unavailable.bAssistActive);
	TestTrue(
		TEXT("Unavailable assist leaves the ship drifting with unchanged velocity"),
		Unavailable.Translation.State.VelocityWorldMetersPerSecond == Drift);
	TestEqual(TEXT("Unavailable assist bills no thrust"), Unavailable.PropulsionDemandNormalized, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsPropulsionDemandMatchesAppliedThrustTest,
	"Eden.Unit.Flight.Dynamics.PropulsionDemandMatchesAppliedThrust",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsPropulsionDemandMatchesAppliedThrustTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using EdenFlightDynamicsTests::MakeCommand;
	using EdenFlightDynamicsTests::SingleStep;

	TestEqual(TEXT("Full forward burn reads 1.0, as before 0008"), SingleStep(MakeCommand(FVector(1.0, 0.0, 0.0), false)).PropulsionDemandNormalized, 1.0f);
	TestEqual(TEXT("Reduced authority halves demand"), SingleStep(MakeCommand(FVector(1.0, 0.0, 0.0), false), FVector::ZeroVector, FQuat::Identity, 0.5).PropulsionDemandNormalized, 0.5f);
	TestEqual(TEXT("Full lateral RCS reads its share of reference thrust"), SingleStep(MakeCommand(FVector(0.0, 1.0, 0.0), false)).PropulsionDemandNormalized, 0.2f);
	TestEqual(TEXT("No thrust reads 0"), SingleStep(MakeCommand(FVector::ZeroVector, false)).PropulsionDemandNormalized, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsFixedStepAccumulatorTest,
	"Eden.Unit.Flight.Dynamics.FixedStepAccumulator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsFixedStepAccumulatorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const double Step = 1.0 / 120.0;
	double Accumulator = 0.0;
	int32 Dropped = 0;

	TestEqual(TEXT("A 60 Hz frame runs two 120 Hz steps"), FEdenFlightMovementModel::CalculateFixedSteps(1.0 / 60.0, Step, 60, Accumulator, Dropped), 2);
	TestEqual(TEXT("No drops at 60 Hz"), Dropped, 0);

	Accumulator = 0.0;
	TestEqual(TEXT("A 240 Hz frame runs no step yet"), FEdenFlightMovementModel::CalculateFixedSteps(1.0 / 240.0, Step, 60, Accumulator, Dropped), 0);
	TestEqual(TEXT("The second 240 Hz frame runs one step"), FEdenFlightMovementModel::CalculateFixedSteps(1.0 / 240.0, Step, 60, Accumulator, Dropped), 1);

	Accumulator = 0.0;
	const int32 Steps = FEdenFlightMovementModel::CalculateFixedSteps(1.0, Step, 60, Accumulator, Dropped);
	TestEqual(TEXT("A 1 s hitch runs at most MaxSteps"), Steps, 60);
	TestEqual(TEXT("The remaining 60 steps are reported as dropped, not hidden"), Dropped, 60);
	TestTrue(TEXT("Dropped time does not linger in the accumulator"), Accumulator < Step);

	Accumulator = 0.0;
	TestEqual(TEXT("Invalid frame delta runs nothing"), FEdenFlightMovementModel::CalculateFixedSteps(std::numeric_limits<double>::quiet_NaN(), Step, 60, Accumulator, Dropped), 0);
	TestEqual(TEXT("Negative frame delta runs nothing"), FEdenFlightMovementModel::CalculateFixedSteps(-0.1, Step, 60, Accumulator, Dropped), 0);

	// Flight time tracks rendered time: no step is lost or invented, whatever the frame cadence.
	for (const EdenFlightHarness::FFrameSource& Source : EdenFlightHarness::StandardFrameSources())
	{
		double ElapsedFrameSeconds = 0.0;
		int64 DroppedSteps = 0;
		const int64 StepsTaken =
			EdenFlightHarness::CountStepsForFrameTime(Source, FEdenFlightStepSettings(), 60.0, ElapsedFrameSeconds, DroppedSteps);
		const double ExpectedSteps = ElapsedFrameSeconds / Step;
		TestTrue(
			FString::Printf(
				TEXT("%s: %lld steps for %.6f s of frames (expected %.3f, within 1)"),
				*Source.Name,
				StepsTaken,
				ElapsedFrameSeconds,
				ExpectedSteps),
			FMath::Abs(static_cast<double>(StepsTaken) - ExpectedSteps) <= 1.0);
		TestEqual(FString::Printf(TEXT("%s: no dropped steps"), *Source.Name), DroppedSteps, static_cast<int64>(0));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsFrameRateIndependence60sTest,
	"Eden.Unit.Flight.Dynamics.FrameRateIndependence60s",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsFrameRateIndependence60sTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using EdenFlightDynamicsTests::MakeCommand;

	// 60 s profile: burn, rotate while coasting, coast, assisted manoeuvre, assisted stop.
	const TArray<EdenFlightHarness::FScriptedSegment> Profile = {
		{0.0, MakeCommand(FVector(1.0, 0.0, 0.0), false)},
		{10.0, MakeCommand(FVector::ZeroVector, false, FVector(0.0, 1.0, 0.0))},
		{12.5, MakeCommand(FVector::ZeroVector, false, FVector(0.5, 0.0, -1.0))},
		{15.0, MakeCommand(FVector::ZeroVector, false)},
		{25.0, MakeCommand(FVector(0.6, 0.4, -0.2), true)},
		{35.0, MakeCommand(FVector::ZeroVector, true)},
	};

	TOptional<EdenFlightHarness::FRunResult> Reference;
	FString ReferenceName;
	for (const EdenFlightHarness::FFrameSource& Source : EdenFlightHarness::StandardFrameSources())
	{
		EdenFlightHarness::FRunConfig Config = EdenFlightDynamicsTests::MakeConfig(60.0, Profile);
		Config.Frames = Source;
		const EdenFlightHarness::FRunResult Result = EdenFlightHarness::Run(Config);

		TestEqual(FString::Printf(TEXT("%s: 7200 steps"), *Source.Name), Result.Steps, static_cast<int64>(7200));
		TestEqual(FString::Printf(TEXT("%s: no dropped steps"), *Source.Name), Result.DroppedSteps, static_cast<int64>(0));
		TestTrue(FString::Printf(TEXT("%s: thrust stays inside the envelope"), *Source.Name), Result.MaxEnvelopeExcessNewtons <= 1.0e-6);

		if (!Reference.IsSet())
		{
			Reference = Result;
			ReferenceName = Source.Name;
			continue;
		}

		const EdenFlightHarness::FBodyState& A = Reference->Final;
		const EdenFlightHarness::FBodyState& B = Result.Final;
		const FString Pair = FString::Printf(TEXT("%s vs %s"), *Source.Name, *ReferenceName);
		TestTrue(FString::Printf(TEXT("%s: position bit-identical"), *Pair), A.Translation.PositionWorldMeters == B.Translation.PositionWorldMeters);
		TestTrue(FString::Printf(TEXT("%s: velocity bit-identical"), *Pair), A.Translation.VelocityWorldMetersPerSecond == B.Translation.VelocityWorldMetersPerSecond);
		TestTrue(FString::Printf(TEXT("%s: orientation bit-identical"), *Pair), A.Rotation == B.Rotation);
		TestTrue(
			FString::Printf(TEXT("%s: angular velocity bit-identical"), *Pair),
			A.AngularVelocityLocalDegreesPerSecond == B.AngularVelocityLocalDegreesPerSecond);
	}

	if (Reference.IsSet())
	{
		AddInfo(FString::Printf(
			TEXT("60 s profile final: position %s m, velocity %s m/s, rotation %s"),
			*Reference->Final.Translation.PositionWorldMeters.ToString(),
			*Reference->Final.Translation.VelocityWorldMetersPerSecond.ToString(),
			*Reference->Final.Rotation.Rotator().ToString()));
		// The assisted stop is thrust-limited: after the rotation segment most momentum lies on the
		// 20 kN RCS axes (2 m/s^2), so 25 s slows the ship but cannot stop it. Assist must not cheat.
		EdenFlightHarness::FRunConfig UntilStop = EdenFlightDynamicsTests::MakeConfig(35.0, Profile);
		const double SpeedAtStopCommand = EdenFlightHarness::Run(UntilStop).Final.Translation.VelocityWorldMetersPerSecond.Size();
		const double FinalSpeed = Reference->Final.Translation.VelocityWorldMetersPerSecond.Size();
		const FEdenFlightPropulsionSettings Propulsion;
		const double MaxDeceleration = Propulsion.ForwardThrustNewtons / Propulsion.DryMassKilograms;
		TestTrue(
			FString::Printf(TEXT("Assisted stop slows the ship (%.3f -> %.3f m/s)"), SpeedAtStopCommand, FinalSpeed),
			FinalSpeed < SpeedAtStopCommand);
		TestTrue(
			TEXT("Assisted stop sheds no more speed than the strongest thruster allows in 25 s"),
			SpeedAtStopCommand - FinalSpeed <= MaxDeceleration * 25.0 + 1.0e-9);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsFrameSampledInputBoundedTest,
	"Eden.Unit.Flight.Dynamics.FrameSampledInputBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsFrameSampledInputBoundedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using EdenFlightDynamicsTests::MakeCommand;

	// Like the player controller, input is sampled once per frame, so a burn can end up to one frame late.
	const TArray<EdenFlightHarness::FScriptedSegment> Profile = {
		{0.0, MakeCommand(FVector(1.0, 0.0, 0.0), false)},
		{10.0, MakeCommand(FVector::ZeroVector, false)},
	};
	const double Acceleration = 10.0;

	for (const EdenFlightHarness::FFrameSource& Source : EdenFlightHarness::StandardFrameSources())
	{
		EdenFlightHarness::FRunConfig Config = EdenFlightDynamicsTests::MakeConfig(20.0, Profile);
		Config.Frames = Source;
		Config.Timing = EdenFlightHarness::EInputTiming::FrameSampled;
		const EdenFlightHarness::FRunResult Result = EdenFlightHarness::Run(Config);

		const double FinalSpeed = Result.Final.Translation.VelocityWorldMetersPerSecond.X;
		const double Bound = Acceleration * (Result.MaxFrameDeltaSeconds + Config.Step.GetFixedStepSeconds());
		TestTrue(
			FString::Printf(
				TEXT("%s: frame-sampled burn within one frame of the ideal 100 m/s. Actual=%.6f Bound=%.6f"),
				*Source.Name,
				FinalSpeed,
				Bound),
			FMath::Abs(FinalSpeed - 100.0) <= Bound);
		const double Steps = FinalSpeed / (Acceleration * Config.Step.GetFixedStepSeconds());
		TestTrue(
			FString::Printf(TEXT("%s: velocity is a whole number of thrust steps (%.9f)"), *Source.Name, Steps),
			FMath::Abs(Steps - FMath::RoundToDouble(Steps)) <= 1.0e-6);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsInvalidInputsDoNotMutateStateTest,
	"Eden.Unit.Flight.Dynamics.InvalidInputsDoNotMutateState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsInvalidInputsDoNotMutateStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightTranslationState State;
	State.PositionWorldMeters = FVector(1.0, 2.0, 3.0);
	State.VelocityWorldMetersPerSecond = FVector(4.0, 5.0, 6.0);

	const double InvalidDeltas[] = {0.0, -0.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
	for (const double DeltaTime : InvalidDeltas)
	{
		const FEdenFlightTranslationStepResult Result =
			FEdenFlightMovementModel::IntegrateTranslation(State, FVector(1000.0, 0.0, 0.0), 10000.0, FQuat::Identity, DeltaTime);
		TestFalse(TEXT("Invalid dt is reported"), Result.bStepWasValid);
		TestTrue(TEXT("Invalid dt leaves velocity unchanged"), Result.State.VelocityWorldMetersPerSecond == State.VelocityWorldMetersPerSecond);
		TestTrue(TEXT("Invalid dt leaves position unchanged"), Result.State.PositionWorldMeters == State.PositionWorldMeters);
	}

	const double InvalidMasses[] = {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()};
	for (const double Mass : InvalidMasses)
	{
		const FEdenFlightTranslationStepResult Result =
			FEdenFlightMovementModel::IntegrateTranslation(State, FVector(1000.0, 0.0, 0.0), Mass, FQuat::Identity, 0.01);
		TestFalse(TEXT("Invalid mass is reported"), Result.bStepWasValid);
		TestTrue(TEXT("Invalid mass leaves velocity unchanged"), Result.State.VelocityWorldMetersPerSecond == State.VelocityWorldMetersPerSecond);
	}

	const FEdenFlightTranslationStepResult NonFiniteForce = FEdenFlightMovementModel::IntegrateTranslation(
		State,
		FVector(std::numeric_limits<double>::infinity(), 0.0, 0.0),
		10000.0,
		FQuat::Identity,
		0.01);
	TestFalse(TEXT("Non-finite force is reported"), NonFiniteForce.bStepWasValid);

	const FVector ClampedNonFinite = FEdenFlightMovementModel::ClampToEnvelope(
		FVector(std::numeric_limits<double>::quiet_NaN(), 1.0e9, -1.0e9),
		FEdenFlightMovementModel::MakeThrustEnvelope(FEdenFlightPropulsionSettings(), 1.0));
	EdenFlightDynamicsTests::TestVectorNear(
		*this,
		TEXT("Envelope clamps NaN to zero and saturates out-of-range requests"),
		ClampedNonFinite,
		FVector(0.0, 20000.0, -20000.0),
		1.0e-9);

	FEdenFlightPropulsionSettings Invalid;
	Invalid.DryMassKilograms = 0.0f;
	TestFalse(TEXT("Zero dry mass is invalid"), FEdenFlightMovementModel::ValidatePropulsionSettings(Invalid, 1.0 / 120.0));
	FEdenFlightPropulsionSettings TooFast;
	TooFast.AssistResponseTimeSeconds = 0.01f;
	TestFalse(TEXT("Assist faster than four flight steps is invalid"), FEdenFlightMovementModel::ValidatePropulsionSettings(TooFast, 1.0 / 120.0));
	TestTrue(TEXT("C++ default propulsion settings are valid"), FEdenFlightMovementModel::ValidatePropulsionSettings(FEdenFlightPropulsionSettings(), 1.0 / 120.0));
	TestTrue(TEXT("C++ default step settings are valid"), FEdenFlightMovementModel::ValidateStepSettings(FEdenFlightStepSettings()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightDynamicsOperatorAuthorityContractUnchangedTest,
	"Eden.Unit.Flight.Dynamics.OperatorAuthorityContractUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightDynamicsOperatorAuthorityContractUnchangedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenFlightMovementComponent* Movement = NewObject<UEdenFlightMovementComponent>();
	TestNotNull(TEXT("Movement component constructs without a map"), Movement);
	if (!Movement)
	{
		return false;
	}

	TestTrue(TEXT("Operator sets reduced authority"), Movement->SetThrustAuthority(0.5f));
	TestEqual(TEXT("GetThrustAuthority still reports the operator value (telemetry contract)"), Movement->GetThrustAuthority(), 0.5f);
	TestEqual(TEXT("Effective authority equals operator authority until resource limits exist"), Movement->GetEffectiveThrustAuthority(), 0.5f);

	const FEdenFlightDynamicsSnapshot Snapshot = Movement->GetFlightDynamicsSnapshot();
	EdenFlightDynamicsTests::TestVectorNear(
		*this,
		TEXT("Available thrust reflects the operator limit"),
		Snapshot.AvailablePositiveThrustNewtons,
		FVector(50000.0, 10000.0, 10000.0),
		1.0e-6);
	EdenFlightDynamicsTests::TestVectorNear(
		*this,
		TEXT("Available reverse thrust reflects the operator limit"),
		Snapshot.AvailableNegativeThrustNewtons,
		FVector(25000.0, 10000.0, 10000.0),
		1.0e-6);
	TestEqual(TEXT("Snapshot mass is the dynamics mass"), Snapshot.MassKilograms, FEdenFlightPropulsionSettings().DryMassKilograms);

	return true;
}

#endif
