// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsTypes.h"
#include "Operations/EdenOperatorTypes.h"

#include "EdenExternalCommandTypes.generated.h"

/** Closed 0007 J/K allowlist. Validation never executes; K executes only via validated artifact. */
UENUM(BlueprintType)
enum class EEdenExternalCommandType : uint8
{
	SetThermalControlMode UMETA(DisplayName = "Set Thermal Control Mode"),
	SetLoadShedMode UMETA(DisplayName = "Set Load Shed Mode"),
	SetPropulsionPriorityMode UMETA(DisplayName = "Set Propulsion Priority Mode")
};

UENUM(BlueprintType)
enum class EEdenExternalCommandParameterKind : uint8
{
	None,
	ThermalControlMode,
	LoadShedMode,
	PropulsionPriorityMode
};

UENUM(BlueprintType)
enum class EEdenExternalCommandRejectionReason : uint8
{
	None,
	BoundaryDisabled,
	WrongAuthorityMode,
	UnsupportedSchemaVersion,
	InvalidProposalId,
	NoActiveSession,
	SessionMismatch,
	NoAcceptedEvaluation,
	EvaluationMismatch,
	DuplicateProposal,
	UnsupportedCommand,
	InvalidParameters
};

UENUM(BlueprintType)
enum class EEdenExternalCommandValidationStatus : uint8
{
	Rejected,
	Valid
};

/** Typed discriminated parameters. Kind must match CommandType; modes reuse 0005 enums. */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	EEdenExternalCommandParameterKind Kind = EEdenExternalCommandParameterKind::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	EEdenThermalControlMode ThermalMode = EEdenThermalControlMode::Nominal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	EEdenLoadShedMode LoadShedMode = EEdenLoadShedMode::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	EEdenPropulsionPriorityMode PropulsionPriorityMode = EEdenPropulsionPriorityMode::Full;
};

/** Internal C++ proposal contract only — not an HTTP/JSON wire schema. */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandProposal
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	FString ProposalId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	FString SessionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	FString EvaluationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	EEdenExternalCommandType CommandType = EEdenExternalCommandType::SetThermalControlMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	FEdenExternalCommandParameters Parameters;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandValidationContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	bool bExternalCommandValidationEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	EEdenOsAuthorityMode AuthorityMode = EEdenOsAuthorityMode::Advisory;

	/** Empty means no active EDEN mission session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	FString ActiveSessionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	bool bHasAcceptedEvaluation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	FString LatestAcceptedEvaluationId;
};

/**
 * Immutable Checkpoint K execution artifact. Produced only by a Valid J validation.
 * Never accept a raw FEdenExternalCommandProposal for execution.
 */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenValidatedExternalCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString ProposalId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString EvaluationId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandType CommandType = EEdenExternalCommandType::SetThermalControlMode;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FEdenExternalCommandParameters Parameters;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandValidationOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandValidationStatus Status = EEdenExternalCommandValidationStatus::Rejected;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandRejectionReason RejectionReason = EEdenExternalCommandRejectionReason::BoundaryDisabled;

	/** Populated only when Status == Valid. Binds exact typed parameters for Checkpoint K. */
	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	bool bHasValidatedCommand = false;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FEdenValidatedExternalCommand ValidatedCommand;

	bool IsValid() const
	{
		return Status == EEdenExternalCommandValidationStatus::Valid;
	}

	static FEdenExternalCommandValidationOutcome MakeValid()
	{
		FEdenExternalCommandValidationOutcome Outcome;
		Outcome.Status = EEdenExternalCommandValidationStatus::Valid;
		Outcome.RejectionReason = EEdenExternalCommandRejectionReason::None;
		return Outcome;
	}

	static FEdenExternalCommandValidationOutcome MakeValid(const FEdenValidatedExternalCommand& Artifact)
	{
		FEdenExternalCommandValidationOutcome Outcome = MakeValid();
		Outcome.bHasValidatedCommand = true;
		Outcome.ValidatedCommand = Artifact;
		return Outcome;
	}

	static FEdenExternalCommandValidationOutcome MakeRejected(EEdenExternalCommandRejectionReason Reason)
	{
		FEdenExternalCommandValidationOutcome Outcome;
		Outcome.Status = EEdenExternalCommandValidationStatus::Rejected;
		Outcome.RejectionReason = Reason;
		return Outcome;
	}
};

/** Immutable validation fact — never implies execution. */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandValidationRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString ProposalId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString EvaluationId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandType CommandType = EEdenExternalCommandType::SetThermalControlMode;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandValidationStatus Status = EEdenExternalCommandValidationStatus::Rejected;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandRejectionReason RejectionReason = EEdenExternalCommandRejectionReason::BoundaryDisabled;
};

