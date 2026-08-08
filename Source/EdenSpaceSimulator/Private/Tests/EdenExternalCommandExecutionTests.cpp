// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenOs/EdenExternalCommandExecutor.h"
#include "EdenOs/EdenExternalCommandModel.h"
#include "EdenOs/EdenExternalCommandRouter.h"
#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsAdvisoryModel.h"
#include "EdenOs/EdenOsConnectionSettings.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Missions/EdenMissionSubsystem.h"
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

namespace EdenExternalCommandExecutionTests
{
	FEdenOsConnectionConfig MakeFullGateConfig()
	{
		FEdenOsConnectionConfig Config;
		Config.bEnabled = true;
		Config.BaseUrl = TEXT("https://example.test");
		Config.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
		Config.bExternalCommandValidationEnabled = true;
		Config.bExternalCommandExecutionEnabled = true;
		Config.RuntimeBearerJwt = TEXT("test-token");
		return Config;
	}

	FEdenExternalCommandExecutionContext MakePermittingExecutionContext(
		const FString& SessionId = TEXT("session-k"),
		const FString& EvaluationId = TEXT("eval-k-1"))
	{
		FEdenExternalCommandExecutionContext Context;
		Context.bExternalCommandExecutionEnabled = true;
		Context.bExternalCommandValidationEnabled = true;
		Context.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
		Context.ActiveSessionId = SessionId;
		Context.bHasAcceptedEvaluation = true;
		Context.LatestAcceptedEvaluationId = EvaluationId;
		Context.AttemptSimulationTimeSeconds = 1.25f;
		return Context;
	}

	FEdenExternalCommandProposal MakeThermalProposal(
		const FString& ProposalId,
		EEdenThermalControlMode Mode,
		const FString& SessionId,
		const FString& EvaluationId)
	{
		FEdenExternalCommandProposal Proposal;
		Proposal.SchemaVersion = EdenExternalCommandContract::CurrentSchemaVersion;
		Proposal.ProposalId = ProposalId;
		Proposal.SessionId = SessionId;
		Proposal.EvaluationId = EvaluationId;
		Proposal.CommandType = EEdenExternalCommandType::SetThermalControlMode;
		Proposal.Parameters = FEdenExternalCommandModel::MakeThermalParameters(Mode);
		return Proposal;
	}

	FEdenOsAcceptedAdvisory MakeAcceptedAdvisory(const FString& EvaluationId)
	{
		FEdenOsAcceptedAdvisory Advisory;
		Advisory.bIsValid = true;
		Advisory.AdvisoryId = TEXT("adv-k-1");
		Advisory.EvaluationId = EvaluationId;
		Advisory.Recommendation = TEXT("Increase cooling.");
		Advisory.Rationale = TEXT("Thermal trend.");
		Advisory.IssuedSimulationTimeSeconds = 1.0f;
		Advisory.EvaluationSimulationTimeSeconds = 0.5f;
		Advisory.ContextSnapshotSimulationTimeSeconds = 0.5f;
		return Advisory;
	}

	struct FScopedWorld
	{
		FWorldContext* WorldContext = nullptr;
		UWorld* World = nullptr;

		FScopedWorld()
		{
			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("EdenExternalCommandExecWorld"),
				EUniqueObjectNameOptions::GloballyUnique);
			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			WorldContext->SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
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

	struct FOperatorFixture
	{
		AActor* Owner = nullptr;
		UEdenPowerSystemComponent* Power = nullptr;
		UEdenThermalSystemComponent* Thermal = nullptr;
		UEdenFlightMovementComponent* Flight = nullptr;
		UEdenOperatorControlComponent* Operator = nullptr;

