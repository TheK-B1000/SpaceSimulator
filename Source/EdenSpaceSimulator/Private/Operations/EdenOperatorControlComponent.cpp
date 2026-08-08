// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorControlComponent.h"

#include "Core/EdenLogCategories.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Operations/EdenOperatorControlConfigDataAsset.h"
#include "Operations/EdenOperatorControlModel.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "UObject/SoftObjectPath.h"

UEdenOperatorControlComponent::UEdenOperatorControlComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	OperatorControlConfigDataAsset = TSoftObjectPtr<UEdenOperatorControlConfigDataAsset>(
		FSoftObjectPath(TEXT("/Game/Eden/Data/Operations/DA_EdenOperatorControlConfig.DA_EdenOperatorControlConfig")));
}

void UEdenOperatorControlComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeFromConfiguredDataAsset();
}

bool UEdenOperatorControlComponent::InitializeOperatorControl(const FEdenOperatorControlConfig& InConfig)
{
	TArray<FString> ValidationErrors;
	if (!FEdenOperatorControlModel::ValidateConfig(InConfig, &ValidationErrors))
	{
		for (const FString& ValidationError : ValidationErrors)
		{
			UE_LOG(LogEdenOperations, Error, TEXT("%s invalid operator config: %s"), *MakeLogContext(), *ValidationError);
		}
		bOperatorControlEnabled = false;
		return false;
	}

	ActiveConfig = InConfig;
	CurrentIntent = FEdenOperatorIntent();
	bOperatorControlEnabled = true;
	return ApplyCurrentIntent(CurrentIntent, false);
}

bool UEdenOperatorControlComponent::ResetOperatorControl()
{
	if (!bOperatorControlEnabled)
	{
		if (!InitializeFromConfiguredDataAsset())
		{
			UE_LOG(LogEdenOperations, Warning, TEXT("%s cannot reset; operator control is disabled."), *MakeLogContext());
			return false;
		}
		return true;
	}

	const FEdenOperatorIntent PreviousIntent = CurrentIntent;
	CurrentIntent = FEdenOperatorIntent();
	return ApplyCurrentIntent(PreviousIntent, true);
}

bool UEdenOperatorControlComponent::SetThermalControlMode(
	EEdenThermalControlMode NewMode,
	EEdenOperatorCommandSource Source)
{
	if (!bOperatorControlEnabled)
	{
		return false;
	}

	if (CurrentIntent.ThermalMode == NewMode)
	{
		return true;
	}

	LastCommandSource = Source;
	const FEdenOperatorIntent PreviousIntent = CurrentIntent;
	CurrentIntent.ThermalMode = NewMode;
	if (!ApplyCurrentIntent(PreviousIntent, true))
	{
		CurrentIntent = PreviousIntent;
		return false;
	}

	return true;
}

bool UEdenOperatorControlComponent::SetLoadShedMode(
	EEdenLoadShedMode NewMode,
	EEdenOperatorCommandSource Source)
{
	if (!bOperatorControlEnabled)
	{
		return false;
	}

	if (CurrentIntent.LoadShedMode == NewMode)
	{
		return true;
	}

	LastCommandSource = Source;
	const FEdenOperatorIntent PreviousIntent = CurrentIntent;
	CurrentIntent.LoadShedMode = NewMode;
	if (!ApplyCurrentIntent(PreviousIntent, true))
	{
		CurrentIntent = PreviousIntent;
		return false;
	}

	return true;
}

bool UEdenOperatorControlComponent::SetPropulsionPriorityMode(
	EEdenPropulsionPriorityMode NewMode,
	EEdenOperatorCommandSource Source)
{
	if (!bOperatorControlEnabled)
	{
		return false;
	}

	if (CurrentIntent.PropulsionPriority == NewMode)
	{
		return true;
	}

	LastCommandSource = Source;
	const FEdenOperatorIntent PreviousIntent = CurrentIntent;
	CurrentIntent.PropulsionPriority = NewMode;
	if (!ApplyCurrentIntent(PreviousIntent, true))
	{
		CurrentIntent = PreviousIntent;
		return false;
	}

	return true;
}

bool UEdenOperatorControlComponent::CycleThermalControlMode()
{
	const uint8 NextMode = (static_cast<uint8>(CurrentIntent.ThermalMode) + 1) % 4;
	return SetThermalControlMode(static_cast<EEdenThermalControlMode>(NextMode));
}

