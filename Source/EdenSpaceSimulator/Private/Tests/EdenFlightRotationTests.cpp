// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementModel.h"
#include "Tests/EdenFlightSimulationHarness.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenFlightRotationTests
{
constexpr double FixedStep = 1.0 / 120.0;

bool TestNearlyEqualRelative(FAutomationTestBase& Test, const FString& What, double Actual, double Expected, double Tolerance)
{
	const double Scale = FMath::Max(1.0, FMath::Abs(Expected));
	const bool bMatches = FMath::Abs(Actual - Expected) <= Tolerance * Scale;
	Test.TestTrue(FString::Printf(TEXT("%s. Actual=%.12g Expected=%.12g"), *What, Actual, Expected), bMatches);
	return bMatches;
}

bool TestVectorNear(FAutomationTestBase& Test, const FString& What, const FVector& Actual, const FVector& Expected, double Tolerance)
{
	const bool bMatches = Actual.Equals(Expected, Tolerance);
	Test.TestTrue(FString::Printf(TEXT("%s. Actual=%s Expected=%s"), *What, *Actual.ToString(), *Expected.ToString()), bMatches);
	return bMatches;
}

FEdenFlightInputCommand MakeCommand(const FVector& PitchYawRoll, bool bAssist, const FVector& Translation = FVector::ZeroVector)
{
	FEdenFlightInputCommand Command;
	Command.RotationInput = PitchYawRoll;
	Command.TranslationInput = Translation;
	Command.bStabilizationEnabled = bAssist;
	return Command;
}

/** Steps the full flight model N times with a fixed command; returns the final input state and worst-case envelope excess. */
struct FSteppedRun
{
	FEdenFlightStepInput State;
	double MaxTorqueExcess = 0.0;
	double MaxAppliedTorqueNewtonMeters = 0.0;
	bool bAllStepsValid = true;
	bool bControllerOnlyActedThroughTorque = true;
};

FSteppedRun StepN(FEdenFlightStepInput State, int32 Steps, const FEdenFlightAttitudeSettings& Attitude = FEdenFlightAttitudeSettings())
{
	FSteppedRun Run;
	const FEdenFlightPropulsionSettings Propulsion;
	if (State.MassKilograms <= 0.0)
	{
		State.MassKilograms = Propulsion.DryMassKilograms;
	}

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const FEdenFlightStepOutput Output = FEdenFlightMovementModel::StepFlight(State, Propulsion, Attitude, FixedStep);
		Run.bAllStepsValid &= Output.bStepWasValid;
		Run.MaxAppliedTorqueNewtonMeters = FMath::Max(Run.MaxAppliedTorqueNewtonMeters, Output.AppliedBodyTorqueNewtonMeters.GetAbsMax());
		Run.MaxTorqueExcess = FMath::Max(
			Run.MaxTorqueExcess,
			EdenFlightHarness::TorqueEnvelopeExcessNewtonMeters(Output.AppliedBodyTorqueNewtonMeters, Output.TorqueEnvelope));

		// The controller must never write angular velocity: the result must equal pure dynamics under the applied torque.
		const FEdenFlightRotationStepResult PureDynamics = FEdenFlightMovementModel::IntegrateRotation(
			State.Rotation,
			Output.AppliedBodyTorqueNewtonMeters,
			Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared,
			FixedStep);
		Run.bControllerOnlyActedThroughTorque &=
			PureDynamics.State.AngularVelocityBodyRadiansPerSecond == Output.Rotation.State.AngularVelocityBodyRadiansPerSecond
			&& PureDynamics.State.Orientation == Output.Rotation.State.Orientation;

		State.Translation = Output.Translation.State;
		State.Rotation = Output.Rotation.State;
	}

	Run.State = State;
	return Run;
}

