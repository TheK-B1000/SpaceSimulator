// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenExternalCommandTypes.h"
#include "UObject/Object.h"

#include "EdenExternalCommandExecutor.generated.h"

class UEdenOperatorControlComponent;

/**
 * Checkpoint K authorized execution boundary.
 * Owns apply-attempt bookkeeping and diagnostic history only — never mission/advisory truth.
 */
UCLASS()
class EDENSPACESIMULATOR_API UEdenExternalCommandExecutor : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Execute a previously validated artifact through UEdenOperatorControlComponent.
	 * Pre-apply rejections do not consume the ProposalId apply attempt.
	 */
	FEdenExternalCommandExecutionResult ExecuteValidatedCommand(
		const FEdenValidatedExternalCommand& Command,
		const FEdenExternalCommandExecutionContext& Context,
		UEdenOperatorControlComponent* OperatorControl);

	TArray<FEdenExternalCommandExecutionRecord> GetExecutionHistory() const;
	TSet<FString> GetAttemptedProposalIdsForTesting() const;
	void ResetExecutionState();

private:
	struct FEvaluationCommandKey
	{
		FString EvaluationId;
		EEdenExternalCommandType CommandType = EEdenExternalCommandType::SetThermalControlMode;

		bool operator==(const FEvaluationCommandKey& Other) const
		{
			return EvaluationId == Other.EvaluationId && CommandType == Other.CommandType;
		}

		friend uint32 GetTypeHash(const FEvaluationCommandKey& Key)
		{
			return HashCombine(GetTypeHash(Key.EvaluationId), GetTypeHash(static_cast<uint8>(Key.CommandType)));
		}
	};

	void RebindSessionIfNeeded(const FString& ActiveSessionId);
	void AppendExecutionRecord(
		const FEdenValidatedExternalCommand& Command,
		float AttemptSimulationTimeSeconds,
		const FEdenExternalCommandExecutionResult& Result);
	void MarkApplyAttempted(const FEdenValidatedExternalCommand& Command);

	static FString ModeLabelForCommand(
		EEdenExternalCommandType CommandType,
		const FEdenExternalCommandParameters& Parameters);
	static FString CurrentModeLabel(
		EEdenExternalCommandType CommandType,
		const UEdenOperatorControlComponent& OperatorControl);
	static bool IsAlreadySatisfied(
		EEdenExternalCommandType CommandType,
		const FEdenExternalCommandParameters& Parameters,
		const UEdenOperatorControlComponent& OperatorControl);
	static bool ApplyOnce(
		EEdenExternalCommandType CommandType,
		const FEdenExternalCommandParameters& Parameters,
		UEdenOperatorControlComponent& OperatorControl);
	static bool ResultMatchesRequested(
		EEdenExternalCommandType CommandType,
		const FEdenExternalCommandParameters& Parameters,
		const UEdenOperatorControlComponent& OperatorControl);

	UPROPERTY(Transient)
	TArray<FEdenExternalCommandExecutionRecord> ExecutionHistory;

	TSet<FString> AttemptedProposalIds;
	TSet<FEvaluationCommandKey> AttemptedEvaluationCommandKeys;
	FString BoundSessionId;
};
