// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "EdenOsAdapterSubsystem.generated.h"

UCLASS()
class EDENSPACESIMULATOR_API UEdenOsAdapterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	UFUNCTION(BlueprintPure, Category = "Eden|OS")
	FEdenOsConnectionSnapshot GetConnectionSnapshot() const;

	FEdenOsValidationResult GetLastValidationResult() const;

	bool ApplyRuntimeConfig(const FEdenOsConnectionConfig& InConfig);
	void SetRuntimeBearerJwt(const FString& InBearerJwt);
	void ClearRuntimeBearerJwt();
	bool LoadRuntimeBearerJwtFromEnvironment(const FString& VariableName);

private:
	void RefreshSnapshotFromRuntimeConfig();

	FEdenOsConnectionConfig RuntimeConfig;
	FEdenOsValidationResult LastValidationResult;

	UPROPERTY(Transient)
	FEdenOsConnectionSnapshot ConnectionSnapshot;
};
