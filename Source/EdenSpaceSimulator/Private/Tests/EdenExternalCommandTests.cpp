// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenExternalCommandModel.h"
#include "EdenOs/EdenExternalCommandRouter.h"
#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsConnectionSettings.h"
#include "Core/EdenSimulationClockSubsystem.h"
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

namespace EdenExternalCommandTests
{
	FEdenExternalCommandValidationContext MakePermittingContext(
		const FString& SessionId = TEXT("session-j"),
		const FString& EvaluationId = TEXT("eval-j-1"))
	{
		FEdenExternalCommandValidationContext Context;
		Context.bExternalCommandValidationEnabled = true;
		Context.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
		Context.ActiveSessionId = SessionId;
		Context.bHasAcceptedEvaluation = true;
		Context.LatestAcceptedEvaluationId = EvaluationId;
		return Context;
	}

	FEdenExternalCommandProposal MakeThermalProposal(
		const FString& ProposalId = TEXT("proposal-thermal-1"),
		EEdenThermalControlMode Mode = EEdenThermalControlMode::Boost,
		const FString& SessionId = TEXT("session-j"),
		const FString& EvaluationId = TEXT("eval-j-1"))
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

	FEdenExternalCommandProposal MakeLoadShedProposal(
		const FString& ProposalId = TEXT("proposal-loadshed-1"),
		EEdenLoadShedMode Mode = EEdenLoadShedMode::Shed)
	{
		FEdenExternalCommandProposal Proposal = MakeThermalProposal(ProposalId);
		Proposal.CommandType = EEdenExternalCommandType::SetLoadShedMode;
		Proposal.Parameters = FEdenExternalCommandModel::MakeLoadShedParameters(Mode);
		return Proposal;
	}

	FEdenExternalCommandProposal MakePropulsionProposal(
		const FString& ProposalId = TEXT("proposal-propulsion-1"),
		EEdenPropulsionPriorityMode Mode = EEdenPropulsionPriorityMode::Reduced)
	{
		FEdenExternalCommandProposal Proposal = MakeThermalProposal(ProposalId);
		Proposal.CommandType = EEdenExternalCommandType::SetPropulsionPriorityMode;
		Proposal.Parameters = FEdenExternalCommandModel::MakePropulsionPriorityParameters(Mode);
		return Proposal;
	}

	struct FScopedExternalCommandWorld
	{
		FWorldContext* WorldContext = nullptr;
		UWorld* World = nullptr;

		FScopedExternalCommandWorld()
		{
			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("EdenExternalCommandWorld"),
				EUniqueObjectNameOptions::GloballyUnique);

			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			check(World);
			World->AddToRoot();
			WorldContext->SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FScopedExternalCommandWorld()
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

