// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenExternalCommandExecutor.h"

#include "Core/EdenLogCategories.h"
#include "EdenOs/EdenExternalCommandModel.h"
#include "Operations/EdenOperatorControlComponent.h"

namespace EdenExternalCommandExecutorPrivate
{
	bool HasIdentifier(const FString& Value)
	{
		return !Value.TrimStartAndEnd().IsEmpty();
	}

	FString ShortEnumName(const UEnum* Enum, int64 Value)
	{
		if (!Enum)
		{
			return TEXT("Unknown");
		}
		const FString Full = Enum->GetNameStringByValue(Value);
		const int32 ColonIndex = Full.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		return ColonIndex == INDEX_NONE ? Full : Full.Mid(ColonIndex + 2);
	}
}

void UEdenExternalCommandExecutor::RebindSessionIfNeeded(const FString& ActiveSessionId)
{
	if (BoundSessionId != ActiveSessionId)
	{
		AttemptedProposalIds.Reset();
		AttemptedEvaluationCommandKeys.Reset();
		BoundSessionId = ActiveSessionId;
	}
}

void UEdenExternalCommandExecutor::MarkApplyAttempted(const FEdenValidatedExternalCommand& Command)
{
	AttemptedProposalIds.Add(Command.ProposalId);
	FEvaluationCommandKey Key;
	Key.EvaluationId = Command.EvaluationId;
	Key.CommandType = Command.CommandType;
	AttemptedEvaluationCommandKeys.Add(Key);
}

void UEdenExternalCommandExecutor::AppendExecutionRecord(
	const FEdenValidatedExternalCommand& Command,
	float AttemptSimulationTimeSeconds,
	const FEdenExternalCommandExecutionResult& Result)
{
	FEdenExternalCommandExecutionRecord Record;
	Record.ProposalId = Command.ProposalId;
	Record.SessionId = Command.SessionId;
	Record.EvaluationId = Command.EvaluationId;
	Record.CommandType = Command.CommandType;
	Record.AttemptSimulationTimeSeconds = AttemptSimulationTimeSeconds;
	Record.Outcome = Result.Outcome;
	Record.RejectionReason = Result.RejectionReason;
	Record.RequestedMode = Result.RequestedModeLabel;
	Record.PreviousMode = Result.PreviousModeLabel;
	Record.ResultingMode = Result.ResultingModeLabel;

	if (ExecutionHistory.Num() >= EdenExternalCommandContract::MaxExecutionHistory)
	{
		ExecutionHistory.RemoveAt(0, 1, EAllowShrinking::No);
	}
	ExecutionHistory.Add(Record);
}

FString UEdenExternalCommandExecutor::ModeLabelForCommand(
	EEdenExternalCommandType CommandType,
	const FEdenExternalCommandParameters& Parameters)
{
	using namespace EdenExternalCommandExecutorPrivate;
	switch (CommandType)
	{
	case EEdenExternalCommandType::SetThermalControlMode:
		return ShortEnumName(StaticEnum<EEdenThermalControlMode>(), static_cast<int64>(Parameters.ThermalMode));
	case EEdenExternalCommandType::SetLoadShedMode:
		return ShortEnumName(StaticEnum<EEdenLoadShedMode>(), static_cast<int64>(Parameters.LoadShedMode));
	case EEdenExternalCommandType::SetPropulsionPriorityMode:
		return ShortEnumName(
			StaticEnum<EEdenPropulsionPriorityMode>(),
			static_cast<int64>(Parameters.PropulsionPriorityMode));
	}
	return TEXT("Unknown");
}