		explicit FOperatorFixture(UWorld* World)
		{
			Owner = World->SpawnActor<AActor>();
			Power = NewObject<UEdenPowerSystemComponent>(Owner);
			Thermal = NewObject<UEdenThermalSystemComponent>(Owner);
			Flight = NewObject<UEdenFlightMovementComponent>(Owner);
			Operator = NewObject<UEdenOperatorControlComponent>(Owner);
			Power->RegisterComponent();
			Thermal->RegisterComponent();
			Flight->RegisterComponent();
			Operator->RegisterComponent();

			FEdenPowerConfig PowerConfig;
			PowerConfig.BatteryCapacityKilowattHours = 10.0f;
			PowerConfig.GenerationKilowatts = 1.0f;
			PowerConfig.BaselineDemandKilowatts = 2.0f;
			PowerConfig.InitialChargeFraction = 1.0f;
			PowerConfig.WarningThresholdFraction = 0.25f;
			PowerConfig.CriticalThresholdFraction = 0.1f;

			FEdenThermalConfig ThermalConfig;
			ThermalConfig.AbsoluteMinTemperatureCelsius = -50.0f;
			ThermalConfig.AmbientTemperatureCelsius = 20.0f;
			ThermalConfig.WarningTemperatureCelsius = 70.0f;
			ThermalConfig.CriticalTemperatureCelsius = 100.0f;
			ThermalConfig.AbsoluteMaxTemperatureCelsius = 120.0f;
			ThermalConfig.InitialTemperatureCelsius = 20.0f;
			ThermalConfig.HeatGenerationDegreesCelsiusPerSecond = 10.0f;
			ThermalConfig.DissipationDegreesCelsiusPerSecond = 2.0f;

			Power->InitializePowerSimulation(PowerConfig);
			Thermal->InitializeThermalSimulation(ThermalConfig);

			FEdenOperatorControlConfig OpConfig;
			OpConfig.BoostDissipationDegreesCelsiusPerSecond = 1.0f;
			OpConfig.EmergencyDissipationDegreesCelsiusPerSecond = 2.0f;
			OpConfig.BoostCoolingDemandKilowatts = 1.5f;
			OpConfig.EmergencyCoolingDemandKilowatts = 4.5f;
			OpConfig.LoadShedDemandReductionKilowatts = 2.0f;
			OpConfig.LoadShedDissipationReductionDegreesCelsiusPerSecond = 0.4f;
			OpConfig.ReducedThrustAuthority = 0.5f;
			Operator->InitializeOperatorControl(OpConfig);
		}
	};

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
	FEdenExternalCommandExecutionDisabledByDefaultTest,
	"Eden.Unit.EdenOs.ExternalCommand.Execution.ExecutionDisabledByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandExecutionDisabledByDefaultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("Config default"), FEdenOsConnectionConfig().bExternalCommandExecutionEnabled);
	TestFalse(TEXT("Settings default"), GetDefault<UEdenOsConnectionSettings>()->bExternalCommandExecutionEnabled);

	UEdenExternalCommandExecutor* Executor = NewObject<UEdenExternalCommandExecutor>();
	FEdenValidatedExternalCommand Command;
	Command.ProposalId = TEXT("p1");
	Command.SessionId = TEXT("s1");
	Command.EvaluationId = TEXT("e1");
	Command.CommandType = EEdenExternalCommandType::SetThermalControlMode;
	Command.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Boost);

	const FEdenExternalCommandExecutionResult Result =
		Executor->ExecuteValidatedCommand(Command, FEdenExternalCommandExecutionContext(), nullptr);
	TestEqual(
		TEXT("Default rejects ExecutionDisabled"),
		Result.RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::ExecutionDisabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandObserveCannotExecuteTest,
	"Eden.Unit.EdenOs.ExternalCommand.Execution.ObserveCannotExecute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandObserveCannotExecuteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;
	UEdenExternalCommandExecutor* Executor = NewObject<UEdenExternalCommandExecutor>();
	FEdenValidatedExternalCommand Command;
	Command.ProposalId = TEXT("p-obs");
	Command.SessionId = TEXT("session-k");
	Command.EvaluationId = TEXT("eval-k-1");
	Command.CommandType = EEdenExternalCommandType::SetThermalControlMode;
	Command.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Boost);

	FEdenExternalCommandExecutionContext Context = MakePermittingExecutionContext();
	Context.AuthorityMode = EEdenOsAuthorityMode::Observe;
	TestEqual(
		TEXT("Observe rejects"),
		Executor->ExecuteValidatedCommand(Command, Context, nullptr).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::WrongAuthorityMode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAdvisoryCannotExecuteTest,
	"Eden.Unit.EdenOs.ExternalCommand.Execution.AdvisoryCannotExecute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAdvisoryCannotExecuteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;
	UEdenExternalCommandExecutor* Executor = NewObject<UEdenExternalCommandExecutor>();
	FEdenValidatedExternalCommand Command;
	Command.ProposalId = TEXT("p-adv");
	Command.SessionId = TEXT("session-k");
	Command.EvaluationId = TEXT("eval-k-1");
	Command.CommandType = EEdenExternalCommandType::SetThermalControlMode;
	Command.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Boost);

