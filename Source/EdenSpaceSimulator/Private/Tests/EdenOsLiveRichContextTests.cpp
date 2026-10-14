// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsConnectionSettings.h"
#include "Flight/EdenFlightPlayerController.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Operations/EdenOperatorHudTypes.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "Telemetry/EdenTelemetryExportModel.h"
#include "Telemetry/EdenTelemetrySubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Live H.1 rich-context check (ExecPlan 0010). Runs only when Run-EdenOsLiveE2E-style env vars
// point at a running ProjectEden. Uses the real BP spacecraft (its Data Assets), a possessed
// flight controller, and the Solar Event mission, so telemetry binds through the same path as
// play. Mode changes go through UEdenOperatorControlComponent, the component T/L/P drive.
// This is automation, not human PIE.

namespace EdenOsLiveRichContextTests
{
	const TCHAR* PawnBlueprintClassPath = TEXT("/Game/Eden/Blueprints/BP_EdenSpacecraftPawn.BP_EdenSpacecraftPawn_C");

	FString EnvVar(const TCHAR* Name)
	{
		return FPlatformMisc::GetEnvironmentVariable(Name).TrimStartAndEnd();
	}

	struct FPhase
	{
		FString Name;
		EEdenThermalControlMode ThermalMode = EEdenThermalControlMode::Nominal;
		EEdenLoadShedMode LoadShedMode = EEdenLoadShedMode::Normal;
		EEdenPropulsionPriorityMode Propulsion = EEdenPropulsionPriorityMode::Full;
		bool bCustomThresholds = false;
		float SetTemperatureCelsius = -1.0f;
	};

	struct FPhaseRecord
	{
		FString Name;
		FString IntentThermal;
		FString IntentLoadShed;
		FString IntentPropulsion;
		FString AdvisoryId;
		FString HudRecommendation;
		FString HudRationale;
		bool bHudHasAdvisory = false;
		float TemperatureCelsius = 0.0f;
		float EffectiveDissipation = 0.0f;
		float TotalDemandKilowatts = 0.0f;
		float WarningCelsius = 0.0f;
		float CriticalCelsius = 0.0f;
		bool bSourcesThermal = false;
		bool bSourcesPower = false;
		bool bSourcesOperator = false;
	};

	/** Shared across latent commands; owns the world for the test's lifetime. */
	struct FLiveState
	{
		FWorldContext* WorldContext = nullptr;
		UWorld* World = nullptr;
		AEdenSpacecraftPawn* Pawn = nullptr;
		AEdenFlightPlayerController* Controller = nullptr;
		TArray<FPhase> Phases;
		TArray<FPhaseRecord> Records;
		FString EvidenceDirectory;
		int32 PhaseIndex = 0;
		int32 CommandProposalCountAtStart = 0;

		~FLiveState()
		{
			if (!World)
			{
				return;
			}
			for (AActor* Actor : FActorRange(World))
			{
				if (Actor && Actor->HasActorBegunPlay())
				{
					Actor->RouteEndPlay(EEndPlayReason::LevelTransition);
				}
			}
			GEngine->ShutdownWorldNetDriver(World);
			World->DestroyWorld(true);
			World->RemoveFromRoot();
			GEngine->DestroyWorldContext(World);
		}
	};

	FString Token(EEdenThermalControlMode Mode)
	{
		return FEdenTelemetryExportModel::EnumToken(Mode);
	}

	FString Token(EEdenLoadShedMode Mode)
	{
		return FEdenTelemetryExportModel::EnumToken(Mode);
	}

	FString Token(EEdenPropulsionPriorityMode Mode)
	{
		return FEdenTelemetryExportModel::EnumToken(Mode);
	}

	/** Waits until a new advisory is accepted for the phase and the outbound queue is empty. */
	class FRunPhaseCommand final : public IAutomationLatentCommand
	{
	public:
		FRunPhaseCommand(FAutomationTestBase* InTest, TSharedRef<FLiveState> InState, double InTimeoutSeconds)
			: Test(InTest)
			, State(InState)
			, TimeoutSeconds(InTimeoutSeconds)
		{
		}

