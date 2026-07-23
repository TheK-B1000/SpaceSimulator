// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenFixedStepClockModel.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenSimClockTestSubscriber.h"

#include "Misc/AutomationTest.h"

#include <limits>

void UEdenSimClockTestSubscriber::AdvanceSimulation(float FixedDeltaSeconds)
{
	++AdvanceCallCount;
	LastFixedDeltaSeconds = FixedDeltaSeconds;
	TotalAdvancedSeconds += FixedDeltaSeconds;

	if (AdvanceCallCount == 1 && ClockToMutate.IsValid())
	{
		if (bRegisterTargetOnFirstAdvance && SubscriberToRegister.IsValid())
		{
			ClockToMutate->RegisterSimulationTickable(SubscriberToRegister.Get());
		}

		if (bUnregisterTargetOnFirstAdvance && SubscriberToUnregister.IsValid())
		{
			ClockToMutate->UnregisterSimulationTickable(SubscriberToUnregister.Get());
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenSimClockTests
{
constexpr double Tolerance = 0.001;

UEdenSimulationClockSubsystem* MakeClock()
{
	UEdenSimulationClockSubsystem* Clock = NewObject<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(4);
	Clock->ResetSimulationClock();
	return Clock;
}

bool TestFloatNearlyEqual(FAutomationTestBase& Test, const TCHAR* What, float Actual, float Expected)
{
	return Test.TestTrue(
		FString::Printf(TEXT("%s. Actual=%f Expected=%f"), What, Actual, Expected),
		FMath::IsNearlyEqual(Actual, Expected, Tolerance));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockFixedStepAdvancesSubscribersTest,
	"Eden.Unit.SimClock.FixedStepAdvancesSubscribers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockFixedStepAdvancesSubscribersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	UEdenSimClockTestSubscriber* Subscriber = NewObject<UEdenSimClockTestSubscriber>();

	TestTrue(TEXT("Subscriber registers"), Clock->RegisterSimulationTickable(Subscriber));
	Clock->Tick(0.35f);

	TestEqual(TEXT("Three fixed steps are taken"), Clock->GetLastStepsTaken(), 3);
	TestEqual(TEXT("Subscriber receives one call per fixed step"), Subscriber->AdvanceCallCount, 3);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Fixed delta is supplied to subscribers"), Subscriber->LastFixedDeltaSeconds, 0.1f);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Elapsed time advances by fixed steps"), Clock->GetElapsedSimulationTimeSeconds(), 0.3f);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Remainder is preserved"), Clock->GetAccumulatorSeconds(), 0.05f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockAccumulatorHandlesPartialFramesTest,
	"Eden.Unit.SimClock.AccumulatorHandlesPartialFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockAccumulatorHandlesPartialFramesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	float AccumulatorSeconds = 0.0f;
	int32 DroppedSteps = 0;

	TestEqual(
		TEXT("Partial frame does not step"),
		FEdenFixedStepClockModel::CalculateSteps(0.05f, 0.1f, 4, AccumulatorSeconds, DroppedSteps),
		0);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Partial frame is accumulated"), AccumulatorSeconds, 0.05f);
	TestEqual(TEXT("No steps dropped for partial frame"), DroppedSteps, 0);

	TestEqual(
		TEXT("Second partial frame completes a step"),
		FEdenFixedStepClockModel::CalculateSteps(0.05f, 0.1f, 4, AccumulatorSeconds, DroppedSteps),
		1);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Accumulator is consumed"), AccumulatorSeconds, 0.0f);
	TestEqual(TEXT("No steps dropped below cap"), DroppedSteps, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockCatchUpBoundedTest,
	"Eden.Unit.SimClock.CatchUpBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockCatchUpBoundedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	float AccumulatorSeconds = 0.0f;
	int32 DroppedSteps = 0;
	const int32 StepsTaken = FEdenFixedStepClockModel::CalculateSteps(0.5f, 0.1f, 3, AccumulatorSeconds, DroppedSteps);

	TestEqual(TEXT("Catch-up is bounded by MaxCatchUpSteps"), StepsTaken, 3);
	TestEqual(TEXT("Excess fixed steps are reported as dropped"), DroppedSteps, 2);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Dropped time is removed from accumulator"), AccumulatorSeconds, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockOverrunDropsExcessStepsTest,
	"Eden.Unit.SimClock.OverrunDropsExcessSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockOverrunDropsExcessStepsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	float AccumulatorSeconds = 0.0f;
	int32 DroppedSteps = 0;
	const int32 StepsTaken = FEdenFixedStepClockModel::CalculateSteps(0.55f, 0.1f, 3, AccumulatorSeconds, DroppedSteps);

	TestEqual(TEXT("Only bounded steps are taken"), StepsTaken, 3);
	TestEqual(TEXT("Dropped steps are reported from model state"), DroppedSteps, 2);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Only sub-step remainder remains"), AccumulatorSeconds, 0.05f);

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	Clock->SetMaxCatchUpSteps(3);
	Clock->Tick(0.55f);

	TestEqual(TEXT("Subsystem reports bounded steps"), Clock->GetLastStepsTaken(), 3);
	TestEqual(TEXT("Subsystem reports dropped steps"), Clock->GetLastDroppedSteps(), 2);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Dropped time does not increase elapsed time"), Clock->GetElapsedSimulationTimeSeconds(), 0.3f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockPausePreventsAdvanceTest,
	"Eden.Unit.SimClock.PausePreventsAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockPausePreventsAdvanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	UEdenSimClockTestSubscriber* Subscriber = NewObject<UEdenSimClockTestSubscriber>();
	Clock->RegisterSimulationTickable(Subscriber);

	Clock->PauseSimulation();
	Clock->Tick(1.0f);

	TestTrue(TEXT("Clock is paused"), Clock->IsSimulationPaused());
	TestEqual(TEXT("Paused clock advances no subscribers"), Subscriber->AdvanceCallCount, 0);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Paused frame does not accumulate remainder"), Clock->GetAccumulatorSeconds(), 0.0f);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Paused frame does not increase elapsed time"), Clock->GetElapsedSimulationTimeSeconds(), 0.0f);

	Clock->ResumeSimulation();
	Clock->Tick(0.1f);
	TestEqual(TEXT("Resumed clock advances subscribers"), Subscriber->AdvanceCallCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockResetClearsAccumulatorAndTimeTest,
	"Eden.Unit.SimClock.ResetClearsAccumulatorAndTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockResetClearsAccumulatorAndTimeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	Clock->Tick(0.25f);

	TestEqual(TEXT("Clock has taken steps before reset"), Clock->GetLastStepsTaken(), 2);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Clock has accumulated elapsed time before reset"), Clock->GetElapsedSimulationTimeSeconds(), 0.2f);

	Clock->ResetSimulationClock();

	TestEqual(TEXT("Reset clears last steps"), Clock->GetLastStepsTaken(), 0);
	TestEqual(TEXT("Reset clears last dropped steps"), Clock->GetLastDroppedSteps(), 0);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Reset clears accumulator"), Clock->GetAccumulatorSeconds(), 0.0f);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Reset clears elapsed time"), Clock->GetElapsedSimulationTimeSeconds(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockInvalidDeltaTimeRejectedTest,
	"Eden.Unit.SimClock.InvalidDeltaTimeRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockInvalidDeltaTimeRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float InvalidDeltaTimes[] = {
		0.0f,
		-0.1f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()
	};

	for (const float DeltaTimeSeconds : InvalidDeltaTimes)
	{
		float AccumulatorSeconds = 0.04f;
		int32 DroppedSteps = 1;
		const int32 StepsTaken =
			FEdenFixedStepClockModel::CalculateSteps(DeltaTimeSeconds, 0.1f, 4, AccumulatorSeconds, DroppedSteps);

		TestFalse(TEXT("Invalid DeltaTime is rejected"), FEdenFixedStepClockModel::IsValidDeltaTime(DeltaTimeSeconds));
		TestEqual(TEXT("Invalid DeltaTime takes no steps"), StepsTaken, 0);
		TestEqual(TEXT("Invalid DeltaTime drops no steps"), DroppedSteps, 0);
		EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Valid accumulator remains unchanged"), AccumulatorSeconds, 0.04f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockInvalidFixedStepConfigRejectedTest,
	"Eden.Unit.SimClock.InvalidFixedStepConfigRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockInvalidFixedStepConfigRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float InvalidFixedSteps[] = {
		0.0f,
		-0.1f,
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity()
	};

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	for (const float FixedStepSeconds : InvalidFixedSteps)
	{
		TestFalse(TEXT("Invalid fixed step is rejected by model"), FEdenFixedStepClockModel::IsValidFixedStepSeconds(FixedStepSeconds));
		TestFalse(TEXT("Invalid fixed step is rejected by subsystem"), Clock->SetFixedStepSeconds(FixedStepSeconds));
		EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Subsystem retains previous fixed step"), Clock->GetFixedStepSeconds(), 0.1f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockMaxCatchUpStepsRequiresPositiveValueTest,
	"Eden.Unit.SimClock.MaxCatchUpStepsRequiresPositiveValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockMaxCatchUpStepsRequiresPositiveValueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	for (const int32 MaxCatchUpSteps : {0, -1})
	{
		TestFalse(TEXT("Invalid catch-up cap is rejected by model"), FEdenFixedStepClockModel::IsValidMaxCatchUpSteps(MaxCatchUpSteps));
		TestFalse(TEXT("Invalid catch-up cap is rejected by subsystem"), Clock->SetMaxCatchUpSteps(MaxCatchUpSteps));
		TestEqual(TEXT("Subsystem retains previous catch-up cap"), Clock->GetMaxCatchUpSteps(), 4);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockWorldTypeSupportIsLockedTest,
	"Eden.Unit.SimClock.WorldTypeSupportIsLocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockWorldTypeSupportIsLockedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UEdenSimulationClockSubsystem* Clock = GetDefault<UEdenSimulationClockSubsystem>();

	TestTrue(TEXT("Game worlds are supported"), Clock->DoesSupportWorldType(EWorldType::Game));
	TestTrue(TEXT("PIE worlds are supported"), Clock->DoesSupportWorldType(EWorldType::PIE));
	TestFalse(TEXT("GamePreview worlds are not required for automation and are excluded"), Clock->DoesSupportWorldType(EWorldType::GamePreview));
	TestFalse(TEXT("Editor worlds are excluded"), Clock->DoesSupportWorldType(EWorldType::Editor));
	TestFalse(TEXT("EditorPreview worlds are excluded"), Clock->DoesSupportWorldType(EWorldType::EditorPreview));
	TestFalse(TEXT("Inactive worlds are excluded"), Clock->DoesSupportWorldType(EWorldType::Inactive));
	TestFalse(TEXT("None worlds are excluded"), Clock->DoesSupportWorldType(EWorldType::None));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockSubscriberMutationDeferredDuringStepTest,
	"Eden.Unit.SimClock.SubscriberMutationDeferredDuringStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockSubscriberMutationDeferredDuringStepTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* RegistrationClock = EdenSimClockTests::MakeClock();
	UEdenSimClockTestSubscriber* RegisteringSubscriber = NewObject<UEdenSimClockTestSubscriber>();
	UEdenSimClockTestSubscriber* DeferredSubscriber = NewObject<UEdenSimClockTestSubscriber>();
	RegisteringSubscriber->ClockToMutate = RegistrationClock;
	RegisteringSubscriber->SubscriberToRegister = DeferredSubscriber;
	RegisteringSubscriber->bRegisterTargetOnFirstAdvance = true;

	TestTrue(TEXT("Registering subscriber registers"), RegistrationClock->RegisterSimulationTickable(RegisteringSubscriber));
	RegistrationClock->Tick(0.25f);

	TestEqual(TEXT("Original subscriber receives the active batch"), RegisteringSubscriber->AdvanceCallCount, 2);
	TestEqual(TEXT("Deferred subscriber is not called in the active batch"), DeferredSubscriber->AdvanceCallCount, 0);
	TestEqual(TEXT("Deferred registration flushes after stepping"), RegistrationClock->GetSubscriberCount(), 2);

	RegistrationClock->Tick(0.1f);
	TestEqual(TEXT("Deferred subscriber receives future ticks"), DeferredSubscriber->AdvanceCallCount, 1);

	UEdenSimulationClockSubsystem* UnregistrationClock = EdenSimClockTests::MakeClock();
	UEdenSimClockTestSubscriber* SelfUnregisteringSubscriber = NewObject<UEdenSimClockTestSubscriber>();
	SelfUnregisteringSubscriber->ClockToMutate = UnregistrationClock;
	SelfUnregisteringSubscriber->SubscriberToUnregister = SelfUnregisteringSubscriber;
	SelfUnregisteringSubscriber->bUnregisterTargetOnFirstAdvance = true;

	TestTrue(TEXT("Self-unregistering subscriber registers"), UnregistrationClock->RegisterSimulationTickable(SelfUnregisteringSubscriber));
	UnregistrationClock->Tick(0.25f);

	TestEqual(TEXT("Unregistering subscriber remains in stable active snapshot"), SelfUnregisteringSubscriber->AdvanceCallCount, 2);
	TestEqual(TEXT("Deferred unregistration flushes after stepping"), UnregistrationClock->GetSubscriberCount(), 0);

	UnregistrationClock->Tick(0.1f);
	TestEqual(TEXT("Unregistered subscriber is not called on future ticks"), SelfUnregisteringSubscriber->AdvanceCallCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockDuplicateRegistrationRejectedTest,
	"Eden.Unit.SimClock.DuplicateRegistrationRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockDuplicateRegistrationRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	UEdenSimClockTestSubscriber* Subscriber = NewObject<UEdenSimClockTestSubscriber>();

	TestTrue(TEXT("First registration succeeds"), Clock->RegisterSimulationTickable(Subscriber));
	TestFalse(TEXT("Duplicate registration is rejected"), Clock->RegisterSimulationTickable(Subscriber));
	TestEqual(TEXT("Only one subscriber is retained"), Clock->GetSubscriberCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockInvalidSubscribersHandledSafelyTest,
	"Eden.Unit.SimClock.InvalidSubscribersHandledSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockInvalidSubscribersHandledSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	UEdenSimClockPlainTestObject* PlainObject = NewObject<UEdenSimClockPlainTestObject>();

	TestFalse(TEXT("Null subscriber is rejected"), Clock->RegisterSimulationTickable(nullptr));
	TestFalse(TEXT("Object without IEdenSimulationTickable is rejected"), Clock->RegisterSimulationTickable(PlainObject));
	TestEqual(TEXT("Invalid subscribers are not retained"), Clock->GetSubscriberCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockElapsedTimeAccumulatesCorrectlyTest,
	"Eden.Unit.SimClock.ElapsedTimeAccumulatesCorrectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockElapsedTimeAccumulatesCorrectlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenSimulationClockSubsystem* Clock = EdenSimClockTests::MakeClock();
	Clock->Tick(0.25f);

	TestEqual(TEXT("Two fixed steps are taken"), Clock->GetLastStepsTaken(), 2);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Elapsed time is fixed steps only"), Clock->GetElapsedSimulationTimeSeconds(), 0.2f);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Raw frame remainder is preserved"), Clock->GetAccumulatorSeconds(), 0.05f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSimClockEquivalentTimeMatchesBelowCatchUpCapTest,
	"Eden.Unit.SimClock.EquivalentTimeMatchesBelowCatchUpCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSimClockEquivalentTimeMatchesBelowCatchUpCapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	float SingleAccumulatorSeconds = 0.0f;
	int32 SingleDroppedSteps = 0;
	const int32 SingleSteps = FEdenFixedStepClockModel::CalculateSteps(
		0.25f,
		0.1f,
		4,
		SingleAccumulatorSeconds,
		SingleDroppedSteps);

	float PartitionedAccumulatorSeconds = 0.0f;
	int32 PartitionedDroppedSteps = 0;
	int32 PartitionedSteps = 0;
	for (const float DeltaTimeSeconds : {0.05f, 0.05f, 0.05f, 0.1f})
	{
		PartitionedSteps += FEdenFixedStepClockModel::CalculateSteps(
			DeltaTimeSeconds,
			0.1f,
			4,
			PartitionedAccumulatorSeconds,
			PartitionedDroppedSteps);
		TestEqual(TEXT("Equivalent partition remains below catch-up cap"), PartitionedDroppedSteps, 0);
	}

	TestEqual(TEXT("Single and partitioned step counts match below cap"), PartitionedSteps, SingleSteps);
	TestEqual(TEXT("Single run does not drop steps below cap"), SingleDroppedSteps, 0);
	EdenSimClockTests::TestFloatNearlyEqual(*this, TEXT("Single and partitioned accumulators match"), PartitionedAccumulatorSeconds, SingleAccumulatorSeconds);

	return true;
}

#endif