	FEdenOsAcceptedAdvisory MakeAcceptedAdvisory(
		const FString& EvaluationId,
		const FString& Recommendation = TEXT("Increase cooling and maintain load shedding."))
	{
		FEdenOsAcceptedAdvisory Advisory;
		Advisory.bIsValid = true;
		Advisory.AdvisoryId = TEXT("adv-j-1");
		Advisory.EvaluationId = EvaluationId;
		Advisory.Recommendation = Recommendation;
		Advisory.Rationale = TEXT("Thermal trend warrants operator attention.");
		Advisory.IssuedSimulationTimeSeconds = 1.0f;
		Advisory.EvaluationSimulationTimeSeconds = 0.5f;
		Advisory.ContextSnapshotSimulationTimeSeconds = 0.5f;
		return Advisory;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandBoundaryDisabledByDefaultTest,
	"Eden.Unit.EdenOs.ExternalCommand.BoundaryDisabledByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandBoundaryDisabledByDefaultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestFalse(TEXT("Config default disables validation"), FEdenOsConnectionConfig().bExternalCommandValidationEnabled);
	TestFalse(
		TEXT("Settings default disables validation"),
		GetDefault<UEdenOsConnectionSettings>()->bExternalCommandValidationEnabled);

	const FEdenExternalCommandValidationOutcome Outcome = FEdenExternalCommandModel::ValidateProposal(
		EdenExternalCommandTests::MakeThermalProposal(),
		FEdenExternalCommandValidationContext(),
		false);
	TestFalse(TEXT("Default context rejects"), Outcome.IsValid());
	TestEqual(
		TEXT("Default rejection is BoundaryDisabled"),
		Outcome.RejectionReason,
		EEdenExternalCommandRejectionReason::BoundaryDisabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandObserveRejectsProposalTest,
	"Eden.Unit.EdenOs.ExternalCommand.ObserveRejectsProposal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandObserveRejectsProposalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();
	Context.AuthorityMode = EEdenOsAuthorityMode::Observe;
	const FEdenExternalCommandValidationOutcome Outcome =
		FEdenExternalCommandModel::ValidateProposal(EdenExternalCommandTests::MakeThermalProposal(), Context, false);
	TestEqual(TEXT("Observe rejects"), Outcome.RejectionReason, EEdenExternalCommandRejectionReason::WrongAuthorityMode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAdvisoryRejectsProposalTest,
	"Eden.Unit.EdenOs.ExternalCommand.AdvisoryRejectsProposal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAdvisoryRejectsProposalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();
	Context.AuthorityMode = EEdenOsAuthorityMode::Advisory;
	const FEdenExternalCommandValidationOutcome Outcome =
		FEdenExternalCommandModel::ValidateProposal(EdenExternalCommandTests::MakeThermalProposal(), Context, false);
	TestEqual(TEXT("Advisory rejects"), Outcome.RejectionReason, EEdenExternalCommandRejectionReason::WrongAuthorityMode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAuthorizedControlStillRejectsWhenBoundaryDisabledTest,
	"Eden.Unit.EdenOs.ExternalCommand.AuthorizedControlStillRejectsWhenBoundaryDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAuthorizedControlStillRejectsWhenBoundaryDisabledTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();
	Context.bExternalCommandValidationEnabled = false;
	const FEdenExternalCommandValidationOutcome Outcome =
		FEdenExternalCommandModel::ValidateProposal(EdenExternalCommandTests::MakeThermalProposal(), Context, false);
	TestEqual(
		TEXT("AuthorizedControl alone is insufficient"),
		Outcome.RejectionReason,
		EEdenExternalCommandRejectionReason::BoundaryDisabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandAuthorizedControlCanValidateWhenExplicitlyEnabledTest,
	"Eden.Unit.EdenOs.ExternalCommand.AuthorizedControlCanValidateWhenExplicitlyEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandAuthorizedControlCanValidateWhenExplicitlyEnabledTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UEdenExternalCommandRouter* Router = NewObject<UEdenExternalCommandRouter>();
	const FEdenExternalCommandValidationOutcome Outcome = Router->ValidateProposal(
		EdenExternalCommandTests::MakeThermalProposal(),
		EdenExternalCommandTests::MakePermittingContext());
	TestTrue(TEXT("Enabled AuthorizedControl can validate"), Outcome.IsValid());
	TestEqual(TEXT("No rejection on Valid"), Outcome.RejectionReason, EEdenExternalCommandRejectionReason::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidThermalModeProposalValidatesTest,
	"Eden.Unit.EdenOs.ExternalCommand.ValidThermalModeProposalValidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidThermalModeProposalValidatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<EEdenThermalControlMode> Modes = {
		EEdenThermalControlMode::Off,
		EEdenThermalControlMode::Nominal,
		EEdenThermalControlMode::Boost,
		EEdenThermalControlMode::Emergency};
	for (EEdenThermalControlMode Mode : Modes)
	{
		UEdenExternalCommandRouter* Router = NewObject<UEdenExternalCommandRouter>();
		const FEdenExternalCommandValidationOutcome Outcome = Router->ValidateProposal(
			EdenExternalCommandTests::MakeThermalProposal(
				FString::Printf(TEXT("thermal-%d"), static_cast<int32>(Mode)),
				Mode),
			EdenExternalCommandTests::MakePermittingContext());
		TestTrue(TEXT("Each 0005 thermal mode validates"), Outcome.IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidLoadShedModeProposalValidatesTest,
	"Eden.Unit.EdenOs.ExternalCommand.ValidLoadShedModeProposalValidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidLoadShedModeProposalValidatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (EEdenLoadShedMode Mode : {EEdenLoadShedMode::Normal, EEdenLoadShedMode::Shed})
	{
		UEdenExternalCommandRouter* Router = NewObject<UEdenExternalCommandRouter>();
		TestTrue(
			TEXT("Each 0005 load-shed mode validates"),
			Router->ValidateProposal(
					EdenExternalCommandTests::MakeLoadShedProposal(
						FString::Printf(TEXT("load-%d"), static_cast<int32>(Mode)),
						Mode),
					EdenExternalCommandTests::MakePermittingContext())
				.IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidPropulsionPriorityProposalValidatesTest,
	"Eden.Unit.EdenOs.ExternalCommand.ValidPropulsionPriorityProposalValidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidPropulsionPriorityProposalValidatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (EEdenPropulsionPriorityMode Mode :
		 {EEdenPropulsionPriorityMode::Full, EEdenPropulsionPriorityMode::Reduced})
	{
		UEdenExternalCommandRouter* Router = NewObject<UEdenExternalCommandRouter>();
		TestTrue(
			TEXT("Each 0005 propulsion priority validates"),
			Router->ValidateProposal(
					EdenExternalCommandTests::MakePropulsionProposal(
						FString::Printf(TEXT("prop-%d"), static_cast<int32>(Mode)),
						Mode),
					EdenExternalCommandTests::MakePermittingContext())
				.IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandUnsupportedCommandRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.UnsupportedCommandRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandUnsupportedCommandRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal();
	Proposal.CommandType = static_cast<EEdenExternalCommandType>(255);
	const FEdenExternalCommandValidationOutcome Outcome = FEdenExternalCommandModel::ValidateProposal(
		Proposal,
		EdenExternalCommandTests::MakePermittingContext(),
		false);
	TestEqual(
		TEXT("Unknown command type rejected"),
		Outcome.RejectionReason,
		EEdenExternalCommandRejectionReason::UnsupportedCommand);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandInvalidThermalModeRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.InvalidThermalModeRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandInvalidThermalModeRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal();
	Proposal.Parameters.ThermalMode = static_cast<EEdenThermalControlMode>(250);
	TestEqual(
		TEXT("Invalid thermal enum rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			Proposal,
			EdenExternalCommandTests::MakePermittingContext(),
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::InvalidParameters);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandInvalidLoadShedModeRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.InvalidLoadShedModeRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandInvalidLoadShedModeRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeLoadShedProposal();
	Proposal.Parameters.LoadShedMode = static_cast<EEdenLoadShedMode>(250);
	TestEqual(
		TEXT("Invalid load-shed enum rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			Proposal,
			EdenExternalCommandTests::MakePermittingContext(),
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::InvalidParameters);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandInvalidPropulsionPriorityRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.InvalidPropulsionPriorityRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandInvalidPropulsionPriorityRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakePropulsionProposal();
	Proposal.Parameters.PropulsionPriorityMode = static_cast<EEdenPropulsionPriorityMode>(250);
	TestEqual(
		TEXT("Invalid propulsion enum rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			Proposal,
			EdenExternalCommandTests::MakePermittingContext(),
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::InvalidParameters);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandUnsupportedSchemaRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.UnsupportedSchemaRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandUnsupportedSchemaRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal();
	Proposal.SchemaVersion = 99;
	TestEqual(
		TEXT("Unsupported schema rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			Proposal,
			EdenExternalCommandTests::MakePermittingContext(),
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::UnsupportedSchemaVersion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandInvalidProposalIdRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.InvalidProposalIdRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandInvalidProposalIdRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal(TEXT("   "));
	TestEqual(
		TEXT("Blank proposal id rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			Proposal,
			EdenExternalCommandTests::MakePermittingContext(),
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::InvalidProposalId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandNoActiveSessionRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.NoActiveSessionRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandNoActiveSessionRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();
	Context.ActiveSessionId.Reset();
	TestEqual(
		TEXT("Empty active session rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			EdenExternalCommandTests::MakeThermalProposal(),
			Context,
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::NoActiveSession);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandWrongOrOldSessionRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.WrongOrOldSessionRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandWrongOrOldSessionRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(
		TEXT("Session mismatch rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			EdenExternalCommandTests::MakeThermalProposal(
				TEXT("p1"),
				EEdenThermalControlMode::Boost,
				TEXT("old-session")),
			EdenExternalCommandTests::MakePermittingContext(TEXT("session-j")),
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::SessionMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandNoAcceptedEvaluationRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.NoAcceptedEvaluationRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandNoAcceptedEvaluationRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();
	Context.bHasAcceptedEvaluation = false;
	Context.LatestAcceptedEvaluationId.Reset();
	TestEqual(
		TEXT("No accepted evaluation rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			EdenExternalCommandTests::MakeThermalProposal(),
			Context,
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::NoAcceptedEvaluation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandWrongOrOldEvaluationRejectedTest,
	"Eden.Unit.EdenOs.ExternalCommand.WrongOrOldEvaluationRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandWrongOrOldEvaluationRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal();
	Proposal.EvaluationId = TEXT("eval-old");
	TestEqual(
		TEXT("Evaluation mismatch rejected"),
		FEdenExternalCommandModel::ValidateProposal(
			Proposal,
			EdenExternalCommandTests::MakePermittingContext(),
			false)
			.RejectionReason,
		EEdenExternalCommandRejectionReason::EvaluationMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandPreviouslyValidatedProposalIdRejectedAsDuplicateTest,
	"Eden.Unit.EdenOs.ExternalCommand.PreviouslyValidatedProposalIdRejectedAsDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandPreviouslyValidatedProposalIdRejectedAsDuplicateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UEdenExternalCommandRouter* Router = NewObject<UEdenExternalCommandRouter>();
	const FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();
	TestTrue(TEXT("First validation succeeds"), Router->ValidateProposal(EdenExternalCommandTests::MakeThermalProposal(), Context).IsValid());

	FEdenExternalCommandProposal Replay = EdenExternalCommandTests::MakeThermalProposal();
	Replay.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Emergency);
	const FEdenExternalCommandValidationOutcome Second = Router->ValidateProposal(Replay, Context);
	TestFalse(TEXT("Replay rejected"), Second.IsValid());
	TestEqual(TEXT("Replay is DuplicateProposal"), Second.RejectionReason, EEdenExternalCommandRejectionReason::DuplicateProposal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandRejectedProposalDoesNotConsumeProposalIdTest,
	"Eden.Unit.EdenOs.ExternalCommand.RejectedProposalDoesNotConsumeProposalId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandRejectedProposalDoesNotConsumeProposalIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UEdenExternalCommandRouter* Router = NewObject<UEdenExternalCommandRouter>();
	const FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();

	FEdenExternalCommandProposal Bad = EdenExternalCommandTests::MakeThermalProposal(TEXT("fixable-1"));
	Bad.Parameters.ThermalMode = static_cast<EEdenThermalControlMode>(250);
	TestEqual(
		TEXT("First attempt invalid"),
		Router->ValidateProposal(Bad, Context).RejectionReason,
		EEdenExternalCommandRejectionReason::InvalidParameters);

	FEdenExternalCommandProposal Fixed = EdenExternalCommandTests::MakeThermalProposal(TEXT("fixable-1"));
	TestTrue(TEXT("Same id validates after prior rejection"), Router->ValidateProposal(Fixed, Context).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidationRejectionPrecedenceIsDeterministicTest,
	"Eden.Unit.EdenOs.ExternalCommand.ValidationRejectionPrecedenceIsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidationRejectionPrecedenceIsDeterministicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	auto Expect = [this](
					  const FEdenExternalCommandProposal& Proposal,
					  const FEdenExternalCommandValidationContext& Context,
					  EEdenExternalCommandRejectionReason Expected)
	{
		const FEdenExternalCommandValidationOutcome Outcome =
			FEdenExternalCommandModel::ValidateProposal(Proposal, Context, false);
		TestEqual(TEXT("Precedence reason"), Outcome.RejectionReason, Expected);
	};

	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal();
	Proposal.SchemaVersion = 99;
	Proposal.ProposalId.Reset();
	Proposal.SessionId = TEXT("wrong");
	Proposal.EvaluationId = TEXT("wrong");
	Proposal.CommandType = static_cast<EEdenExternalCommandType>(255);
	Proposal.Parameters.ThermalMode = static_cast<EEdenThermalControlMode>(250);

	FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();
	Context.bExternalCommandValidationEnabled = false;
	Context.AuthorityMode = EEdenOsAuthorityMode::Observe;
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::BoundaryDisabled);

	Context.bExternalCommandValidationEnabled = true;
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::WrongAuthorityMode);

	Context.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::UnsupportedSchemaVersion);

	Proposal.SchemaVersion = EdenExternalCommandContract::CurrentSchemaVersion;
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::InvalidProposalId);

	Proposal.ProposalId = TEXT("p-order");
	Context.ActiveSessionId.Reset();
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::NoActiveSession);

	Context.ActiveSessionId = TEXT("session-j");
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::SessionMismatch);

	Proposal.SessionId = TEXT("session-j");
	Context.bHasAcceptedEvaluation = false;
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::NoAcceptedEvaluation);

	Context.bHasAcceptedEvaluation = true;
	Context.LatestAcceptedEvaluationId = TEXT("eval-j-1");
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::EvaluationMismatch);

	Proposal.EvaluationId = TEXT("eval-j-1");
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::UnsupportedCommand);

	Proposal.CommandType = EEdenExternalCommandType::SetThermalControlMode;
	Expect(Proposal, Context, EEdenExternalCommandRejectionReason::InvalidParameters);

	Proposal.Parameters = FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode::Boost);
	TestTrue(
		TEXT("Finally Valid"),
		FEdenExternalCommandModel::ValidateProposal(Proposal, Context, false).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandRepeatedEquivalentInputProducesEquivalentResultTest,
	"Eden.Unit.EdenOs.ExternalCommand.RepeatedEquivalentInputProducesEquivalentResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandRepeatedEquivalentInputProducesEquivalentResultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal();
	const FEdenExternalCommandValidationContext Context = EdenExternalCommandTests::MakePermittingContext();
	const FEdenExternalCommandValidationOutcome A =
		FEdenExternalCommandModel::ValidateProposal(Proposal, Context, false);
	const FEdenExternalCommandValidationOutcome B =
		FEdenExternalCommandModel::ValidateProposal(Proposal, Context, false);
	TestEqual(TEXT("Status deterministic"), A.Status, B.Status);
	TestEqual(TEXT("Reason deterministic"), A.RejectionReason, B.RejectionReason);
	TestTrue(TEXT("Both Valid when not consumed"), A.IsValid() && B.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandRecommendationTextNeverBecomesCommandTest,
	"Eden.Unit.EdenOs.ExternalCommand.RecommendationTextNeverBecomesCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandRecommendationTextNeverBecomesCommandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	EdenExternalCommandTests::FScopedExternalCommandWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	FEdenOsConnectionConfig Config;
	Config.bEnabled = true;
	Config.BaseUrl = TEXT("https://example.test");
	Config.AuthorityMode = EEdenOsAuthorityMode::Advisory;
	Config.RuntimeBearerJwt = TEXT("test-token");
	Adapter->ApplyRuntimeConfig(Config);

	FEdenOsAcceptedAdvisory Advisory = EdenExternalCommandTests::MakeAcceptedAdvisory(
		TEXT("eval-nl"),
		TEXT("SetThermalControlMode Boost and SetLoadShedMode Shed"));
	Adapter->SetLatestAcceptedAdvisoryForTesting(Advisory);

	TestEqual(TEXT("No auto validation from advisory text"), Adapter->GetExternalCommandValidationHistory().Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidProposalDoesNotMutateOperatorStateTest,
	"Eden.Integration.EdenOs.ExternalCommand.ValidProposalDoesNotMutateOperatorState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidProposalDoesNotMutateOperatorStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	EdenExternalCommandTests::FScopedExternalCommandWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	AActor* Owner = World->SpawnActor<AActor>();
	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>(Owner);
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>(Owner);
	UEdenFlightMovementComponent* Flight = NewObject<UEdenFlightMovementComponent>(Owner);
	UEdenOperatorControlComponent* Operator = NewObject<UEdenOperatorControlComponent>(Owner);
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
	TestTrue(TEXT("Power initializes"), Power->InitializePowerSimulation(PowerConfig));
	TestTrue(TEXT("Thermal initializes"), Thermal->InitializeThermalSimulation(ThermalConfig));

	FEdenOperatorControlConfig OpConfig;
	OpConfig.BoostDissipationDegreesCelsiusPerSecond = 1.0f;
	OpConfig.EmergencyDissipationDegreesCelsiusPerSecond = 2.0f;
	OpConfig.BoostCoolingDemandKilowatts = 1.5f;
	OpConfig.EmergencyCoolingDemandKilowatts = 4.5f;
	OpConfig.LoadShedDemandReductionKilowatts = 2.0f;
	OpConfig.LoadShedDissipationReductionDegreesCelsiusPerSecond = 0.4f;
	OpConfig.ReducedThrustAuthority = 0.5f;
	TestTrue(TEXT("Operator initializes"), Operator->InitializeOperatorControl(OpConfig));
	const FEdenOperatorIntent Before = Operator->GetOperatorIntent();
	const float PowerDemandBefore = Power->GetPowerStateSnapshot().OperatorDemandKilowatts;
	const float ThermalDissipationBefore =
		Thermal->GetThermalStateSnapshot().OperatorDissipationDegreesCelsiusPerSecond;
	const float ThrustBefore = Flight->GetThrustAuthority();

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	FEdenOsConnectionConfig Config;
	Config.bEnabled = true;
	Config.BaseUrl = TEXT("https://example.test");
	Config.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	Config.bExternalCommandValidationEnabled = true;
	Config.RuntimeBearerJwt = TEXT("test-token");
	Adapter->ApplyRuntimeConfig(Config);
	Adapter->SetLatestAcceptedAdvisoryForTesting(EdenExternalCommandTests::MakeAcceptedAdvisory(TEXT("eval-j-1")));

	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal(
		TEXT("mutate-op"),
		EEdenThermalControlMode::Emergency,
		Telemetry->GetSessionId(),
		TEXT("eval-j-1"));
	TestTrue(TEXT("Proposal validates"), Adapter->ValidateExternalCommandProposal(Proposal).IsValid());
	TestEqual(TEXT("Operator intent unchanged"), Operator->GetOperatorIntent().ThermalMode, Before.ThermalMode);
	TestEqual(TEXT("Load shed unchanged"), Operator->GetOperatorIntent().LoadShedMode, Before.LoadShedMode);
	TestEqual(
		TEXT("Propulsion unchanged"),
		Operator->GetOperatorIntent().PropulsionPriority,
		Before.PropulsionPriority);
	TestEqual(TEXT("Power operator demand unchanged"), Power->GetPowerStateSnapshot().OperatorDemandKilowatts, PowerDemandBefore);
	TestEqual(
		TEXT("Thermal operator dissipation unchanged"),
		Thermal->GetThermalStateSnapshot().OperatorDissipationDegreesCelsiusPerSecond,
		ThermalDissipationBefore);
	TestEqual(TEXT("Flight thrust unchanged"), Flight->GetThrustAuthority(), ThrustBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidProposalDoesNotMutateResourcesTest,
	"Eden.Integration.EdenOs.ExternalCommand.ValidProposalDoesNotMutateResources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidProposalDoesNotMutateResourcesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenExternalCommandTests;

	EdenExternalCommandTests::FScopedExternalCommandWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	AActor* Owner = World->SpawnActor<AActor>();
	UEdenFuelSystemComponent* Fuel = NewObject<UEdenFuelSystemComponent>(Owner);
	UEdenPowerSystemComponent* Power = NewObject<UEdenPowerSystemComponent>(Owner);
	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>(Owner);
	Fuel->RegisterComponent();
	Power->RegisterComponent();
	Thermal->RegisterComponent();

	FEdenFuelConfig FuelConfig;
	FuelConfig.CapacityKilograms = 100.0f;
	FuelConfig.ConsumptionRateKilogramsPerSecond = 10.0f;
	FuelConfig.InitialFuelFraction = 1.0f;
	FuelConfig.WarningThresholdFraction = 0.25f;
	FuelConfig.CriticalThresholdFraction = 0.1f;

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

	TestTrue(TEXT("Fuel initializes"), Fuel->InitializeFuelSimulation(FuelConfig));
	TestTrue(TEXT("Power initializes"), Power->InitializePowerSimulation(PowerConfig));
	TestTrue(TEXT("Thermal initializes"), Thermal->InitializeThermalSimulation(ThermalConfig));

	const float FuelBefore = Fuel->GetFuelStateSnapshot().FuelFraction;
	const float PowerBefore = Power->GetPowerStateSnapshot().ChargeFraction;
	const float TempBefore = Thermal->GetThermalStateSnapshot().TemperatureCelsius;

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	FEdenOsConnectionConfig Config;
	Config.bEnabled = true;
	Config.BaseUrl = TEXT("https://example.test");
	Config.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	Config.bExternalCommandValidationEnabled = true;
	Config.RuntimeBearerJwt = TEXT("test-token");
	Adapter->ApplyRuntimeConfig(Config);
	Adapter->SetLatestAcceptedAdvisoryForTesting(MakeAcceptedAdvisory(TEXT("eval-j-1")));

	TestTrue(
		TEXT("Validates"),
		Adapter
			->ValidateExternalCommandProposal(MakeThermalProposal(
				TEXT("mutate-res"),
				EEdenThermalControlMode::Boost,
				Telemetry->GetSessionId(),
				TEXT("eval-j-1")))
			.IsValid());

	TestEqual(TEXT("Fuel unchanged"), Fuel->GetFuelStateSnapshot().FuelFraction, FuelBefore);
	TestEqual(TEXT("Power unchanged"), Power->GetPowerStateSnapshot().ChargeFraction, PowerBefore);
	TestEqual(TEXT("Thermal unchanged"), Thermal->GetThermalStateSnapshot().TemperatureCelsius, TempBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidProposalDoesNotMutateMissionTest,
	"Eden.Integration.EdenOs.ExternalCommand.ValidProposalDoesNotMutateMission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidProposalDoesNotMutateMissionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	EdenExternalCommandTests::FScopedExternalCommandWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
	const EEdenMissionState StateBefore = Mission->GetMissionStateSnapshot().MissionState;
	const EEdenMissionPhase PhaseBefore = Mission->GetMissionStateSnapshot().MissionPhase;

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	FEdenOsConnectionConfig Config;
	Config.bEnabled = true;
	Config.BaseUrl = TEXT("https://example.test");
	Config.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	Config.bExternalCommandValidationEnabled = true;
	Config.RuntimeBearerJwt = TEXT("test-token");
	Adapter->ApplyRuntimeConfig(Config);
	Adapter->SetLatestAcceptedAdvisoryForTesting(EdenExternalCommandTests::MakeAcceptedAdvisory(TEXT("eval-j-1")));

	TestTrue(
		TEXT("Validates"),
		Adapter
			->ValidateExternalCommandProposal(EdenExternalCommandTests::MakeThermalProposal(
				TEXT("mutate-mission"),
				EEdenThermalControlMode::Boost,
				Telemetry->GetSessionId(),
				TEXT("eval-j-1")))
			.IsValid());

	TestEqual(TEXT("Mission state unchanged"), Mission->GetMissionStateSnapshot().MissionState, StateBefore);
	TestEqual(TEXT("Mission phase unchanged"), Mission->GetMissionStateSnapshot().MissionPhase, PhaseBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidProposalDoesNotMutateFlightTest,
	"Eden.Integration.EdenOs.ExternalCommand.ValidProposalDoesNotMutateFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidProposalDoesNotMutateFlightTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	EdenExternalCommandTests::FScopedExternalCommandWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	AActor* Owner = World->SpawnActor<AActor>();
	UEdenFlightMovementComponent* Flight = NewObject<UEdenFlightMovementComponent>(Owner);
	Flight->RegisterComponent();
	const float AuthorityBefore = Flight->GetThrustAuthority();
	const bool bAssistBefore = Flight->IsStabilizationAssistAvailable();

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	FEdenOsConnectionConfig Config;
	Config.bEnabled = true;
	Config.BaseUrl = TEXT("https://example.test");
	Config.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	Config.bExternalCommandValidationEnabled = true;
	Config.RuntimeBearerJwt = TEXT("test-token");
	Adapter->ApplyRuntimeConfig(Config);
	Adapter->SetLatestAcceptedAdvisoryForTesting(EdenExternalCommandTests::MakeAcceptedAdvisory(TEXT("eval-j-1")));

	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakePropulsionProposal(TEXT("mutate-flight"));
	Proposal.SessionId = Telemetry->GetSessionId();
	Proposal.EvaluationId = TEXT("eval-j-1");
	TestTrue(TEXT("Validates"), Adapter->ValidateExternalCommandProposal(Proposal).IsValid());
	TestEqual(TEXT("Thrust authority unchanged"), Flight->GetThrustAuthority(), AuthorityBefore);
	TestEqual(TEXT("Stabilization assist unchanged"), Flight->IsStabilizationAssistAvailable(), bAssistBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandValidProposalDoesNotMutateClockTest,
	"Eden.Integration.EdenOs.ExternalCommand.ValidProposalDoesNotMutateClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandValidProposalDoesNotMutateClockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	EdenExternalCommandTests::FScopedExternalCommandWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	Clock->SetFixedStepSeconds(0.1f);
	Clock->ResetSimulationClock();
	const float Before = Clock->GetElapsedSimulationTimeSeconds();

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	FEdenOsConnectionConfig Config;
	Config.bEnabled = true;
	Config.BaseUrl = TEXT("https://example.test");
	Config.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	Config.bExternalCommandValidationEnabled = true;
	Config.RuntimeBearerJwt = TEXT("test-token");
	Adapter->ApplyRuntimeConfig(Config);
	Adapter->SetLatestAcceptedAdvisoryForTesting(EdenExternalCommandTests::MakeAcceptedAdvisory(TEXT("eval-j-1")));

	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal(
		TEXT("mutate-clock"),
		EEdenThermalControlMode::Boost,
		Telemetry->GetSessionId(),
		TEXT("eval-j-1"));
	TestTrue(TEXT("Validates"), Adapter->ValidateExternalCommandProposal(Proposal).IsValid());
	TestEqual(TEXT("Clock unchanged"), Clock->GetElapsedSimulationTimeSeconds(), Before);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenExternalCommandResetInvalidatesPriorSessionAndEvaluationIdentityTest,
	"Eden.Unit.EdenOs.ExternalCommand.ResetInvalidatesPriorSessionAndEvaluationIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenExternalCommandResetInvalidatesPriorSessionAndEvaluationIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	EdenExternalCommandTests::FScopedExternalCommandWorld ScopedWorld;
	UWorld* World = ScopedWorld.World;

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	FEdenOsConnectionConfig Config;
	Config.bEnabled = true;
	Config.BaseUrl = TEXT("https://example.test");
	Config.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	Config.bExternalCommandValidationEnabled = true;
	Config.RuntimeBearerJwt = TEXT("test-token");
	Adapter->ApplyRuntimeConfig(Config);

	const FString OldSession = Telemetry->GetSessionId();
	Adapter->SetLatestAcceptedAdvisoryForTesting(EdenExternalCommandTests::MakeAcceptedAdvisory(TEXT("eval-old")));
	FEdenExternalCommandProposal Proposal = EdenExternalCommandTests::MakeThermalProposal(
		TEXT("reset-id"),
		EEdenThermalControlMode::Boost,
		OldSession,
		TEXT("eval-old"));
	TestTrue(TEXT("Valid before reset"), Adapter->ValidateExternalCommandProposal(Proposal).IsValid());

	Telemetry->ClearHistory();
	Adapter->SetLatestAcceptedAdvisoryForTesting(FEdenOsAcceptedAdvisory());

	TestEqual(
		TEXT("Old session no longer valid"),
		Adapter->ValidateExternalCommandProposal(Proposal).RejectionReason,
		EEdenExternalCommandRejectionReason::SessionMismatch);

	Adapter->SetLatestAcceptedAdvisoryForTesting(EdenExternalCommandTests::MakeAcceptedAdvisory(TEXT("eval-new")));
	Proposal.SessionId = Telemetry->GetSessionId();
	Proposal.EvaluationId = TEXT("eval-old");
	TestEqual(
		TEXT("Old evaluation rejected after new accept"),
		Adapter->ValidateExternalCommandProposal(Proposal).RejectionReason,
		EEdenExternalCommandRejectionReason::EvaluationMismatch);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