		virtual bool Update() override
		{
			UWorld* World = State->World;
			UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
			UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
			if (!bStarted)
			{
				bStarted = true;
				StartSeconds = FPlatformTime::Seconds();
				AdvisoryIdBefore = Adapter->GetLatestAcceptedAdvisory().AdvisoryId;
				StartPhase(Clock);
				return false;
			}

			const FEdenOsAcceptedAdvisory Latest = Adapter->GetLatestAcceptedAdvisory();
			const bool bDrained = Adapter->GetConnectionSnapshot().PendingMessageCount == 0;
			if (bDrained && Latest.bIsValid && Latest.AdvisoryId != AdvisoryIdBefore)
			{
				// Let any trailing advisory of this phase land, then record the newest one.
				if (SettledSince <= 0.0)
				{
					SettledSince = FPlatformTime::Seconds();
					return false;
				}
				if (FPlatformTime::Seconds() - SettledSince < 1.0)
				{
					return false;
				}
				RecordPhase(Adapter);
				++State->PhaseIndex;
				return true;
			}
			SettledSince = 0.0;

			if (FPlatformTime::Seconds() - StartSeconds >= TimeoutSeconds)
			{
				Test->AddError(FString::Printf(
					TEXT("Phase '%s' got no accepted advisory within %.0f s."),
					*State->Phases[State->PhaseIndex].Name,
					TimeoutSeconds));
				++State->PhaseIndex;
				return true;
			}
			return false;
		}

	private:
		void StartPhase(UEdenSimulationClockSubsystem* Clock)
		{
			const FPhase& Phase = State->Phases[State->PhaseIndex];
			UEdenOperatorControlComponent* Operator = State->Pawn->GetOperatorControlComponent();
			UEdenThermalSystemComponent* Thermal = State->Pawn->GetThermalSystemComponent();
			Clock->ResumeSimulation();

			Operator->SetThermalControlMode(Phase.ThermalMode);
			Operator->SetLoadShedMode(Phase.LoadShedMode);
			Operator->SetPropulsionPriorityMode(Phase.Propulsion);
			if (Phase.bCustomThresholds)
			{
				// Modes are back at Nominal/Normal/Full (no operator dissipation), so re-initialising
				// thermal does not leave the reported modes and applied rates out of step.
				FEdenThermalConfig Custom = Thermal->GetActiveThermalConfig();
				Custom.WarningTemperatureCelsius = 60.0f;
				Custom.CriticalTemperatureCelsius = 95.0f;
				Test->TestTrue(TEXT("Custom thresholds accepted"), Thermal->InitializeThermalSimulation(Custom));
			}
			if (Phase.SetTemperatureCelsius > 0.0f)
			{
				Thermal->SetTemperatureCelsius(Phase.SetTemperatureCelsius);
			}

			// Three seconds of simulation: an operator-action evaluation, then a heartbeat at least
			// 1.5 s after the change, whose newest snapshot reflects the new modes.
			for (int32 Step = 0; Step < 30; ++Step)
			{
				Clock->Tick(0.1f);
			}
			Clock->PauseSimulation();
		}

