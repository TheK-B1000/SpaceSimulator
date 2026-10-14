// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightPresentationTypes.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightMouseStickAccumulatesAndHoldsTest,
	"Eden.Unit.Flight.MouseStickAccumulatesAndHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightMouseStickAccumulatesAndHoldsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightMouseSettings Settings;
	Settings.Sensitivity = 0.01f;
	Settings.AutoCenterRatePerSecond = 0.0f;

	// The old path read each mouse delta as a full stick deflection; a 3-count nudge saturated it.
	FVector2D Stick = FEdenFlightPresentationModel::AccumulateMouseStick(FVector2D::ZeroVector, FVector2D(3.0, 0.0), Settings);
	TestTrue(TEXT("A small mouse nudge is a small deflection"), FMath::IsNearlyEqual(Stick.X, 0.03, 1.0e-6));

	Stick = FEdenFlightPresentationModel::AccumulateMouseStick(Stick, FVector2D(37.0, -20.0), Settings);
	TestTrue(TEXT("Deltas accumulate on pitch"), FMath::IsNearlyEqual(Stick.X, 0.40, 1.0e-6));
	TestTrue(TEXT("Deltas accumulate on yaw"), FMath::IsNearlyEqual(Stick.Y, -0.20, 1.0e-6));

	// Mouse stops: no delta, and with centring off the deflection is held (it used to drop to zero).
	const FVector2D Held = FEdenFlightPresentationModel::CenterMouseStick(Stick, Settings, 0.5);
	TestTrue(TEXT("With centring off the stick holds its deflection"), Held == Stick);

	Stick = FEdenFlightPresentationModel::AccumulateMouseStick(Stick, FVector2D(500.0, -500.0), Settings);
	TestEqual(TEXT("Deflection clamps at +1"), Stick.X, 1.0);
	TestEqual(TEXT("Deflection clamps at -1"), Stick.Y, -1.0);

	const FVector2D NaN(std::numeric_limits<double>::quiet_NaN(), 1.0);
	TestTrue(TEXT("Non-finite deltas are ignored"), FEdenFlightPresentationModel::AccumulateMouseStick(Stick, NaN, Settings) == Stick);

	Settings.bInvertPitch = true;
	const FVector2D Inverted = FEdenFlightPresentationModel::AccumulateMouseStick(FVector2D::ZeroVector, FVector2D(10.0, 10.0), Settings);
	TestTrue(TEXT("Invert pitch flips pitch only"), FMath::IsNearlyEqual(Inverted.X, -0.1, 1.0e-6) && FMath::IsNearlyEqual(Inverted.Y, 0.1, 1.0e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightMouseStickCenteringIsFrameRateIndependentTest,
	"Eden.Unit.Flight.MouseStickCenteringIsFrameRateIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightMouseStickCenteringIsFrameRateIndependentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightMouseSettings Settings;
	Settings.AutoCenterRatePerSecond = 1.5f;
	const FVector2D Start(0.8, -0.6);
	const double Expected = FMath::Exp(-1.5);

	for (const int32 FramesPerSecond : {30, 60, 144, 240})
	{
		FVector2D Stick = Start;
		for (int32 Frame = 0; Frame < FramesPerSecond; ++Frame)
		{
			Stick = FEdenFlightPresentationModel::CenterMouseStick(Stick, Settings, 1.0 / FramesPerSecond);
		}
		TestTrue(
			FString::Printf(TEXT("After 1 s at %d fps the stick has decayed by exp(-rate). Actual=%s"), FramesPerSecond, *Stick.ToString()),
			Stick.Equals(Start * Expected, 1.0e-9));
	}

	TestTrue(TEXT("Zero frame time leaves the stick alone"), FEdenFlightPresentationModel::CenterMouseStick(Start, Settings, 0.0) == Start);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightMouseStickDeadZoneRescalesTest,
	"Eden.Unit.Flight.MouseStickDeadZoneRescales",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightMouseStickDeadZoneRescalesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightMouseSettings Settings;
	Settings.DeadZone = 0.1f;
	auto Shape = [&Settings](double X, double Y) { return FEdenFlightPresentationModel::ApplyStickDeadZone(FVector2D(X, Y), Settings); };

	TestTrue(TEXT("Inside the dead zone is centred"), Shape(0.09, -0.05) == FVector2D::ZeroVector);
	TestTrue(TEXT("Full deflection still reaches exactly +/-1"), Shape(1.0, -1.0) == FVector2D(1.0, -1.0));
	TestTrue(TEXT("Output is continuous at the dead-zone edge"), Shape(0.1000001, 0.0).X < 1.0e-5);
	TestTrue(TEXT("Mid deflection is rescaled linearly"), FMath::IsNearlyEqual(Shape(0.55, 0.0).X, 0.5, 1.0e-6));
	TestTrue(TEXT("Sign is preserved"), FMath::IsNearlyEqual(Shape(0.0, -0.55).Y, -0.5, 1.0e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightChaseCameraKeepsHullInViewTest,
	"Eden.Unit.Flight.ChaseCameraKeepsHullInView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightChaseCameraKeepsHullInViewTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFlightCameraSettings Defaults;
	for (const double Radius : {200.0, 650.0, 2500.0})
	{
		for (const float Fov : {50.0f, 75.0f, 100.0f})
		{
			for (const float Aspect : {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f})
			{
				FEdenFlightCameraSettings Settings = Defaults;
				Settings.FieldOfViewDegrees = Fov;
				Settings.ViewAspectRatio = Aspect;

				const double Limit = FEdenFlightPresentationModel::ComputeLimitingHalfFieldOfViewRadians(Settings);
				const double Arm = FEdenFlightPresentationModel::ComputeChaseArmLengthCentimeters(Radius, Settings);
				const double Extent = FEdenFlightPresentationModel::ComputeHullAngularExtentRadians(Radius, Arm, Settings);
				TestTrue(
					FString::Printf(TEXT("R=%.0f FOV=%.0f aspect=%.2f: whole hull sphere in view (extent %.2f deg <= %.2f deg)"),
						Radius, Fov, Aspect, FMath::RadiansToDegrees(Extent), FMath::RadiansToDegrees(Limit)),
					Extent <= Limit + 1.0e-9);

				// With no margin the fit is exact: the sphere touches the frame edge.
				Settings.FramingMargin = 1.0f;
				const double ExactArm = FEdenFlightPresentationModel::ComputeChaseArmLengthCentimeters(Radius, Settings);
				const double ExactExtent = FEdenFlightPresentationModel::ComputeHullAngularExtentRadians(Radius, ExactArm, Settings);
				TestTrue(TEXT("Margin 1 is an exact fit"), FMath::IsNearlyEqual(ExactExtent, Limit, 1.0e-6));
				TestTrue(TEXT("A shorter boom would clip the hull"), FEdenFlightPresentationModel::ComputeHullAngularExtentRadians(Radius, ExactArm * 0.95, Settings) > Limit);
			}
		}
	}

	FEdenFlightCameraSettings Landscape = Defaults;
	Landscape.ViewAspectRatio = 16.0f / 9.0f;
	TestTrue(
		TEXT("On a landscape screen the vertical half-angle is the limit (UE FOV is horizontal)"),
		FEdenFlightPresentationModel::ComputeLimitingHalfFieldOfViewRadians(Landscape) < FMath::DegreesToRadians(Landscape.FieldOfViewDegrees * 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenFlightMainEngineVisualFollowsDeliveredForwardForceTest,
	"Eden.Unit.Flight.MainEngineVisualFollowsDeliveredForwardForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenFlightMainEngineVisualFollowsDeliveredForwardForceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenFlightPropulsionSettings Propulsion;

	// Pure rotation: thrusters are firing (aggregate output is non-zero) but no forward force.
	FEdenFlightDynamicsSnapshot Rotating;
	Rotating.AppliedBodyTorqueNewtonMeters = FVector(0.0, 180000.0, 0.0);
	Rotating.DeliveredThrusterOutputNewtons = 18000.0;
	TestEqual(TEXT("Pure rotation does not light the main engine"), FEdenFlightPresentationModel::CalculateMainEngineVisualLevel(Rotating, Propulsion), 0.0f);

	FEdenFlightDynamicsSnapshot Reverse;
	Reverse.AppliedBodyForceNewtons = FVector(-50000.0, 0.0, 0.0);
	TestEqual(TEXT("Reverse thrust does not light the main engine"), FEdenFlightPresentationModel::CalculateMainEngineVisualLevel(Reverse, Propulsion), 0.0f);

	FEdenFlightDynamicsSnapshot Strafe;
	Strafe.AppliedBodyForceNewtons = FVector(0.0, 20000.0, -20000.0);
	TestEqual(TEXT("Lateral and vertical thrust do not light the main engine"), FEdenFlightPresentationModel::CalculateMainEngineVisualLevel(Strafe, Propulsion), 0.0f);

	FEdenFlightDynamicsSnapshot Critical;
	Critical.AppliedBodyForceNewtons = FVector(Propulsion.ForwardThrustNewtons * 0.4, 0.0, 0.0);
	TestTrue(TEXT("Critical-power burn shows its delivered 40 %"), FMath::IsNearlyEqual(FEdenFlightPresentationModel::CalculateMainEngineVisualLevel(Critical, Propulsion), 0.4f, 1.0e-6f));

	FEdenFlightDynamicsSnapshot Full;
	Full.AppliedBodyForceNewtons = FVector(Propulsion.ForwardThrustNewtons * 2.0, 0.0, 0.0);
	TestEqual(TEXT("Level clamps at 1"), FEdenFlightPresentationModel::CalculateMainEngineVisualLevel(Full, Propulsion), 1.0f);

	FEdenFlightPropulsionSettings NoRating = Propulsion;
	NoRating.ForwardThrustNewtons = 0.0f;
	TestEqual(TEXT("No forward rating shows nothing"), FEdenFlightPresentationModel::CalculateMainEngineVisualLevel(Full, NoRating), 0.0f);
	return true;
}

#endif
