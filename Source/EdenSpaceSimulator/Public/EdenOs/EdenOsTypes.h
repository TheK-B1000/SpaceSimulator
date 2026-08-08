// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdenOsTypes.generated.h"

UENUM(BlueprintType)
enum class EEdenOsAuthorityMode : uint8
{
	Observe,
	Advisory,
	AuthorizedControl
};

UENUM(BlueprintType)
enum class EEdenOsConnectionState : uint8
{
	Disabled,
	Disconnected,
	Connecting,
	Connected,
	Degraded
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOsConnectionConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS")
	FString BaseUrl;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS")
	FString DefaultScenarioId = TEXT("SolarEventEmergency");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS", meta = (ClampMin = "0.001", Units = "s"))
	float ConnectionTimeoutSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS", meta = (ClampMin = "0.001", Units = "s"))
	float RequestTimeoutSeconds = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS", meta = (ClampMin = "1"))
	int32 MaxQueueDepth = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS", meta = (ClampMin = "0.001", Units = "s"))
	float AdvisoryHeartbeatSimulationSeconds = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS")
	EEdenOsAuthorityMode AuthorityMode = EEdenOsAuthorityMode::Advisory;

	/**
	 * Checkpoint J: when true with AuthorizedControl, typed proposals may Validate.
	 * Default false. Validation never executes commands (Checkpoint K).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|OS")
	bool bExternalCommandValidationEnabled = false;

	FString RuntimeBearerJwt;
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOsValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	TArray<FString> Errors;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	TArray<FString> Warnings;

	bool IsValid() const
	{
		return Errors.IsEmpty();
	}

	void AddError(const FString& Error)
	{
		Errors.Add(Error);
	}

	void AddWarning(const FString& Warning)
	{
		Warnings.Add(Warning);
	}

	FString GetFirstErrorOrEmpty() const
	{
		return Errors.IsEmpty() ? FString() : Errors[0];
	}
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenOsConnectionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	int32 SchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	bool bEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	EEdenOsAuthorityMode AuthorityMode = EEdenOsAuthorityMode::Advisory;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	bool bExternalCommandValidationEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	EEdenOsConnectionState ConnectionState = EEdenOsConnectionState::Disabled;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	bool bHasBearerJwt = false;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	int32 PendingMessageCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	int32 DroppedMessageCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Eden|OS")
	FString LastErrorSummary;
};
