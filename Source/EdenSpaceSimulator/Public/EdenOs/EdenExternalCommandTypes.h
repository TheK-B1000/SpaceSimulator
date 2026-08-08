// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsTypes.h"
#include "Operations/EdenOperatorTypes.h"

#include "EdenExternalCommandTypes.generated.h"

/** Closed 0007 J allowlist. Intent labels only — validation never executes these. */
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

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenExternalCommandValidationOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandValidationStatus Status = EEdenExternalCommandValidationStatus::Rejected;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS|Command")
	EEdenExternalCommandRejectionReason RejectionReason = EEdenExternalCommandRejectionReason::BoundaryDisabled;

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

namespace EdenExternalCommandContract
{
	inline constexpr int32 CurrentSchemaVersion = 1;
	inline constexpr int32 MaxValidationHistory = 128;
}