		void RecordPhase(UEdenOsAdapterSubsystem* Adapter)
		{
			const FPhase& Phase = State->Phases[State->PhaseIndex];
			FPhaseRecord Record;
			Record.Name = Phase.Name;
			const FEdenOperatorIntent Intent = State->Pawn->GetOperatorControlComponent()->GetOperatorIntent();
			Record.IntentThermal = Token(Intent.ThermalMode);
			Record.IntentLoadShed = Token(Intent.LoadShedMode);
			Record.IntentPropulsion = Token(Intent.PropulsionPriority);
			const FEdenOsAcceptedAdvisory Advisory = Adapter->GetLatestAcceptedAdvisory();
			Record.AdvisoryId = Advisory.AdvisoryId;
			// In play a 10 Hz world timer refreshes the HUD; this test world advances only the
			// simulation clock, so refresh it the same way the timer would.
			State->Controller->RefreshOperatorHudNow();
			const FEdenOperatorHudSnapshot Hud = State->Controller->GetOperatorHudSnapshot();
			Record.bHudHasAdvisory = Hud.bHasAdvisory;
			Record.HudRecommendation = Hud.AdvisoryRecommendation;
			Record.HudRationale = Hud.AdvisoryRationale;
			const FEdenOsAdvisoryContext Context = Adapter->GetLastAdvisoryContext();
			Record.TemperatureCelsius = Context.Thermal.TemperatureCelsius;
			Record.EffectiveDissipation = Context.Thermal.EffectiveDissipationDegreesCelsiusPerSecond;
			Record.TotalDemandKilowatts = Context.Power.TotalDemandKilowatts;
			Record.WarningCelsius = Context.ThermalLimits.WarningTemperatureCelsius;
			Record.CriticalCelsius = Context.ThermalLimits.CriticalTemperatureCelsius;
			Record.bSourcesThermal = Context.Sources.bThermal;
			Record.bSourcesPower = Context.Sources.bPower;
			Record.bSourcesOperator = Context.Sources.bOperator;

			Test->TestEqual(*FString::Printf(TEXT("%s: thermal mode applied"), *Phase.Name), Intent.ThermalMode, Phase.ThermalMode);
			Test->TestEqual(*FString::Printf(TEXT("%s: load shed applied"), *Phase.Name), Intent.LoadShedMode, Phase.LoadShedMode);
			Test->TestTrue(*FString::Printf(TEXT("%s: all rich sources available"), *Phase.Name),
				Context.Sources.bThermal && Context.Sources.bPower && Context.Sources.bOperator);
			Test->TestEqual(
				*FString::Printf(TEXT("%s: context operator mode matches intent"), *Phase.Name),
				Context.Operator.ThermalMode,
				Phase.ThermalMode);
			Test->TestTrue(*FString::Printf(TEXT("%s: HUD shows the advisory"), *Phase.Name), Hud.bHasAdvisory);
			Test->TestEqual(
				*FString::Printf(TEXT("%s: HUD shows the latest accepted advisory"), *Phase.Name),
				Hud.AdvisoryRecommendation,
				Advisory.Recommendation);
			State->Records.Add(MoveTemp(Record));
		}

		FAutomationTestBase* Test = nullptr;
		TSharedRef<FLiveState> State;
		double TimeoutSeconds = 0.0;
		bool bStarted = false;
		double StartSeconds = 0.0;
		double SettledSince = 0.0;
		FString AdvisoryIdBefore;
	};

	class FWriteEvidenceCommand final : public IAutomationLatentCommand
	{
	public:
		FWriteEvidenceCommand(FAutomationTestBase* InTest, TSharedRef<FLiveState> InState)
			: Test(InTest)
			, State(InState)
		{
		}

