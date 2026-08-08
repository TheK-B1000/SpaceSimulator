// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsTypes.h"
#include "UObject/Object.h"

#include "EdenOsConnectionSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, BlueprintType)
class EDENSPACESIMULATOR_API UEdenOsConnectionSettings : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Eden|OS")
	FEdenOsConnectionConfig MakeConnectionConfig() const;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS")
	bool bEnabled = false;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS")
	FString BaseUrl;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS")
	FString DefaultScenarioId = TEXT("SolarEventEmergency");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS", meta = (ClampMin = "0.001", Units = "s"))
	float ConnectionTimeoutSeconds = 2.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS", meta = (ClampMin = "0.001", Units = "s"))
	float RequestTimeoutSeconds = 5.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS", meta = (ClampMin = "1"))
	int32 MaxQueueDepth = 256;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS", meta = (ClampMin = "0.001", Units = "s"))
	float AdvisoryHeartbeatSimulationSeconds = 5.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS")
	EEdenOsAuthorityMode AuthorityMode = EEdenOsAuthorityMode::Advisory;

	/** Checkpoint J validation airlock. Default false — never enables execution. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS")
	bool bExternalCommandValidationEnabled = false;

	/** Checkpoint K execution airlock. Default false — never auto-enabled. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS")
	bool bExternalCommandExecutionEnabled = false;

	/** Checkpoint L automation airlock. Default false — never auto-enabled. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Eden|OS")
	bool bExternalCommandAutomationEnabled = false;
};

struct EDENSPACESIMULATOR_API FEdenOsConnectionConfigModel
{
	static FEdenOsValidationResult Validate(const FEdenOsConnectionConfig& Config);
	static FEdenOsConnectionSnapshot MakeInitialSnapshot(
		const FEdenOsConnectionConfig& Config,
		const FEdenOsValidationResult& Validation);
	static FString DescribeForLog(
		const FEdenOsConnectionConfig& Config,
		const FEdenOsConnectionSnapshot& Snapshot,
		const FEdenOsValidationResult& Validation);
};