	FEdenExternalCommandExecutionContext Context = MakePermittingExecutionContext();
	Context.AuthorityMode = EEdenOsAuthorityMode::Advisory;
	TestEqual(
		TEXT("Advisory rejects"),
		Executor->ExecuteValidatedCommand(Command, Context, nullptr).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::WrongAuthorityMode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAuthorizedValidationDisabledCannotExecuteTest,
	"Eden.Unit.EdenOs.ExternalCommand.Execution.AuthorizedControlCannotExecuteWithValidationDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAuthorizedValidationDisabledCannotExecuteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;
	UEdenExternalCommandExecutor* Executor = NewObject<UEdenExternalCommandExecutor>();
	FEdenValidatedExternalCommand Command;
	Command.ProposalId = TEXT("p-valoff");
	Command.SessionId = TEXT("session-k");
	Command.EvaluationId = TEXT("eval-k-1");
	Command.CommandType = EEdenExternalCommandType::SetThermalControlMode;
	Command.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Boost);

	FEdenExternalCommandExecutionContext Context = MakePermittingExecutionContext();
	Context.bExternalCommandValidationEnabled = false;
	TestEqual(
		TEXT("Validation disabled rejects"),
		Executor->ExecuteValidatedCommand(Command, Context, nullptr).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::ValidationBoundaryDisabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAuthorizedExecutionDisabledCannotExecuteTest,
	"Eden.Unit.EdenOs.ExternalCommand.Execution.AuthorizedControlCannotExecuteWithExecutionDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAuthorizedExecutionDisabledCannotExecuteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;
	UEdenExternalCommandExecutor* Executor = NewObject<UEdenExternalCommandExecutor>();
	FEdenValidatedExternalCommand Command;
	Command.ProposalId = TEXT("p-exeoff");
	Command.SessionId = TEXT("session-k");
	Command.EvaluationId = TEXT("eval-k-1");
	Command.CommandType = EEdenExternalCommandType::SetThermalControlMode;
	Command.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Boost);

	FEdenExternalCommandExecutionContext Context = MakePermittingExecutionContext();
	Context.bExternalCommandExecutionEnabled = false;
	TestEqual(
		TEXT("Execution disabled rejects"),
		Executor->ExecuteValidatedCommand(Command, Context, nullptr).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::ExecutionDisabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAuthorizedCanExecuteWhenAllGatesEnabledTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.AuthorizedControlCanExecuteWhenAllGatesEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAuthorizedCanExecuteWhenAllGatesEnabledTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("k-gate-ok"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Validates"), Validated.IsValid());
	TestTrue(TEXT("Has artifact"), Validated.bHasValidatedCommand);

	const FEdenExternalCommandExecutionResult Result =
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand);
	TestTrue(TEXT("Executes"), Result.IsExecuted());
	TestEqual(
		TEXT("Thermal converged"),
		Fixture.Operator->GetOperatorIntent().ThermalMode,
		EEdenThermalControlMode::Boost);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandRawProposalCannotBypassValidationTest,
	"Eden.Unit.EdenOs.ExternalCommand.Execution.RawProposalCannotBypassValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandRawProposalCannotBypassValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	// Hand-crafted "validated" artifact that never passed J must fail structural check.
	FEdenValidatedExternalCommand Bogus;
	Bogus.ProposalId = TEXT("");
	Bogus.SessionId = TEXT("session-k");
	Bogus.EvaluationId = TEXT("eval-k-1");
	Bogus.CommandType = EEdenExternalCommandType::SetThermalControlMode;
	Bogus.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Boost);

	UEdenExternalCommandExecutor* Executor = NewObject<UEdenExternalCommandExecutor>();
	TestEqual(
		TEXT("Empty proposal id rejected"),
		Executor->ExecuteValidatedCommand(Bogus, MakePermittingExecutionContext(), nullptr).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::InvalidValidatedCommand);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidatedArtifactBindsExactParametersTest,
	"Eden.Unit.EdenOs.ExternalCommand.Execution.ValidatedArtifactBindsExactParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidatedArtifactBindsExactParametersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	UEdenExternalCommandRouter* Router = NewObject<UEdenExternalCommandRouter>();
	FEdenExternalCommandValidationContext ValidationContext;
	ValidationContext.bExternalCommandValidationEnabled = true;
	ValidationContext.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	ValidationContext.ActiveSessionId = TEXT("session-k");
	ValidationContext.bHasAcceptedEvaluation = true;
	ValidationContext.LatestAcceptedEvaluationId = TEXT("eval-k-1");