FString UEdenExternalCommandExecutor::CurrentModeLabel(
	EEdenExternalCommandType CommandType,
	const UEdenOperatorControlComponent& OperatorControl)
{
	using namespace EdenExternalCommandExecutorPrivate;
	const FEdenOperatorIntent Intent = OperatorControl.GetOperatorIntent();
	switch (CommandType)
	{
	case EEdenExternalCommandType::SetThermalControlMode:
		return ShortEnumName(StaticEnum<EEdenThermalControlMode>(), static_cast<int64>(Intent.ThermalMode));
	case EEdenExternalCommandType::SetLoadShedMode:
		return ShortEnumName(StaticEnum<EEdenLoadShedMode>(), static_cast<int64>(Intent.LoadShedMode));
	case EEdenExternalCommandType::SetPropulsionPriorityMode:
		return ShortEnumName(
			StaticEnum<EEdenPropulsionPriorityMode>(),
			static_cast<int64>(Intent.PropulsionPriority));
	}
	return TEXT("Unknown");
}

bool UEdenExternalCommandExecutor::IsAlreadySatisfied(
	EEdenExternalCommandType CommandType,
	const FEdenExternalCommandParameters& Parameters,
	const UEdenOperatorControlComponent& OperatorControl)
{
	const FEdenOperatorIntent Intent = OperatorControl.GetOperatorIntent();
	switch (CommandType)
	{
	case EEdenExternalCommandType::SetThermalControlMode:
		return Intent.ThermalMode == Parameters.ThermalMode;
	case EEdenExternalCommandType::SetLoadShedMode:
		return Intent.LoadShedMode == Parameters.LoadShedMode;
	case EEdenExternalCommandType::SetPropulsionPriorityMode:
		return Intent.PropulsionPriority == Parameters.PropulsionPriorityMode;
	}
	return false;
}

bool UEdenExternalCommandExecutor::ApplyOnce(
	EEdenExternalCommandType CommandType,
	const FEdenExternalCommandParameters& Parameters,
	UEdenOperatorControlComponent& OperatorControl)
{
	switch (CommandType)
	{
	case EEdenExternalCommandType::SetThermalControlMode:
		return OperatorControl.SetThermalControlMode(
			Parameters.ThermalMode,
			EEdenOperatorCommandSource::EdenAuthorizedControl);
	case EEdenExternalCommandType::SetLoadShedMode:
		return OperatorControl.SetLoadShedMode(
			Parameters.LoadShedMode,
			EEdenOperatorCommandSource::EdenAuthorizedControl);
	case EEdenExternalCommandType::SetPropulsionPriorityMode:
		return OperatorControl.SetPropulsionPriorityMode(
			Parameters.PropulsionPriorityMode,
			EEdenOperatorCommandSource::EdenAuthorizedControl);
	}
	return false;
}

bool UEdenExternalCommandExecutor::ResultMatchesRequested(
	EEdenExternalCommandType CommandType,
	const FEdenExternalCommandParameters& Parameters,
	const UEdenOperatorControlComponent& OperatorControl)
{
	return IsAlreadySatisfied(CommandType, Parameters, OperatorControl);
}

