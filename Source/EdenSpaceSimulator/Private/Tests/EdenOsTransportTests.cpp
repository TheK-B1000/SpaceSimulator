// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsTelemetrySink.h"
#include "EdenOs/EdenOsTransport.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Telemetry/EdenTelemetrySink.h"
#include "Telemetry/EdenTelemetrySubsystem.h"

#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenOsTransportTests
{
	class FFakeHttpTransport final : public IEdenOsHttpTransport
	{
	public:
		virtual bool SendAsync(const FEdenOsHttpRequestData& Request, FEdenOsHttpCompletion Completion) override
		{
			SentRequests.Add(Request);
			if (bReturnFalseWithoutCompletion)
			{
				return false;
			}
			if (bDelayCompletion)
			{
				DeferredCompletions.Add(MoveTemp(Completion));
				return true;
			}

			const FEdenOsHttpResult Result = Results.IsEmpty()
				? FEdenOsHttpResult::Succeeded(204)
				: Results[0];
			if (!Results.IsEmpty())
			{
				Results.RemoveAt(0, 1, EAllowShrinking::No);
			}
			Completion.ExecuteIfBound(Result);
			return true;
		}

		void CompleteNext(const FEdenOsHttpResult& Result)
		{
			if (DeferredCompletions.IsEmpty())
			{
				return;
			}

			FEdenOsHttpCompletion Completion = MoveTemp(DeferredCompletions[0]);
			DeferredCompletions.RemoveAt(0, 1, EAllowShrinking::No);
			Completion.ExecuteIfBound(Result);
		}

		TArray<FEdenOsHttpRequestData> SentRequests;
		TArray<FEdenOsHttpResult> Results;
		TArray<FEdenOsHttpCompletion> DeferredCompletions;
		bool bDelayCompletion = false;
		bool bReturnFalseWithoutCompletion = false;
	};

	FEdenOsConnectionConfig MakeEnabledConfig(int32 MaxQueueDepth = 8)
	{
		FEdenOsConnectionConfig Config;
		Config.bEnabled = true;
		Config.BaseUrl = TEXT("https://example.test");
		Config.ConnectionTimeoutSeconds = 2.0f;
		Config.RequestTimeoutSeconds = 5.0f;
		Config.MaxQueueDepth = MaxQueueDepth;
		Config.AdvisoryHeartbeatSimulationSeconds = 5.0f;
		Config.RuntimeBearerJwt = TEXT("test-token");
		return Config;
	}

	FEdenOsQueuedRequest MakeRequest(const FString& Body, const FString& Route = EdenOsWireContract::TelemetryRouteTemplate)
	{
		FEdenOsQueuedRequest Request;
		Request.MessageType = EEdenOsOutboundMessageType::Telemetry;
		Request.RoutePath = FEdenOsUrlModel::BuildSessionRoute(Route, TEXT("session-001"));
		Request.BodyJson = Body;
		return Request;
	}

	FEdenTelemetrySessionPayload MakePayload(
		const TArray<FEdenTelemetryEvent>& Events,
		const TArray<FEdenTelemetrySnapshot>& Snapshots,
		const FEdenTelemetrySessionMetadata& Metadata)
	{
		return FEdenTelemetrySessionPayload(Events, Snapshots, Metadata, TEXT("session-001"), TEXT("SolarCrisis"));
	}

	void PopulateTelemetry(TArray<FEdenTelemetryEvent>& Events, TArray<FEdenTelemetrySnapshot>& Snapshots, FEdenTelemetrySessionMetadata& Metadata)
	{
		FEdenTelemetryEvent Event;
		Event.SequenceNumber = 1;
		Event.SimulationTimeSeconds = 1.0f;
		Event.MissionElapsedTimeSeconds = 1.0f;
		Event.EventType = EEdenTelemetryEventType::MissionStarted;
		Event.SourceSystem = TEXT("Mission");
		Event.EventId = TEXT("Running");
		Events.Add(Event);

		FEdenTelemetrySnapshot Snapshot;
		Snapshot.SequenceNumber = 2;
		Snapshot.SimulationTimeSeconds = 1.0f;
		Snapshot.MissionElapsedTimeSeconds = 1.0f;
		Snapshot.Mission.ActiveMissionId = TEXT("SolarCrisis");
		Snapshot.Fuel.FuelFraction = 0.9f;
		Snapshot.Power.ChargeFraction = 0.8f;
		Snapshot.Thermal.TemperatureCelsius = 30.0f;
		Snapshots.Add(Snapshot);

		Metadata.FirstAvailableSequence = 1;
		Metadata.LastAvailableSequence = 2;
		Metadata.SnapshotIntervalSeconds = 0.5f;
		Metadata.PeakTemperatureCelsius = 30.0f;
		Metadata.MinimumBatteryChargeFraction = 0.8f;
		Metadata.MinimumFuelFraction = 0.9f;
	}

	class FAlwaysFailingSink final : public IEdenTelemetrySink
	{
	public:
		virtual FName GetTelemetrySinkName() const override
		{
			return TEXT("AlwaysFailingSink");
		}

		virtual FEdenTelemetrySinkResult DeliverTelemetrySession(const FEdenTelemetrySessionPayload& Payload) override
		{
			(void)Payload;
			return FEdenTelemetrySinkResult::Failed(TEXT("Synthetic EDEN transport failure."));
		}
	};

	struct FResourceProbe
	{
		float FuelFraction = 0.0f;
		float BatteryFraction = 0.0f;
		float TemperatureCelsius = 0.0f;
		EEdenFuelState FuelState = EEdenFuelState::Normal;
		EEdenPowerState PowerState = EEdenPowerState::Normal;
		EEdenThermalState ThermalState = EEdenThermalState::Normal;
	};

	FEdenFuelConfig MakeFuelConfig()
	{
		FEdenFuelConfig Config;
		Config.CapacityKilograms = 100.0f;
		Config.ConsumptionRateKilogramsPerSecond = 10.0f;
		Config.InitialFuelFraction = 1.0f;
		Config.WarningThresholdFraction = 0.25f;
		Config.CriticalThresholdFraction = 0.1f;
		return Config;
	}

	FEdenPowerConfig MakePowerConfig()
	{
		FEdenPowerConfig Config;
		Config.BatteryCapacityKilowattHours = 10.0f;
		Config.GenerationKilowatts = 0.0f;
		Config.BaselineDemandKilowatts = 1.0f;
		Config.InitialChargeFraction = 1.0f;
		Config.WarningThresholdFraction = 0.25f;
		Config.CriticalThresholdFraction = 0.1f;
		return Config;
	}

	FEdenThermalConfig MakeThermalConfig()
	{
		FEdenThermalConfig Config;
		Config.AbsoluteMinTemperatureCelsius = -100.0f;
		Config.AmbientTemperatureCelsius = 20.0f;
		Config.WarningTemperatureCelsius = 80.0f;
		Config.CriticalTemperatureCelsius = 100.0f;
		Config.AbsoluteMaxTemperatureCelsius = 120.0f;
		Config.InitialTemperatureCelsius = 30.0f;
		Config.HeatGenerationDegreesCelsiusPerSecond = 2.0f;
		Config.DissipationDegreesCelsiusPerSecond = 0.5f;
		return Config;
	}

	FResourceProbe RunResourceProbe(bool bSubmitFailingTransport)
	{
		AActor* Owner = NewObject<AActor>();

		UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>(Owner);
		Fuel->InitializeFuelSimulation(MakeFuelConfig());

		UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>(Owner);
		Power->InitializePowerSimulation(MakePowerConfig());

		UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>(Owner);
		Thermal->InitializeThermalSimulation(MakeThermalConfig());

		if (bSubmitFailingTransport)
		{
			FFakeHttpTransport Transport;
			Transport.Results.Add(FEdenOsHttpResult::Failed(0, TEXT("offline")));
			UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
			Adapter->SetHttpTransportForTesting(&Transport);
			Adapter->ApplyRuntimeConfig(MakeEnabledConfig());
			Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"schemaVersion\":1}")));
		}

		for (int32 Index = 0; Index < 5; ++Index)
		{
			Fuel->AdvanceSimulation(1.0f);
			Power->AdvanceSimulation(1.0f);
			Thermal->AdvanceSimulation(1.0f);
		}

		const FEdenFuelStateSnapshot FuelSnapshot = Fuel->GetFuelStateSnapshot();
		const FEdenPowerStateSnapshot PowerSnapshot = Power->GetPowerStateSnapshot();
		const FEdenThermalStateSnapshot ThermalSnapshot = Thermal->GetThermalStateSnapshot();

		FResourceProbe Probe;
		Probe.FuelFraction = FuelSnapshot.FuelFraction;
		Probe.BatteryFraction = PowerSnapshot.ChargeFraction;
		Probe.TemperatureCelsius = ThermalSnapshot.TemperatureCelsius;
		Probe.FuelState = FuelSnapshot.FuelState;
		Probe.PowerState = PowerSnapshot.PowerState;
		Probe.ThermalState = ThermalSnapshot.ThermalState;
		return Probe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportUrlJoinIsDeterministicTest,
	"Eden.Unit.EdenOs.Transport.UrlJoinIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportUrlJoinIsDeterministicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString NoSlash = FEdenOsUrlModel::JoinBaseUrlAndRoute(TEXT("https://example.test"), EdenOsWireContract::CreateSessionRoute);
	const FString WithSlash = FEdenOsUrlModel::JoinBaseUrlAndRoute(TEXT("https://example.test/"), EdenOsWireContract::CreateSessionRoute);
	TestEqual(TEXT("Trailing slash is deterministic"), NoSlash, WithSlash);
	TestEqual(TEXT("Route URL"), NoSlash, TEXT("https://example.test/api/missions/sessions"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportBuildsBearerHeaderWithoutLoggingTokenTest,
	"Eden.Unit.EdenOs.Transport.BuildsBearerHeaderWithoutLoggingToken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportBuildsBearerHeaderWithoutLoggingTokenTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());

	TestTrue(TEXT("Enqueue succeeds"), Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"schemaVersion\":1}"))).IsSuccess());
	TestEqual(TEXT("One request sent"), Transport.SentRequests.Num(), 1);
	TestEqual(TEXT("Bearer token passed to transport only"), Transport.SentRequests[0].AuthorizationBearerJwt, TEXT("test-token"));
	TestTrue(TEXT("Request timeout maps to HTTP timeout"), FMath::IsNearlyEqual(Transport.SentRequests[0].TimeoutSeconds, 5.0f));
	TestFalse(TEXT("Snapshot does not expose token"), Adapter->GetConnectionSnapshot().LastErrorSummary.Contains(TEXT("test-token")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportQueuePreservesOrderTest,
	"Eden.Unit.EdenOs.Transport.QueuePreservesOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportQueuePreservesOrderTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	Transport.bDelayCompletion = true;
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());

	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"A\"}")));
	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"B\"}")));
	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"C\"}")));

	TestEqual(TEXT("Only first starts while in flight"), Transport.SentRequests.Num(), 1);
	TestTrue(TEXT("A sent first"), Transport.SentRequests[0].BodyJson.Contains(TEXT("\"A\"")));
	Transport.CompleteNext(FEdenOsHttpResult::Succeeded(204));
	TestEqual(TEXT("Second starts after first completes"), Transport.SentRequests.Num(), 2);
	TestTrue(TEXT("B sent second"), Transport.SentRequests[1].BodyJson.Contains(TEXT("\"B\"")));
	Transport.CompleteNext(FEdenOsHttpResult::Succeeded(204));
	TestEqual(TEXT("Third starts after second completes"), Transport.SentRequests.Num(), 3);
	TestTrue(TEXT("C sent third"), Transport.SentRequests[2].BodyJson.Contains(TEXT("\"C\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportQueueHonorsMaxDepthTest,
	"Eden.Unit.EdenOs.Transport.QueueHonorsMaxDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportQueueHonorsMaxDepthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	Transport.bDelayCompletion = true;
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig(2));

	TestTrue(TEXT("A accepted"), Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"A\"}"))).IsSuccess());
	TestTrue(TEXT("B accepted"), Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"B\"}"))).IsSuccess());
	TestFalse(TEXT("C dropped when max outstanding reached"), Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"C\"}"))).IsSuccess());
	TestEqual(TEXT("Pending includes one in flight and one queued"), Adapter->GetConnectionSnapshot().PendingMessageCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportQueueDropIsObservableTest,
	"Eden.Unit.EdenOs.Transport.QueueDropIsObservable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportQueueDropIsObservableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	Transport.bDelayCompletion = true;
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig(1));

	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"A\"}")));
	const FEdenTelemetrySinkResult Dropped = Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"B\"}")));
	TestFalse(TEXT("Drop reported to sink caller"), Dropped.IsSuccess());
	TestEqual(TEXT("Dropped count"), Adapter->GetConnectionSnapshot().DroppedMessageCount, 1);
	TestTrue(TEXT("Drop summary observable"), Adapter->GetConnectionSnapshot().LastErrorSummary.Contains(TEXT("queue full")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportNon2xxIsFailureTest,
	"Eden.Unit.EdenOs.Transport.Non2xxIsFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportNon2xxIsFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	Transport.Results.Add(FEdenOsHttpResult::Failed(500, TEXT("HTTP status 500")));
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());
	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"schemaVersion\":1}")));

	TestEqual(TEXT("Failure state before any success"), Adapter->GetConnectionSnapshot().ConnectionState, EEdenOsConnectionState::Disconnected);
	TestTrue(TEXT("Failure summary"), Adapter->GetConnectionSnapshot().LastErrorSummary.Contains(TEXT("500")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportNetworkFailureIsFailureTest,
	"Eden.Unit.EdenOs.Transport.NetworkFailureIsFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportNetworkFailureIsFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	Transport.Results.Add(FEdenOsHttpResult::Failed(0, TEXT("offline")));
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());
	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"schemaVersion\":1}")));

	TestEqual(TEXT("Network failure is disconnected"), Adapter->GetConnectionSnapshot().ConnectionState, EEdenOsConnectionState::Disconnected);
	TestTrue(TEXT("Failure summary sanitized"), Adapter->GetConnectionSnapshot().LastErrorSummary.Contains(TEXT("offline")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportSuccessUpdatesConnectionStateTest,
	"Eden.Unit.EdenOs.Transport.SuccessUpdatesConnectionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportSuccessUpdatesConnectionStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	Transport.Results.Add(FEdenOsHttpResult::Succeeded(204));
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());
	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"schemaVersion\":1}")));

	TestEqual(TEXT("Success connects"), Adapter->GetConnectionSnapshot().ConnectionState, EEdenOsConnectionState::Connected);
	TestTrue(TEXT("No error after success"), Adapter->GetConnectionSnapshot().LastErrorSummary.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportFailureUpdatesConnectionStateTest,
	"Eden.Unit.EdenOs.Transport.FailureUpdatesConnectionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportFailureUpdatesConnectionStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	Transport.Results.Add(FEdenOsHttpResult::Succeeded(204));
	Transport.Results.Add(FEdenOsHttpResult::Failed(503, TEXT("HTTP status 503")));
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());
	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"A\"}")));
	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"id\":\"B\"}")));

	TestEqual(TEXT("Failure after success degrades"), Adapter->GetConnectionSnapshot().ConnectionState, EEdenOsConnectionState::Degraded);
	TestTrue(TEXT("Failure summary"), Adapter->GetConnectionSnapshot().LastErrorSummary.Contains(TEXT("503")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsTransportLateCompletionFailsSafelyTest,
	"Eden.Unit.EdenOs.Transport.LateCompletionFailsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsTransportLateCompletionFailsSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	FFakeHttpTransport Transport;
	Transport.bDelayCompletion = true;
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());
	Adapter->EnqueueOutboundRequest(MakeRequest(TEXT("{\"schemaVersion\":1}")));
	TestEqual(TEXT("In flight before deinit"), Adapter->GetConnectionSnapshot().PendingMessageCount, 1);

	Adapter->Deinitialize();
	Transport.CompleteNext(FEdenOsHttpResult::Succeeded(204));
	TestEqual(TEXT("Late completion does not mutate deinitialized snapshot"), Adapter->GetConnectionSnapshot().ConnectionState, EEdenOsConnectionState::Disabled);
	TestEqual(TEXT("No pending after deinit"), Adapter->GetConnectionSnapshot().PendingMessageCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsFailingTransportDoesNotChangeSimulationTest,
	"Eden.Integration.EdenOs.FailingTransportDoesNotChangeSimulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsFailingTransportDoesNotChangeSimulationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	const FResourceProbe DisabledProbe = RunResourceProbe(false);
	const FResourceProbe FailingProbe = RunResourceProbe(true);

	TestTrue(TEXT("Fuel fraction identical"), FMath::IsNearlyEqual(DisabledProbe.FuelFraction, FailingProbe.FuelFraction));
	TestTrue(TEXT("Battery fraction identical"), FMath::IsNearlyEqual(DisabledProbe.BatteryFraction, FailingProbe.BatteryFraction));
	TestTrue(TEXT("Temperature identical"), FMath::IsNearlyEqual(DisabledProbe.TemperatureCelsius, FailingProbe.TemperatureCelsius));
	TestEqual(TEXT("Fuel state identical"), DisabledProbe.FuelState, FailingProbe.FuelState);
	TestEqual(TEXT("Power state identical"), DisabledProbe.PowerState, FailingProbe.PowerState);
	TestEqual(TEXT("Thermal state identical"), DisabledProbe.ThermalState, FailingProbe.ThermalState);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenTelemetryLocalSinkSurvivesEdenSinkFailureTest,
	"Eden.Integration.Telemetry.LocalSinkSurvivesEdenSinkFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenTelemetryLocalSinkSurvivesEdenSinkFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	UEdenTelemetrySubsystem* Telemetry = NewObject<UEdenTelemetrySubsystem>();
	Telemetry->ClearHistory();

	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry"), TEXT("EdenSinkFailure"));
	FEdenLocalJsonTelemetrySink LocalSink(Directory);
	FAlwaysFailingSink EdenFailureSink;

	TestTrue(TEXT("Local registers"), Telemetry->RegisterTelemetrySink(&LocalSink));
	TestTrue(TEXT("Failing EDEN sink registers"), Telemetry->RegisterTelemetrySink(&EdenFailureSink));

	const TArray<FEdenTelemetryEvent> EventsBefore = Telemetry->GetEventHistory();
	const TArray<FEdenTelemetrySnapshot> SnapshotsBefore = Telemetry->GetSnapshotHistory();
	const FEdenTelemetrySinkDeliverySummary Summary = Telemetry->DeliverSessionToRegisteredSinks();

	TestEqual(TEXT("Two sinks attempted"), Summary.AttemptedCount, 2);
	TestEqual(TEXT("Local succeeded"), Summary.SucceededCount, 1);
	TestEqual(TEXT("EDEN failed"), Summary.FailedCount, 1);
	TestTrue(TEXT("Local artifact path returned"), Summary.Records[0].Result.Destination.Contains(TEXT("telemetry_")));
	TestEqual(TEXT("History unchanged events"), Telemetry->GetEventHistory().Num(), EventsBefore.Num());
	TestEqual(TEXT("History unchanged snapshots"), Telemetry->GetSnapshotHistory().Num(), SnapshotsBefore.Num());
	return true;
}

#endif