		virtual bool Update() override
		{
			UWorld* World = State->World;
			UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
			UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();

			// Advisory mode never asks for or executes commands.
			Test->TestEqual(
				TEXT("No command proposals requested in Advisory mode"),
				Adapter->GetRequestedCommandProposalEvaluationCountForTesting(),
				State->CommandProposalCountAtStart);
			int32 Executed = 0;
			for (const FEdenTelemetryEvent& Event : Telemetry->GetEventHistory())
			{
				Executed += Event.EventType == EEdenTelemetryEventType::EdenExternalCommandExecuted ? 1 : 0;
			}
			Test->TestEqual(TEXT("No external command executed"), Executed, 0);

			FString Json = TEXT("{\n");
			Json += FString::Printf(TEXT("  \"sessionId\": \"%s\",\n"), *FEdenTelemetryExportModel::EscapeJsonString(Telemetry->GetSessionId()));
			Json += FString::Printf(TEXT("  \"executedExternalCommands\": %d,\n"), Executed);
			Json += TEXT("  \"phases\": [\n");
			for (int32 Index = 0; Index < State->Records.Num(); ++Index)
			{
				const FPhaseRecord& R = State->Records[Index];
				Json += TEXT("    {\n");
				Json += FString::Printf(TEXT("      \"name\": \"%s\",\n"), *R.Name);
				Json += FString::Printf(
					TEXT("      \"intent\": {\"thermalControlMode\": \"%s\", \"loadShedMode\": \"%s\", \"propulsionPriority\": \"%s\"},\n"),
					*R.IntentThermal,
					*R.IntentLoadShed,
					*R.IntentPropulsion);
				Json += FString::Printf(
					TEXT("      \"sources\": {\"thermal\": %s, \"power\": %s, \"operator\": %s},\n"),
					R.bSourcesThermal ? TEXT("true") : TEXT("false"),
					R.bSourcesPower ? TEXT("true") : TEXT("false"),
					R.bSourcesOperator ? TEXT("true") : TEXT("false"));
				Json += FString::Printf(
					TEXT("      \"unrealContext\": {\"temperatureCelsius\": %.6f, \"effectiveDissipation\": %.6f, \"totalDemandKilowatts\": %.6f, \"warningCelsius\": %.6f, \"criticalCelsius\": %.6f},\n"),
					R.TemperatureCelsius,
					R.EffectiveDissipation,
					R.TotalDemandKilowatts,
					R.WarningCelsius,
					R.CriticalCelsius);
				Json += FString::Printf(TEXT("      \"advisoryId\": \"%s\",\n"), *FEdenTelemetryExportModel::EscapeJsonString(R.AdvisoryId));
				Json += FString::Printf(TEXT("      \"hudHasAdvisory\": %s,\n"), R.bHudHasAdvisory ? TEXT("true") : TEXT("false"));
				Json += FString::Printf(
					TEXT("      \"hudRecommendation\": \"%s\",\n"),
					*FEdenTelemetryExportModel::EscapeJsonString(R.HudRecommendation));
				Json += FString::Printf(
					TEXT("      \"hudRationale\": \"%s\"\n"),
					*FEdenTelemetryExportModel::EscapeJsonString(R.HudRationale));
				Json += Index + 1 < State->Records.Num() ? TEXT("    },\n") : TEXT("    }\n");
			}
			Json += TEXT("  ]\n}\n");
			const FString Path = FPaths::Combine(State->EvidenceDirectory, TEXT("UnrealLiveRichContext.json"));
			Test->TestTrue(TEXT("Evidence written"), FFileHelper::SaveStringToFile(Json, *Path));
			Test->AddInfo(FString::Printf(TEXT("Live rich-context evidence: %s"), *Path));
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		TSharedRef<FLiveState> State;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsLiveRichAdvisoryContextTest,
	"Eden.External.EdenOs.LiveRichAdvisoryContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsLiveRichAdvisoryContextTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsLiveRichContextTests;

	const FString BaseUrl = EnvVar(TEXT("EDEN_OS_LIVE_E2E_BASE_URL"));
	const FString Jwt = EnvVar(TEXT("EDEN_OS_LIVE_E2E_BEARER_JWT"));
	if (BaseUrl.IsEmpty() || Jwt.IsEmpty())
	{
		AddInfo(TEXT("Skipped live rich-context E2E: EDEN_OS_LIVE_E2E_BASE_URL or EDEN_OS_LIVE_E2E_BEARER_JWT is not set."));
		return true;
	}

	TSharedRef<FLiveState> State = MakeShared<FLiveState>();
	State->EvidenceDirectory = EnvVar(TEXT("EDEN_OS_LIVE_E2E_EVIDENCE_DIR"));
	if (State->EvidenceDirectory.IsEmpty())
	{
		State->EvidenceDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("EdenOsLiveE2E"));
	}

	const FName WorldName = MakeUniqueObjectName(
		nullptr,
		UWorld::StaticClass(),
		TEXT("EdenLiveRichContextWorld"),
		EUniqueObjectNameOptions::GloballyUnique);
	State->WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
	State->World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
	UWorld* World = State->World;
	World->AddToRoot();
	State->WorldContext->SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->SetMaxCatchUpSteps(8);
	Clock->ResetSimulationClock();
	World->GetSubsystem<UEdenTelemetrySubsystem>()->ClearHistory();