	const FEdenExternalCommandProposal Proposal = MakeThermalProposal(
		TEXT("bind-params"),
		EEdenThermalControlMode::Emergency,
		TEXT("session-k"),
		TEXT("eval-k-1"));
	const FEdenExternalCommandValidationOutcome Outcome = Router->ValidateProposal(Proposal, ValidationContext);
	TestTrue(TEXT("Valid"), Outcome.IsValid());
	TestTrue(TEXT("Artifact present"), Outcome.bHasValidatedCommand);
	TestEqual(TEXT("ProposalId bound"), Outcome.ValidatedCommand.ProposalId, Proposal.ProposalId);
	TestEqual(TEXT("Session bound"), Outcome.ValidatedCommand.SessionId, Proposal.SessionId);
	TestEqual(TEXT("Eval bound"), Outcome.ValidatedCommand.EvaluationId, Proposal.EvaluationId);
	TestEqual(TEXT("Type bound"), Outcome.ValidatedCommand.CommandType, Proposal.CommandType);
	TestEqual(
		TEXT("Thermal mode bound"),
		Outcome.ValidatedCommand.Parameters.ThermalMode,
		EEdenThermalControlMode::Emergency);
	TestEqual(
		TEXT("Kind bound"),
		Outcome.ValidatedCommand.Parameters.Kind,
		EEdenExternalCommandParameterKind::ThermalControlMode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandNewerEvaluationInvalidatesValidatedCommandTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.NewerEvaluationInvalidatesValidatedCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandNewerEvaluationInvalidatesValidatedCommandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	(void)Fixture;
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-old")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("stale-eval"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-old")));
	TestTrue(TEXT("Validated against old"), Validated.IsValid());

	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-new")));
	TestEqual(
		TEXT("Stale eval rejected"),
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::EvaluationMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandSessionResetInvalidatesValidatedCommandTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.SessionResetInvalidatesValidatedCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandSessionResetInvalidatesValidatedCommandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	(void)Fixture;
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FString OldSession = Telemetry->GetSessionId();
	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(TEXT("stale-session"), EEdenThermalControlMode::Boost, OldSession, TEXT("eval-k-1")));
	TestTrue(TEXT("Validated"), Validated.IsValid());

	Telemetry->ClearHistory();
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));
	TestEqual(
		TEXT("Session mismatch"),
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::SessionMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandProposalCanApplyAtMostOnceTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.ProposalCanApplyAtMostOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandProposalCanApplyAtMostOnceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("once-only"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("First executes"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());
	TestEqual(
		TEXT("Second rejected"),
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::ProposalAlreadyAttempted);
	TestEqual(
		TEXT("Mode remains Boost"),
		Fixture.Operator->GetOperatorIntent().ThermalMode,
		EEdenThermalControlMode::Boost);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandFailedPreflightDoesNotConsumeApplyAttemptTest,
	"Eden.Unit.EdenOs.ExternalCommand.Execution.FailedPreflightDoesNotConsumeApplyAttempt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandFailedPreflightDoesNotConsumeApplyAttemptTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenExternalCommandExecutor* Executor = NewObject<UEdenExternalCommandExecutor>();

	FEdenValidatedExternalCommand Command;
	Command.ProposalId = TEXT("preflight-retry");
	Command.SessionId = TEXT("session-k");
	Command.EvaluationId = TEXT("eval-k-1");
	Command.CommandType = EEdenExternalCommandType::SetThermalControlMode;
	Command.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Boost);

	FEdenExternalCommandExecutionContext Context = MakePermittingExecutionContext();
	Context.bExternalCommandExecutionEnabled = false;
	TestEqual(
		TEXT("Preflight rejects"),
		Executor->ExecuteValidatedCommand(Command, Context, Fixture.Operator).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::ExecutionDisabled);
	TestFalse(TEXT("Not consumed"), Executor->GetAttemptedProposalIdsForTesting().Contains(Command.ProposalId));

	Context.bExternalCommandExecutionEnabled = true;
	TestTrue(
		TEXT("Later succeeds"),
		Executor->ExecuteValidatedCommand(Command, Context, Fixture.Operator).IsExecuted());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandSameCommandTypeCannotApplyTwiceForOneEvaluationTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.SameCommandTypeCannotApplyTwiceForOneEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandSameCommandTypeCannotApplyTwiceForOneEvaluationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	(void)Fixture;
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome First = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("thermal-a"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("First executes"), Adapter->ExecuteValidatedExternalCommand(First.ValidatedCommand).IsExecuted());

	const FEdenExternalCommandValidationOutcome Second = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("thermal-b"),
			EEdenThermalControlMode::Emergency,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Second validates"), Second.IsValid());
	TestEqual(
		TEXT("Same type blocked"),
		Adapter->ExecuteValidatedExternalCommand(Second.ValidatedCommand).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::EvaluationCommandTypeAlreadyAttempted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandDifferentCommandTypesCanApplyForOneEvaluationTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.DifferentCommandTypesCanApplyForOneEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandDifferentCommandTypesCanApplyForOneEvaluationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FString Session = Telemetry->GetSessionId();
	const FEdenExternalCommandValidationOutcome Thermal = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(TEXT("multi-thermal"), EEdenThermalControlMode::Boost, Session, TEXT("eval-k-1")));

	FEdenExternalCommandProposal LoadShed = MakeThermalProposal(
		TEXT("multi-loadshed"),
		EEdenThermalControlMode::Nominal,
		Session,
		TEXT("eval-k-1"));
	LoadShed.CommandType = EEdenExternalCommandType::SetLoadShedMode;
	LoadShed.Parameters = FEdenExternalCommandModel::MakeLoadShedParameters(EEdenLoadShedMode::Shed);
	const FEdenExternalCommandValidationOutcome Shed = Adapter->ValidateExternalCommandProposal(LoadShed);

	TestTrue(TEXT("Thermal executes"), Adapter->ExecuteValidatedExternalCommand(Thermal.ValidatedCommand).IsExecuted());
	TestTrue(TEXT("LoadShed executes"), Adapter->ExecuteValidatedExternalCommand(Shed.ValidatedCommand).IsExecuted());
	TestEqual(TEXT("Thermal mode"), Fixture.Operator->GetOperatorIntent().ThermalMode, EEdenThermalControlMode::Boost);
	TestEqual(TEXT("Load shed"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Shed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandNewEvaluationReopensCommandTypeEligibilityTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.NewEvaluationReopensCommandTypeEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandNewEvaluationReopensCommandTypeEligibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-1")));

	const FString Session = Telemetry->GetSessionId();
	const FEdenExternalCommandValidationOutcome First = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(TEXT("reopen-1"), EEdenThermalControlMode::Boost, Session, TEXT("eval-1")));
	TestTrue(TEXT("First executes"), Adapter->ExecuteValidatedExternalCommand(First.ValidatedCommand).IsExecuted());

	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-2")));
	const FEdenExternalCommandValidationOutcome Second = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(TEXT("reopen-2"), EEdenThermalControlMode::Emergency, Session, TEXT("eval-2")));
	TestTrue(TEXT("Second executes"), Adapter->ExecuteValidatedExternalCommand(Second.ValidatedCommand).IsExecuted());
	TestEqual(
		TEXT("Emergency applied"),
		Fixture.Operator->GetOperatorIntent().ThermalMode,
		EEdenThermalControlMode::Emergency);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAlreadyRequestedModeIsTerminalNoOpTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.AlreadyRequestedModeIsTerminalNoOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAlreadyRequestedModeIsTerminalNoOpTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	TestTrue(
		TEXT("Human sets Boost"),
		Fixture.Operator->SetThermalControlMode(EEdenThermalControlMode::Boost));
	TestEqual(
		TEXT("Human provenance"),
		Fixture.Operator->GetLastCommandSource(),
		EEdenOperatorCommandSource::HumanOperator);

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("noop-boost"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	const FEdenExternalCommandExecutionResult Result =
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand);
	TestTrue(TEXT("No-op"), Result.IsNoOp());
	TestEqual(
		TEXT("Setter not called — provenance unchanged"),
		Fixture.Operator->GetLastCommandSource(),
		EEdenOperatorCommandSource::HumanOperator);

	const TArray<FEdenTelemetryEvent> History = Telemetry->GetEventHistory();
	TestEqual(
		TEXT("No Executed telemetry"),
		CountEvents(History, EEdenTelemetryEventType::EdenExternalCommandExecuted),
		0);

	TestEqual(
		TEXT("Slot consumed"),
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::ProposalAlreadyAttempted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandThermalCommandConvergesThroughOperatorComponentTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.ThermalCommandConvergesThroughOperatorComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandThermalCommandConvergesThroughOperatorComponentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("thermal-converge"),
			EEdenThermalControlMode::Emergency,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());
	TestEqual(
		TEXT("Thermal"),
		Fixture.Operator->GetOperatorIntent().ThermalMode,
		EEdenThermalControlMode::Emergency);
	TestTrue(
		TEXT("Power demand changed via operator path"),
		Fixture.Power->GetPowerStateSnapshot().OperatorDemandKilowatts > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandLoadShedCommandConvergesThroughOperatorComponentTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.LoadShedCommandConvergesThroughOperatorComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandLoadShedCommandConvergesThroughOperatorComponentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	FEdenExternalCommandProposal Proposal = MakeThermalProposal(
		TEXT("shed-converge"),
		EEdenThermalControlMode::Nominal,
		Telemetry->GetSessionId(),
		TEXT("eval-k-1"));
	Proposal.CommandType = EEdenExternalCommandType::SetLoadShedMode;
	Proposal.Parameters = FEdenExternalCommandModel::MakeLoadShedParameters(EEdenLoadShedMode::Shed);
	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(Proposal);
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());
	TestEqual(TEXT("Load shed"), Fixture.Operator->GetOperatorIntent().LoadShedMode, EEdenLoadShedMode::Shed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandPropulsionCommandConvergesThroughOperatorComponentTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.PropulsionCommandConvergesThroughOperatorComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandPropulsionCommandConvergesThroughOperatorComponentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	FEdenExternalCommandProposal Proposal = MakeThermalProposal(
		TEXT("prop-converge"),
		EEdenThermalControlMode::Nominal,
		Telemetry->GetSessionId(),
		TEXT("eval-k-1"));
	Proposal.CommandType = EEdenExternalCommandType::SetPropulsionPriorityMode;
	Proposal.Parameters =
		FEdenExternalCommandModel::MakePropulsionPriorityParameters(EEdenPropulsionPriorityMode::Reduced);
	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(Proposal);
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());
	TestEqual(
		TEXT("Propulsion"),
		Fixture.Operator->GetOperatorIntent().PropulsionPriority,
		EEdenPropulsionPriorityMode::Reduced);
	TestEqual(TEXT("Thrust authority reduced"), Fixture.Flight->GetThrustAuthority(), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandSuccessfulExecutionEmitsExactlyOneExecutedEventTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.SuccessfulExecutionEmitsExactlyOneEdenExternalCommandExecuted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandSuccessfulExecutionEmitsExactlyOneExecutedEventTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	(void)Fixture;
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("emit-one"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());

	const TArray<FEdenTelemetryEvent> History = Telemetry->GetEventHistory();
	TestEqual(
		TEXT("Exactly one Executed"),
		CountEvents(History, EEdenTelemetryEventType::EdenExternalCommandExecuted),
		1);

	const FEdenTelemetryEvent* ExecutedEvent = nullptr;
	for (const FEdenTelemetryEvent& Event : History)
	{
		if (Event.EventType == EEdenTelemetryEventType::EdenExternalCommandExecuted)
		{
			ExecutedEvent = &Event;
			break;
		}
	}
	TestNotNull(TEXT("Event found"), ExecutedEvent);
	if (ExecutedEvent)
	{
		TestTrue(TEXT("Detail has ProposalId"), ExecutedEvent->Detail.Contains(TEXT("emit-one")));
		TestTrue(TEXT("Detail has SessionId"), ExecutedEvent->Detail.Contains(Telemetry->GetSessionId()));
		TestTrue(TEXT("Detail has EvaluationId"), ExecutedEvent->Detail.Contains(TEXT("eval-k-1")));
		TestTrue(TEXT("Detail has CommandType"), ExecutedEvent->Detail.Contains(TEXT("SetThermalControlMode")));
		TestTrue(TEXT("Detail has PreviousMode"), ExecutedEvent->Detail.Contains(TEXT("PreviousMode")));
		TestTrue(TEXT("Detail has RequestedMode"), ExecutedEvent->Detail.Contains(TEXT("RequestedMode")));
		TestTrue(TEXT("Detail has ResultingMode"), ExecutedEvent->Detail.Contains(TEXT("ResultingMode")));
		TestTrue(
			TEXT("Provenance source"),
			ExecutedEvent->Detail.Contains(TEXT("eden_authorized_control")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandEdenExecutionDoesNotEmitHumanOperatorActionTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.EdenExecutionDoesNotEmitHumanOperatorAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandEdenExecutionDoesNotEmitHumanOperatorActionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Telemetry->BindOperatorControlForTesting(Fixture.Operator);
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const int32 HumanBefore = CountEvents(Telemetry->GetEventHistory(), EEdenTelemetryEventType::OperatorCommandIssued);

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("no-human"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());
	TestEqual(
		TEXT("Source is Eden"),
		Fixture.Operator->GetLastCommandSource(),
		EEdenOperatorCommandSource::EdenAuthorizedControl);

	const TArray<FEdenTelemetryEvent> History = Telemetry->GetEventHistory();
	TestEqual(
		TEXT("No new OperatorCommandIssued"),
		CountEvents(History, EEdenTelemetryEventType::OperatorCommandIssued),
		HumanBefore);
	TestEqual(
		TEXT("Executed present"),
		CountEvents(History, EEdenTelemetryEventType::EdenExternalCommandExecuted),
		1);

	EEdenOsAdvisoryTriggerReason Reason = EEdenOsAdvisoryTriggerReason::Heartbeat;
	TestFalse(
		TEXT("Executed is not operator_action trigger"),
		FEdenOsAdvisoryModel::TryGetTriggerReasonForEventType(
			EEdenTelemetryEventType::EdenExternalCommandExecuted,
			Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidationAloneStillDoesNotExecuteTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.ValidationAloneStillDoesNotExecute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidationAloneStillDoesNotExecuteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const EEdenThermalControlMode Before = Fixture.Operator->GetOperatorIntent().ThermalMode;
	TestTrue(
		TEXT("Validates"),
		Adapter
			->ValidateExternalCommandProposal(MakeThermalProposal(
				TEXT("validate-only"),
				EEdenThermalControlMode::Emergency,
				Telemetry->GetSessionId(),
				TEXT("eval-k-1")))
			.IsValid());
	TestEqual(TEXT("Ship untouched"), Fixture.Operator->GetOperatorIntent().ThermalMode, Before);
	TestEqual(
		TEXT("No Executed event"),
		CountEvents(Telemetry->GetEventHistory(), EEdenTelemetryEventType::EdenExternalCommandExecuted),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandExecutionDoesNotDirectlyMutateMissionTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.ExecutionDoesNotDirectlyMutateMission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandExecutionDoesNotDirectlyMutateMissionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenMissionSubsystem* Mission = Scoped.World->GetSubsystem<UEdenMissionSubsystem>();
	const EEdenMissionState StateBefore = Mission->GetMissionStateSnapshot().MissionState;
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("no-mission"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());
	TestEqual(TEXT("Mission unchanged"), Mission->GetMissionStateSnapshot().MissionState, StateBefore);
	TestEqual(
		TEXT("Operator changed"),
		Fixture.Operator->GetOperatorIntent().ThermalMode,
		EEdenThermalControlMode::Boost);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandExecutionDoesNotDirectlyMutateFuelPowerThermalTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.ExecutionDoesNotDirectlyMutateFuelPowerThermal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandExecutionDoesNotDirectlyMutateFuelPowerThermalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>(Fixture.Owner);
	Fuel->RegisterComponent();
	FEdenFuelConfig FuelConfig;
	FuelConfig.CapacityKilograms = 100.0f;
	FuelConfig.ConsumptionRateKilogramsPerSecond = 10.0f;
	FuelConfig.InitialFuelFraction = 1.0f;
	FuelConfig.WarningThresholdFraction = 0.25f;
	FuelConfig.CriticalThresholdFraction = 0.1f;
	Fuel->InitializeFuelSimulation(FuelConfig);

	const float FuelBefore = Fuel->GetFuelStateSnapshot().FuelFraction;
	const float ChargeBefore = Fixture.Power->GetPowerStateSnapshot().ChargeFraction;
	const float TempBefore = Fixture.Thermal->GetThermalStateSnapshot().TemperatureCelsius;

	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("no-res"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());

	TestEqual(TEXT("Fuel fraction unchanged"), Fuel->GetFuelStateSnapshot().FuelFraction, FuelBefore);
	TestEqual(TEXT("Charge unchanged"), Fixture.Power->GetPowerStateSnapshot().ChargeFraction, ChargeBefore);
	TestEqual(TEXT("Temp unchanged"), Fixture.Thermal->GetThermalStateSnapshot().TemperatureCelsius, TempBefore);
	TestTrue(
		TEXT("Operator demand may change"),
		Fixture.Power->GetPowerStateSnapshot().OperatorDemandKilowatts > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandExecutionDoesNotMutateClockTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.ExecutionDoesNotMutateClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandExecutionDoesNotMutateClockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	(void)Fixture;
	UEdenSimulationClockSubsystem* Clock = Scoped.World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->ResetSimulationClock();
	const float Before = Clock->GetElapsedSimulationTimeSeconds();

	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("no-clock"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());
	TestEqual(TEXT("Clock unchanged"), Clock->GetElapsedSimulationTimeSeconds(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandOperatorControlUnavailableRejectsWithoutMutationTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.OperatorControlUnavailableRejectsWithoutMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandOperatorControlUnavailableRejectsWithoutMutationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("no-operator"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestEqual(
		TEXT("Unavailable"),
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::OperatorControlUnavailable);
	TestFalse(
		TEXT("Apply not consumed"),
		Adapter->GetExternalCommandExecutionHistory().Num() > 0
			&& Adapter->GetExternalCommandExecutionHistory().Last().Outcome
				== EEdenExternalCommandExecutionOutcome::Executed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandConvergenceFailureIsTerminalAndNotRetriedTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.ConvergenceFailureIsTerminalAndNotRetried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandConvergenceFailureIsTerminalAndNotRetriedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("converge-fail"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));

	// Break operator apply targets so the mutator is invoked but cannot converge.
	Fixture.Thermal->DestroyComponent();
	Fixture.Thermal = nullptr;

	const FEdenExternalCommandExecutionResult Failed =
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand);
	TestEqual(
		TEXT("ConvergenceFailed"),
		Failed.RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::ConvergenceFailed);
	TestEqual(
		TEXT("Retry rejected"),
		Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).RejectionReason,
		EEdenExternalCommandExecutionRejectionReason::ProposalAlreadyAttempted);
	TestEqual(
		TEXT("No Executed telemetry"),
		CountEvents(Telemetry->GetEventHistory(), EEdenTelemetryEventType::EdenExternalCommandExecuted),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandNormalFixedStepMayObserveChangedOperatorModeAfterExecutionTest,
	"Eden.Integration.EdenOs.ExternalCommand.Execution.NormalFixedStepMayObserveChangedOperatorModeAfterExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandNormalFixedStepMayObserveChangedOperatorModeAfterExecutionTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandExecutionTests;

	FScopedWorld Scoped;
	FOperatorFixture Fixture(Scoped.World);
	UEdenSimulationClockSubsystem* Clock = Scoped.World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->ResetSimulationClock();

	UEdenOsAdapterSubsystem* Adapter = Scoped.World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = Scoped.World->GetSubsystem<UEdenTelemetrySubsystem>();
	Adapter->ApplyRuntimeConfig(MakeFullGateConfig());
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-k-1")));

	const FEdenExternalCommandValidationOutcome Validated = Adapter->ValidateExternalCommandProposal(
		MakeThermalProposal(
			TEXT("after-step"),
			EEdenThermalControlMode::Boost,
			Telemetry->GetSessionId(),
			TEXT("eval-k-1")));
	TestTrue(TEXT("Executed"), Adapter->ExecuteValidatedExternalCommand(Validated.ValidatedCommand).IsExecuted());

	Clock->Tick(0.1f);
	TestEqual(
		TEXT("Operator mode still Boost after step"),
		Fixture.Operator->GetOperatorIntent().ThermalMode,
		EEdenThermalControlMode::Boost);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
