// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenExternalCommandTypes.h"
#include "UObject/Object.h"

#include "EdenExternalCommandRouter.generated.h"

/**
 * Checkpoint J validation boundary. Owns consumed ProposalIds and validation audit history.
 * Never mutates simulation state. Execution remains Checkpoint K.
 */
UCLASS()
class EDENSPACESIMULATOR_API UEdenExternalCommandRouter : public UObject
{
	GENERATED_BODY()

public:
	FEdenExternalCommandValidationOutcome ValidateProposal(
		const FEdenExternalCommandProposal& Proposal,
		const FEdenExternalCommandValidationContext& Context);

	TArray<FEdenExternalCommandValidationRecord> GetValidationHistory() const;
	TSet<FString> GetConsumedProposalIdsForTesting() const;
	void ResetValidationState();

private:
	void AppendValidationRecord(
		const FEdenExternalCommandProposal& Proposal,
		const FEdenExternalCommandValidationOutcome& Outcome);

	UPROPERTY(Transient)
	TArray<FEdenExternalCommandValidationRecord> ValidationHistory;

	TSet<FString> ConsumedProposalIds;
	FString BoundSessionId;
};