bool UEdenOperatorControlComponent::ToggleLoadShedMode()
{
	return SetLoadShedMode(
		CurrentIntent.LoadShedMode == EEdenLoadShedMode::Normal ? EEdenLoadShedMode::Shed : EEdenLoadShedMode::Normal);
}

bool UEdenOperatorControlComponent::TogglePropulsionPriorityMode()
{
	return SetPropulsionPriorityMode(
		CurrentIntent.PropulsionPriority == EEdenPropulsionPriorityMode::Full
			? EEdenPropulsionPriorityMode::Reduced
			: EEdenPropulsionPriorityMode::Full);
}

bool UEdenOperatorControlComponent::IsOperatorControlEnabled() const
{
	return bOperatorControlEnabled;
}

FEdenOperatorIntent UEdenOperatorControlComponent::GetOperatorIntent() const
{
	return CurrentIntent;
}

EEdenOperatorCommandSource UEdenOperatorControlComponent::GetLastCommandSource() const
{
	return LastCommandSource;
}

FEdenOperatorStateSnapshot UEdenOperatorControlComponent::GetOperatorStateSnapshot() const
{
	return FEdenOperatorControlModel::MakeSnapshot(CurrentIntent, CurrentModifiers);
}

bool UEdenOperatorControlComponent::InitializeFromConfiguredDataAsset()
{
	UEdenOperatorControlConfigDataAsset* LoadedAsset = OperatorControlConfigDataAsset.LoadSynchronous();
	if (!LoadedAsset)
	{
		UE_LOG(
			LogEdenOperations,
			Warning,
			TEXT("%s has no OperatorControlConfigDataAsset; operator control remains disabled."),
			*MakeLogContext());
		bOperatorControlEnabled = false;
		return false;
	}

	return InitializeOperatorControl(LoadedAsset->OperatorControlConfig);
}

bool UEdenOperatorControlComponent::ApplyCurrentIntent(const FEdenOperatorIntent& PreviousIntent, bool bBroadcastEvents)
{
	if (!ResolveTargets())
	{
		UE_LOG(LogEdenOperations, Warning, TEXT("%s cannot apply intent; missing resource/flight targets."), *MakeLogContext());
		return false;
	}

	CurrentModifiers = FEdenOperatorControlModel::ResolveIntent(CurrentIntent, ActiveConfig);

	UEdenPowerSystemComponent* Power = CachedPower.Get();
	UEdenThermalSystemComponent* Thermal = CachedThermal.Get();
	UEdenFlightMovementComponent* Movement = CachedMovement.Get();

	const bool bPowerOk = Power->SetOperatorDemandKilowatts(CurrentModifiers.OperatorDemandKilowatts);
	const bool bThermalOk =
		Thermal->SetOperatorDissipationDegreesCelsiusPerSecond(CurrentModifiers.OperatorDissipationDegreesCelsiusPerSecond);
	const bool bThrustOk = Movement->SetThrustAuthority(CurrentModifiers.ThrustAuthority);
	Movement->SetStabilizationAssistAvailable(CurrentModifiers.bStabilizationAssistAvailable);

	if (bBroadcastEvents)
	{
		OnOperatorIntentChanged.Broadcast(PreviousIntent, CurrentIntent);
	}

	return bPowerOk && bThermalOk && bThrustOk;
}

bool UEdenOperatorControlComponent::ResolveTargets()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	if (!CachedPower.IsValid())
	{
		CachedPower = OwnerActor->FindComponentByClass<UEdenPowerSystemComponent>();
	}
	if (!CachedThermal.IsValid())
	{
		CachedThermal = OwnerActor->FindComponentByClass<UEdenThermalSystemComponent>();
	}
	if (!CachedMovement.IsValid())
	{
		CachedMovement = OwnerActor->FindComponentByClass<UEdenFlightMovementComponent>();
	}

	return CachedPower.IsValid() && CachedThermal.IsValid() && CachedMovement.IsValid();
}

FString UEdenOperatorControlComponent::MakeLogContext() const
{
	return FString::Printf(TEXT("%s on %s"), *GetNameSafe(this), *GetNameSafe(GetOwner()));
}
