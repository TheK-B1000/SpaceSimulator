// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenExternalCommandRouter.h"

#include "EdenOs/EdenExternalCommandModel.h"

FEdenExternalCommandValidationOutcome UEdenExternalCommandRouter::ValidateProposal(
	const FEdenExternalCommandProposal& Proposal,
	const FEdenExternalCommandValidationContext& Context)
{
	// Session identity change invalidates prior consumed ProposalIds for the previous session.
	if (BoundSessionId != Context.ActiveSessionId)
	{
		ConsumedProposalIds.Reset();
		ValidatedArtifactsByProposalId.Reset();
		BoundSessionId = Context.ActiveSessionId;
	}

	const bool bAlreadyConsumed =
		!Proposal.ProposalId.IsEmpty() && ConsumedProposalIds.Contains(Proposal.ProposalId);

	FEdenExternalCommandValidationOutcome Outcome =
		FEdenExternalCommandModel::ValidateProposal(Proposal, Context, bAlreadyConsumed);

	if (Outcome.IsValid())
	{
		ConsumedProposalIds.Add(Proposal.ProposalId);
		const FEdenValidatedExternalCommand Artifact = FEdenExternalCommandModel::MakeValidatedCommand(Proposal);
		ValidatedArtifactsByProposalId.Add(Proposal.ProposalId, Artifact);
		Outcome = FEdenExternalCommandValidationOutcome::MakeValid(Artifact);
	}

	AppendValidationRecord(Proposal, Outcome);
	return Outcome;
}

bool UEdenExternalCommandRouter::TryGetValidatedCommand(
	const FString& ProposalId,
	FEdenValidatedExternalCommand& OutCommand) const
{
	if (const FEdenValidatedExternalCommand* Found = ValidatedArtifactsByProposalId.Find(ProposalId))
	{
		OutCommand = *Found;
		return true;
	}
	return false;
}

TArray<FEdenExternalCommandValidationRecord> UEdenExternalCommandRouter::GetValidationHistory() const
{
	return ValidationHistory;
}

TSet<FString> UEdenExternalCommandRouter::GetConsumedProposalIdsForTesting() const
{
	return ConsumedProposalIds;
}

void UEdenExternalCommandRouter::ResetValidationState()
{
	ValidationHistory.Reset();
	ValidatedArtifactsByProposalId.Reset();
	ConsumedProposalIds.Reset();
	BoundSessionId.Reset();
}

void UEdenExternalCommandRouter::AppendValidationRecord(
	const FEdenExternalCommandProposal& Proposal,
	const FEdenExternalCommandValidationOutcome& Outcome)
{
	FEdenExternalCommandValidationRecord Record;
	Record.ProposalId = Proposal.ProposalId;
	Record.SessionId = Proposal.SessionId;
	Record.EvaluationId = Proposal.EvaluationId;
	Record.CommandType = Proposal.CommandType;
	Record.Status = Outcome.Status;
	Record.RejectionReason = Outcome.RejectionReason;

	if (ValidationHistory.Num() >= EdenExternalCommandContract::MaxValidationHistory)
	{
		ValidationHistory.RemoveAt(0, 1, EAllowShrinking::No);
	}
	ValidationHistory.Add(Record);
}
