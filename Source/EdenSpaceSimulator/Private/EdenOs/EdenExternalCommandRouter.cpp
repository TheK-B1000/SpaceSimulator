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
		BoundSessionId = Context.ActiveSessionId;
	}

	const bool bAlreadyConsumed =
		!Proposal.ProposalId.IsEmpty() && ConsumedProposalIds.Contains(Proposal.ProposalId);

	const FEdenExternalCommandValidationOutcome Outcome =
		FEdenExternalCommandModel::ValidateProposal(Proposal, Context, bAlreadyConsumed);

	if (Outcome.IsValid())
	{
		ConsumedProposalIds.Add(Proposal.ProposalId);
	}

	AppendValidationRecord(Proposal, Outcome);
	return Outcome;
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