	UClass* PawnClass = StaticLoadClass(AEdenSpacecraftPawn::StaticClass(), nullptr, PawnBlueprintClassPath);
	if (!TestNotNull(TEXT("BP_EdenSpacecraftPawn loads"), PawnClass))
	{
		return false;
	}
	State->Pawn = World->SpawnActor<AEdenSpacecraftPawn>(PawnClass, FTransform::Identity);
	State->Controller = World->SpawnActor<AEdenFlightPlayerController>(
		AEdenFlightPlayerController::StaticClass(),
		FTransform::Identity);
	if (!TestNotNull(TEXT("Pawn spawns"), State->Pawn) || !TestNotNull(TEXT("Controller spawns"), State->Controller))
	{
		return false;
	}
	World->BeginPlay();
	if (!State->Pawn->HasActorBegunPlay())
	{
		State->Pawn->DispatchBeginPlay();
	}
	if (!State->Controller->HasActorBegunPlay())
	{
		State->Controller->DispatchBeginPlay();
	}
	State->Controller->Possess(State->Pawn);
	TestTrue(TEXT("Thermal simulating from its Data Asset"), State->Pawn->GetThermalSystemComponent()->IsThermalSimulationEnabled());
	TestTrue(TEXT("Power simulating from its Data Asset"), State->Pawn->GetPowerSystemComponent()->IsPowerSimulationEnabled());
	TestTrue(TEXT("Operator control enabled"), State->Pawn->GetOperatorControlComponent()->IsOperatorControlEnabled());

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	TestTrue(TEXT("Production HTTP transport"), Adapter->IsUsingProductionHttpTransportForTesting());
	FEdenOsConnectionConfig Config;
	Config.bEnabled = true;
	Config.BaseUrl = BaseUrl;
	Config.RuntimeBearerJwt = Jwt;
	Config.ConnectionTimeoutSeconds = 5.0f;
	Config.RequestTimeoutSeconds = 10.0f;
	Config.MaxQueueDepth = 256;
	Config.AdvisoryHeartbeatSimulationSeconds = 2.0f;
	Config.AuthorityMode = EEdenOsAuthorityMode::Advisory;
	Config.DefaultScenarioId = TEXT("SolarEventEmergency");
	TestTrue(TEXT("Live config accepted"), Adapter->ApplyRuntimeConfig(Config));
	State->CommandProposalCountAtStart = Adapter->GetRequestedCommandProposalEvaluationCountForTesting();

	State->Controller->StartMission();
	TestEqual(
		TEXT("Solar Event mission running"),
		World->GetSubsystem<UEdenMissionSubsystem>()->GetMissionState(),
		EEdenMissionState::Running);

	auto Phase = [](const TCHAR* Name,
		EEdenThermalControlMode Thermal,
		EEdenLoadShedMode Shed,
		EEdenPropulsionPriorityMode Propulsion,
		float Temperature = -1.0f,
		bool bCustom = false)
	{
		FPhase Out;
		Out.Name = Name;
		Out.ThermalMode = Thermal;
		Out.LoadShedMode = Shed;
		Out.Propulsion = Propulsion;
		Out.SetTemperatureCelsius = Temperature;
		Out.bCustomThresholds = bCustom;
		return Out;
	};
	using T = EEdenThermalControlMode;
	using L = EEdenLoadShedMode;
	using P = EEdenPropulsionPriorityMode;
	State->Phases = {
		Phase(TEXT("hot-nominal"), T::Nominal, L::Normal, P::Full, 80.0f),
		Phase(TEXT("boost"), T::Boost, L::Normal, P::Full),
		Phase(TEXT("boost-shed"), T::Boost, L::Shed, P::Full),
		Phase(TEXT("emergency-normal"), T::Emergency, L::Normal, P::Full),
		Phase(TEXT("emergency-reduced-propulsion"), T::Emergency, L::Normal, P::Reduced),
		Phase(TEXT("custom-thresholds"), T::Nominal, L::Normal, P::Full, 65.0f, true),
	};

	for (int32 Index = 0; Index < State->Phases.Num(); ++Index)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FRunPhaseCommand(this, State, 45.0));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FWriteEvidenceCommand(this, State));
	return true;
}

#endif