FEdenExternalCommandExecutionResult UEdenExternalCommandExecutor::ExecuteValidatedCommand(
	const FEdenValidatedExternalCommand& Command,
	const FEdenExternalCommandExecutionContext& Context,
	UEdenOperatorControlComponent* OperatorControl)
{
	using namespace EdenExternalCommandExecutorPrivate;

	auto Finish = [this, &Command, &Context](FEdenExternalCommandExecutionResult Result)
	{
		AppendExecutionRecord(Command, Context.AttemptSimulationTimeSeconds, Result);
		return Result;
	};

	// Locked deterministic pre-apply precedence (Checkpoint K contract).
	if (!Context.bExternalCommandExecutionEnabled)
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::ExecutionDisabled));
	}
	if (!Context.bExternalCommandValidationEnabled)
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::ValidationBoundaryDisabled));
	}
	if (Context.AuthorityMode != EEdenOsAuthorityMode::AuthorizedControl)
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::WrongAuthorityMode));
	}
	if (!FEdenExternalCommandModel::IsValidValidatedCommand(Command))
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::InvalidValidatedCommand));
	}
	if (!HasIdentifier(Context.ActiveSessionId))
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::NoActiveSession));
	}
	if (Command.SessionId != Context.ActiveSessionId)
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::SessionMismatch));
	}
	if (!Context.bHasAcceptedEvaluation || !HasIdentifier(Context.LatestAcceptedEvaluationId))
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::NoAcceptedEvaluation));
	}
	if (Command.EvaluationId != Context.LatestAcceptedEvaluationId)
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::EvaluationMismatch));
	}

	RebindSessionIfNeeded(Context.ActiveSessionId);

	if (AttemptedProposalIds.Contains(Command.ProposalId))
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::ProposalAlreadyAttempted));
	}

	FEvaluationCommandKey SlotKey;
	SlotKey.EvaluationId = Command.EvaluationId;
	SlotKey.CommandType = Command.CommandType;
	if (AttemptedEvaluationCommandKeys.Contains(SlotKey))
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::EvaluationCommandTypeAlreadyAttempted));
	}

	if (!OperatorControl || !OperatorControl->IsOperatorControlEnabled())
	{
		return Finish(FEdenExternalCommandExecutionResult::MakeRejected(
			EEdenExternalCommandExecutionRejectionReason::OperatorControlUnavailable));
	}

	const FString RequestedLabel = ModeLabelForCommand(Command.CommandType, Command.Parameters);
	const FString PreviousLabel = CurrentModeLabel(Command.CommandType, *OperatorControl);

	if (IsAlreadySatisfied(Command.CommandType, Command.Parameters, *OperatorControl))
	{
		// No-op consumes ProposalId and evaluation/command-type slot without calling the setter.
		MarkApplyAttempted(Command);
		return Finish(FEdenExternalCommandExecutionResult::MakeNoOp(RequestedLabel));
	}

	// Exactly-once apply attempt: mark before invoking the authoritative mutator.
	MarkApplyAttempted(Command);

	ApplyOnce(Command.CommandType, Command.Parameters, *OperatorControl);

	const FString ResultingLabel = CurrentModeLabel(Command.CommandType, *OperatorControl);
	if (ResultMatchesRequested(Command.CommandType, Command.Parameters, *OperatorControl))
	{
		UE_LOG(
			LogEdenOs,
			Log,
			TEXT("External command executed ProposalId=%s CommandType=%d Previous=%s Resulting=%s"),
			*Command.ProposalId,
			static_cast<int32>(Command.CommandType),
			*PreviousLabel,
			*ResultingLabel);
		return Finish(
			FEdenExternalCommandExecutionResult::MakeExecuted(PreviousLabel, RequestedLabel, ResultingLabel));
	}

	UE_LOG(
		LogEdenOs,
		Warning,
		TEXT("External command convergence failed ProposalId=%s Requested=%s Resulting=%s"),
		*Command.ProposalId,
		*RequestedLabel,
		*ResultingLabel);

	FEdenExternalCommandExecutionResult Failed = FEdenExternalCommandExecutionResult::MakeRejected(
		EEdenExternalCommandExecutionRejectionReason::ConvergenceFailed);
	Failed.bHasPreviousMode = true;
	Failed.bHasRequestedMode = true;
	Failed.bHasResultingMode = true;
	Failed.PreviousModeLabel = PreviousLabel;
	Failed.RequestedModeLabel = RequestedLabel;
	Failed.ResultingModeLabel = ResultingLabel;
	return Finish(Failed);
}

TArray<FEdenExternalCommandExecutionRecord> UEdenExternalCommandExecutor::GetExecutionHistory() const
{
	return ExecutionHistory;
}

TSet<FString> UEdenExternalCommandExecutor::GetAttemptedProposalIdsForTesting() const
{
	return AttemptedProposalIds;
}

void UEdenExternalCommandExecutor::ResetExecutionState()
{
	ExecutionHistory.Reset();
	AttemptedProposalIds.Reset();
	AttemptedEvaluationCommandKeys.Reset();
	BoundSessionId.Reset();
}
