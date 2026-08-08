// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenExternalCommandTypes.h"
#include "UObject/Object.h"

#include "EdenExternalCommandRouter.generated.h"

/**
 * Checkpoint J validation boundary. Owns consumed ProposalIds, validation audit history,
 * and validated artifacts for Checkpoint K. Never mutates simulation state.
 */
UCLASS()
class EDENSPACESIMULATOR_API UEdenExternalCommandRouter : public UObject
{
	GENERATED_BODY()

public:
	FEdenExternalCommandValidationOutcome ValidateProposal(
		const FEdenExternalCommandProposal& Proposal,
		const FEdenExternalCommandValidationContext& Context);

	bool TryGetValidatedCommand(const FString& ProposalId, FEdenValidatedExternalCommand& OutCommand) const;

	TArray<FEdenExternalCommandValidationRecord> GetValidationHistory() const;
	TSet<FString> GetConsumedProposalIdsForTesting() const;
	void ResetValidationState();

private:
	void AppendValidationRecord(
		const FEdenExternalCommandProposal& Proposal,
		const FEdenExternalCommandValidationOutcome& Outcome);

	UPROPERTY(Transient)
	TArray<FEdenExternalCommandValidationRecord> ValidationHistory;

	UPROPERTY(Transient)
	TMap<FString, FEdenValidatedExternalCommand> ValidatedArtifactsByProposalId;

	TSet<FString> ConsumedProposalIds;
	FString BoundSessionId;
};
