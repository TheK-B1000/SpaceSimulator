// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"

#include "EdenOs/EdenOsConnectionSettings.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"

bool UEdenOsAdapterSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UEdenOsAdapterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UEdenOsConnectionSettings* Settings = GetDefault<UEdenOsConnectionSettings>();
	ApplyRuntimeConfig(Settings ? Settings->MakeConnectionConfig() : FEdenOsConnectionConfig());
}

void UEdenOsAdapterSubsystem::Deinitialize()
{
	RuntimeConfig = FEdenOsConnectionConfig();
	LastValidationResult = FEdenOsValidationResult();
	ConnectionSnapshot = FEdenOsConnectionSnapshot();

	Super::Deinitialize();
}

FEdenOsConnectionSnapshot UEdenOsAdapterSubsystem::GetConnectionSnapshot() const
{
	return ConnectionSnapshot;
}

FEdenOsValidationResult UEdenOsAdapterSubsystem::GetLastValidationResult() const
{
	return LastValidationResult;
}

bool UEdenOsAdapterSubsystem::ApplyRuntimeConfig(const FEdenOsConnectionConfig& InConfig)
{
	RuntimeConfig = InConfig;
	RefreshSnapshotFromRuntimeConfig();
	return LastValidationResult.IsValid();
}

void UEdenOsAdapterSubsystem::SetRuntimeBearerJwt(const FString& InBearerJwt)
{
	RuntimeConfig.RuntimeBearerJwt = InBearerJwt;
	RefreshSnapshotFromRuntimeConfig();
}

void UEdenOsAdapterSubsystem::ClearRuntimeBearerJwt()
{
	RuntimeConfig.RuntimeBearerJwt.Reset();
	RefreshSnapshotFromRuntimeConfig();
}

bool UEdenOsAdapterSubsystem::LoadRuntimeBearerJwtFromEnvironment(const FString& VariableName)
{
	if (VariableName.IsEmpty())
	{
		return false;
	}

	const FString RuntimeJwt = FPlatformMisc::GetEnvironmentVariable(*VariableName).TrimStartAndEnd();
	if (RuntimeJwt.IsEmpty())
	{
		ClearRuntimeBearerJwt();
		return false;
	}

	SetRuntimeBearerJwt(RuntimeJwt);
	return true;
}

void UEdenOsAdapterSubsystem::RefreshSnapshotFromRuntimeConfig()
{
	LastValidationResult = FEdenOsConnectionConfigModel::Validate(RuntimeConfig);
	ConnectionSnapshot = FEdenOsConnectionConfigModel::MakeInitialSnapshot(RuntimeConfig, LastValidationResult);
}
