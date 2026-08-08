// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsMissionLifecycle.h"
#include "EdenOs/EdenOsTelemetrySink.h"
#include "EdenOs/EdenOsTransport.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Operations/EdenOperatorHudTypes.h"
#include "Operations/EdenOperatorTypes.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Telemetry/EdenTelemetryExportModel.h"
#include "Telemetry/EdenTelemetrySink.h"
#include "Telemetry/EdenTelemetrySubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

	FEdenOsConnectionConfig MakeEnabledConfig(
		int32 MaxQueueDepth = 8,
		EEdenOsAuthorityMode AuthorityMode = EEdenOsAuthorityMode::Advisory)
	{
		FEdenOsConnectionConfig Config;
		Config.bEnabled = true;
		Config.BaseUrl = TEXT("https://example.test");
		Config.ConnectionTimeoutSeconds = 2.0f;
		Config.RequestTimeoutSeconds = 5.0f;
		Config.MaxQueueDepth = MaxQueueDepth;
		Config.AdvisoryHeartbeatSimulationSeconds = 5.0f;
		Config.AuthorityMode = AuthorityMode;
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
		// 0.7s, not 0.5s: telemetry decimates snapshots every 5 steps, so a 0.5s target made the
		// mission terminal in the exact step that recorded the first snapshot. The probe then had no
		// settled Running snapshot at all. 0.7s leaves a Running snapshot at 0.5s and still completes
		// within the probe's 8 steps, so completion routes and terminal assertions are unaffected.
		Survive.TargetValue = 0.7f;
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
		FEdenFlightStateSnapshot Flight;
		FEdenFuelStateSnapshot Fuel;
		FEdenPowerStateSnapshot Power;
		FEdenThermalStateSnapshot Thermal;
		FEdenOperatorStateSnapshot Operator;
		int32 TelemetryEventCount = 0;
		int32 TelemetrySnapshotCount = 0;
		int32 RegisteredSinkCountBeforeDelivery = 0;
		int32 TelemetryDeliveryAttempts = 0;
		int32 EdenTransportAttempts = 0;
		TArray<FString> EdenTransportUrls;
		EEdenOsConnectionState EdenConnectionState = EEdenOsConnectionState::Disabled;
		EEdenOsAuthorityMode EdenAuthorityMode = EEdenOsAuthorityMode::Advisory;
		FString EdenLastErrorSummary;
		/** Advisory bookkeeping, deliberately excluded from authoritative-result comparison. */
		int32 AdvisoryTickCount = 0;
		int32 AdvisoryEvaluationCount = 0;
		bool bAdvisoryContextValid = false;
		int32 AdvisoryContextTriggerReasonCount = 0;
	};

	FMissionIsolationProbe RunMissionIsolationProbe(
		bool bEnableEden,
		EEdenOsAuthorityMode AuthorityMode,
		bool bUseFailingTransport)
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
		if (bEnableEden)
		{
			if (bUseFailingTransport)
			{
				for (int32 Index = 0; Index < 32; ++Index)
				{
					Transport.Results.Add(FEdenOsHttpResult::Failed(0, TEXT("offline")));
				}
			}
			Adapter->SetHttpTransportForTesting(&Transport);
			Adapter->ApplyRuntimeConfig(MakeEnabledConfig(64, AuthorityMode));
		}

		for (int32 Index = 0; Index < 8; ++Index)
		{
			Clock->Tick(0.1f);
		}

		FMissionIsolationProbe Probe;
		Probe.RegisteredSinkCountBeforeDelivery = Telemetry->GetRegisteredTelemetrySinkCount();
		if (bEnableEden)
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
		if (const TArray<FEdenTelemetrySnapshot> Snapshots = Telemetry->GetSnapshotHistory(); !Snapshots.IsEmpty())
		{
			const FEdenTelemetrySnapshot& LastSnapshot = Snapshots.Last();
			Probe.Flight = LastSnapshot.Flight;
			Probe.Operator = LastSnapshot.Operator;
		}
		Probe.Fuel = Fuel->GetFuelStateSnapshot();
		Probe.Power = Power->GetPowerStateSnapshot();
		Probe.Thermal = Thermal->GetThermalStateSnapshot();
		Probe.TelemetryEventCount = Telemetry->GetEventHistory().Num();
		Probe.TelemetrySnapshotCount = Telemetry->GetSnapshotHistory().Num();
		if (bEnableEden)
		{
			const FEdenOsConnectionSnapshot Snapshot = Adapter->GetConnectionSnapshot();
			Probe.EdenTransportAttempts = Transport.SentRequests.Num();
			for (const FEdenOsHttpRequestData& Request : Transport.SentRequests)
			{
				Probe.EdenTransportUrls.Add(Request.Url);
			}
			Probe.EdenConnectionState = Snapshot.ConnectionState;
			Probe.EdenAuthorityMode = Snapshot.AuthorityMode;
			Probe.EdenLastErrorSummary = Snapshot.LastErrorSummary;

			const FEdenOsAdvisoryContext AdvisoryContext = Adapter->GetLastAdvisoryContext();
			Probe.AdvisoryTickCount = Adapter->GetAdvisoryTickCountForTesting();
			Probe.AdvisoryEvaluationCount = Adapter->GetAdvisoryEvaluationCount();
			Probe.bAdvisoryContextValid = AdvisoryContext.bIsValid;
			Probe.AdvisoryContextTriggerReasonCount = AdvisoryContext.TriggerReasons.Num();
		}
		return Probe;
	}

	FMissionIsolationProbe RunMissionIsolationProbe(bool bEnableFailingEden)
	{
		return RunMissionIsolationProbe(bEnableFailingEden, EEdenOsAuthorityMode::Advisory, bEnableFailingEden);
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
		bPassed &= Test.TestEqual(TEXT("Flight thrust authority identical"), Failing.Flight.ThrustAuthority, Disabled.Flight.ThrustAuthority);
		bPassed &= Test.TestEqual(TEXT("Flight stabilization availability identical"), Failing.Flight.bStabilizationAssistAvailable, Disabled.Flight.bStabilizationAssistAvailable);
		bPassed &= Test.TestEqual(TEXT("Flight propulsion demand identical"), Failing.Flight.PropulsionDemandNormalized, Disabled.Flight.PropulsionDemandNormalized);
		bPassed &= Test.TestEqual(TEXT("Operator thermal mode identical"), Failing.Operator.ThermalMode, Disabled.Operator.ThermalMode);
		bPassed &= Test.TestEqual(TEXT("Operator load shed mode identical"), Failing.Operator.LoadShedMode, Disabled.Operator.LoadShedMode);
		bPassed &= Test.TestEqual(TEXT("Operator propulsion priority identical"), Failing.Operator.PropulsionPriority, Disabled.Operator.PropulsionPriority);
		bPassed &= Test.TestEqual(TEXT("Operator demand identical"), Failing.Operator.OperatorDemandKilowatts, Disabled.Operator.OperatorDemandKilowatts);
		bPassed &= Test.TestEqual(
			TEXT("Operator dissipation identical"),
			Failing.Operator.OperatorDissipationDegreesCelsiusPerSecond,
			Disabled.Operator.OperatorDissipationDegreesCelsiusPerSecond);
		bPassed &= Test.TestEqual(TEXT("Operator thrust authority identical"), Failing.Operator.ThrustAuthority, Disabled.Operator.ThrustAuthority);
		bPassed &= Test.TestEqual(
			TEXT("Operator stabilization availability identical"),
			Failing.Operator.bStabilizationAssistAvailable,
			Disabled.Operator.bStabilizationAssistAvailable);
		bPassed &= Test.TestEqual(TEXT("Telemetry event count identical"), Failing.TelemetryEventCount, Disabled.TelemetryEventCount);
		bPassed &= Test.TestEqual(TEXT("Telemetry snapshot count identical"), Failing.TelemetrySnapshotCount, Disabled.TelemetrySnapshotCount);
		return bPassed;
	}

	bool TransportUrlsContain(const TArray<FString>& Urls, const FString& Fragment)
	{
		for (const FString& Url : Urls)
		{
			if (Url.Contains(Fragment))
			{
				return true;
			}
		}
		return false;
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
		case EEdenOsOutboundMessageType::Advisory:
			return TEXT("Advisory");
		case EEdenOsOutboundMessageType::CommandProposal:
			return TEXT("CommandProposal");
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

	const TCHAR* AuthorityModeToString(EEdenOsAuthorityMode AuthorityMode)
	{
		switch (AuthorityMode)
		{
		case EEdenOsAuthorityMode::Observe:
			return TEXT("Observe");
		case EEdenOsAuthorityMode::Advisory:
			return TEXT("Advisory");
		case EEdenOsAuthorityMode::AuthorizedControl:
			return TEXT("AuthorizedControl");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* LoadShedModeToString(EEdenLoadShedMode Mode)
	{
		switch (Mode)
		{
		case EEdenLoadShedMode::Normal:
			return TEXT("Normal");
		case EEdenLoadShedMode::Shed:
			return TEXT("Shed");
		default:
			return TEXT("Unknown");
		}
	}

	EEdenOsAuthorityMode ParseLiveAuthorityMode(const FString& RequestedAuthorityMode)
	{
		if (RequestedAuthorityMode.Equals(TEXT("Observe"), ESearchCase::IgnoreCase))
		{
			return EEdenOsAuthorityMode::Observe;
		}
		if (RequestedAuthorityMode.Equals(TEXT("AuthorizedControl"), ESearchCase::IgnoreCase))
		{
			return EEdenOsAuthorityMode::AuthorizedControl;
		}
		return EEdenOsAuthorityMode::Advisory;
	}

	int32 CountTelemetryEventsOfType(const TArray<FEdenTelemetryEvent>& History, EEdenTelemetryEventType Type)
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

	int32 CountSuccessfulDeliveriesOfType(const TArray<FEdenOsDeliveryRecord>& Records, EEdenOsOutboundMessageType MessageType)
	{
		int32 Count = 0;
		for (const FEdenOsDeliveryRecord& Record : Records)
		{
			if (Record.MessageType == MessageType && Record.bSucceeded)
			{
				++Count;
			}
		}
		return Count;
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

	struct FLiveAdvisoryReturnPathEvidence
	{
		int32 AdvisoryIssuedCount = 0;
		bool bHasLatestEvent = false;
		FString AdvisoryId;
		FString EvaluationId;
		FString Recommendation;
		FString Rationale;
		float IssuedSimulationTimeSeconds = 0.0f;
		float EvaluationSimulationTimeSeconds = 0.0f;
		float ContextSnapshotSimulationTimeSeconds = 0.0f;
		TArray<FString> TriggerReasons;
		bool bHasPresentation = false;
		FString PresentationRecommendation;
		FString PresentationRationale;
		FString PresentationAdvisoryId;
		float PresentationIssuedSimulationTimeSeconds = 0.0f;
		int32 CommandProposalCount = 0;
		FString LoadShedMode;
		int32 ExecutedEventCount = 0;
	};

	bool TryParseIssuedAdvisoryDetail(
		const FString& DetailJson,
		FString& OutAdvisoryId,
		FString& OutEvaluationId,
		FString& OutRecommendation,
		FString& OutRationale,
		float& OutEvaluationSimulationTimeSeconds,
		float& OutContextSnapshotSimulationTimeSeconds,
		TArray<FString>& OutTriggerReasons)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DetailJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}

		if (!Root->TryGetStringField(TEXT("advisoryId"), OutAdvisoryId)
			|| !Root->TryGetStringField(TEXT("evaluationId"), OutEvaluationId)
			|| !Root->TryGetStringField(TEXT("recommendation"), OutRecommendation)
			|| !Root->TryGetStringField(TEXT("rationale"), OutRationale))
		{
			return false;
		}

		double EvaluationTime = 0.0;
		double SnapshotTime = 0.0;
		if (!Root->TryGetNumberField(TEXT("evaluationSimulationTimeSeconds"), EvaluationTime)
			|| !Root->TryGetNumberField(TEXT("contextSnapshotSimulationTimeSeconds"), SnapshotTime))
		{
			return false;
		}
		OutEvaluationSimulationTimeSeconds = static_cast<float>(EvaluationTime);
		OutContextSnapshotSimulationTimeSeconds = static_cast<float>(SnapshotTime);

		OutTriggerReasons.Reset();
		const TArray<TSharedPtr<FJsonValue>>* TriggerArray = nullptr;
		if (Root->TryGetArrayField(TEXT("triggerReasons"), TriggerArray) && TriggerArray)
		{
			for (const TSharedPtr<FJsonValue>& Value : *TriggerArray)
			{
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					OutTriggerReasons.Add(Value->AsString());
				}
			}
		}
		return true;
	}

	bool WriteLiveE2EEvidence(
		const FString& EvidenceDirectory,
		const FString& SessionId,
		int32 ExpectedRequestCount,
		EEdenMissionState MissionState,
		EEdenOsAuthorityMode AuthorityMode,
		const FEdenOsConnectionSnapshot& Snapshot,
		const TArray<FEdenOsDeliveryRecord>& Records,
		const FLiveAdvisoryReturnPathEvidence& ReturnPath)
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
		Json += FString::Printf(TEXT("  \"authorityMode\": \"%s\",\n"), AuthorityModeToString(AuthorityMode));
		Json += FString::Printf(
			TEXT("  \"missionState\": \"%s\",\n"),
			MissionState == EEdenMissionState::Succeeded ? TEXT("Succeeded") : TEXT("Other"));
		Json += FString::Printf(
			TEXT("  \"connectionState\": \"%s\",\n"),
			ConnectionStateToString(Snapshot.ConnectionState));
		Json += FString::Printf(TEXT("  \"pendingMessageCount\": %d,\n"), Snapshot.PendingMessageCount);
		Json += FString::Printf(TEXT("  \"droppedMessageCount\": %d,\n"), Snapshot.DroppedMessageCount);
		Json += FString::Printf(TEXT("  \"expectedRequestCount\": %d,\n"), ExpectedRequestCount);
		Json += FString::Printf(TEXT("  \"advisoryIssuedCount\": %d,\n"), ReturnPath.AdvisoryIssuedCount);
		Json += FString::Printf(TEXT("  \"commandProposalCount\": %d,\n"), ReturnPath.CommandProposalCount);
		Json += FString::Printf(TEXT("  \"executedEventCount\": %d,\n"), ReturnPath.ExecutedEventCount);
		if (ReturnPath.LoadShedMode.IsEmpty())
		{
			Json += TEXT("  \"loadShedMode\": null,\n");
		}
		else
		{
			Json += FString::Printf(
				TEXT("  \"loadShedMode\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.LoadShedMode));
		}

		if (ReturnPath.bHasLatestEvent)
		{
			Json += TEXT("  \"latestAdvisoryEvent\": {\n");
			Json += FString::Printf(
				TEXT("    \"advisoryId\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.AdvisoryId));
			Json += FString::Printf(
				TEXT("    \"evaluationId\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.EvaluationId));
			Json += FString::Printf(
				TEXT("    \"recommendation\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.Recommendation));
			Json += FString::Printf(
				TEXT("    \"rationale\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.Rationale));
			Json += FString::Printf(
				TEXT("    \"issuedSimulationTimeSeconds\": %.6f,\n"),
				ReturnPath.IssuedSimulationTimeSeconds);
			Json += FString::Printf(
				TEXT("    \"evaluationSimulationTimeSeconds\": %.6f,\n"),
				ReturnPath.EvaluationSimulationTimeSeconds);
			Json += FString::Printf(
				TEXT("    \"contextSnapshotSimulationTimeSeconds\": %.6f,\n"),
				ReturnPath.ContextSnapshotSimulationTimeSeconds);
			Json += TEXT("    \"triggerReasons\": [");
			for (int32 Index = 0; Index < ReturnPath.TriggerReasons.Num(); ++Index)
			{
				Json += FString::Printf(
					TEXT("%s\"%s\""),
					Index > 0 ? TEXT(", ") : TEXT(""),
					*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.TriggerReasons[Index]));
			}
			Json += TEXT("]\n");
			Json += TEXT("  },\n");
		}
		else
		{
			Json += TEXT("  \"latestAdvisoryEvent\": null,\n");
		}

		if (ReturnPath.bHasPresentation)
		{
			Json += TEXT("  \"latestAdvisoryPresentation\": {\n");
			Json += FString::Printf(
				TEXT("    \"recommendation\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.PresentationRecommendation));
			Json += FString::Printf(
				TEXT("    \"rationale\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.PresentationRationale));
			Json += FString::Printf(
				TEXT("    \"advisoryId\": \"%s\",\n"),
				*FEdenTelemetryExportModel::EscapeJsonString(ReturnPath.PresentationAdvisoryId));
			Json += FString::Printf(
				TEXT("    \"issuedSimulationTimeSeconds\": %.6f\n"),
				ReturnPath.PresentationIssuedSimulationTimeSeconds);
			Json += TEXT("  },\n");
		}
		else
		{
			Json += TEXT("  \"latestAdvisoryPresentation\": null,\n");
		}

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

	/**
	 * Live AuthorizedControl requires advisory→command-proposal→execute to finish before
	 * SessionComplete is queued (HasSessionCompletedOrCompletionQueued would otherwise block L).
	 * The simulation clock is paused after seeding so wall-clock HTTP cannot race SurviveUntilTime
	 * or thermal objectives while waiting on ProjectEden.
	 */
	class FWaitForLiveAuthorizedControlChainCommand final : public IAutomationLatentCommand
	{
	public:
		FWaitForLiveAuthorizedControlChainCommand(
			TWeakObjectPtr<UEdenOsAdapterSubsystem> InAdapter,
			TWeakObjectPtr<UEdenOperatorControlComponent> InOperator,
			TWeakObjectPtr<UEdenTelemetrySubsystem> InTelemetry,
			double InTimeoutSeconds)
			: Adapter(InAdapter)
			, Operator(InOperator)
			, Telemetry(InTelemetry)
			, TimeoutSeconds(InTimeoutSeconds)
		{
		}

		virtual bool Update() override
		{
			if (StartTimeSeconds <= 0.0)
			{
				StartTimeSeconds = FPlatformTime::Seconds();
			}

			const UEdenOsAdapterSubsystem* AdapterPtr = Adapter.Get();
			const UEdenOperatorControlComponent* OperatorPtr = Operator.Get();
			const UEdenTelemetrySubsystem* TelemetryPtr = Telemetry.Get();
			if (!AdapterPtr || !OperatorPtr || !TelemetryPtr)
			{
				return FPlatformTime::Seconds() - StartTimeSeconds >= TimeoutSeconds;
			}

			const TArray<FEdenOsDeliveryRecord> Records = AdapterPtr->GetDeliveryHistoryForTesting();
			const int32 SuccessfulAdvisories =
				CountSuccessfulDeliveriesOfType(Records, EEdenOsOutboundMessageType::Advisory);
			const int32 SuccessfulProposals =
				CountSuccessfulDeliveriesOfType(Records, EEdenOsOutboundMessageType::CommandProposal);
			const int32 ExecutedCount = CountTelemetryEventsOfType(
				TelemetryPtr->GetEventHistory(),
				EEdenTelemetryEventType::EdenExternalCommandExecuted);
			const bool bLoadShed = OperatorPtr->GetOperatorIntent().LoadShedMode == EEdenLoadShedMode::Shed;
			const FEdenOsConnectionSnapshot Snapshot = AdapterPtr->GetConnectionSnapshot();

			if (SuccessfulAdvisories >= 1
				&& SuccessfulProposals >= 1
				&& ExecutedCount >= 1
				&& bLoadShed
				&& Snapshot.PendingMessageCount == 0)
			{
				return true;
			}

			return FPlatformTime::Seconds() - StartTimeSeconds >= TimeoutSeconds;
		}

	private:
		TWeakObjectPtr<UEdenOsAdapterSubsystem> Adapter;
		TWeakObjectPtr<UEdenOperatorControlComponent> Operator;
		TWeakObjectPtr<UEdenTelemetrySubsystem> Telemetry;
		double TimeoutSeconds = 0.0;
		double StartTimeSeconds = 0.0;
	};

	class FFinishLiveAuthorizedControlMissionCommand final : public IAutomationLatentCommand
	{
	public:
		FFinishLiveAuthorizedControlMissionCommand(
			FAutomationTestBase* InTest,
			TWeakObjectPtr<UEdenSimulationClockSubsystem> InClock,
			TWeakObjectPtr<UEdenMissionSubsystem> InMission,
			int32 InMaxAdditionalTicks)
			: Test(InTest)
			, Clock(InClock)
			, Mission(InMission)
			, MaxAdditionalTicks(InMaxAdditionalTicks)
		{
		}

		virtual bool Update() override
		{
			UEdenSimulationClockSubsystem* ClockPtr = Clock.Get();
			UEdenMissionSubsystem* MissionPtr = Mission.Get();
			if (!Test->TestNotNull(TEXT("Live AC clock exists"), ClockPtr)
				|| !Test->TestNotNull(TEXT("Live AC mission exists"), MissionPtr))
			{
				return true;
			}

			if (ClockPtr->IsSimulationPaused())
			{
				ClockPtr->ResumeSimulation();
			}

			while (TicksApplied < MaxAdditionalTicks
				&& MissionPtr->GetMissionState() == EEdenMissionState::Running)
			{
				ClockPtr->Tick(0.1f);
				++TicksApplied;
			}

			Test->TestEqual(
				TEXT("Live AuthorizedControl mission reached succeeded state"),
				MissionPtr->GetMissionState(),
				EEdenMissionState::Succeeded);
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<UEdenSimulationClockSubsystem> Clock;
		TWeakObjectPtr<UEdenMissionSubsystem> Mission;
		int32 MaxAdditionalTicks = 0;
		int32 TicksApplied = 0;
	};

	struct FLiveProjectEdenDeliverSharedState
	{
		int32 ExpectedRequestCount = 0;
		int32 ExpectedEventCount = 0;
		FString SessionId;
		bool bReady = false;
	};

	class FDeliverLiveProjectEdenSessionCommand final : public IAutomationLatentCommand
	{
	public:
		FDeliverLiveProjectEdenSessionCommand(
			FAutomationTestBase* InTest,
			TWeakObjectPtr<UEdenTelemetrySubsystem> InTelemetry,
			TSharedRef<FLiveProjectEdenDeliverSharedState> InShared,
			bool bInAuthorizedControl)
			: Test(InTest)
			, Telemetry(InTelemetry)
			, Shared(InShared)
			, bAuthorizedControl(bInAuthorizedControl)
		{
		}

		virtual bool Update() override
		{
			UEdenTelemetrySubsystem* TelemetryPtr = Telemetry.Get();
			if (!Test->TestNotNull(TEXT("Live telemetry exists for DeliverSession"), TelemetryPtr))
			{
				return true;
			}

			const FEdenTelemetrySessionPayload PayloadBeforeDelivery = TelemetryPtr->BuildSessionPayload();
			EEdenOsMissionFinalStatus FinalStatus = EEdenOsMissionFinalStatus::Failed;
			Test->TestTrue(
				TEXT("Terminal telemetry fact is present"),
				FEdenOsMissionLifecycleModel::ResolveFinalStatus(PayloadBeforeDelivery, FinalStatus));
			Test->TestEqual(TEXT("Terminal status maps to succeeded"), FinalStatus, EEdenOsMissionFinalStatus::Succeeded);

			Shared->ExpectedEventCount = PayloadBeforeDelivery.Events.Num();
			// Lifecycle floor: create + telemetry + events + complete. Advisory/CommandProposal
			// are additive and already may be present from in-mission flushes.
			Shared->ExpectedRequestCount = 1 + 1 + Shared->ExpectedEventCount + 1;
			if (bAuthorizedControl)
			{
				Shared->ExpectedRequestCount += 1; // Advisory floor
				Shared->ExpectedRequestCount += 1; // CommandProposal floor
			}
			Shared->SessionId = TelemetryPtr->GetSessionId();

			const FEdenTelemetrySinkDeliverySummary DeliverySummary =
				TelemetryPtr->DeliverSessionToRegisteredSinks();
			Test->TestEqual(TEXT("One EDEN sink delivery attempted"), DeliverySummary.AttemptedCount, 1);
			Test->TestEqual(TEXT("EDEN sink queued lifecycle requests"), DeliverySummary.SucceededCount, 1);
			Shared->bReady = true;
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<UEdenTelemetrySubsystem> Telemetry;
		TSharedRef<FLiveProjectEdenDeliverSharedState> Shared;
		bool bAuthorizedControl = false;
	};

	class FWaitForLiveDeliverSharedHistoryCommand final : public IAutomationLatentCommand
	{
	public:
		FWaitForLiveDeliverSharedHistoryCommand(
			TWeakObjectPtr<UEdenOsAdapterSubsystem> InAdapter,
			TSharedRef<FLiveProjectEdenDeliverSharedState> InShared,
			double InTimeoutSeconds)
			: Adapter(InAdapter)
			, Shared(InShared)
			, TimeoutSeconds(InTimeoutSeconds)
		{
		}

		virtual bool Update() override
		{
			if (!Shared->bReady)
			{
				return false;
			}
			if (StartTimeSeconds <= 0.0)
			{
				StartTimeSeconds = FPlatformTime::Seconds();
			}

			if (const UEdenOsAdapterSubsystem* AdapterPtr = Adapter.Get())
			{
				const FEdenOsConnectionSnapshot Snapshot = AdapterPtr->GetConnectionSnapshot();
				if (AdapterPtr->GetDeliveryHistoryForTesting().Num() >= Shared->ExpectedRequestCount
					&& Snapshot.PendingMessageCount == 0)
				{
					return true;
				}
			}

			return FPlatformTime::Seconds() - StartTimeSeconds >= TimeoutSeconds;
		}

	private:
		TWeakObjectPtr<UEdenOsAdapterSubsystem> Adapter;
		TSharedRef<FLiveProjectEdenDeliverSharedState> Shared;
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
			FString InEvidenceDirectory,
			TWeakObjectPtr<UEdenOperatorControlComponent> InOperator = nullptr,
			TSharedPtr<FLiveProjectEdenDeliverSharedState> InShared = nullptr)
			: Test(InTest)
			, ScopedWorld(InScopedWorld)
			, Adapter(InAdapter)
			, SessionId(MoveTemp(InSessionId))
			, ExpectedRequestCount(InExpectedRequestCount)
			, ExpectedEventCount(InExpectedEventCount)
			, EvidenceDirectory(MoveTemp(InEvidenceDirectory))
			, Operator(InOperator)
			, Shared(InShared)
		{
		}

		virtual bool Update() override
		{
			if (Shared.IsValid())
			{
				if (!Shared->bReady)
				{
					return false;
				}
				SessionId = Shared->SessionId;
				ExpectedRequestCount = Shared->ExpectedRequestCount;
				ExpectedEventCount = Shared->ExpectedEventCount;
			}

			UEdenOsAdapterSubsystem* AdapterPtr = Adapter.Get();
			if (!Test->TestNotNull(TEXT("Live adapter still exists"), AdapterPtr))
			{
				ScopedWorld.Reset();
				return true;
			}

			const TArray<FEdenOsDeliveryRecord> Records = AdapterPtr->GetDeliveryHistoryForTesting();
			const FEdenOsConnectionSnapshot Snapshot = AdapterPtr->GetConnectionSnapshot();
			const int32 AdvisoryCount = CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::Advisory);
			const int32 CommandProposalCount =
				CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::CommandProposal);
			const int32 SuccessfulCommandProposals =
				CountSuccessfulDeliveriesOfType(Records, EEdenOsOutboundMessageType::CommandProposal);
			const int32 TelemetryCount = CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::Telemetry);
			const int32 EventCount = CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::Event);
			const int32 LifecycleMinimum = ExpectedRequestCount;
			FLiveAdvisoryReturnPathEvidence ReturnPath;
			ReturnPath.CommandProposalCount = CommandProposalCount;

			// Advisory/AuthorizedControl may flush lifecycle during the mission, so total deliveries
			// can exceed the classic create+telemetry+events+complete count. Require the floor.
			Test->TestTrue(
				TEXT("Live delivery history covers lifecycle floor"),
				Records.Num() >= LifecycleMinimum);
			Test->TestEqual(TEXT("No live lifecycle messages remain pending"), Snapshot.PendingMessageCount, 0);
			Test->TestEqual(TEXT("Successful live run leaves adapter connected"), Snapshot.ConnectionState, EEdenOsConnectionState::Connected);
			Test->TestEqual(TEXT("Live run did not drop messages"), Snapshot.DroppedMessageCount, 0);
			Test->TestEqual(TEXT("One create response"), CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::SessionCreate), 1);
			Test->TestTrue(TEXT("At least one telemetry response"), TelemetryCount >= 1);
			Test->TestTrue(TEXT("At least expected event responses"), EventCount >= ExpectedEventCount);
			Test->TestEqual(TEXT("One completion response"), CountDeliveryRecordsOfType(Records, EEdenOsOutboundMessageType::SessionComplete), 1);
			if (Snapshot.AuthorityMode == EEdenOsAuthorityMode::Advisory)
			{
				Test->TestTrue(TEXT("Advisory mode issued at least one advisory request"), AdvisoryCount >= 1);
				int32 SuccessfulAdvisories = 0;
				const FEdenOsDeliveryRecord* FirstSuccessfulAdvisory = nullptr;
				for (const FEdenOsDeliveryRecord& Record : Records)
				{
					if (Record.MessageType == EEdenOsOutboundMessageType::Advisory && Record.bSucceeded)
					{
						++SuccessfulAdvisories;
						if (!FirstSuccessfulAdvisory)
						{
							FirstSuccessfulAdvisory = &Record;
						}
					}
				}
				Test->TestTrue(TEXT("At least one advisory HTTP response succeeded"), SuccessfulAdvisories >= 1);

				UWorld* World = AdapterPtr->GetWorld();
				UEdenTelemetrySubsystem* Telemetry =
					World ? World->GetSubsystem<UEdenTelemetrySubsystem>() : nullptr;
				if (!Test->TestNotNull(TEXT("Live telemetry subsystem exists for return-path proof"), Telemetry)
					|| !Test->TestNotNull(TEXT("Successful advisory delivery record exists"), FirstSuccessfulAdvisory))
				{
					ScopedWorld.Reset();
					return true;
				}

				const FEdenOsAdvisoryResponseParseResult ParsedResponse =
					FEdenOsWireSerializationModel::ParseAdvisoryResponseV1(
						FirstSuccessfulAdvisory->ResponseBodyJson,
						FString());
				if (!Test->TestTrue(TEXT("Live advisory response parses"), ParsedResponse.IsSuccess()))
				{
					ScopedWorld.Reset();
					return true;
				}

				// GetEventHistory returns by value — keep the array alive while reading IssuedEvent.
				const TArray<FEdenTelemetryEvent> EventHistory = Telemetry->GetEventHistory();
				int32 IssuedCount = 0;
				const FEdenTelemetryEvent* IssuedEvent = nullptr;
				for (const FEdenTelemetryEvent& Event : EventHistory)
				{
					if (Event.EventType == EEdenTelemetryEventType::EdenAdvisoryIssued)
					{
						++IssuedCount;
						IssuedEvent = &Event;
					}
				}
				Test->TestEqual(TEXT("Exactly one EdenAdvisoryIssued for live accepted response"), IssuedCount, 1);
				if (!Test->TestNotNull(TEXT("Issued advisory event exists"), IssuedEvent))
				{
					ScopedWorld.Reset();
					return true;
				}

				FString EventAdvisoryId;
				FString EventEvaluationId;
				FString EventRecommendation;
				FString EventRationale;
				float EventEvaluationTime = 0.0f;
				float EventSnapshotTime = 0.0f;
				TArray<FString> EventTriggers;
				Test->TestTrue(
					TEXT("Issued advisory Detail parses"),
					TryParseIssuedAdvisoryDetail(
						IssuedEvent->Detail,
						EventAdvisoryId,
						EventEvaluationId,
						EventRecommendation,
						EventRationale,
						EventEvaluationTime,
						EventSnapshotTime,
						EventTriggers));

				Test->TestEqual(TEXT("Issued advisoryId matches ProjectEden response"), EventAdvisoryId, ParsedResponse.Response.AdvisoryId);
				Test->TestEqual(TEXT("Issued evaluationId matches ProjectEden response"), EventEvaluationId, ParsedResponse.Response.EvaluationId);
				Test->TestEqual(TEXT("Issued recommendation matches ProjectEden response"), EventRecommendation, ParsedResponse.Response.Recommendation);
				Test->TestEqual(TEXT("Issued rationale matches ProjectEden response"), EventRationale, ParsedResponse.Response.Rationale);
				Test->TestTrue(
					TEXT("issued >= evaluation"),
					IssuedEvent->SimulationTimeSeconds >= EventEvaluationTime);
				Test->TestTrue(
					TEXT("evaluation >= context snapshot"),
					EventEvaluationTime >= EventSnapshotTime);
				Test->TestFalse(TEXT("Issued Detail invents no confidence"), IssuedEvent->Detail.Contains(TEXT("confidence")));
				Test->TestFalse(TEXT("Issued Detail invents no severity"), IssuedEvent->Detail.Contains(TEXT("severity")));
				Test->TestFalse(TEXT("Issued Detail invents no recommendationCode"), IssuedEvent->Detail.Contains(TEXT("recommendationCode")));

				const FEdenOsAcceptedAdvisory Accepted = AdapterPtr->GetLatestAcceptedAdvisory();
				Test->TestTrue(TEXT("Accepted advisory is available for HUD"), Accepted.bIsValid);
				Test->TestEqual(TEXT("Presentation recommendation matches response"), Accepted.Recommendation, ParsedResponse.Response.Recommendation);
				Test->TestEqual(TEXT("Presentation rationale matches response"), Accepted.Rationale, ParsedResponse.Response.Rationale);
				Test->TestEqual(TEXT("Presentation advisoryId matches response"), Accepted.AdvisoryId, ParsedResponse.Response.AdvisoryId);

				const FEdenOperatorHudSnapshot Hud = FEdenOperatorHudModel::Assemble(
					FEdenMissionStateSnapshot(),
					FEdenFuelStateSnapshot(),
					FEdenPowerStateSnapshot(),
					FEdenThermalStateSnapshot(),
					FEdenOperatorStateSnapshot(),
					TArray<FEdenAlert>(),
					IssuedEvent->SimulationTimeSeconds,
					Accepted);
				Test->TestTrue(TEXT("HUD snapshot surfaces advisory"), Hud.bHasAdvisory);
				Test->TestEqual(TEXT("HUD recommendation matches response"), Hud.AdvisoryRecommendation, ParsedResponse.Response.Recommendation);
				Test->TestEqual(TEXT("HUD rationale matches response"), Hud.AdvisoryRationale, ParsedResponse.Response.Rationale);
				Test->TestFalse(TEXT("HUD invents no confidence field name in recommendation"), Hud.AdvisoryRecommendation.Contains(TEXT("confidence")));
				Test->TestFalse(TEXT("HUD invents no severity field name in rationale"), Hud.AdvisoryRationale.Contains(TEXT("severity")));

				ReturnPath.AdvisoryIssuedCount = IssuedCount;
				ReturnPath.bHasLatestEvent = true;
				ReturnPath.AdvisoryId = EventAdvisoryId;
				ReturnPath.EvaluationId = EventEvaluationId;
				ReturnPath.Recommendation = EventRecommendation;
				ReturnPath.Rationale = EventRationale;
				ReturnPath.IssuedSimulationTimeSeconds = IssuedEvent->SimulationTimeSeconds;
				ReturnPath.EvaluationSimulationTimeSeconds = EventEvaluationTime;
				ReturnPath.ContextSnapshotSimulationTimeSeconds = EventSnapshotTime;
				ReturnPath.TriggerReasons = EventTriggers;
				ReturnPath.bHasPresentation = true;
				ReturnPath.PresentationRecommendation = Accepted.Recommendation;
				ReturnPath.PresentationRationale = Accepted.Rationale;
				ReturnPath.PresentationAdvisoryId = Accepted.AdvisoryId;
				ReturnPath.PresentationIssuedSimulationTimeSeconds = Accepted.IssuedSimulationTimeSeconds;
			}
			else if (Snapshot.AuthorityMode == EEdenOsAuthorityMode::AuthorizedControl)
			{
				Test->TestTrue(TEXT("AuthorizedControl issued at least one advisory request"), AdvisoryCount >= 1);
				Test->TestTrue(
					TEXT("AuthorizedControl has at least one successful CommandProposal delivery"),
					SuccessfulCommandProposals >= 1);

				UWorld* World = AdapterPtr->GetWorld();
				UEdenTelemetrySubsystem* Telemetry =
					World ? World->GetSubsystem<UEdenTelemetrySubsystem>() : nullptr;
				UEdenOperatorControlComponent* OperatorPtr = Operator.Get();
				if (!OperatorPtr && World)
				{
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						if (UEdenOperatorControlComponent* Found =
								It->FindComponentByClass<UEdenOperatorControlComponent>())
						{
							OperatorPtr = Found;
							break;
						}
					}
				}

				if (!Test->TestNotNull(TEXT("Live telemetry exists for AuthorizedControl verify"), Telemetry)
					|| !Test->TestNotNull(TEXT("Operator control exists for AuthorizedControl verify"), OperatorPtr))
				{
					ScopedWorld.Reset();
					return true;
				}

				const TArray<FEdenTelemetryEvent> EventHistory = Telemetry->GetEventHistory();
				const int32 ExecutedCount = CountTelemetryEventsOfType(
					EventHistory,
					EEdenTelemetryEventType::EdenExternalCommandExecuted);
				const int32 OperatorIssuedCount = CountTelemetryEventsOfType(
					EventHistory,
					EEdenTelemetryEventType::OperatorCommandIssued);

				Test->TestEqual(
					TEXT("AuthorizedControl emits exactly one EdenExternalCommandExecuted"),
					ExecutedCount,
					1);
				Test->TestEqual(
					TEXT("AuthorizedControl emits no OperatorCommandIssued from Eden path"),
					OperatorIssuedCount,
					0);
				Test->TestEqual(
					TEXT("Operator LoadShedMode converged to Shed"),
					OperatorPtr->GetOperatorIntent().LoadShedMode,
					EEdenLoadShedMode::Shed);
				Test->TestEqual(
					TEXT("LastCommandSource is EdenAuthorizedControl"),
					OperatorPtr->GetLastCommandSource(),
					EEdenOperatorCommandSource::EdenAuthorizedControl);

				// Delivery records retain response bodies only (not request bodies), so scanning
				// Event deliveries for EdenExternalCommandExecuted is not practical here.
				// Executed presence is proven via telemetry history above; final DeliverSession
				// includes that event in the Event delivery floor.

				ReturnPath.AdvisoryIssuedCount = CountTelemetryEventsOfType(
					EventHistory,
					EEdenTelemetryEventType::EdenAdvisoryIssued);
				ReturnPath.ExecutedEventCount = ExecutedCount;
				ReturnPath.LoadShedMode = LoadShedModeToString(OperatorPtr->GetOperatorIntent().LoadShedMode);
				ReturnPath.CommandProposalCount = CommandProposalCount;
			}
			else
			{
				Test->TestEqual(TEXT("Observe mode issues no advisory requests"), AdvisoryCount, 0);
				Test->TestEqual(TEXT("Observe mode issues no command proposals"), CommandProposalCount, 0);
				if (UWorld* World = AdapterPtr->GetWorld())
				{
					if (UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>())
					{
						int32 IssuedCount = 0;
						for (const FEdenTelemetryEvent& Event : Telemetry->GetEventHistory())
						{
							if (Event.EventType == EEdenTelemetryEventType::EdenAdvisoryIssued)
							{
								++IssuedCount;
							}
						}
						Test->TestEqual(TEXT("Observe mode emits no EdenAdvisoryIssued"), IssuedCount, 0);
						ReturnPath.AdvisoryIssuedCount = IssuedCount;
					}
				}
			}

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
					Records.Num(),
					EEdenMissionState::Succeeded,
					Snapshot.AuthorityMode,
					Snapshot,
					Records,
					ReturnPath));

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
		TWeakObjectPtr<UEdenOperatorControlComponent> Operator;
		TSharedPtr<FLiveProjectEdenDeliverSharedState> Shared;
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
	FEdenOsObserveModePreservesAuthoritativeMissionResultTest,
	"Eden.Integration.EdenOs.ObserveModePreservesAuthoritativeMissionResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsObserveModePreservesAuthoritativeMissionResultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	const FMissionIsolationProbe DisabledProbe = RunMissionIsolationProbe(false);
	const FMissionIsolationProbe ObserveProbe = RunMissionIsolationProbe(
		true,
		EEdenOsAuthorityMode::Observe,
		false);

	TestEqual(TEXT("Observe run registers the EDEN sink"), ObserveProbe.RegisteredSinkCountBeforeDelivery, 1);
	TestEqual(TEXT("Observe sink delivery attempted"), ObserveProbe.TelemetryDeliveryAttempts, 1);
	TestTrue(TEXT("Observe transport received lifecycle HTTP attempts"), ObserveProbe.EdenTransportAttempts > 1);
	TestEqual(TEXT("Observe mode reflected in connection snapshot"), ObserveProbe.EdenAuthorityMode, EEdenOsAuthorityMode::Observe);
	TestEqual(TEXT("Observe transport reports connected after success"), ObserveProbe.EdenConnectionState, EEdenOsConnectionState::Connected);
	TestTrue(TEXT("Observe run sends create route"), TransportUrlsContain(ObserveProbe.EdenTransportUrls, EdenOsWireContract::CreateSessionRoute));
	TestTrue(TEXT("Observe run sends telemetry route"), TransportUrlsContain(ObserveProbe.EdenTransportUrls, TEXT("/telemetry")));
	TestTrue(TEXT("Observe run sends event route"), TransportUrlsContain(ObserveProbe.EdenTransportUrls, TEXT("/events")));
	TestTrue(TEXT("Observe run sends completion route"), TransportUrlsContain(ObserveProbe.EdenTransportUrls, TEXT("/complete")));
	TestFalse(TEXT("Observe run does not call advisory route"), TransportUrlsContain(ObserveProbe.EdenTransportUrls, TEXT("advis")));
	TestFalse(TEXT("Observe run does not call command route"), TransportUrlsContain(ObserveProbe.EdenTransportUrls, TEXT("command")));

	// Observe must not enter the advisory-evaluation path at all.
	TestEqual(TEXT("Observe performs no advisory evaluation"), ObserveProbe.AdvisoryEvaluationCount, 0);
	TestFalse(TEXT("Observe builds no advisory context"), ObserveProbe.bAdvisoryContextValid);

	return CompareMissionIsolationProbes(*this, DisabledProbe, ObserveProbe);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryModePreservesAuthoritativeMissionResultTest,
	"Eden.Integration.EdenOs.AdvisoryModePreservesAuthoritativeMissionResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryModePreservesAuthoritativeMissionResultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

	const FMissionIsolationProbe DisabledProbe = RunMissionIsolationProbe(false);
	const FMissionIsolationProbe AdvisoryProbe = RunMissionIsolationProbe(
		true,
		EEdenOsAuthorityMode::Advisory,
		false);

	TestEqual(TEXT("Advisory mode reflected in connection snapshot"), AdvisoryProbe.EdenAuthorityMode, EEdenOsAuthorityMode::Advisory);

	AddInfo(FString::Printf(
		TEXT("DIAG ticks=%d snapshots=%d events=%d advisoryEvals=%d contextValid=%d"),
		AdvisoryProbe.AdvisoryTickCount,
		AdvisoryProbe.TelemetrySnapshotCount,
		AdvisoryProbe.TelemetryEventCount,
		AdvisoryProbe.AdvisoryEvaluationCount,
		AdvisoryProbe.bAdvisoryContextValid ? 1 : 0));

	// Guard against comparing "disabled" against an advisory path that never actually ran.
	TestTrue(
		TEXT("Advisory run performed at least one advisory evaluation"),
		AdvisoryProbe.AdvisoryEvaluationCount > 0);
	TestTrue(TEXT("Advisory run built a valid context"), AdvisoryProbe.bAdvisoryContextValid);
	TestTrue(
		TEXT("Advisory context carries at least one trigger reason"),
		AdvisoryProbe.AdvisoryContextTriggerReasonCount > 0);

	// Checkpoint I: Advisory mode must contact the plural advisories route. Observe must not.
	TestTrue(
		TEXT("Advisory run calls /advisories route"),
		TransportUrlsContain(AdvisoryProbe.EdenTransportUrls, TEXT("/advisories")));
	TestFalse(TEXT("Advisory run does not call command route"), TransportUrlsContain(AdvisoryProbe.EdenTransportUrls, TEXT("command")));

	// The decisive invariant: advisory machinery running changes no authoritative simulation truth.
	// Fake transport returns 204 without a parseable advisory body, so EdenAdvisoryIssued is not emitted
	// and telemetry event counts remain comparable.
	return CompareMissionIsolationProbes(*this, DisabledProbe, AdvisoryProbe);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAdvisoryResponseIssuesTelemetryAndHudFactTest,
	"Eden.Integration.EdenOs.AdvisoryResponseIssuesTelemetryAndHudFact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAdvisoryResponseIssuesTelemetryAndHudFactTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsTransportTests;

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

	class FAdvisoryEchoTransport final : public IEdenOsHttpTransport
	{
	public:
		virtual bool SendAsync(const FEdenOsHttpRequestData& Request, FEdenOsHttpCompletion Completion) override
		{
			SentUrls.Add(Request.Url);
			if (Request.Url.Contains(TEXT("/advisories")))
			{
				FString EvaluationId = TEXT("eval-missing");
				const FString Key = TEXT("\"evaluationId\": \"");
				const int32 KeyIndex = Request.BodyJson.Find(Key, ESearchCase::CaseSensitive);
				if (KeyIndex != INDEX_NONE)
				{
					const int32 Start = KeyIndex + Key.Len();
					const int32 End = Request.BodyJson.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
					if (End != INDEX_NONE)
					{
						EvaluationId = Request.BodyJson.Mid(Start, End - Start);
					}
				}

				const FString Body = FString::Printf(
					TEXT(
						"{\"schemaVersion\":1,\"advisoryId\":\"adv-integration-1\","
						"\"evaluationId\":\"%s\","
						"\"recommendation\":\"Increase cooling and maintain load shedding.\","
						"\"rationale\":\"Temperature trend warrants additional thermal control.\"}"),
					*EvaluationId);
				Completion.ExecuteIfBound(FEdenOsHttpResult::Succeeded(201, Body));
				++AdvisoryResponses;
				return true;
			}

			Completion.ExecuteIfBound(FEdenOsHttpResult::Succeeded(204));
			return true;
		}

		TArray<FString> SentUrls;
		int32 AdvisoryResponses = 0;
	};

	FAdvisoryEchoTransport Transport;
	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	Adapter->SetHttpTransportForTesting(&Transport);
	Adapter->ApplyRuntimeConfig(MakeEnabledConfig(64, EEdenOsAuthorityMode::Advisory));

	for (int32 Index = 0; Index < 12; ++Index)
	{
		Clock->Tick(0.1f);
	}

	TestTrue(TEXT("At least one advisory HTTP response"), Transport.AdvisoryResponses > 0);
	TestTrue(TEXT("Advisories route contacted"), TransportUrlsContain(Transport.SentUrls, TEXT("/advisories")));

	const FEdenOsAcceptedAdvisory Accepted = Adapter->GetLatestAcceptedAdvisory();
	TestTrue(TEXT("Adapter holds accepted advisory"), Accepted.bIsValid);
	TestEqual(TEXT("Accepted advisory id"), Accepted.AdvisoryId, FString(TEXT("adv-integration-1")));
	TestTrue(TEXT("Recommendation preserved"), Accepted.Recommendation.Contains(TEXT("cooling")));
	TestTrue(
		TEXT("Evaluation and snapshot times remain distinct facts"),
		Accepted.EvaluationSimulationTimeSeconds >= Accepted.ContextSnapshotSimulationTimeSeconds);

	int32 IssuedCount = 0;
	for (const FEdenTelemetryEvent& Event : Telemetry->GetEventHistory())
	{
		if (Event.EventType == EEdenTelemetryEventType::EdenAdvisoryIssued)
		{
			++IssuedCount;
			TestTrue(TEXT("Issued detail carries advisoryId"), Event.Detail.Contains(TEXT("adv-integration-1")));
			TestTrue(TEXT("Issued detail carries evaluationId"), Event.Detail.Contains(TEXT("evaluationId")));
			TestFalse(TEXT("No invented severity field"), Event.Detail.Contains(TEXT("severity")));
			TestFalse(TEXT("No invented confidence field"), Event.Detail.Contains(TEXT("confidence")));
		}
	}
	TestTrue(TEXT("Exactly one EdenAdvisoryIssued for one accepted response"), IssuedCount == 1);

	const FEdenOperatorHudSnapshot Hud = FEdenOperatorHudModel::Assemble(
		Mission->GetMissionStateSnapshot(),
		Fuel->GetFuelStateSnapshot(),
		Power->GetPowerStateSnapshot(),
		Thermal->GetThermalStateSnapshot(),
		FEdenOperatorStateSnapshot(),
		TArray<FEdenAlert>(),
		Clock->GetElapsedSimulationTimeSeconds(),
		Accepted);
	TestTrue(TEXT("HUD snapshot surfaces advisory"), Hud.bHasAdvisory);
	TestEqual(TEXT("HUD recommendation matches adapter"), Hud.AdvisoryRecommendation, Accepted.Recommendation);
	TestEqual(TEXT("HUD rationale matches adapter"), Hud.AdvisoryRationale, Accepted.Rationale);
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
	const FString RequestedAuthorityMode = GetLiveEnvVar(TEXT("EDEN_OS_LIVE_E2E_AUTHORITY_MODE"));
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

	const EEdenOsAuthorityMode AuthorityMode = ParseLiveAuthorityMode(RequestedAuthorityMode);
	const bool bAuthorizedControl = AuthorityMode == EEdenOsAuthorityMode::AuthorizedControl;

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

	TWeakObjectPtr<UEdenOperatorControlComponent> OperatorWeak;
	if (bAuthorizedControl)
	{
		UEdenFlightMovementComponent* Flight = NewObject<UEdenFlightMovementComponent>(Owner);
		Flight->RegisterComponent();

		UEdenOperatorControlComponent* Operator = NewObject<UEdenOperatorControlComponent>(Owner);
		Operator->RegisterComponent();

		FEdenOperatorControlConfig OpConfig;
		OpConfig.BoostDissipationDegreesCelsiusPerSecond = 1.0f;
		OpConfig.EmergencyDissipationDegreesCelsiusPerSecond = 2.0f;
		OpConfig.BoostCoolingDemandKilowatts = 1.5f;
		OpConfig.EmergencyCoolingDemandKilowatts = 4.5f;
		OpConfig.LoadShedDemandReductionKilowatts = 2.0f;
		// Live L fixture proves Normal→Shed without thermally failing SurviveUntilTime.
		OpConfig.LoadShedDissipationReductionDegreesCelsiusPerSecond = 0.0f;
		OpConfig.ReducedThrustAuthority = 0.5f;
		TestTrue(TEXT("Operator initializes for AuthorizedControl live E2E"), Operator->InitializeOperatorControl(OpConfig));
		TestEqual(
			TEXT("LoadShed starts Normal"),
			Operator->GetOperatorIntent().LoadShedMode,
			EEdenLoadShedMode::Normal);
		Telemetry->BindOperatorControlForTesting(Operator);
		OperatorWeak = Operator;
	}

	UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
	TestNotNull(TEXT("Mission subsystem exists"), Mission);
	Mission->SetMissionResourceTargets(Thermal, Power, Fuel);

	FEdenMissionDefinitionConfig MissionDefinition = MakeIsolationMissionDefinition();
	if (bAuthorizedControl)
	{
		// L live proof needs wall-clock HTTP for advisory→proposal→execute while the mission
		// stays Running, then still completes Succeeded. Soften KeepCool and heating so Shed
		// (which reduces dissipation) cannot fail the mission before SurviveUntilTime.
		for (FEdenMissionObjectiveConfig& Objective : MissionDefinition.Objectives)
		{
			if (Objective.ObjectiveType == EEdenObjectiveType::SurviveUntilTime)
			{
				Objective.TargetValue = 8.0f;
			}
			else if (Objective.ObjectiveType == EEdenObjectiveType::KeepTemperatureBelow)
			{
				Objective.TargetValue = 1000.0f;
			}
		}
		for (FEdenMissionEventConfig& Event : MissionDefinition.Events)
		{
			if (Event.EventId == FName("SolarHeating"))
			{
				Event.FloatParameter = 0.0f;
			}
		}
	}
	Mission->LoadMission(MissionDefinition);
	Mission->StartMission();

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	TestNotNull(TEXT("EDEN OS adapter subsystem exists"), Adapter);
	TestTrue(TEXT("Live E2E uses production Unreal HTTP transport"), Adapter->IsUsingProductionHttpTransportForTesting());

	FEdenOsConnectionConfig Config = MakeEnabledConfig(64);
	Config.BaseUrl = BaseUrl;
	Config.RuntimeBearerJwt = RuntimeBearerJwt;
	Config.RequestTimeoutSeconds = 10.0f;
	Config.DefaultScenarioId = TEXT("SolarEventEmergency");
	Config.AuthorityMode = AuthorityMode;
	if (bAuthorizedControl)
	{
		Config.bExternalCommandValidationEnabled = true;
		Config.bExternalCommandAutomationEnabled = true;
		Config.bExternalCommandExecutionEnabled = true;
		// One evaluation opportunity per ~2s sim — avoids flooding proposals before Shed lands.
		Config.AdvisoryHeartbeatSimulationSeconds = 2.0f;
	}
	TestTrue(TEXT("Live runtime config accepted"), Adapter->ApplyRuntimeConfig(Config));
	TestEqual(TEXT("Live authority mode applied"), Adapter->GetConnectionSnapshot().AuthorityMode, Config.AuthorityMode);
	TestEqual(TEXT("EDEN sink registered"), Telemetry->GetRegisteredTelemetrySinkCount(), 1);

	if (bAuthorizedControl)
	{
		// Seed past the first heartbeat (2.0s @ 0.1s step) without completing SurviveUntilTime=8s.
		for (int32 Index = 0; Index < 25 && Mission->GetMissionState() == EEdenMissionState::Running; ++Index)
		{
			Clock->Tick(0.1f);
		}
		Clock->PauseSimulation();

		TSharedRef<FLiveProjectEdenDeliverSharedState> Shared = MakeShared<FLiveProjectEdenDeliverSharedState>();
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForLiveAuthorizedControlChainCommand(
			Adapter,
			OperatorWeak,
			Telemetry,
			60.0));
		ADD_LATENT_AUTOMATION_COMMAND(FFinishLiveAuthorizedControlMissionCommand(this, Clock, Mission, 400));
		ADD_LATENT_AUTOMATION_COMMAND(FDeliverLiveProjectEdenSessionCommand(this, Telemetry, Shared, true));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForLiveDeliverSharedHistoryCommand(Adapter, Shared, 45.0));
		ADD_LATENT_AUTOMATION_COMMAND(FVerifyLiveProjectEdenLifecycleCommand(
			this,
			ScopedWorld,
			Adapter,
			FString(),
			0,
			0,
			EvidenceDirectory,
			OperatorWeak,
			Shared));
		return true;
	}

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