UENUM(BlueprintType)
enum class EEdenExternalCommandExecutionOutcome : uint8
{
	Executed,
	NoOpAlreadySatisfied,
	Rejected
};

UENUM(BlueprintType)
enum class EEdenExternalCommandExecutionRejectionReason : uint8
{
	None,
	ExecutionDisabled,
	ValidationBoundaryDisabled,
	WrongAuthorityMode,
	InvalidValidatedCommand,
	NoActiveSession,
	SessionMismatch,
	NoAcceptedEvaluation,
	EvaluationMismatch,
	ProposalAlreadyAttempted,
	EvaluationCommandTypeAlreadyAttempted,
	OperatorControlUnavailable,
	ConvergenceFailed
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandExecutionContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	bool bExternalCommandExecutionEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	bool bExternalCommandValidationEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	EEdenOsAuthorityMode AuthorityMode = EEdenOsAuthorityMode::Advisory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	FString ActiveSessionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	bool bHasAcceptedEvaluation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	FString LatestAcceptedEvaluationId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS|Command")
	float AttemptSimulationTimeSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandExecutionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandExecutionOutcome Outcome = EEdenExternalCommandExecutionOutcome::Rejected;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandExecutionRejectionReason RejectionReason =
		EEdenExternalCommandExecutionRejectionReason::ExecutionDisabled;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	bool bHasPreviousMode = false;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	bool bHasRequestedMode = false;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	bool bHasResultingMode = false;

	/** Mode labels for audit/telemetry (empty when not applicable). */
	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString PreviousModeLabel;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString RequestedModeLabel;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString ResultingModeLabel;

	bool IsExecuted() const
	{
		return Outcome == EEdenExternalCommandExecutionOutcome::Executed;
	}

	bool IsNoOp() const
	{
		return Outcome == EEdenExternalCommandExecutionOutcome::NoOpAlreadySatisfied;
	}

	static FEdenExternalCommandExecutionResult MakeExecuted(
		const FString& PreviousMode,
		const FString& RequestedMode,
		const FString& ResultingMode)
	{
		FEdenExternalCommandExecutionResult Result;
		Result.Outcome = EEdenExternalCommandExecutionOutcome::Executed;
		Result.RejectionReason = EEdenExternalCommandExecutionRejectionReason::None;
		Result.bHasPreviousMode = true;
		Result.bHasRequestedMode = true;
		Result.bHasResultingMode = true;
		Result.PreviousModeLabel = PreviousMode;
		Result.RequestedModeLabel = RequestedMode;
		Result.ResultingModeLabel = ResultingMode;
		return Result;
	}

	static FEdenExternalCommandExecutionResult MakeNoOp(const FString& ModeLabel)
	{
		FEdenExternalCommandExecutionResult Result;
		Result.Outcome = EEdenExternalCommandExecutionOutcome::NoOpAlreadySatisfied;
		Result.RejectionReason = EEdenExternalCommandExecutionRejectionReason::None;
		Result.bHasPreviousMode = true;
		Result.bHasRequestedMode = true;
		Result.bHasResultingMode = true;
		Result.PreviousModeLabel = ModeLabel;
		Result.RequestedModeLabel = ModeLabel;
		Result.ResultingModeLabel = ModeLabel;
		return Result;
	}

	static FEdenExternalCommandExecutionResult MakeRejected(EEdenExternalCommandExecutionRejectionReason Reason)
	{
		FEdenExternalCommandExecutionResult Result;
		Result.Outcome = EEdenExternalCommandExecutionOutcome::Rejected;
		Result.RejectionReason = Reason;
		return Result;
	}
};

/** Diagnostic execution attempt record — not authoritative control state. */
USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandExecutionRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString ProposalId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString EvaluationId;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandType CommandType = EEdenExternalCommandType::SetThermalControlMode;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	float AttemptSimulationTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandExecutionOutcome Outcome = EEdenExternalCommandExecutionOutcome::Rejected;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandExecutionRejectionReason RejectionReason =
		EEdenExternalCommandExecutionRejectionReason::ExecutionDisabled;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString RequestedMode;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString PreviousMode;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	FString ResultingMode;
};

namespace EdenExternalCommandContract
{
	inline constexpr int32 CurrentSchemaVersion = 1;
	inline constexpr int32 MaxValidationHistory = 128;
	inline constexpr int32 MaxExecutionHistory = 128;
}
