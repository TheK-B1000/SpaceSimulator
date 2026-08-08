// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenExternalCommandModel.h"

namespace EdenExternalCommandModelPrivate
{
	bool HasIdentifier(const FString& Value)
	{
		return !Value.TrimStartAndEnd().IsEmpty();
	}

	bool IsCommandTypeRecognized(EEdenExternalCommandType CommandType)
	{
		switch (CommandType)
		{
		case EEdenExternalCommandType::SetThermalControlMode:
		case EEdenExternalCommandType::SetLoadShedMode:
		case EEdenExternalCommandType::SetPropulsionPriorityMode:
			return true;
		}
		return false;
	}

	bool ParametersMatchCommand(
		EEdenExternalCommandType CommandType,
		const FEdenExternalCommandParameters& Parameters)
	{
		switch (CommandType)
		{
		case EEdenExternalCommandType::SetThermalControlMode:
			return Parameters.Kind == EEdenExternalCommandParameterKind::ThermalControlMode
				&& FEdenExternalCommandModel::IsValidThermalControlMode(Parameters.ThermalMode);
		case EEdenExternalCommandType::SetLoadShedMode:
			return Parameters.Kind == EEdenExternalCommandParameterKind::LoadShedMode
				&& FEdenExternalCommandModel::IsValidLoadShedMode(Parameters.LoadShedMode);
		case EEdenExternalCommandType::SetPropulsionPriorityMode:
			return Parameters.Kind == EEdenExternalCommandParameterKind::PropulsionPriorityMode
				&& FEdenExternalCommandModel::IsValidPropulsionPriorityMode(Parameters.PropulsionPriorityMode);
		}
		return false;
	}
}

bool FEdenExternalCommandModel::IsAllowlistedCommand(EEdenExternalCommandType CommandType)
{
	return EdenExternalCommandModelPrivate::IsCommandTypeRecognized(CommandType);
}

bool FEdenExternalCommandModel::IsValidThermalControlMode(EEdenThermalControlMode Mode)
{
	switch (Mode)
	{
	case EEdenThermalControlMode::Off:
	case EEdenThermalControlMode::Nominal:
	case EEdenThermalControlMode::Boost:
	case EEdenThermalControlMode::Emergency:
		return true;
	}
	return false;
}

bool FEdenExternalCommandModel::IsValidLoadShedMode(EEdenLoadShedMode Mode)
{
	switch (Mode)
	{
	case EEdenLoadShedMode::Normal:
	case EEdenLoadShedMode::Shed:
		return true;
	}
	return false;
}

bool FEdenExternalCommandModel::IsValidPropulsionPriorityMode(EEdenPropulsionPriorityMode Mode)
{
	switch (Mode)
	{
	case EEdenPropulsionPriorityMode::Full:
	case EEdenPropulsionPriorityMode::Reduced:
		return true;
	}
	return false;
}

FEdenExternalCommandParameters FEdenExternalCommandModel::MakeThermalParameters(EEdenThermalControlMode Mode)
{
	FEdenExternalCommandParameters Parameters;
	Parameters.Kind = EEdenExternalCommandParameterKind::ThermalControlMode;
	Parameters.ThermalMode = Mode;
	return Parameters;
}

FEdenExternalCommandParameters FEdenExternalCommandModel::MakeLoadShedParameters(EEdenLoadShedMode Mode)
{
	FEdenExternalCommandParameters Parameters;
	Parameters.Kind = EEdenExternalCommandParameterKind::LoadShedMode;
	Parameters.LoadShedMode = Mode;
	return Parameters;
}

FEdenExternalCommandParameters FEdenExternalCommandModel::MakePropulsionPriorityParameters(
	EEdenPropulsionPriorityMode Mode)
{
	FEdenExternalCommandParameters Parameters;
	Parameters.Kind = EEdenExternalCommandParameterKind::PropulsionPriorityMode;
	Parameters.PropulsionPriorityMode = Mode;
	return Parameters;
}

FEdenValidatedExternalCommand FEdenExternalCommandModel::MakeValidatedCommand(
	const FEdenExternalCommandProposal& Proposal)
{
	FEdenValidatedExternalCommand Command;
	Command.ProposalId = Proposal.ProposalId;
	Command.SessionId = Proposal.SessionId;
	Command.EvaluationId = Proposal.EvaluationId;
	Command.CommandType = Proposal.CommandType;
	Command.Parameters = Proposal.Parameters;
	return Command;
}

bool FEdenExternalCommandModel::IsValidValidatedCommand(const FEdenValidatedExternalCommand& Command)
{
	using namespace EdenExternalCommandModelPrivate;
	return HasIdentifier(Command.ProposalId)
		&& HasIdentifier(Command.SessionId)
		&& HasIdentifier(Command.EvaluationId)
		&& IsAllowlistedCommand(Command.CommandType)
		&& ParametersMatchCommand(Command.CommandType, Command.Parameters);
}

FEdenExternalCommandValidationOutcome FEdenExternalCommandModel::ValidateProposal(
	const FEdenExternalCommandProposal& Proposal,
	const FEdenExternalCommandValidationContext& Context,
	bool bProposalIdAlreadyConsumed)
{
	using namespace EdenExternalCommandModelPrivate;

	// Locked deterministic precedence (Checkpoint J contract).
	if (!Context.bExternalCommandValidationEnabled)
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::BoundaryDisabled);
	}
	if (Context.AuthorityMode != EEdenOsAuthorityMode::AuthorizedControl)
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::WrongAuthorityMode);
	}
	if (Proposal.SchemaVersion != EdenExternalCommandContract::CurrentSchemaVersion)
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::UnsupportedSchemaVersion);
	}
	if (!HasIdentifier(Proposal.ProposalId))
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::InvalidProposalId);
	}
	if (!HasIdentifier(Context.ActiveSessionId))
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::NoActiveSession);
	}
	if (Proposal.SessionId != Context.ActiveSessionId)
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::SessionMismatch);
	}
	if (!Context.bHasAcceptedEvaluation || !HasIdentifier(Context.LatestAcceptedEvaluationId))
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::NoAcceptedEvaluation);
	}
	if (Proposal.EvaluationId != Context.LatestAcceptedEvaluationId)
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::EvaluationMismatch);
	}
	if (bProposalIdAlreadyConsumed)
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::DuplicateProposal);
	}
	if (!IsAllowlistedCommand(Proposal.CommandType))
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::UnsupportedCommand);
	}
	if (!ParametersMatchCommand(Proposal.CommandType, Proposal.Parameters))
	{
		return FEdenExternalCommandValidationOutcome::MakeRejected(
			EEdenExternalCommandRejectionReason::InvalidParameters);
	}

	return FEdenExternalCommandValidationOutcome::MakeValid();
}