/** Rotation angle between two orientations, accurate near zero (atan2 instead of acos). */
double AngleBetween(const FQuat& A, const FQuat& B)
{
	const FQuat Error = A.GetNormalized().Inverse() * B.GetNormalized();
	const double VectorLength = FVector(Error.X, Error.Y, Error.Z).Size();
	return 2.0 * FMath::Atan2(VectorLength, FMath::Abs(Error.W));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationConstantTorqueAngularAccelerationTest,
	"Eden.Unit.Flight.Rotation.ConstantTorqueAngularAcceleration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationConstantTorqueAngularAccelerationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;
	const FEdenFlightAttitudeSettings Attitude;
	const FVector& I = Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared;
	const FVector MaxTorque(Attitude.RollTorqueNewtonMeters, Attitude.PitchTorqueNewtonMeters, Attitude.YawTorqueNewtonMeters);
	const TCHAR* AxisNames[] = {TEXT("roll (X)"), TEXT("pitch (Y)"), TEXT("yaw (Z)")};

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		FVector BodyTorque = FVector::ZeroVector;
		BodyTorque[Axis] = MaxTorque[Axis];

		// One principal axis from rest: no gyroscopic coupling, so w = (tau / I) t and theta = (tau / I) t^2 / 2.
		FEdenFlightRotationState State;
		for (int32 Step = 0; Step < 240; ++Step)
		{
			State = FEdenFlightMovementModel::IntegrateRotation(State, BodyTorque, I, FixedStep).State;
		}

		const double Alpha = MaxTorque[Axis] / I[Axis];
		TestNearlyEqualRelative(
			*this,
			FString::Printf(TEXT("%s: w = (tau / I) t after 2 s"), AxisNames[Axis]),
			State.AngularVelocityBodyRadiansPerSecond[Axis],
			Alpha * 2.0,
			1.0e-9);
		TestNearlyEqualRelative(
			*this,
			FString::Printf(TEXT("%s: theta = alpha t^2 / 2 after 2 s"), AxisNames[Axis]),
			State.Orientation.GetAngle(),
			0.5 * Alpha * 4.0,
			1.0e-8);

		FVector Axis3 = FVector::ZeroVector;
		Axis3[Axis] = 1.0;
		TestVectorNear(*this, FString::Printf(TEXT("%s: rotates about its own axis"), AxisNames[Axis]), State.Orientation.GetRotationAxis(), Axis3, 1.0e-9);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationInputSignsMatchUnrealConventionTest,
	"Eden.Unit.Flight.Rotation.InputSignsMatchUnrealConvention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationInputSignsMatchUnrealConventionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;

	// Positive stick must turn the ship the same way the kinematic shell did (FRotator +pitch/+yaw/+roll).
	struct FCase
	{
		const TCHAR* Name;
		FVector Input;
		int32 RotatorComponent;
	};
	const FCase Cases[] = {
		{TEXT("Positive pitch input raises the nose"), FVector(1.0, 0.0, 0.0), 0},
		{TEXT("Positive yaw input turns the nose right"), FVector(0.0, 1.0, 0.0), 1},
		{TEXT("Positive roll input rolls right"), FVector(0.0, 0.0, 1.0), 2},
	};

	for (const FCase& Case : Cases)
	{
		FEdenFlightStepInput Input;
		Input.Command = MakeCommand(Case.Input, false);
		const FSteppedRun Run = StepN(Input, 60);
		const FRotator Rotator = Run.State.Rotation.Orientation.Rotator();
		const double Values[] = {Rotator.Pitch, Rotator.Yaw, Rotator.Roll};
		TestTrue(FString::Printf(TEXT("%s (rotator %s)"), Case.Name, *Rotator.ToString()), Values[Case.RotatorComponent] > 0.0);
		for (int32 Other = 0; Other < 3; ++Other)
		{
			if (Other != Case.RotatorComponent)
			{
				TestTrue(FString::Printf(TEXT("%s: other rotator axes untouched"), Case.Name), FMath::Abs(Values[Other]) < 1.0e-9);
			}
		}
	}

	const FVector Mapped = FEdenFlightMovementModel::MapRotationInputToBodyAxes(FVector(0.25, 0.5, 0.75));
	TestVectorNear(*this, TEXT("Input (pitch, yaw, roll) maps to body (X roll, Y pitch, Z yaw)"), Mapped, FVector(-0.75, -0.25, 0.5), 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationTorqueFreeSpinIsStableTest,
	"Eden.Unit.Flight.Rotation.TorqueFreeSpinIsStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationTorqueFreeSpinIsStableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;
	const FVector I = FEdenFlightAttitudeSettings().PrincipalMomentsOfInertiaKilogramMetersSquared;

	FEdenFlightRotationState State;
	State.AngularVelocityBodyRadiansPerSecond = FVector(0.0, 0.0, 0.5);
	for (int32 Step = 0; Step < 7200; ++Step)
	{
		State = FEdenFlightMovementModel::IntegrateRotation(State, FVector::ZeroVector, I, FixedStep).State;
	}

	TestTrue(
		TEXT("Principal-axis spin keeps exactly the same angular velocity for 60 s"),
		State.AngularVelocityBodyRadiansPerSecond == FVector(0.0, 0.0, 0.5));
	const FQuat Expected(FVector(0.0, 0.0, 1.0), 0.5 * 60.0);
	TestTrue(
		FString::Printf(TEXT("Spin angle is w t = 30 rad (error %.3g rad)"), AngleBetween(State.Orientation, Expected)),
		AngleBetween(State.Orientation, Expected) < 1.0e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationAngularMomentumConservedAsymmetricTest,
	"Eden.Unit.Flight.Rotation.AngularMomentumConservedAsymmetric",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationAngularMomentumConservedAsymmetricTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;
	const FVector I = FEdenFlightAttitudeSettings().PrincipalMomentsOfInertiaKilogramMetersSquared;
	TestTrue(TEXT("Default inertia is fully asymmetric (Ixx < Iyy < Izz)"), I.X < I.Y && I.Y < I.Z);

	// Spin mostly about the intermediate (pitch) axis: unstable, so the body tumbles (Dzhanibekov effect).
	FEdenFlightRotationState State;
	State.AngularVelocityBodyRadiansPerSecond = FVector(0.02, 0.8, 0.02);
	const FVector InitialMomentum = FEdenFlightMovementModel::CalculateAngularMomentumWorld(State, I);
	const double InitialEnergy = FEdenFlightMovementModel::CalculateRotationalKineticEnergy(State, I);

	double MaxMomentumDrift = 0.0;
	double MaxEnergyDrift = 0.0;
	double MaxNormError = 0.0;
	bool bPitchRateReversed = false;
	for (int32 Step = 0; Step < 7200; ++Step)
	{
		State = FEdenFlightMovementModel::IntegrateRotation(State, FVector::ZeroVector, I, FixedStep).State;
		const FVector Momentum = FEdenFlightMovementModel::CalculateAngularMomentumWorld(State, I);
		MaxMomentumDrift = FMath::Max(MaxMomentumDrift, (Momentum - InitialMomentum).Size() / InitialMomentum.Size());
		MaxEnergyDrift = FMath::Max(
			MaxEnergyDrift,
			FMath::Abs(FEdenFlightMovementModel::CalculateRotationalKineticEnergy(State, I) - InitialEnergy) / InitialEnergy);
		MaxNormError = FMath::Max(MaxNormError, FMath::Abs(State.Orientation.Size() - 1.0));
		bPitchRateReversed |= State.AngularVelocityBodyRadiansPerSecond.Y < 0.0;
	}

	TestTrue(TEXT("Gyroscopic coupling is active: the intermediate-axis spin flips (tennis-racket effect)"), bPitchRateReversed);
	TestTrue(FString::Printf(TEXT("World angular momentum conserved over 60 s (max relative drift %.3g)"), MaxMomentumDrift), MaxMomentumDrift < 1.0e-6);
	TestTrue(FString::Printf(TEXT("Rotational energy conserved over 60 s (max relative drift %.3g)"), MaxEnergyDrift), MaxEnergyDrift < 1.0e-6);
	TestTrue(FString::Printf(TEXT("Quaternion stays unit length (max error %.3g)"), MaxNormError), MaxNormError < 1.0e-12);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationAssistOffConservesSpinAfterReleaseTest,
	"Eden.Unit.Flight.Rotation.AssistOffConservesSpinAfterRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationAssistOffConservesSpinAfterReleaseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;
	const FVector I = FEdenFlightAttitudeSettings().PrincipalMomentsOfInertiaKilogramMetersSquared;

	FEdenFlightStepInput Input;
	Input.Command = MakeCommand(FVector(0.6, 1.0, -0.4).GetSafeNormal(), false);
	const FSteppedRun Spun = StepN(Input, 240);
	TestTrue(TEXT("Torque input built up a spin"), !Spun.State.Rotation.AngularVelocityBodyRadiansPerSecond.IsNearlyZero());

	FEdenFlightStepInput Released = Spun.State;
	Released.Command = MakeCommand(FVector::ZeroVector, false);
	const FVector MomentumAtRelease = FEdenFlightMovementModel::CalculateAngularMomentumWorld(Released.Rotation, I);
	const FSteppedRun Coasted = StepN(Released, 3600);
	const FVector MomentumAfter = FEdenFlightMovementModel::CalculateAngularMomentumWorld(Coasted.State.Rotation, I);

	TestEqual(TEXT("Released controls with assist off apply no torque"), Coasted.MaxAppliedTorqueNewtonMeters, 0.0);
	TestTrue(TEXT("The ship keeps rotating after release"), Coasted.State.Rotation.AngularVelocityBodyRadiansPerSecond.Size() > 0.1);
	TestTrue(
		FString::Printf(
			TEXT("World angular momentum is conserved after release (drift %.3g)"),
			(MomentumAfter - MomentumAtRelease).Size() / MomentumAtRelease.Size()),
		(MomentumAfter - MomentumAtRelease).Size() / MomentumAtRelease.Size() < 1.0e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationAssistStabilizesThroughTorqueTest,
	"Eden.Unit.Flight.Rotation.AssistStabilizesThroughTorque",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationAssistStabilizesThroughTorqueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;
	const FEdenFlightAttitudeSettings Attitude;
	const FVector& I = Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared;

	FEdenFlightStepInput Input;
	Input.Command = MakeCommand(FVector::ZeroVector, true);
	Input.Rotation.AngularVelocityBodyRadiansPerSecond = FVector(0.6, -0.5, 0.4);
	const FSteppedRun Run = StepN(Input, 120 * 30, Attitude);

	TestTrue(TEXT("Stabilization never exceeds the torque envelope"), Run.MaxTorqueExcess <= 1.0e-6);
	TestTrue(TEXT("Stabilization changes angular velocity only through applied torque"), Run.bControllerOnlyActedThroughTorque);
	TestTrue(
		FString::Printf(TEXT("Assist brings the ship to rest (|w| = %.3g rad/s)"), Run.State.Rotation.AngularVelocityBodyRadiansPerSecond.Size()),
		Run.State.Rotation.AngularVelocityBodyRadiansPerSecond.Size() < 1.0e-6);

	// Lower bound from physics: the fastest possible stop on the roll axis is w / alpha_max.
	FEdenFlightStepInput Short = Input;
	const FSteppedRun AfterHalfSecond = StepN(Short, 60, Attitude);
	const double RollAlpha = Attitude.RollTorqueNewtonMeters / I.X;
	TestTrue(
		TEXT("Assist cannot stop a 0.6 rad/s roll faster than the roll torque allows"),
		AfterHalfSecond.State.Rotation.AngularVelocityBodyRadiansPerSecond.X > 0.6 - RollAlpha * 0.5 - 0.05);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationAssistTracksCommandedRateTest,
	"Eden.Unit.Flight.Rotation.AssistTracksCommandedRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationAssistTracksCommandedRateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;
	const FEdenFlightAttitudeSettings Attitude;
	const double TargetRate = FMath::DegreesToRadians(static_cast<double>(Attitude.AssistMaxAngularRateDegreesPerSecond));

	FEdenFlightStepInput Input;
	Input.Command = MakeCommand(FVector(0.0, 1.0, 0.0), true);
	bool bOvershot = false;
	for (int32 Second = 0; Second < 10; ++Second)
	{
		const FSteppedRun Run = StepN(Input, 120, Attitude);
		TestTrue(TEXT("Rate assist stays inside the torque envelope"), Run.MaxTorqueExcess <= 1.0e-6);
		TestTrue(TEXT("Rate assist acts only through torque"), Run.bControllerOnlyActedThroughTorque);
		bOvershot |= Run.State.Rotation.AngularVelocityBodyRadiansPerSecond.Z > TargetRate + 1.0e-9;
		Input = Run.State;
	}

	TestFalse(TEXT("Rate assist does not overshoot the commanded yaw rate"), bOvershot);
	TestNearlyEqualRelative(
		*this,
		TEXT("Full yaw stick holds the assist yaw rate (60 deg/s)"),
		Input.Rotation.AngularVelocityBodyRadiansPerSecond.Z,
		TargetRate,
		1.0e-6);
	TestVectorNear(
		*this,
		TEXT("Other axes stay at rest"),
		FVector(Input.Rotation.AngularVelocityBodyRadiansPerSecond.X, Input.Rotation.AngularVelocityBodyRadiansPerSecond.Y, 0.0),
		FVector::ZeroVector,
		1.0e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationAuthoritySaturatesTorqueTest,
	"Eden.Unit.Flight.Rotation.AuthoritySaturatesTorque",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationAuthoritySaturatesTorqueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;
	const FEdenFlightAttitudeSettings Attitude;
	const FEdenFlightPropulsionSettings Propulsion;

	FEdenFlightStepInput Input;
	Input.MassKilograms = Propulsion.DryMassKilograms;
	Input.Command = MakeCommand(FVector(1.0, 0.0, 0.0), false);

	Input.ThrustAuthority = 1.0;
	const FVector FullTorque = FEdenFlightMovementModel::StepFlight(Input, Propulsion, Attitude, FixedStep).AppliedBodyTorqueNewtonMeters;
	Input.ThrustAuthority = 0.5;
	const FVector HalfTorque = FEdenFlightMovementModel::StepFlight(Input, Propulsion, Attitude, FixedStep).AppliedBodyTorqueNewtonMeters;
	TestVectorNear(*this, TEXT("Full pitch stick applies the pitch rating"), FullTorque, FVector(0.0, -Attitude.PitchTorqueNewtonMeters, 0.0), 1.0e-6);
	TestVectorNear(*this, TEXT("Operator authority 0.5 halves control torque"), HalfTorque, FullTorque * 0.5, 1.0e-6);

	// Assist asks for far more than the hardware can give; the request must saturate at the rating.
	Input.ThrustAuthority = 0.5;
	Input.Command = MakeCommand(FVector::ZeroVector, true);
	Input.Rotation.AngularVelocityBodyRadiansPerSecond = FVector(3.0, -3.0, 3.0);
	const FEdenFlightStepOutput Saturated = FEdenFlightMovementModel::StepFlight(Input, Propulsion, Attitude, FixedStep);
	TestTrue(
		TEXT("Assist torque saturates at the authority-limited envelope"),
		FMath::IsNearlyEqual(FMath::Abs(Saturated.AppliedBodyTorqueNewtonMeters.X), Attitude.RollTorqueNewtonMeters * 0.5, 1.0e-6)
			&& FMath::IsNearlyEqual(FMath::Abs(Saturated.AppliedBodyTorqueNewtonMeters.Y), Attitude.PitchTorqueNewtonMeters * 0.5, 1.0e-6)
			&& FMath::IsNearlyEqual(FMath::Abs(Saturated.AppliedBodyTorqueNewtonMeters.Z), Attitude.YawTorqueNewtonMeters * 0.5, 1.0e-6));

	// Zero authority or unavailable assist: no control torque, spin continues untouched.
	Input.ThrustAuthority = 0.0;
	const FEdenFlightStepOutput NoAuthority = FEdenFlightMovementModel::StepFlight(Input, Propulsion, Attitude, FixedStep);
	TestTrue(TEXT("Zero authority applies no torque"), NoAuthority.AppliedBodyTorqueNewtonMeters.IsZero());

	Input.ThrustAuthority = 1.0;
	Input.bAssistAvailable = false;
	const FEdenFlightStepOutput NoAssist = FEdenFlightMovementModel::StepFlight(Input, Propulsion, Attitude, FixedStep);
	TestFalse(TEXT("Load shed makes requested assist inactive"), NoAssist.bAssistActive);
	TestTrue(TEXT("Unavailable assist applies no stabilizing torque"), NoAssist.AppliedBodyTorqueNewtonMeters.IsZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationOrientationRequestConvergesTest,
	"Eden.Unit.Flight.Rotation.OrientationRequestConverges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationOrientationRequestConvergesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;
	const FEdenFlightAttitudeSettings Attitude;
	const FVector& I = Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared;
	const FEdenFlightTorqueEnvelope Envelope = FEdenFlightMovementModel::MakeTorqueEnvelope(Attitude, 1.0);

	// Autopilot seam: the same torque envelope as the pilot, no privileged actuators.
	const FQuat Targets[] = {
		FRotator(0.0, 90.0, 0.0).Quaternion(),
		FRotator(30.0, -60.0, 20.0).Quaternion(),
		FRotator(-45.0, 170.0, -80.0).Quaternion(),
	};

	for (const FQuat& Target : Targets)
	{
		const FEdenFlightAttitudeRequest Request = FEdenFlightAttitudeRequest::MakeOrientation(Target);
		FEdenFlightRotationState State;
		double MaxExcess = 0.0;
		for (int32 Step = 0; Step < 120 * 40; ++Step)
		{
			const FVector Torque = FEdenFlightMovementModel::ResolveAttitudeTorque(Request, State, Attitude, Envelope);
			MaxExcess = FMath::Max(MaxExcess, EdenFlightHarness::TorqueEnvelopeExcessNewtonMeters(Torque, Envelope));
			State = FEdenFlightMovementModel::IntegrateRotation(State, Torque, I, FixedStep).State;
		}

		const FString Name = Target.Rotator().ToString();
		TestTrue(FString::Printf(TEXT("%s: orientation request stays inside the torque envelope"), *Name), MaxExcess <= 1.0e-6);
		TestTrue(
			FString::Printf(TEXT("%s: reaches the target within 0.1 deg (%.4f deg)"), *Name, FMath::RadiansToDegrees(AngleBetween(State.Orientation, Target))),
			FMath::RadiansToDegrees(AngleBetween(State.Orientation, Target)) < 0.1);
		TestTrue(
			FString::Printf(TEXT("%s: holds the target at rest (|w| = %.3g rad/s)"), *Name, State.AngularVelocityBodyRadiansPerSecond.Size()),
			State.AngularVelocityBodyRadiansPerSecond.Size() < 1.0e-3);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationRedirectsThrustWithoutChangingMomentumTest,
	"Eden.Unit.Flight.Rotation.RedirectsThrustWithoutChangingMomentum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationRedirectsThrustWithoutChangingMomentumTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;

	FEdenFlightStepInput Input;
	Input.Translation.VelocityWorldMetersPerSecond = FVector(50.0, 0.0, 0.0);
	Input.Command = MakeCommand(FVector(0.0, 1.0, 0.0), false);
	const FSteppedRun Turned = StepN(Input, 360);

	TestTrue(
		TEXT("Rotating with no thrust leaves linear momentum bit-identical"),
		Turned.State.Translation.VelocityWorldMetersPerSecond == FVector(50.0, 0.0, 0.0));
	TestTrue(TEXT("The ship actually turned"), Turned.State.Rotation.Orientation.Rotator().Yaw > 10.0);

	FEdenFlightStepInput Burn = Turned.State;
	Burn.Command = MakeCommand(FVector::ZeroVector, false, FVector(1.0, 0.0, 0.0));
	Burn.Rotation.AngularVelocityBodyRadiansPerSecond = FVector::ZeroVector;
	const FEdenFlightStepOutput Output = FEdenFlightMovementModel::StepFlight(
		Burn,
		FEdenFlightPropulsionSettings(),
		FEdenFlightAttitudeSettings(),
		FixedStep);
	TestVectorNear(
		*this,
		TEXT("Forward thrust after the turn accelerates along the new nose direction"),
		Output.Translation.AccelerationWorldMetersPerSecondSquared,
		Turned.State.Rotation.Orientation.GetForwardVector() * 10.0,
		1.0e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationFrameRateIndependence60sTest,
	"Eden.Unit.Flight.Rotation.FrameRateIndependence60s",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationFrameRateIndependence60sTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace EdenFlightRotationTests;

	// Tumbling profile: multi-axis torque bursts with assist off, free tumble, then assisted stabilization.
	const TArray<EdenFlightHarness::FScriptedSegment> Profile = {
		{0.0, MakeCommand(FVector(1.0, 0.0, 0.0), false)},
		{3.0, MakeCommand(FVector(0.0, 0.7, 0.7), false)},
		{6.0, MakeCommand(FVector(-0.5, 0.5, -0.7), false)},
		{8.0, MakeCommand(FVector::ZeroVector, false)},
		{30.0, MakeCommand(FVector::ZeroVector, true)},
	};

	TOptional<EdenFlightHarness::FRunResult> Reference;
	FString ReferenceName;
	for (const EdenFlightHarness::FFrameSource& Source : EdenFlightHarness::StandardFrameSources())
	{
		EdenFlightHarness::FRunConfig Config;
		Config.DurationSeconds = 60.0;
		Config.Script = Profile;
		Config.Frames = Source;
		const EdenFlightHarness::FRunResult Result = EdenFlightHarness::Run(Config);

		TestEqual(FString::Printf(TEXT("%s: 7200 steps"), *Source.Name), Result.Steps, static_cast<int64>(7200));
		TestTrue(FString::Printf(TEXT("%s: torque stays inside the envelope"), *Source.Name), Result.MaxTorqueEnvelopeExcessNewtonMeters <= 1.0e-6);

		if (!Reference.IsSet())
		{
			Reference = Result;
			ReferenceName = Source.Name;
			continue;
		}

		const FString Pair = FString::Printf(TEXT("%s vs %s"), *Source.Name, *ReferenceName);
		TestTrue(
			FString::Printf(TEXT("%s: orientation bit-identical"), *Pair),
			Reference->Final.Rotation.Orientation == Result.Final.Rotation.Orientation);
		TestTrue(
			FString::Printf(TEXT("%s: angular velocity bit-identical"), *Pair),
			Reference->Final.Rotation.AngularVelocityBodyRadiansPerSecond == Result.Final.Rotation.AngularVelocityBodyRadiansPerSecond);
	}

	if (Reference.IsSet())
	{
		AddInfo(FString::Printf(
			TEXT("Tumble profile final: orientation %s, |w| %.3g rad/s"),
			*Reference->Final.Rotation.Orientation.Rotator().ToString(),
			Reference->Final.Rotation.AngularVelocityBodyRadiansPerSecond.Size()));
		TestTrue(
			TEXT("Assist stabilizes the tumble within 30 s"),
			Reference->Final.Rotation.AngularVelocityBodyRadiansPerSecond.Size() < 1.0e-6);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightRotationInvalidInputsAndSettingsTest,
	"Eden.Unit.Flight.Rotation.InvalidInputsAndSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightRotationInvalidInputsAndSettingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FVector I = FEdenFlightAttitudeSettings().PrincipalMomentsOfInertiaKilogramMetersSquared;
	FEdenFlightRotationState State;
	State.Orientation = FRotator(10.0, 20.0, 30.0).Quaternion();
	State.AngularVelocityBodyRadiansPerSecond = FVector(0.1, 0.2, 0.3);

	const double Nan = std::numeric_limits<double>::quiet_NaN();
	const FEdenFlightRotationStepResult BadDt = FEdenFlightMovementModel::IntegrateRotation(State, FVector::ZeroVector, I, 0.0);
	const FEdenFlightRotationStepResult BadTorque = FEdenFlightMovementModel::IntegrateRotation(State, FVector(Nan, 0.0, 0.0), I, 0.01);
	const FEdenFlightRotationStepResult BadInertia = FEdenFlightMovementModel::IntegrateRotation(State, FVector::ZeroVector, FVector(0.0, 1.0, 1.0), 0.01);
	for (const FEdenFlightRotationStepResult* Result : {&BadDt, &BadTorque, &BadInertia})
	{
		TestFalse(TEXT("Invalid rotation step is reported"), Result->bStepWasValid);
		TestTrue(
			TEXT("Invalid rotation step leaves state untouched"),
			Result->State.Orientation == State.Orientation
				&& Result->State.AngularVelocityBodyRadiansPerSecond == State.AngularVelocityBodyRadiansPerSecond);
	}

	TestTrue(TEXT("Default attitude settings are valid"), FEdenFlightMovementModel::ValidateAttitudeSettings(FEdenFlightAttitudeSettings(), 1.0 / 120.0));

	FEdenFlightAttitudeSettings Impossible;
	Impossible.PrincipalMomentsOfInertiaKilogramMetersSquared = FVector(1000.0, 100.0, 100.0);
	TestFalse(TEXT("Inertia violating the triangle inequality is rejected"), FEdenFlightMovementModel::ValidateAttitudeSettings(Impossible, 1.0 / 120.0));

	FEdenFlightAttitudeSettings ZeroInertia;
	ZeroInertia.PrincipalMomentsOfInertiaKilogramMetersSquared = FVector(0.0, 1.0, 1.0);
	TestFalse(TEXT("Zero inertia is rejected"), FEdenFlightMovementModel::ValidateAttitudeSettings(ZeroInertia, 1.0 / 120.0));

	FEdenFlightAttitudeSettings TooFast;
	TooFast.AssistAngularResponseTimeSeconds = 0.01f;
	TestFalse(TEXT("Rate assist faster than four flight steps is rejected"), FEdenFlightMovementModel::ValidateAttitudeSettings(TooFast, 1.0 / 120.0));

	return true;
}

#endif
