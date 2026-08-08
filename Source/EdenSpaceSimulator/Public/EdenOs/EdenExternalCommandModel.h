// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenExternalCommandTypes.h"

/**
 * Pure Checkpoint J validator. Deterministic; no networking; no simulation mutation.
 * A Valid outcome is not authorization to act — K owns execution.
 */
struct EDENSPACESIMULATOR_API FEdenExternalCommandModel
{
	static bool IsAllowlistedCommand(EEdenExternalCommandType CommandType);
	static bool IsValidThermalControlMode(EEdenThermalControlMode Mode);
	static bool IsValidLoadShedMode(EEdenLoadShedMode Mode);
	static bool IsValidPropulsionPriorityMode(EEdenPropulsionPriorityMode Mode);

	static FEdenExternalCommandParameters MakeThermalParameters(EEdenThermalControlMode Mode);
	static FEdenExternalCommandParameters MakeLoadShedParameters(EEdenLoadShedMode Mode);
	static FEdenExternalCommandParameters MakePropulsionPriorityParameters(EEdenPropulsionPriorityMode Mode);

	/** Bind exact J-validated identity + typed parameters into an immutable K artifact. */
	static FEdenValidatedExternalCommand MakeValidatedCommand(const FEdenExternalCommandProposal& Proposal);

	/** Structural integrity check for a validated artifact (does not re-run authority gates). */
	static bool IsValidValidatedCommand(const FEdenValidatedExternalCommand& Command);

	/**
	 * Stateless validation against the locked precedence. Does not mutate consumed-id state.
	 * Callers that enforce DuplicateProposal must consult consumed ProposalIds before/after.
	 */
	static FEdenExternalCommandValidationOutcome ValidateProposal(
		const FEdenExternalCommandProposal& Proposal,
		const FEdenExternalCommandValidationContext& Context,
		bool bProposalIdAlreadyConsumed);
};
