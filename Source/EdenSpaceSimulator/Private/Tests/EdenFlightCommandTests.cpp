// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightMovementModel.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenFlightCommandTests
{
constexpr double Tolerance = 0.01;

bool TestVectorNearlyEqual(
	FAutomationTestBase& Test,
	const TCHAR* What,
	const FVector& Actual,
	const FVector& Expected,
	double ErrorTolerance = Tolerance)
{
	const bool bMatches = Actual.Equals(Expected, ErrorTolerance);
	Test.TestTrue(
		FString::Printf(TEXT("%s. Actual=%s Expected=%s"), What, *Actual.ToString(), *Expected.ToString()),
		bMatches);
	return bMatches;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightCommandSanitizesZeroInputTest,
	"Eden.Unit.Flight.CommandSanitizesZeroInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightCommandSanitizesZeroInputTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bInputWasSanitized = true;
	const FEdenFlightInputCommand SanitizedCommand =
		FEdenFlightMovementModel::SanitizeCommand(FEdenFlightInputCommand(), &bInputWasSanitized);

	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Translation remains zero"), SanitizedCommand.TranslationInput, FVector::ZeroVector);
	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Rotation remains zero"), SanitizedCommand.RotationInput, FVector::ZeroVector);
	TestFalse(TEXT("Zero input is already sanitized"), bInputWasSanitized);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightCommandClampsPositiveAndNegativeMaximumInputTest,
	"Eden.Unit.Flight.CommandClampsPositiveAndNegativeMaximumInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightCommandClampsPositiveAndNegativeMaximumInputTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightInputCommand Command;
	Command.TranslationInput = FVector(1.0, -1.0, 0.0).GetSafeNormal();
	Command.RotationInput = FVector(-1.0, 0.0, 1.0).GetSafeNormal();

	bool bInputWasSanitized = true;
	const FEdenFlightInputCommand SanitizedCommand = FEdenFlightMovementModel::SanitizeCommand(Command, &bInputWasSanitized);

	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Translation max input is preserved"), SanitizedCommand.TranslationInput, Command.TranslationInput);
	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Rotation max input is preserved"), SanitizedCommand.RotationInput, Command.RotationInput);
	TestFalse(TEXT("Normalized maximum input does not need sanitization"), bInputWasSanitized);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightCommandClampsOutOfRangeInputTest,
	"Eden.Unit.Flight.CommandClampsOutOfRangeInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightCommandClampsOutOfRangeInputTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightInputCommand Command;
	Command.TranslationInput = FVector(2.0, 0.0, 0.0);
	Command.RotationInput = FVector(0.0, -4.0, 0.0);

	bool bInputWasSanitized = false;
	const FEdenFlightInputCommand SanitizedCommand = FEdenFlightMovementModel::SanitizeCommand(Command, &bInputWasSanitized);

	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Translation clamps to positive X"), SanitizedCommand.TranslationInput, FVector(1.0, 0.0, 0.0));
	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Rotation clamps to negative Y"), SanitizedCommand.RotationInput, FVector(0.0, -1.0, 0.0));
	TestTrue(TEXT("Out-of-range input reports sanitization"), bInputWasSanitized);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightCommandRejectsOrSanitizesNaNAndInfinityTest,
	"Eden.Unit.Flight.CommandRejectsOrSanitizesNaNAndInfinity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightCommandRejectsOrSanitizesNaNAndInfinityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightInputCommand Command;
	Command.TranslationInput = FVector(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity());
	Command.RotationInput = FVector(0.5, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity());

	bool bInputWasSanitized = false;
	const FEdenFlightInputCommand SanitizedCommand = FEdenFlightMovementModel::SanitizeCommand(Command, &bInputWasSanitized);

	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Non-finite translation becomes zero"), SanitizedCommand.TranslationInput, FVector::ZeroVector);
	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Finite rotation component is preserved"), SanitizedCommand.RotationInput, FVector(0.5, 0.0, 0.0));
	TestTrue(TEXT("Non-finite input reports sanitization"), bInputWasSanitized);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightInvalidDeltaTimeDoesNotMutateVelocityTest,
	"Eden.Unit.Flight.InvalidDeltaTimeDoesNotMutateVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightInvalidDeltaTimeDoesNotMutateVelocityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightInputCommand Command;
	Command.TranslationInput = FVector(1.0, 0.0, 0.0);
	Command.RotationInput = FVector(0.0, 1.0, 0.0);

	const FVector InitialAngularVelocity(-0.1, 0.25, -0.4);

	const FEdenFlightAttitudeSettings Attitude;
	const float InvalidDeltaTimes[] = {
		0.0f,
		-0.1f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()
	};

	for (const float DeltaTimeSeconds : InvalidDeltaTimes)
	{
		FEdenFlightStepInput StepInput;
		StepInput.Command = Command;
		StepInput.Translation.VelocityWorldMetersPerSecond = FVector(10.0, -20.0, 30.0);
		StepInput.Rotation.AngularVelocityBodyRadiansPerSecond = InitialAngularVelocity;
		StepInput.MassKilograms = 10000.0;
		const FEdenFlightStepOutput StepOutput =
			FEdenFlightMovementModel::StepFlight(StepInput, FEdenFlightPropulsionSettings(), Attitude, DeltaTimeSeconds);
		TestFalse(TEXT("Invalid DeltaTime flight step is reported"), StepOutput.bStepWasValid);
		EdenFlightCommandTests::TestVectorNearlyEqual(
			*this,
			TEXT("Invalid DeltaTime does not mutate angular velocity"),
			StepOutput.Rotation.State.AngularVelocityBodyRadiansPerSecond,
			InitialAngularVelocity,
			0.0);
		TestTrue(TEXT("Invalid DeltaTime does not rotate the ship"), StepOutput.Rotation.State.Orientation.Equals(FQuat::Identity, 0.0));
		EdenFlightCommandTests::TestVectorNearlyEqual(
			*this,
			TEXT("Invalid DeltaTime does not mutate linear velocity"),
			StepOutput.Translation.State.VelocityWorldMetersPerSecond,
			StepInput.Translation.VelocityWorldMetersPerSecond);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightBlockingHitRemovesInwardVelocityTest,
	"Eden.Unit.Flight.BlockingHitRemovesInwardVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightBlockingHitRemovesInwardVelocityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FVector ImpactNormal(-1.0, 0.0, 0.0);
	const FVector IncomingVelocity(100.0, 25.0, 0.0);
	const FVector SlidingVelocity = FEdenFlightMovementModel::RemoveInwardVelocity(IncomingVelocity, ImpactNormal);

	EdenFlightCommandTests::TestVectorNearlyEqual(
		*this,
		TEXT("Blocking response removes inward normal velocity and preserves tangent velocity"),
		SlidingVelocity,
		FVector(0.0, 25.0, 0.0));

	const FVector OutwardVelocity(-50.0, 10.0, 0.0);
	EdenFlightCommandTests::TestVectorNearlyEqual(
		*this,
		TEXT("Blocking response preserves outward velocity"),
		FEdenFlightMovementModel::RemoveInwardVelocity(OutwardVelocity, ImpactNormal),
		OutwardVelocity);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightInputIntentResetClearsCommandTest,
	"Eden.Unit.Flight.InputIntentResetClearsCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightInputIntentResetClearsCommandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightInputIntent Intent;
	Intent.CurrentCommand.TranslationInput = FVector(1.0, -1.0, 0.5);
	Intent.CurrentCommand.RotationInput = FVector(-0.25, 0.75, 1.0);
	Intent.CurrentCommand.bStabilizationEnabled = false;

	Intent.Reset();

	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Intent reset clears translation"), Intent.CurrentCommand.TranslationInput, FVector::ZeroVector);
	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Intent reset clears rotation"), Intent.CurrentCommand.RotationInput, FVector::ZeroVector);
	TestTrue(TEXT("Intent reset restores stabilization enabled"), Intent.CurrentCommand.bStabilizationEnabled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightMovementResetClearsLinearAndAngularVelocityTest,
	"Eden.Unit.Flight.MovementResetClearsLinearAndAngularVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightMovementResetClearsLinearAndAngularVelocityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenFlightMovementComponent* MovementComponent = NewObject<UEdenFlightMovementComponent>();
	TestNotNull(TEXT("Movement component can be constructed without a map"), MovementComponent);

	MovementComponent->Velocity = FVector(10.0, 20.0, -30.0);
	MovementComponent->SetAngularVelocityBodyRadiansPerSecond(FVector(-0.15, 0.25, 0.35));
	MovementComponent->ResetFlightMovement();

	EdenFlightCommandTests::TestVectorNearlyEqual(*this, TEXT("Movement reset clears inherited linear velocity"), MovementComponent->Velocity, FVector::ZeroVector);
	EdenFlightCommandTests::TestVectorNearlyEqual(
		*this,
		TEXT("Movement reset clears body angular velocity"),
		MovementComponent->GetAngularVelocityBodyRadiansPerSecond(),
		FVector::ZeroVector);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightEquivalentSimulatedTimeMatchesAcrossDeltaTimePartitionsTest,
	"Eden.Unit.Flight.EquivalentSimulatedTimeMatchesAcrossDeltaTimePartitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightEquivalentSimulatedTimeMatchesAcrossDeltaTimePartitionsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// Constant torque about one principal axis from rest gives w = (tau / I) t exactly in any partition.
	const FEdenFlightAttitudeSettings Attitude;
	const FVector& Inertia = Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared;
	const FVector Torque(0.0, 0.0, 200000.0);
	const FEdenFlightRotationState SingleRotation =
		FEdenFlightMovementModel::IntegrateRotation(FEdenFlightRotationState(), Torque, Inertia, 1.0).State;
	FEdenFlightRotationState PartitionedRotation;
	for (int32 StepIndex = 0; StepIndex < 10; ++StepIndex)
	{
		PartitionedRotation = FEdenFlightMovementModel::IntegrateRotation(PartitionedRotation, Torque, Inertia, 0.1).State;
	}
	EdenFlightCommandTests::TestVectorNearlyEqual(
		*this,
		TEXT("Angular velocity is equivalent across DeltaTime partitions"),
		PartitionedRotation.AngularVelocityBodyRadiansPerSecond,
		SingleRotation.AngularVelocityBodyRadiansPerSecond,
		1.0e-9);

	// Constant-force translation is exact, so any partition of the same time gives the same state.
	const FVector Force(100000.0, -20000.0, 5000.0);
	const FEdenFlightTranslationState SingleStep =
		FEdenFlightMovementModel::IntegrateTranslation(FEdenFlightTranslationState(), Force, 10000.0, FQuat::Identity, 1.0).State;
	FEdenFlightTranslationState Partitioned;
	for (int32 StepIndex = 0; StepIndex < 10; ++StepIndex)
	{
		Partitioned = FEdenFlightMovementModel::IntegrateTranslation(Partitioned, Force, 10000.0, FQuat::Identity, 0.1).State;
	}
	EdenFlightCommandTests::TestVectorNearlyEqual(
		*this,
		TEXT("Linear velocity is equivalent across DeltaTime partitions"),
		Partitioned.VelocityWorldMetersPerSecond,
		SingleStep.VelocityWorldMetersPerSecond,
		1.0e-9);
	EdenFlightCommandTests::TestVectorNearlyEqual(
		*this,
		TEXT("Position is equivalent across DeltaTime partitions"),
		Partitioned.PositionWorldMeters,
		SingleStep.PositionWorldMeters,
		1.0e-9);

	return true;
}

#endif
