// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsMissionLifecycle.h"
#include "EdenOs/EdenOsTelemetrySink.h"
#include "EdenOs/EdenOsTransport.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Telemetry/EdenTelemetryExportModel.h"
#include "Telemetry/EdenTelemetrySink.h"
#include "Telemetry/EdenTelemetrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

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

	struct FScopedEdenOsMissionWorld
	{
		FWorldContext* WorldContext = nullptr;
		UWorld* World = nullptr;

		FScopedEdenOsMissionWorld()
		{
			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("EdenOsMissionIsolationWorld"),
				EUniqueObjectNameOptions::GloballyUnique);

			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			WorldContext->SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedEdenOsMissionWorld()
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

	FEdenMissionDefinitionConfig MakeIsolationMissionDefinition()
	{
		FEdenMissionDefinitionConfig Config;
		Config.MissionId = FName("EdenOsIsolationMission");
		Config.DisplayName = FText::FromString(TEXT("EDEN OS Isolation Mission"));

		FEdenMissionObjectiveConfig Survive;
		Survive.ObjectiveId = FName("Survive");
		Survive.ObjectiveType = EEdenObjectiveType::SurviveUntilTime;
		Survive.TargetValue = 0.5f;
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

		FEdenMissionObjectiveConfig RestorePower;
		RestorePower.ObjectiveId = FName("RestorePower");
		RestorePower.ObjectiveType = EEdenObjectiveType::RestorePowerAbove;
		RestorePower.TargetValue = 0.2f;
		RestorePower.bRequired = true;
		RestorePower.bActivateOnStart = true;
		Config.Objectives.Add(RestorePower);

		FEdenMissionObjectiveConfig ConserveFuel;
		ConserveFuel.ObjectiveId = FName("ConserveFuel");
		ConserveFuel.ObjectiveType = EEdenObjectiveType::MaintainFuelAbove;
		ConserveFuel.TargetValue = 0.2f;
		ConserveFuel.bRequired = true;
		ConserveFuel.bActivateOnStart = true;
		Config.Objectives.Add(ConserveFuel);

		FEdenMissionEventConfig EnterWarning;
		EnterWarning.EventId = FName("EnterWarning");
		EnterWarning.TriggerTimeSeconds = 0.1f;
		EnterWarning.CommandType = EEdenMissionCommandType::SetMissionPhase;
		EnterWarning.PhaseParameter = EEdenMissionPhase::Warning;
		Config.Events.Add(EnterWarning);

		FEdenMissionEventConfig AddHeating;
		AddHeating.EventId = FName("SolarHeating");
		AddHeating.TriggerTimeSeconds = 0.1f;
		AddHeating.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
		AddHeating.FloatParameter = 3.0f;
		Config.Events.Add(AddHeating);

		FEdenMissionEventConfig AddPowerDemand;
		AddPowerDemand.EventId = FName("PowerDemand");
		AddPowerDemand.TriggerTimeSeconds = 0.2f;
		AddPowerDemand.CommandType = EEdenMissionCommandType::SetExternalPowerDemand;
		AddPowerDemand.FloatParameter = 2.0f;
		Config.Events.Add(AddPowerDemand);

		FEdenMissionEventConfig EnterImpact;
		EnterImpact.EventId = FName("EnterImpact");
		EnterImpact.TriggerTimeSeconds = 0.2f;
		EnterImpact.CommandType = EEdenMissionCommandType::SetMissionPhase;
		EnterImpact.PhaseParameter = EEdenMissionPhase::Impact;
		Config.Events.Add(EnterImpact);

		return Config;
	}

	struct FMissionIsolationProbe
	{
		EEdenMissionState MissionState = EEdenMissionState::Inactive;
		EEdenMissionPhase MissionPhase = EEdenMissionPhase::Nominal;
		float MissionElapsedTimeSeconds = 0.0f;
		float ClockElapsedTimeSeconds = 0.0f;
		int32 ClockLastDroppedSteps = 0;
		TArray<FEdenMissionEventRuntime> EventStates;
		TArray<FEdenMissionObjectiveRuntime> ObjectiveStates;
		FEdenFuelStateSnapshot Fuel;
		FEdenPowerStateSnapshot Power;
		FEdenThermalStateSnapshot Thermal;
		int32 TelemetryEventCount = 0;
		int32 TelemetrySnapshotCount = 0;
		int32 RegisteredSinkCountBeforeDelivery = 0;
		int32 TelemetryDeliveryAttempts = 0;
		int32 EdenTransportAttempts = 0;
		EEdenOsConnectionState EdenConnectionState = EEdenOsConnectionState::Disabled;
		FString EdenLastErrorSummary;
	};

	FMissionIsolationProbe RunMissionIsolationProbe(bool bEnableFailingEden)
	{
		FScopedEdenOsMissionWorld ScopedWorld;
		UWorld* World = ScopedWorld.World;

		UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
		Clock->SetFixedStepSeconds(0.1f);
		Clock->SetMaxCatchUpSteps(8);
		Clock->ResetSimulationClock();

		UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
		Telemetry->ClearHistory();

		AActor* Owner = World->SpawnActor<AActor>();
		UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>(Owner);
		Fuel->RegisterComponent();
		Fuel->InitializeFuelSimulation(MakeFuelConfig());
		Fuel->RegisterWithSimulationClock();

		UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>(Owner);
		Power->RegisterComponent();
		Power->InitializePowerSimulation(MakePowerConfig());
		Power->RegisterWithSimulationClock();

		UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>(Owner);
		Thermal->RegisterComponent();
		Thermal->InitializeThermalSimulation(MakeThermalConfig());
		Thermal->RegisterWithSimulationClock();

		UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
		Mission->SetMissionResourceTargets(Thermal, Power, Fuel);
		Mission->LoadMission(MakeIsolationMissionDefinition());
		Mission->StartMission();

		FFakeHttpTransport Transport;
		UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
		if (bEnableFailingEden)
		{
			for (int32 Index = 0; Index < 32; ++Index)
			{
				Transport.Results.Add(FEdenOsHttpResult::Failed(0, TEXT("offline")));
			}
			Adapter->SetHttpTransportForTesting(&Transport);
			Adapter->ApplyRuntimeConfig(MakeEnabledConfig());
		}

		for (int32 Index = 0; Index < 8; ++Index)
		{
			Clock->Tick(0.1f);
		}

		FMissionIsolationProbe Probe;
		Probe.RegisteredSinkCountBeforeDelivery = Telemetry->GetRegisteredTelemetrySinkCount();
		if (bEnableFailingEden)
		{
			const FEdenTelemetrySinkDeliverySummary Summary = Telemetry->DeliverSessionToRegisteredSinks();
			Probe.TelemetryDeliveryAttempts = Summary.AttemptedCount;
		}

		const FEdenMissionRuntimeState RuntimeState = Mission->GetMissionRuntimeState();
		Probe.MissionState = Mission->GetMissionState();
		Probe.MissionPhase = Mission->GetMissionPhase();
		Probe.MissionElapsedTimeSeconds = Mission->GetMissionElapsedTimeSeconds();
		Probe.ClockElapsedTimeSeconds = Clock->GetElapsedSimulationTimeSeconds();
		Probe.ClockLastDroppedSteps = Clock->GetLastDroppedSteps();
		Probe.EventStates = RuntimeState.EventStates;
		Probe.ObjectiveStates = RuntimeState.ObjectiveStates;
		Probe.Fuel = Fuel->GetFuelStateSnapshot();
		Probe.Power = Power->GetPowerStateSnapshot();
		Probe.Thermal = Thermal->GetThermalStateSnapshot();
		Probe.TelemetryEventCount = Telemetry->GetEventHistory().Num();
		Probe.TelemetrySnapshotCount = Telemetry->GetSnapshotHistory().Num();
		if (bEnableFailingEden)
		{
			const FEdenOsConnectionSnapshot Snapshot = Adapter->GetConnectionSnapshot();
			Probe.EdenTransportAttempts = Transport.SentRequests.Num();
			Probe.EdenConnectionState = Snapshot.ConnectionState;
			Probe.EdenLastErrorSummary = Snapshot.LastErrorSummary;
		}
		return Probe;
	}

	bool CompareMissionIsolationProbes(FAutomationTestBase& Test, const FMissionIsolationProbe& Disabled, const FMissionIsolationProbe& Failing)
	{
		bool bPassed = true;
		bPassed &= Test.TestEqual(TEXT("Mission state identical"), Failing.MissionState, Disabled.MissionState);
		bPassed &= Test.TestEqual(TEXT("Mission phase identical"), Failing.MissionPhase, Disabled.MissionPhase);
		bPassed &= Test.TestEqual(TEXT("Mission elapsed time identical"), Failing.MissionElapsedTimeSeconds, Disabled.MissionElapsedTimeSeconds);
		bPassed &= Test.TestEqual(TEXT("Clock elapsed time identical"), Failing.ClockElapsedTimeSeconds, Disabled.ClockElapsedTimeSeconds);
		bPassed &= Test.TestEqual(TEXT("Clock dropped steps identical"), Failing.ClockLastDroppedSteps, Disabled.ClockLastDroppedSteps);
		bPassed &= Test.TestEqual(TEXT("Mission event count identical"), Failing.EventStates.Num(), Disabled.EventStates.Num());
		for (int32 Index = 0; Index < FMath::Min(Failing.EventStates.Num(), Disabled.EventStates.Num()); ++Index)
		{
			bPassed &= Test.TestEqual(TEXT("Mission event id identical"), Failing.EventStates[Index].EventId, Disabled.EventStates[Index].EventId);
			bPassed &= Test.TestEqual(TEXT("Mission event state identical"), Failing.EventStates[Index].EventState, Disabled.EventStates[Index].EventState);
		}
		bPassed &= Test.TestEqual(TEXT("Objective count identical"), Failing.ObjectiveStates.Num(), Disabled.ObjectiveStates.Num());
		for (int32 Index = 0; Index < FMath::Min(Failing.ObjectiveStates.Num(), Disabled.ObjectiveStates.Num()); ++Index)
		{
			bPassed &= Test.TestEqual(TEXT("Objective id identical"), Failing.ObjectiveStates[Index].ObjectiveId, Disabled.ObjectiveStates[Index].ObjectiveId);
			bPassed &= Test.TestEqual(TEXT("Objective state identical"), Failing.ObjectiveStates[Index].State, Disabled.ObjectiveStates[Index].State);
		}
		bPassed &= Test.TestEqual(TEXT("Fuel kg identical"), Failing.Fuel.FuelQuantityKilograms, Disabled.Fuel.FuelQuantityKilograms);
		bPassed &= Test.TestEqual(TEXT("Fuel fraction identical"), Failing.Fuel.FuelFraction, Disabled.Fuel.FuelFraction);
		bPassed &= Test.TestEqual(TEXT("Fuel state identical"), Failing.Fuel.FuelState, Disabled.Fuel.FuelState);
		bPassed &= Test.TestEqual(TEXT("Battery kWh identical"), Failing.Power.BatteryChargeKilowattHours, Disabled.Power.BatteryChargeKilowattHours);
		bPassed &= Test.TestEqual(TEXT("Battery fraction identical"), Failing.Power.ChargeFraction, Disabled.Power.ChargeFraction);
		bPassed &= Test.TestEqual(TEXT("Power state identical"), Failing.Power.PowerState, Disabled.Power.PowerState);
		bPassed &= Test.TestEqual(TEXT("Thermal temperature identical"), Failing.Thermal.TemperatureCelsius, Disabled.Thermal.TemperatureCelsius);
		bPassed &= Test.TestEqual(TEXT("Thermal state identical"), Failing.Thermal.ThermalState, Disabled.Thermal.ThermalState);
		bPassed &= Test.TestEqual(TEXT("Telemetry event count identical"), Failing.TelemetryEventCount, Disabled.TelemetryEventCount);
		bPassed &= Test.TestEqual(TEXT("Telemetry snapshot count identical"), Failing.TelemetrySnapshotCount, Disabled.TelemetrySnapshotCount);
		return bPassed;
	}

	FString GetLiveEnvVar(const TCHAR* Name)
	{
		return FPlatformMisc::GetEnvironmentVariable(Name).TrimStartAndEnd();
	}

	const TCHAR* MessageTypeToString(EEdenOsOutboundMessageType MessageType)
	{
		switch (MessageType)
		{
		case EEdenOsOutboundMessageType::SessionCreate:
			return TEXT("SessionCreate");
		case EEdenOsOutboundMessageType::Telemetry:
			return TEXT("Telemetry");
		case EEdenOsOutboundMessageType::Event:
			return TEXT("Event");
		case EEdenOsOutboundMessageType::SessionComplete:
			return TEXT("SessionComplete");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* ConnectionStateToString(EEdenOsConnectionState State)
	{
		switch (State)
		{
		case EEdenOsConnectionState::Disabled:
			return TEXT("Disabled");
		case EEdenOsConnectionState::Disconnected:
			return TEXT("Disconnected");
		case EEdenOsConnectionState::Connecting:
			return TEXT("Connecting");
		case EEdenOsConnectionState::Connected:
			return TEXT("Connected");
		case EEdenOsConnectionState::Degraded:
			return TEXT("Degraded");
		default:
			return TEXT("Unknown");
		}
	}

	int32 CountDeliveryRecordsOfType(const TArray<FEdenOsDeliveryRecord>& Records, EEdenOsOutboundMessageType MessageType)
	{
		int32 Count = 0;
		for (const FEdenOsDeliveryRecord& Record : Records)
		{
			if (Record.MessageType == MessageType)
			{
				++Count;
			}
		}
		return Count;
	}

	const FEdenOsDeliveryRecord* FindFirstDeliveryRecordOfType(
		const TArray<FEdenOsDeliveryRecord>& Records,
		EEdenOsOutboundMessageType MessageType)
	{
		for (const FEdenOsDeliveryRecord& Record : Records)
		{
			if (Record.MessageType == MessageType)
			{
				return &Record;
			}
		}
		return nullptr;
	}

	bool WriteLiveE2EEvidence(
		const FString& EvidenceDirectory,
		const FString& SessionId,
		int32 ExpectedRequestCount,
		EEdenMissionState MissionState,
		const FEdenOsConnectionSnapshot& Snapshot,
		const TArray<FEdenOsDeliveryRecord>& Records)
	{
		IFileManager::Get().MakeDirectory(*EvidenceDirectory, true);
		const FString EvidencePath = FPaths::Combine(EvidenceDirectory, TEXT("UnrealLiveE2E.json"));

		FString Json;
		Json += TEXT("{\n");
		Json += TEXT("  \"transport\": \"FEdenOsUnrealHttpTransport\",\n");
		Json += FString::Printf(
			TEXT("  \"sessionId\": \"%s\",\n"),
			*FEdenTelemetryExportModel::EscapeJsonString(SessionId));
		Json += TEXT("  \"scenarioId\": \"SolarEventEmergency\",\n");
		Json += FString::Printf(
			TEXT("  \"missionState\": \"%s\",\n"),
			MissionState == EEdenMissionState::Succeeded ? TEXT("Succeeded") : TEXT("Other"));
		Json += FString::Printf(
			TEXT("  \"connectionState\": \"%s\",\n"),
			ConnectionStateToString(Snapshot.ConnectionState));
		Json += FString::Printf(TEXT("  \"pendingMessageCount\": %d,\n"), Snapshot.PendingMessageCount);
		Json += FString::Printf(TEXT("  \"droppedMessageCount\": %d,\n"), Snapshot.DroppedMessageCount);
		Json += FString::Printf(TEXT("  \"expectedRequestCount\": %d,\n"), ExpectedRequestCount);
		Json += TEXT("  \"deliveries\": [\n");
		for (int32 Index = 0; Index < Records.Num(); ++Index)
		{
			const FEdenOsDeliveryRecord& Record = Records[Index];
			Json += TEXT("    {\n");
			Json += FString::Printf(TEXT("      \"messageType\": \"%s\",\n"), MessageTypeToString(Record.MessageType));
			Json += FString::Printf(
				TEXT("      \"routePath\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(Record.RoutePath));
			Json += FString::Printf(TEXT("      \"sequenceNumber\": %lld,\n"), Record.SequenceNumber);
			Json += FString::Printf(TEXT("      \"httpStatusCode\": %d,\n"), Record.HttpStatusCode);
			Json += FString::Printf(TEXT("      \"succeeded\": %s,\n"), Record.bSucceeded ? TEXT("true") : TEXT("false"));
			Json += FString::Printf(
				TEXT("      \"responseBody\": \"%s\"\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(Record.ResponseBodyJson));
			Json += Index + 1 < Records.Num() ? TEXT("    },\n") : TEXT("    }\n");
		}
		Json += TEXT("  ]\n");
		Json += TEXT("}\n");

		return FFileHelper::SaveStringToFile(Json, *EvidencePath);
	}

	class FWaitForEdenOsDeliveryHistoryCountCommand final : public IAutomationLatentCommand
	{
	public:
		FWaitForEdenOsDeliveryHistoryCountCommand(
			TWeakObjectPtr<UEdenOsAdapterSubsystem> InAdapter,
			int32 InExpectedCount,
			double InTimeoutSeconds)
			: Adapter(InAdapter)
			, ExpectedCount(InExpectedCount)
			, TimeoutSeconds(InTimeoutSeconds)
		{
		}

		virtual bool Update() override
		{
			if (StartTimeSeconds <= 0.0)
			{
				StartTimeSeconds = FPlatformTime::Seconds();
			}

			if (const UEdenOsAdapterSubsystem* AdapterPtr = Adapter.Get())
			{
				const FEdenOsConnectionSnapshot Snapshot = AdapterPtr->GetConnectionSnapshot();
				if (AdapterPtr->GetDeliveryHistoryForTesting().Num() >= ExpectedCount && Snapshot.PendingMessageCount == 0)
				{
					return true;
				}
			}

			return FPlatformTime::Seconds() - StartTimeSeconds >= TimeoutSeconds;
		}

	private:
		TWeakObjectPtr<UEdenOsAdapterSubsystem> Adapter;
		int32 ExpectedCount = 0;
		double TimeoutSeconds = 0.0;
		double StartTimeSeconds = 0.0;
	};

	class FVerifyLiveProjectEdenLifecycleCommand final : public IAutomationLatentCommand
	{
	public:
		FVerifyLiveProjectEdenLifecycleCommand(
			FAutomationTestBase* InTest,
			TSharedRef<FScopedEdenOsMissionWorld> InScopedWorld,
			TWeakObjectPtr<UEdenOsAdapterSubsystem> InAdapter,
			FString InSessionId,
			int32 InExpectedRequestCount,
			int32 InExpectedEventCount,
			FString InEvidenceDirectory)
			: Test(InTest)
			, ScopedWorld(InScopedWorld)
			, Adapter(InAdapter)
			, SessionId(MoveTemp(InSessionId))
			, ExpectedRequestCount(InExpectedRequestCount)
			, ExpectedEventCount(InExpectedEventCount)
			, EvidenceDirectory(MoveTemp(InEvidenceDirectory))
		{
		}

		virtual bool Update() override
		{
			UEdenOsAdapterSubsystem* AdapterPtr = Adapter.Get();
			if (!Test->TestNotNull(TEXT("Live adapter still exists"), AdapterPtr))
			{
				ScopedWorld.Reset();
				return true;
			}

			const TArray<FEdenOsDeliveryRecord> Records = AdapterPtr->GetDeliveryHistoryForTesting();
			const FEdenOsConnectionSnapshot Snapshot = AdapterPtr->GetConnectionSnapshot();
			Test->TestEqual(TEXT("All live lifecycle HTTP requests completed"), Records.Num(), ExpectedRequestCount);
			Test->TestEqual(TEXT("No live lifecycle messages remain pending"), Snapshot.PendingMessageCount, 0);
			Test->TestEqual(TEXT("Successful live run leaves adapter connected"), Snapshot.ConnectionState, EEdenOsConnectionState::Connected);
			Test->TestEqual(TEXT("Live run did not drop messages"), Snapshot.DroppedMessageCount, 0);
			Test->TestEqual(TEXT("One create response"), CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::SessionCreate), 1);
			Test->TestEqual(TEXT("One telemetry response"), CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::Telemetry), 1);
			Test->TestEqual(TEXT("Expected event responses"), CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::Event), ExpectedEventCount);
			Test->TestEqual(TEXT("One completion response"), CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::SessionComplete), 1);

			const FEdenOsDeliveryRecord* CreateRecord = FindFirstDeliveryRecordOfType(Records, EEdenOsOutboundMessageType::SessionCreate);
			const FEdenOsDeliveryRecord* TelemetryRecord = FindFirstDeliveryRecordOfType(Records, EEdenOsOutboundMessageType::Telemetry);
			const FEdenOsDeliveryRecord* EventRecord = FindFirstDeliveryRecordOfType(Records, EEdenOsOutboundMessageType::Event);
			const FEdenOsDeliveryRecord* CompleteRecord = FindFirstDeliveryRecordOfType(Records, EEdenOsOutboundMessageType::SessionComplete);
			if (Test->TestNotNull(TEXT("Create delivery record exists"), CreateRecord))
			{
				Test->TestEqual(TEXT("Create HTTP status"), CreateRecord->HttpStatusCode, 201);
				Test->TestTrue(TEXT("Create response includes public sessionId"), CreateRecord->ResponseBodyJson.Contains(SessionId));
				Test->TestTrue(TEXT("Create response includes running status"), CreateRecord->ResponseBodyJson.Contains(TEXT("running")));
			}
			if (Test->TestNotNull(TEXT("Telemetry delivery record exists"), TelemetryRecord))
			{
				Test->TestEqual(TEXT("Telemetry HTTP status"), TelemetryRecord->HttpStatusCode, 202);
				Test->TestTrue(TEXT("Telemetry succeeded"), TelemetryRecord->bSucceeded);
			}
			if (Test->TestNotNull(TEXT("At least one event delivery record exists"), EventRecord))
			{
				Test->TestEqual(TEXT("Event HTTP status"), EventRecord->HttpStatusCode, 202);
				Test->TestTrue(TEXT("Event succeeded"), EventRecord->bSucceeded);
			}
			if (Test->TestNotNull(TEXT("Complete delivery record exists"), CompleteRecord))
			{
				Test->TestEqual(TEXT("Complete HTTP status"), CompleteRecord->HttpStatusCode, 200);
				Test->TestTrue(TEXT("Complete response includes succeeded final status"), CompleteRecord->ResponseBodyJson.Contains(TEXT("succeeded")));
			}

			Test->TestTrue(
				TEXT("Live E2E evidence artifact written"),
				WriteLiveE2EEvidence(
					EvidenceDirectory,
					SessionId,
					ExpectedRequestCount,
					EEdenMissionState::Succeeded,
					Snapshot,
					Records));

			ScopedWorld.Reset();
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		TSharedPtr<FScopedEdenOsMissionWorld> ScopedWorld;
		TWeakObjectPtr<UEdenOsAdapterSubsystem> Adapter;
		FString SessionId;
		int32 ExpectedRequestCount = 0;
		int32 ExpectedEventCount = 0;
		FString EvidenceDirectory;
	};
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
	FEdenOsFailingTransportPreservesAuthoritativeMissionResultTest,
	"Eden.Integration.EdenOs.FailingTransportPreservesAuthoritativeMissionResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsFailingTransportPreservesAuthoritativeMissionResultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	const FMissionIsolationProbe DisabledProbe = RunMissionIsolationProbe(false);
	const FMissionIsolationProbe FailingProbe = RunMissionIsolationProbe(true);

	TestEqual(TEXT("Disabled run has no EDEN sink"), DisabledProbe.RegisteredSinkCountBeforeDelivery, 0);
	TestEqual(TEXT("Failing run registers the EDEN sink"), FailingProbe.RegisteredSinkCountBeforeDelivery, 1);
	TestEqual(TEXT("EDEN sink delivery attempted"), FailingProbe.TelemetryDeliveryAttempts, 1);
	TestTrue(TEXT("Failing transport received lifecycle HTTP attempts"), FailingProbe.EdenTransportAttempts > 1);
	TestEqual(TEXT("Failing transport reports disconnected"), FailingProbe.EdenConnectionState, EEdenOsConnectionState::Disconnected);
	TestTrue(TEXT("Failure summary records offline transport"), FailingProbe.EdenLastErrorSummary.Contains(TEXT("offline")));

	return CompareMissionIsolationProbes(*this, DisabledProbe, FailingProbe);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsLiveProjectEdenMissionLifecycleTest,
	"Eden.External.EdenOs.LiveProjectEdenMissionLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsLiveProjectEdenMissionLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	const FString BaseUrl = GetLiveEnvVar(TEXT("EDEN_OS_LIVE_E2E_BASE_URL"));
	const FString RuntimeBearerJwt = GetLiveEnvVar(TEXT("EDEN_OS_LIVE_E2E_BEARER_JWT"));
	FString EvidenceDirectory = GetLiveEnvVar(TEXT("EDEN_OS_LIVE_E2E_EVIDENCE_DIR"));
	if (EvidenceDirectory.IsEmpty())
	{
		EvidenceDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("EdenOsLiveE2E"));
	}

	if (BaseUrl.IsEmpty() || RuntimeBearerJwt.IsEmpty())
	{
		AddInfo(TEXT("Skipped live ProjectEden E2E: EDEN_OS_LIVE_E2E_BASE_URL or EDEN_OS_LIVE_E2E_BEARER_JWT is not set."));
		return true;
	}

	TSharedRef<FScopedEdenOsMissionWorld> ScopedWorld = MakeShared<FScopedEdenOsMissionWorld>();
	UWorld* World = ScopedWorld->World;

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	TestNotNull(TEXT("Clock subsystem exists"), Clock);
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(8);
	Clock->ResetSimulationClock();

	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	TestNotNull(TEXT("Telemetry subsystem exists"), Telemetry);
	Telemetry->ClearHistory();

	AActor* Owner = World->SpawnActor<AActor>();
	UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>(Owner);
	Fuel->RegisterComponent();
	Fuel->InitializeFuelSimulation(MakeFuelConfig());
	Fuel->RegisterWithSimulationClock();

	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>(Owner);
	Power->RegisterComponent();
	Power->InitializePowerSimulation(MakePowerConfig());
	Power->RegisterWithSimulationClock();

	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>(Owner);
	Thermal->RegisterComponent();
	Thermal->InitializeThermalSimulation(MakeThermalConfig());
	Thermal->RegisterWithSimulationClock();

	UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
	TestNotNull(TEXT("Mission subsystem exists"), Mission);
	Mission->SetMissionResourceTargets(Thermal, Power, Fuel);
	Mission->LoadMission(MakeIsolationMissionDefinition());
	Mission->StartMission();

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	TestNotNull(TEXT("EDEN OS adapter subsystem exists"), Adapter);
	TestTrue(TEXT("Live E2E uses production Unreal HTTP transport"), Adapter->IsUsingProductionHttpTransportForTesting());

	FEdenOsConnectionConfig Config = MakeEnabledConfig(64);
	Config.BaseUrl = BaseUrl;
	Config.RuntimeBearerJwt = RuntimeBearerJwt;
	Config.RequestTimeoutSeconds = 10.0f;
	Config.DefaultScenarioId = TEXT("SolarEventEmergency");
	TestTrue(TEXT("Live runtime config accepted"), Adapter->ApplyRuntimeConfig(Config));
	TestEqual(TEXT("EDEN sink registered"), Telemetry->GetRegisteredTelemetrySinkCount(), 1);

	for (int32 Index = 0; Index < 20 && Mission->GetMissionState() == EEdenMissionState::Running; ++Index)
	{
		Clock->Tick(0.1f);
	}

	TestEqual(TEXT("Live E2E mission reached succeeded state"), Mission->GetMissionState(), EEdenMissionState::Succeeded);
	const FEdenTelemetrySessionPayload PayloadBeforeDelivery = Telemetry->BuildSessionPayload();
	EEdenOsMissionFinalStatus FinalStatus = EEdenOsMissionFinalStatus::Failed;
	TestTrue(TEXT("Terminal telemetry fact is present"), FEdenOsMissionLifecycleModel::ResolveFinalStatus(PayloadBeforeDelivery, FinalStatus));
	TestEqual(TEXT("Terminal status maps to succeeded"), FinalStatus, EEdenOsMissionFinalStatus::Succeeded);

	const int32 ExpectedEventCount = PayloadBeforeDelivery.Events.Num();
	const int32 ExpectedRequestCount = 1 + 1 + ExpectedEventCount + 1;
	const FString SessionId = Telemetry->GetSessionId();
	const FEdenTelemetrySinkDeliverySummary DeliverySummary = Telemetry->DeliverSessionToRegisteredSinks();
	TestEqual(TEXT("One EDEN sink delivery attempted"), DeliverySummary.AttemptedCount, 1);
	TestEqual(TEXT("EDEN sink queued lifecycle requests"), DeliverySummary.SucceededCount, 1);

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForEdenOsDeliveryHistoryCountCommand(Adapter, ExpectedRequestCount, 30.0));
	ADD_LATENT_AUTOMATION_COMMAND(FVerifyLiveProjectEdenLifecycleCommand(
		this,
		ScopedWorld,
		Adapter,
		SessionId,
		ExpectedRequestCount,
		ExpectedEventCount,
		EvidenceDirectory));
	return true;
}

#endif
