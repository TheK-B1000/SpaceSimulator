// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenOs/EdenExternalCommandExecutor.h"
#include "EdenOs/EdenExternalCommandModel.h"
#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsConnectionSettings.h"
#include "EdenOs/EdenOsTransport.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Missions/EdenMissionTypes.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Telemetry/EdenTelemetrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenExternalCommandAutomationTests
{
	FEdenOsConnectionConfig MakeAutomationConfig(
		EEdenOsAuthorityMode AuthorityMode,
		bool bValidation,
		bool bAutomation,
		bool bExecution)
	{
		FEdenOsConnectionConfig Config;
		Config.bEnabled = true;
		Config.BaseUrl = TEXT("https://example.test");
		Config.AuthorityMode = AuthorityMode;
		Config.bExternalCommandValidationEnabled = bValidation;
		Config.bExternalCommandAutomationEnabled = bAutomation;
		Config.bExternalCommandExecutionEnabled = bExecution;
		Config.RuntimeBearerJwt = TEXT("test-token");
		// Keep heartbeat long so fixtures get one advisory→proposal chain unless they retune.
		Config.AdvisoryHeartbeatSimulationSeconds = 60.0f;
		return Config;
	}

	FString ExtractJsonStringField(const FString& Json, const TCHAR* FieldName)
	{
		const FString Key = FString::Printf(TEXT("\"%s\": \""), FieldName);
		const int32 KeyIndex = Json.Find(Key, ESearchCase::CaseSensitive);
		if (KeyIndex == INDEX_NONE)
		{
			const FString CompactKey = FString::Printf(TEXT("\"%s\":\""), FieldName);
			const int32 CompactIndex = Json.Find(CompactKey, ESearchCase::CaseSensitive);
			if (CompactIndex == INDEX_NONE)
			{
				return FString();
			}
			const int32 Start = CompactIndex + CompactKey.Len();
			const int32 End = Json.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
			return End == INDEX_NONE ? FString() : Json.Mid(Start, End - Start);
		}
		const int32 Start = KeyIndex + Key.Len();
		const int32 End = Json.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		return End == INDEX_NONE ? FString() : Json.Mid(Start, End - Start);
	}

	FString ExtractSessionIdFromUrl(const FString& Url)
	{
		const FString Marker = TEXT("/api/missions/sessions/");
		const int32 MarkerIndex = Url.Find(Marker, ESearchCase::CaseSensitive);
		if (MarkerIndex == INDEX_NONE)
		{
			return FString();
		}
		const int32 Start = MarkerIndex + Marker.Len();
		const int32 End = Url.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		return End == INDEX_NONE ? Url.Mid(Start) : Url.Mid(Start, End - Start);
	}

	class FAutomationHttpTransport final : public IEdenOsHttpTransport
	{
	public:
		virtual bool SendAsync(const FEdenOsHttpRequestData& Request, FEdenOsHttpCompletion Completion) override
		{
			SentUrls.Add(Request.Url);
			SentBodies.Add(Request.BodyJson);

			if (Request.Url.Contains(TEXT("/advisories")))
			{
				++AdvisoryRequests;
				const FString EvaluationId = ExtractJsonStringField(Request.BodyJson, TEXT("evaluationId"));
				LastAdvisoryEvaluationId = EvaluationId;
				const FString Body = FString::Printf(
					TEXT(
						"{\"schemaVersion\":1,\"advisoryId\":\"adv-l-1\","
						"\"evaluationId\":\"%s\","
						"\"recommendation\":\"Maintain load shedding.\","
						"\"rationale\":\"Battery and thermal trends support shedding.\"}"),
					*EvaluationId);
				Completion.ExecuteIfBound(FEdenOsHttpResult::Succeeded(201, Body));
				return true;
			}

			if (Request.Url.Contains(TEXT("/command-proposals")))
			{
				++CommandProposalRequests;
				LastCommandProposalEvaluationId = ExtractJsonStringField(Request.BodyJson, TEXT("evaluationId"));
				LastCommandProposalSessionId = ExtractSessionIdFromUrl(Request.Url);
				ProposalId = FString::Printf(TEXT("prop-%s"), *LastCommandProposalEvaluationId);

				if (bDelayCommandProposalCompletion)
				{
					FDeferredCommandProposal Deferred;
					Deferred.Completion = MoveTemp(Completion);
					Deferred.SessionId = LastCommandProposalSessionId;
					Deferred.EvaluationId = LastCommandProposalEvaluationId;
					DeferredCommandProposals.Add(MoveTemp(Deferred));
					return true;
				}

				Completion.ExecuteIfBound(MakeCommandProposalResult(LastCommandProposalSessionId, LastCommandProposalEvaluationId));
				return true;
			}

			Completion.ExecuteIfBound(FEdenOsHttpResult::Succeeded(204));
			return true;
		}

		FEdenOsHttpResult MakeCommandProposalResult(const FString& SessionId, const FString& EvaluationId) const
		{
			if (bReturnNoProposal)
			{
				return FEdenOsHttpResult::Succeeded(204);
			}
			if (bReturnHttpFailure)
			{
				return FEdenOsHttpResult::Failed(500, TEXT("synthetic command proposal failure"));
			}
			if (bReturnMalformedBody)
			{
				return FEdenOsHttpResult::Succeeded(201, TEXT("{not-json"));
			}

			const FString Body = FString::Printf(
				TEXT(
					"{"
					"\"schemaVersion\":1,"
					"\"proposalId\":\"%s\","
					"\"sessionId\":\"%s\","
					"\"evaluationId\":\"%s\","
					"\"commandType\":\"%s\","
					"\"parameters\":{\"mode\":\"%s\"}"
					"}"),
				*ProposalId,
				*SessionId,
				*EvaluationId,
				*CommandTypeWire,
				*ModeWire);
			return FEdenOsHttpResult::Succeeded(bUseIdempotentStatus ? 200 : 201, Body);
		}

		void CompleteDeferredCommandProposals()
		{
			while (!DeferredCommandProposals.IsEmpty())
			{
				FDeferredCommandProposal Deferred = MoveTemp(DeferredCommandProposals[0]);
				DeferredCommandProposals.RemoveAt(0, 1, EAllowShrinking::No);
				Deferred.Completion.ExecuteIfBound(
					MakeCommandProposalResult(Deferred.SessionId, Deferred.EvaluationId));
			}
		}

		struct FDeferredCommandProposal
		{
			FEdenOsHttpCompletion Completion;
			FString SessionId;
			FString EvaluationId;
		};

		TArray<FString> SentUrls;
		TArray<FString> SentBodies;
		TArray<FDeferredCommandProposal> DeferredCommandProposals;
		int32 AdvisoryRequests = 0;
		int32 CommandProposalRequests = 0;
		FString LastAdvisoryEvaluationId;
		FString LastCommandProposalEvaluationId;
		FString LastCommandProposalSessionId;
		FString ProposalId = TEXT("prop-l-1");
		FString CommandTypeWire = TEXT("set_load_shed_mode");
		FString ModeWire = TEXT("shed");
		bool bDelayCommandProposalCompletion = false;
		bool bReturnNoProposal = false;
		bool bReturnHttpFailure = false;
		bool bReturnMalformedBody = false;
		bool bUseIdempotentStatus = false;
	};

	struct FScopedWorld
	{
		FWorldContext* WorldContext = nullptr;
		UWorld* World = nullptr;

		FScopedWorld()
		{
			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("EdenExternalCommandAutomationWorld"),
				EUniqueObjectNameOptions::GloballyUnique);
			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			WorldContext->SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedWorld()
		{
			if (!World)
			{
				return;
			}
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->RemoveFromRoot();
			GEngine->DestroyWorldContext(World);
			World = nullptr;
			WorldContext = nullptr;
		}
	};

	FEdenMissionDefinitionConfig MakeLongRunningMissionDefinition()
	{
		FEdenMissionDefinitionConfig Config;
		Config.MissionId = FName("EdenOsAutomationMission");
		Config.DisplayName = FText::FromString(TEXT("EDEN OS Automation Mission"));

		FEdenMissionObjectiveConfig Survive;
		Survive.ObjectiveId = FName("Survive");
		Survive.ObjectiveType = EEdenObjectiveType::SurviveUntilTime;
		Survive.TargetValue = 30.0f;
		Survive.bRequired = true;
		Survive.bActivateOnStart = true;
		Config.Objectives.Add(Survive);

		FEdenMissionObjectiveConfig KeepCool;
		KeepCool.ObjectiveId = FName("KeepCool");
		KeepCool.ObjectiveType = EEdenObjectiveType::KeepTemperatureBelow;
		KeepCool.TargetValue = 100.0f;
		KeepCool.bRequired = true;
		KeepCool.bActivateOnStart = true;
		Config.Objectives.Add(KeepCool);

		FEdenMissionEventConfig EnterImpact;
		EnterImpact.EventId = FName("EnterImpact");
		EnterImpact.TriggerTimeSeconds = 0.1f;
		EnterImpact.CommandType = EEdenMissionCommandType::SetMissionPhase;
		EnterImpact.PhaseParameter = EEdenMissionPhase::Impact;
		Config.Events.Add(EnterImpact);
		return Config;
	}

	struct FMissionAutomationFixture
	{
		AActor* Owner = nullptr;
		UEdenFuelSystemComponent* Fuel = nullptr;
		UEdenPowerSystemComponent* Power = nullptr;
		UEdenThermalSystemComponent* Thermal = nullptr;
		UEdenFlightMovementComponent* Flight = nullptr;
		UEdenOperatorControlComponent* Operator = nullptr;
		UEdenMissionSubsystem* Mission = nullptr;
		UEdenSimulationClockSubsystem* Clock = nullptr;
		UEdenTelemetrySubsystem* Telemetry = nullptr;
		UEdenOsAdapterSubsystem* Adapter = nullptr;

		explicit FMissionAutomationFixture(UWorld* World)
		{
			Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
			Clock->SetFixedStepSeconds(0.1f);
			Clock->SetMaxCatchUpSteps(8);
			Clock->ResetSimulationClock();

			Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
			Telemetry->ClearHistory();

			Owner = World->SpawnActor<AActor>();
			Fuel = NewObject<UEdenFuelSystemComponent>(Owner);
			Power = NewObject<UEdenPowerSystemComponent>(Owner);
			Thermal = NewObject<UEdenThermalSystemComponent>(Owner);
			Flight = NewObject<UEdenFlightMovementComponent>(Owner);
			Operator = NewObject<UEdenOperatorControlComponent>(Owner);
			Fuel->RegisterComponent();
			Power->RegisterComponent();
			Thermal->RegisterComponent();
			Flight->RegisterComponent();
			Operator->RegisterComponent();

			FEdenFuelConfig FuelConfig;
			FuelConfig.CapacityKilograms = 100.0f;
			FuelConfig.ConsumptionRateKilogramsPerSecond = 0.0f;
			FuelConfig.InitialFuelFraction = 1.0f;
			Fuel->InitializeFuelSimulation(FuelConfig);
			Fuel->RegisterWithSimulationClock();

			FEdenPowerConfig PowerConfig;
			PowerConfig.BatteryCapacityKilowattHours = 10.0f;
			PowerConfig.GenerationKilowatts = 1.0f;
			PowerConfig.BaselineDemandKilowatts = 2.0f;
			PowerConfig.InitialChargeFraction = 1.0f;
			Power->InitializePowerSimulation(PowerConfig);
			Power->RegisterWithSimulationClock();

			FEdenThermalConfig ThermalConfig;
			ThermalConfig.AbsoluteMinTemperatureCelsius = -50.0f;
			ThermalConfig.AmbientTemperatureCelsius = 20.0f;
			ThermalConfig.WarningTemperatureCelsius = 70.0f;
			ThermalConfig.CriticalTemperatureCelsius = 100.0f;
			ThermalConfig.AbsoluteMaxTemperatureCelsius = 120.0f;
			ThermalConfig.InitialTemperatureCelsius = 20.0f;
			ThermalConfig.HeatGenerationDegreesCelsiusPerSecond = 1.0f;
			ThermalConfig.DissipationDegreesCelsiusPerSecond = 0.5f;
			Thermal->InitializeThermalSimulation(ThermalConfig);
			Thermal->RegisterWithSimulationClock();

			FEdenOperatorControlConfig OpConfig;
			OpConfig.BoostDissipationDegreesCelsiusPerSecond = 1.0f;
			OpConfig.EmergencyDissipationDegreesCelsiusPerSecond = 2.0f;
			OpConfig.BoostCoolingDemandKilowatts = 1.5f;
			OpConfig.EmergencyCoolingDemandKilowatts = 4.5f;
			OpConfig.LoadShedDemandReductionKilowatts = 2.0f;
			OpConfig.LoadShedDissipationReductionDegreesCelsiusPerSecond = 0.4f;
			OpConfig.ReducedThrustAuthority = 0.5f;
			Operator->InitializeOperatorControl(OpConfig);
			Telemetry->BindOperatorControlForTesting(Operator);

			Mission = World->GetSubsystem<UEdenMissionSubsystem>();
			Mission->SetMissionResourceTargets(Thermal, Power, Fuel);
			Mission->LoadMission(MakeLongRunningMissionDefinition());
			Mission->StartMission();

			Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
		}

		void TickSteps(int32 Count)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Clock->Tick(0.1f);
			}
		}
	};

	bool UrlsContain(const TArray<FString>& Urls, const TCHAR* Needle)
	{
		for (const FString& Url : Urls)
		{
			if (Url.Contains(Needle))
			{
				return true;
			}
		}
		return false;
	}

	int32 CountEvents(const TArray<FEdenTelemetryEvent>& History, EEdenTelemetryEventType Type)
	{
		int32 Count = 0;
		for (const FEdenTelemetryEvent& Event : History)
		{
			if (Event.EventType == Type)
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationDisabledByDefaultTest,
	"Eden.Unit.EdenOs.ExternalCommand.Automation.AutomationDisabledByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationDisabledByDefaultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("Config default"), FEdenOsConnectionConfig().bExternalCommandAutomationEnabled);
	TestFalse(TEXT("Settings default"), GetDefault<UEdenOsConnectionSettings>()->bExternalCommandAutomationEnabled);
	TestFalse(
		TEXT("Snapshot default"),
		FEdenOsConnectionConfigModel::MakeInitialSnapshot(FEdenOsConnectionConfig(), FEdenOsValidationResult())
			.bExternalCommandAutomationEnabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationObserveDoesNotRequestTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.ObserveDoesNotRequestCommandProposal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationObserveDoesNotRequestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::Observe, true, true, true));
	Fixture.TickSteps(12);

	TestEqual(TEXT("Observe skips advisory"), Transport.AdvisoryRequests, 0);
	TestEqual(TEXT("Observe skips command proposal"), Transport.CommandProposalRequests, 0);
	TestFalse(TEXT("No command-proposals route"), UrlsContain(Transport.SentUrls, TEXT("/command-proposals")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationAdvisoryDoesNotRequestTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.AdvisoryDoesNotRequestCommandProposal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationAdvisoryDoesNotRequestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::Advisory, true, true, true));
	Fixture.TickSteps(12);

	TestTrue(TEXT("Advisory mode still requests advisories"), Transport.AdvisoryRequests > 0);
	TestEqual(TEXT("Advisory mode does not request command proposals"), Transport.CommandProposalRequests, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationAuthorizedWithoutAutomationFlagTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.AuthorizedControlDoesNotRequestWhenAutomationDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationAuthorizedWithoutAutomationFlagTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, false, true));
	Fixture.TickSteps(12);

	TestTrue(TEXT("AuthorizedControl still accepts advisories"), Transport.AdvisoryRequests > 0);
	TestEqual(TEXT("Automation off blocks proposal HTTP"), Transport.CommandProposalRequests, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationValidationDisabledPreventsRequestTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.ValidationDisabledPreventsAutomationRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationValidationDisabledPreventsRequestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, false, true, true));
	Fixture.TickSteps(12);

	TestTrue(TEXT("Advisories still flow"), Transport.AdvisoryRequests > 0);
	TestEqual(TEXT("Validation off blocks proposal HTTP"), Transport.CommandProposalRequests, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationAcceptedAdvisoryRequestsMatchingProposalTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.AcceptedAdvisoryCanRequestMatchingCommandProposal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationAcceptedAdvisoryRequestsMatchingProposalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Transport.bReturnNoProposal = true;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, false));
	Fixture.TickSteps(12);

	TestTrue(TEXT("Advisory accepted"), Fixture.Adapter->GetLatestAcceptedAdvisory().bIsValid);
	TestTrue(TEXT("Command proposal requested"), Transport.CommandProposalRequests > 0);
	TestEqual(
		TEXT("Proposal evaluation matches accepted advisory"),
		Transport.LastCommandProposalEvaluationId,
		Fixture.Adapter->GetLatestAcceptedAdvisory().EvaluationId);
	TestTrue(
		TEXT("Route uses command-proposals template"),
		UrlsContain(Transport.SentUrls, TEXT("/command-proposals")));
	TestTrue(
		TEXT("Request body carries evaluationId only payload shape"),
		Transport.SentBodies.Last().Contains(TEXT("\"evaluationId\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationNoProposal204DoesNotMutateTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.NoProposal204DoesNotMutateSimulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationNoProposal204DoesNotMutateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Transport.bReturnNoProposal = true;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, true));
	Fixture.TickSteps(12);

	TestEqual(TEXT("Load shed remains Normal"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Normal);
	TestEqual(
		TEXT("No executed telemetry"),
		CountEvents(Fixture.Telemetry->GetEventHistory(), EEdenTelemetryEventType::EdenExternalCommandExecuted),
		0);
	TestEqual(TEXT("No validation history from 204"), Fixture.Adapter->GetExternalCommandValidationHistory().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationHttpFailureDoesNotMutateTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.HTTPFailureDoesNotMutateSimulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationHttpFailureDoesNotMutateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Transport.bReturnHttpFailure = true;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, true));
	Fixture.TickSteps(12);

	TestEqual(TEXT("Load shed unchanged"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Normal);
	TestEqual(
		TEXT("No executed telemetry"),
		CountEvents(Fixture.Telemetry->GetEventHistory(), EEdenTelemetryEventType::EdenExternalCommandExecuted),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationMalformedResponseDoesNotMutateTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.MalformedResponseDoesNotMutateSimulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationMalformedResponseDoesNotMutateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Transport.bReturnMalformedBody = true;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, true));
	Fixture.TickSteps(12);

	TestEqual(TEXT("Load shed unchanged"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Normal);
	TestEqual(
		TEXT("No executed telemetry"),
		CountEvents(Fixture.Telemetry->GetEventHistory(), EEdenTelemetryEventType::EdenExternalCommandExecuted),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationDryRunValidatesButDoesNotExecuteTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.DryRunCanValidateButDoesNotExecute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationDryRunValidatesButDoesNotExecuteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, false));
	Fixture.TickSteps(12);

	TestTrue(TEXT("Proposal HTTP happened"), Transport.CommandProposalRequests > 0);
	TestTrue(TEXT("J validation recorded"), Fixture.Adapter->GetExternalCommandValidationHistory().Num() > 0);
	TestEqual(
		TEXT("Validation was Valid"),
		Fixture.Adapter->GetExternalCommandValidationHistory().Last().Status,
		EEdenExternalCommandValidationStatus::Valid);
	TestEqual(TEXT("No execution history"), Fixture.Adapter->GetExternalCommandExecutionHistory().Num(), 0);
	TestEqual(TEXT("Load shed unchanged"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Normal);
	TestEqual(
		TEXT("No executed telemetry"),
		CountEvents(Fixture.Telemetry->GetEventHistory(), EEdenTelemetryEventType::EdenExternalCommandExecuted),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationDryRunNotDeferredWhenExecutionEnabledLaterTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.DryRunProposalIsNotExecutedWhenExecutionEnabledLater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationDryRunNotDeferredWhenExecutionEnabledLaterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);

	FEdenOsConnectionConfig DryRun =
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, false);
	Fixture.Adapter->ApplyRuntimeConfig(DryRun);
	Fixture.TickSteps(12);
	const int32 ProposalRequestsAfterDryRun = Transport.CommandProposalRequests;
	TestTrue(TEXT("Dry-run requested proposal"), ProposalRequestsAfterDryRun > 0);
	TestTrue(TEXT("Dry-run validated"), Fixture.Adapter->GetExternalCommandValidationHistory().Num() > 0);
	TestEqual(TEXT("Dry-run did not execute"), Fixture.Adapter->GetExternalCommandExecutionHistory().Num(), 0);
	TestEqual(TEXT("Load shed still Normal after dry-run"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Normal);

	// Enable execution later but keep automation off so no fresh proposal is fetched.
	// The prior dry-run artifact must not be queued for deferred execute.
	FEdenOsConnectionConfig EnableExecutionNoAutomation = DryRun;
	EnableExecutionNoAutomation.bExternalCommandExecutionEnabled = true;
	EnableExecutionNoAutomation.bExternalCommandAutomationEnabled = false;
	Fixture.Adapter->ApplyRuntimeConfig(EnableExecutionNoAutomation);
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.TickSteps(12);
	TestEqual(TEXT("Still no execution after enabling"), Fixture.Adapter->GetExternalCommandExecutionHistory().Num(), 0);
	TestEqual(TEXT("Load shed still Normal"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Normal);
	TestEqual(
		TEXT("No additional proposal HTTP after automation disabled"),
		Transport.CommandProposalRequests,
		ProposalRequestsAfterDryRun);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationAllGatesReachKExecutionTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.AllGatesEnabledCanReachKExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationAllGatesReachKExecutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, true));
	Fixture.TickSteps(12);

	TestTrue(TEXT("Proposal requested"), Transport.CommandProposalRequests > 0);
	TestEqual(TEXT("Load shed converged"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Shed);
	TestTrue(TEXT("Execution history present"), Fixture.Adapter->GetExternalCommandExecutionHistory().Num() > 0);
	TestEqual(
		TEXT("Exactly one Executed telemetry"),
		CountEvents(Fixture.Telemetry->GetEventHistory(), EEdenTelemetryEventType::EdenExternalCommandExecuted),
		1);
	TestEqual(
		TEXT("No human operator_action telemetry"),
		CountEvents(Fixture.Telemetry->GetEventHistory(), EEdenTelemetryEventType::OperatorCommandIssued),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationOneRequestPerEvaluationTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.OneAutomaticRequestPerEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationOneRequestPerEvaluationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Transport.bReturnNoProposal = true;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, false));
	Fixture.TickSteps(20);

	TestTrue(TEXT("At least one proposal request"), Transport.CommandProposalRequests >= 1);
	TestEqual(
		TEXT("Requested evaluation set size matches unique requests"),
		Fixture.Adapter->GetRequestedCommandProposalEvaluationCountForTesting(),
		Transport.CommandProposalRequests);
	TestTrue(
		TEXT("First evaluation marked requested"),
		Fixture.Adapter->HasRequestedCommandProposalForEvaluationForTesting(Transport.LastCommandProposalEvaluationId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationStaleEvaluationCannotActTest,
	"Eden.Integration.EdenOs.ExternalCommand.Automation.StaleEvaluationResponseCannotAct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationStaleEvaluationCannotActTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandAutomationTests;

	FScopedWorld Scoped;
	FMissionAutomationFixture Fixture(Scoped.World);
	FAutomationHttpTransport Transport;
	Transport.bDelayCommandProposalCompletion = true;
	Fixture.Adapter->SetHttpTransportForTesting(&Transport);
	Fixture.Adapter->ApplyRuntimeConfig(
		MakeAutomationConfig(EEdenOsAuthorityMode::AuthorizedControl, true, true, true));

	Fixture.TickSteps(8);
	TestTrue(TEXT("First proposal in flight"), Transport.CommandProposalRequests >= 1);
	TestEqual(TEXT("Exactly one deferred proposal"), Transport.DeferredCommandProposals.Num(), 1);
	const FString FirstEval = Transport.LastCommandProposalEvaluationId;
	TestFalse(TEXT("First eval not empty"), FirstEval.IsEmpty());

	// Force a newer accepted advisory while the first proposal is still deferred.
	// Long heartbeat keeps further advisory evaluations from overwriting this during completion.
	FEdenOsAcceptedAdvisory Newer = Fixture.Adapter->GetLatestAcceptedAdvisory();
	Newer.EvaluationId = TEXT("eval-stale-guard-newer");
	Fixture.Adapter->SetLatestAcceptedAdvisoryForTesting(Newer);

	Transport.CompleteDeferredCommandProposals();
	TestEqual(TEXT("Stale response did not mutate"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Normal);
	TestEqual(
		TEXT("Stale response did not execute"),
		CountEvents(Fixture.Telemetry->GetEventHistory(), EEdenTelemetryEventType::EdenExternalCommandExecuted),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationWireProposalMustPassThroughJTest,
	"Eden.Unit.EdenOs.ExternalCommand.Automation.WireProposalMustPassThroughJ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationWireProposalMustPassThroughJTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString Body = TEXT(
		"{"
		"\"schemaVersion\":1,"
		"\"proposalId\":\"prop-j\","
		"\"sessionId\":\"session-j\","
		"\"evaluationId\":\"eval-j\","
		"\"commandType\":\"set_load_shed_mode\","
		"\"parameters\":{\"mode\":\"shed\"}"
		"}");
	const FEdenOsCommandProposalResponseParseResult Parsed =
		FEdenOsWireSerializationModel::ParseCommandProposalResponseV1(201, Body, TEXT("session-j"), TEXT("eval-j"));
	TestTrue(TEXT("Parse yields proposal"), Parsed.HasProposal());

	// Network parse never yields FEdenValidatedExternalCommand; caller must invoke J.
	FEdenExternalCommandValidationContext Context;
	Context.bExternalCommandValidationEnabled = false;
	Context.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	Context.ActiveSessionId = TEXT("session-j");
	Context.bHasAcceptedEvaluation = true;
	Context.LatestAcceptedEvaluationId = TEXT("eval-j");
	const FEdenExternalCommandValidationOutcome Outcome =
		FEdenExternalCommandModel::ValidateProposal(Parsed.Proposal, Context, false);
	TestFalse(TEXT("Without J gates, proposal is not valid"), Outcome.IsValid());
	TestFalse(TEXT("No validated artifact without J"), Outcome.bHasValidatedCommand);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAutomationExactModeWireMappingsTest,
	"Eden.Unit.EdenOs.ExternalCommand.Automation.ExactModeWireMappings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAutomationExactModeWireMappingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const auto ParseMode = [](const TCHAR* Command, const TCHAR* Mode)
	{
		const FString Body = FString::Printf(
			TEXT(
				"{\"schemaVersion\":1,\"proposalId\":\"p\",\"sessionId\":\"s\",\"evaluationId\":\"e\","
				"\"commandType\":\"%s\",\"parameters\":{\"mode\":\"%s\"}}"),
			Command,
			Mode);
		return FEdenOsWireSerializationModel::ParseCommandProposalResponseV1(200, Body, TEXT("s"), TEXT("e"));
	};

	TestEqual(TEXT("thermal off"), ParseMode(TEXT("set_thermal_control_mode"), TEXT("off")).Proposal.Parameters.ThermalMode, EEdenThermalControlMode::Off);
	TestEqual(TEXT("thermal nominal"), ParseMode(TEXT("set_thermal_control_mode"), TEXT("nominal")).Proposal.Parameters.ThermalMode, EEdenThermalControlMode::Nominal);
	TestEqual(TEXT("thermal boost"), ParseMode(TEXT("set_thermal_control_mode"), TEXT("boost")).Proposal.Parameters.ThermalMode, EEdenThermalControlMode::Boost);
	TestEqual(TEXT("thermal emergency"), ParseMode(TEXT("set_thermal_control_mode"), TEXT("emergency")).Proposal.Parameters.ThermalMode, EEdenThermalControlMode::Emergency);
	TestEqual(TEXT("load normal"), ParseMode(TEXT("set_load_shed_mode"), TEXT("normal")).Proposal.Parameters.LoadShedMode, EEdenLoadShedMode::Normal);
	TestEqual(TEXT("load shed"), ParseMode(TEXT("set_load_shed_mode"), TEXT("shed")).Proposal.Parameters.LoadShedMode, EEdenLoadShedMode::Shed);
	TestEqual(TEXT("prop full"), ParseMode(TEXT("set_propulsion_priority_mode"), TEXT("full")).Proposal.Parameters.PropulsionPriorityMode, EEdenPropulsionPriorityMode::Full);
	TestEqual(TEXT("prop reduced"), ParseMode(TEXT("set_propulsion_priority_mode"), TEXT("reduced")).Proposal.Parameters.PropulsionPriorityMode, EEdenPropulsionPriorityMode::Reduced);
	return true;
}

#endif
