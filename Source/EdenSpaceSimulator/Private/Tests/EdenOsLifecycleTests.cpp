// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsMissionLifecycle.h"
#include "EdenOs/EdenOsTelemetrySink.h"
#include "EdenOs/EdenOsTransport.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Telemetry/EdenTelemetryExportModel.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenOsLifecycleTests
{
	class FFakeHttpTransport final : public IEdenOsHttpTransport
	{
	public:
		virtual bool SendAsync(const FEdenOsHttpRequestData& Request, FEdenOsHttpCompletion Completion) override
		{
			SentRequests.Add(Request);
			Completion.ExecuteIfBound(FEdenOsHttpResult::Succeeded(202));
			return true;
		}

		TArray<FEdenOsHttpRequestData> SentRequests;
	};

	FEdenOsConnectionConfig MakeEnabledConfig()
	{
		FEdenOsConnectionConfig Config;
		Config.bEnabled = true;
		Config.BaseUrl = TEXT("https://eden.test");
		Config.DefaultScenarioId = TEXT("SolarEventEmergency");
		Config.ConnectionTimeoutSeconds = 2.0f;
		Config.RequestTimeoutSeconds = 5.0f;
		Config.MaxQueueDepth = 32;
		Config.AdvisoryHeartbeatSimulationSeconds = 5.0f;
		Config.RuntimeBearerJwt = TEXT("test-token");
		return Config;
	}

	FEdenTelemetryEvent MakeEvent(int64 Sequence, EEdenTelemetryEventType Type, FName EventId)
	{
		FEdenTelemetryEvent Event;
		Event.SequenceNumber = Sequence;
		Event.SimulationTimeSeconds = static_cast<float>(Sequence);
		Event.MissionElapsedTimeSeconds = static_cast<float>(Sequence);
		Event.EventType = Type;
		Event.SourceSystem = TEXT("Mission");
		Event.EventId = EventId;
		Event.Detail = FString::Printf(TEXT("Event %s"), *EventId.ToString());
		return Event;
	}

	FEdenTelemetrySessionPayload MakeLifecyclePayload(
		const FString& SessionId = TEXT("mission-session-f"),
		bool bTerminal = true)
	{
		static TArray<FEdenTelemetryEvent> Events;
		static TArray<FEdenTelemetrySnapshot> Snapshots;
		Events.Reset();
		Snapshots.Reset();

		Events.Add(MakeEvent(1, EEdenTelemetryEventType::MissionStarted, TEXT("Running")));
		Events.Add(MakeEvent(2, EEdenTelemetryEventType::AlertRaised, TEXT("ThermalWarning")));
		Events.Add(MakeEvent(3, EEdenTelemetryEventType::PhaseChanged, TEXT("Impact")));
		if (bTerminal)
		{
			Events.Add(MakeEvent(4, EEdenTelemetryEventType::MissionSucceeded, TEXT("Succeeded")));
		}

		FEdenTelemetrySnapshot Snapshot;
		Snapshot.SequenceNumber = 5;
		Snapshot.SimulationTimeSeconds = 5.0f;
		Snapshot.MissionElapsedTimeSeconds = 5.0f;
		Snapshot.Mission.ActiveMissionId = TEXT("SolarCrisis");
		Snapshot.Mission.MissionState = bTerminal ? EEdenMissionState::Succeeded : EEdenMissionState::Running;
		Snapshot.Mission.MissionPhase = EEdenMissionPhase::Resolved;
		Snapshot.Fuel.FuelFraction = 0.91f;
		Snapshot.Fuel.FuelQuantityKilograms = 91.0f;
		Snapshot.Power.ChargeFraction = 0.84f;
		Snapshot.Power.BatteryChargeKilowattHours = 8.4f;
		Snapshot.Power.GenerationKilowatts = 2.0f;
		Snapshot.Power.TotalDemandKilowatts = 1.0f;
		Snapshot.Thermal.TemperatureCelsius = 42.0f;
		Snapshots.Add(Snapshot);

		FEdenTelemetrySessionMetadata Metadata;
		Metadata.FirstAvailableSequence = 1;
		Metadata.LastAvailableSequence = 5;
		Metadata.SnapshotIntervalSeconds = 0.5f;
		Metadata.PeakTemperatureCelsius = 42.0f;
		Metadata.MinimumBatteryChargeFraction = 0.84f;
		Metadata.MinimumFuelFraction = 0.91f;

		return FEdenTelemetrySessionPayload(Events, Snapshots, Metadata, SessionId, TEXT("SolarCrisis"));
	}

	FString RouteFromUrl(const FString& Url)
	{
		const FString Prefix = TEXT("https://eden.test");
		return Url.StartsWith(Prefix) ? Url.RightChop(Prefix.Len()) : Url;
	}

	bool WriteProjectEdenRequestArtifact(const TArray<FEdenOsHttpRequestData>& Requests)
	{
		FString Json;
		Json += TEXT("[\n");
		for (int32 Index = 0; Index < Requests.Num(); ++Index)
		{
			const FEdenOsHttpRequestData& Request = Requests[Index];
			FString Body = Request.BodyJson;
			Body.TrimEndInline();
			Json += TEXT("  {\n");
			Json += FString::Printf(
				TEXT("    \"routePath\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(RouteFromUrl(Request.Url)));
			Json += TEXT("    \"body\": ");
			Json += Body;
			Json += TEXT("\n  }");
			Json += Index + 1 < Requests.Num() ? TEXT(",\n") : TEXT("\n");
		}
		Json += TEXT("]\n");

		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, TEXT("EdenOsMissionLifecycleRequests.json"));
		return FFileHelper::SaveStringToFile(Json, *Path);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsLifecycleFinalStatusModelTest,
	"Eden.Unit.EdenOs.Lifecycle.FinalStatusModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsLifecycleFinalStatusModelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsLifecycleTests;

	EEdenOsMissionFinalStatus Status = EEdenOsMissionFinalStatus::Failed;
	TestTrue(TEXT("Succeeded terminal status resolves"), FEdenOsMissionLifecycleModel::ResolveFinalStatus(MakeLifecyclePayload(), Status));
	TestEqual(TEXT("Status maps to succeeded"), Status, EEdenOsMissionFinalStatus::Succeeded);

	TestFalse(TEXT("Running payload is not terminal"), FEdenOsMissionLifecycleModel::ResolveFinalStatus(MakeLifecyclePayload(TEXT("mission-session-running"), false), Status));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsLifecycleEmitsProjectEdenRouteSequenceTest,
	"Eden.Integration.EdenOs.SessionLifecycleEmitsProjectEdenRouteSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsLifecycleEmitsProjectEdenRouteSequenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsLifecycleTests;

	FFakeHttpTransport Transport;
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());

	FEdenOsTelemetrySink Sink(*Adapter);
	const FEdenTelemetrySinkResult Result = Sink.DeliverTelemetrySession(MakeLifecyclePayload());

	TestTrue(TEXT("Lifecycle delivery succeeds"), Result.IsSuccess());
	TestEqual(TEXT("Create + telemetry + four events + complete"), Transport.SentRequests.Num(), 7);
	TestEqual(TEXT("Create route first"), RouteFromUrl(Transport.SentRequests[0].Url), FString(EdenOsWireContract::CreateSessionRoute));
	TestEqual(TEXT("Telemetry route second"), RouteFromUrl(Transport.SentRequests[1].Url), TEXT("/api/missions/sessions/mission-session-f/telemetry"));
	TestEqual(TEXT("First event route"), RouteFromUrl(Transport.SentRequests[2].Url), TEXT("/api/missions/sessions/mission-session-f/events"));
	TestEqual(TEXT("Complete route last"), RouteFromUrl(Transport.SentRequests.Last().Url), TEXT("/api/missions/sessions/mission-session-f/complete"));
	TestTrue(TEXT("Create carries scenarioId"), Transport.SentRequests[0].BodyJson.Contains(TEXT("\"scenarioId\": \"SolarEventEmergency\"")));
	TestTrue(TEXT("Telemetry carries canonical body"), Transport.SentRequests[1].BodyJson.Contains(TEXT("\"telemetry\": {")));
	TestTrue(TEXT("Event carries eventId"), Transport.SentRequests[2].BodyJson.Contains(TEXT("\"eventId\": \"Running\"")));
	TestTrue(TEXT("Complete carries final status"), Transport.SentRequests.Last().BodyJson.Contains(TEXT("\"finalStatus\": \"succeeded\"")));
	TestTrue(TEXT("Complete carries real completedAt field"), Transport.SentRequests.Last().BodyJson.Contains(TEXT("\"completedAt\"")));
	TestFalse(TEXT("Complete does not fabricate ticks"), Transport.SentRequests.Last().BodyJson.Contains(TEXT("\"ticks\"")));
	TestTrue(TEXT("Lifecycle artifact written"), WriteProjectEdenRequestArtifact(Transport.SentRequests));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsLifecycleOmitsCompletionUntilTerminalFactTest,
	"Eden.Unit.EdenOs.Lifecycle.OmitsCompletionUntilTerminalFact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsLifecycleOmitsCompletionUntilTerminalFactTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsLifecycleTests;

	FFakeHttpTransport Transport;
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());

	FEdenOsTelemetrySink Sink(*Adapter);
	TestTrue(TEXT("Running delivery succeeds"), Sink.DeliverTelemetrySession(MakeLifecyclePayload(TEXT("mission-running"), false)).IsSuccess());
	TestEqual(TEXT("Create + telemetry + three events only"), Transport.SentRequests.Num(), 5);
	TestFalse(TEXT("No complete route"), RouteFromUrl(Transport.SentRequests.Last().Url).Contains(TEXT("/complete")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsLifecycleDoesNotReplayDeliveredFactsTest,
	"Eden.Unit.EdenOs.Lifecycle.DoesNotReplayDeliveredFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsLifecycleDoesNotReplayDeliveredFactsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsLifecycleTests;

	FFakeHttpTransport Transport;
	UEdenOsAdapterSubsystem* Adapter = NewObject<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig());

	FEdenOsTelemetrySink Sink(*Adapter);
	TestTrue(TEXT("First delivery succeeds"), Sink.DeliverTelemetrySession(MakeLifecyclePayload()).IsSuccess());
	const int32 FirstRequestCount = Transport.SentRequests.Num();
	TestTrue(TEXT("Repeated delivery succeeds"), Sink.DeliverTelemetrySession(MakeLifecyclePayload()).IsSuccess());
	TestEqual(TEXT("No duplicate create/telemetry/events/complete"), Transport.SentRequests.Num(), FirstRequestCount);
	return true;
}

#endif
