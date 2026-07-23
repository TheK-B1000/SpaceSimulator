// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"

#include "Core/EdenFixedStepClockModel.h"
#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationTickable.h"
#include "Stats/Stats.h"

void UEdenSimulationClockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	AccumulatorSeconds = 0.0f;
	ElapsedSimulationTimeSeconds = 0.0f;
	LastStepsTaken = 0;
	LastDroppedSteps = 0;
	bPaused = false;
	bIsStepping = false;
	bLoggedInvalidConfiguration = false;
	bLoggedInvalidDeltaTime = false;
}

void UEdenSimulationClockSubsystem::Deinitialize()
{
	Subscribers.Reset();
	PendingSubscriberRegistrations.Reset();
	PendingSubscriberUnregistrations.Reset();
	SubscriberSnapshot.Reset();
	bIsStepping = false;

	Super::Deinitialize();
}

void UEdenSimulationClockSubsystem::Tick(float DeltaTime)
{
	LastStepsTaken = 0;
	LastDroppedSteps = 0;

	if (bPaused)
	{
		return;
	}

	if (!IsClockConfigurationValid())
	{
		LogInvalidConfiguration();
		return;
	}
	bLoggedInvalidConfiguration = false;

	if (!FEdenFixedStepClockModel::IsValidDeltaTime(DeltaTime))
	{
		LogInvalidDeltaTime(DeltaTime);
		return;
	}
	bLoggedInvalidDeltaTime = false;

	int32 DroppedSteps = 0;
	const int32 StepsTaken = FEdenFixedStepClockModel::CalculateSteps(
		DeltaTime,
		FixedStepSeconds,
		MaxCatchUpSteps,
		AccumulatorSeconds,
		DroppedSteps);

	LastStepsTaken = StepsTaken;
	LastDroppedSteps = DroppedSteps;

	PruneInvalidSubscribers(Subscribers);
	BuildSubscriberSnapshot();

	bIsStepping = true;
	for (int32 StepIndex = 0; StepIndex < StepsTaken; ++StepIndex)
	{
		for (const TWeakObjectPtr<UObject>& SubscriberWeakObject : SubscriberSnapshot)
		{
			UObject* SubscriberObject = SubscriberWeakObject.Get();
			IEdenSimulationTickable* Subscriber = Cast<IEdenSimulationTickable>(SubscriberObject);
			if (Subscriber)
			{
				Subscriber->AdvanceSimulation(FixedStepSeconds);
			}
		}

		ElapsedSimulationTimeSeconds += FixedStepSeconds;
	}
	bIsStepping = false;

	FlushDeferredSubscriberMutations();
	PruneInvalidSubscribers(Subscribers);

	if (DroppedSteps > 0)
	{
		LogDroppedSteps(DroppedSteps);
		OnSimulationClockOverrun.Broadcast(DroppedSteps);
	}
}

TStatId UEdenSimulationClockSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEdenSimulationClockSubsystem, STATGROUP_Tickables);
}

bool UEdenSimulationClockSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UEdenSimulationClockSubsystem::RegisterSimulationTickable(UObject* Subscriber)
{
	if (!IsValidSubscriber(Subscriber))
	{
		UE_LOG(LogEdenSimClock, Warning, TEXT("Rejected invalid simulation tick subscriber '%s'."), *GetNameSafe(Subscriber));
		return false;
	}

	if (ContainsSubscriber(Subscribers, Subscriber) || ContainsSubscriber(PendingSubscriberRegistrations, Subscriber))
	{
		UE_LOG(LogEdenSimClock, Verbose, TEXT("Rejected duplicate simulation tick subscriber '%s'."), *GetNameSafe(Subscriber));
		return false;
	}

	if (bIsStepping)
	{
		PendingSubscriberRegistrations.Add(Subscriber);
		return true;
	}

	Subscribers.Add(Subscriber);
	return true;
}

bool UEdenSimulationClockSubsystem::UnregisterSimulationTickable(UObject* Subscriber)
{
	if (!Subscriber)
	{
		return false;
	}

	if (bIsStepping)
	{
		const int32 RemovedFromPendingRegistrations = PendingSubscriberRegistrations.RemoveAll(
			[Subscriber](const TWeakObjectPtr<UObject>& ExistingSubscriber)
			{
				return !ExistingSubscriber.IsValid() || ExistingSubscriber.Get() == Subscriber;
			});
		if (RemovedFromPendingRegistrations > 0)
		{
			return true;
		}

		if (ContainsSubscriber(Subscribers, Subscriber) && !ContainsSubscriber(PendingSubscriberUnregistrations, Subscriber))
		{
			PendingSubscriberUnregistrations.Add(Subscriber);
			return true;
		}

		return false;
	}

	return RemoveSubscriberNow(Subscriber);
}

void UEdenSimulationClockSubsystem::PauseSimulation()
{
	bPaused = true;
}

void UEdenSimulationClockSubsystem::ResumeSimulation()
{
	bPaused = false;
}

void UEdenSimulationClockSubsystem::ResetSimulationClock()
{
	AccumulatorSeconds = 0.0f;
	ElapsedSimulationTimeSeconds = 0.0f;
	LastStepsTaken = 0;
	LastDroppedSteps = 0;
}

bool UEdenSimulationClockSubsystem::SetFixedStepSeconds(float NewFixedStepSeconds)
{
	if (!FEdenFixedStepClockModel::IsValidFixedStepSeconds(NewFixedStepSeconds))
	{
		UE_LOG(LogEdenSimClock, Warning, TEXT("Rejected invalid FixedStepSeconds=%f."), NewFixedStepSeconds);
		return false;
	}

	FixedStepSeconds = NewFixedStepSeconds;
	return true;
}

bool UEdenSimulationClockSubsystem::SetMaxCatchUpSteps(int32 NewMaxCatchUpSteps)
{
	if (!FEdenFixedStepClockModel::IsValidMaxCatchUpSteps(NewMaxCatchUpSteps))
	{
		UE_LOG(LogEdenSimClock, Warning, TEXT("Rejected invalid MaxCatchUpSteps=%d."), NewMaxCatchUpSteps);
		return false;
	}

	MaxCatchUpSteps = NewMaxCatchUpSteps;
	return true;
}

bool UEdenSimulationClockSubsystem::IsSimulationPaused() const
{
	return bPaused;
}

float UEdenSimulationClockSubsystem::GetFixedStepSeconds() const
{
	return FixedStepSeconds;
}

int32 UEdenSimulationClockSubsystem::GetMaxCatchUpSteps() const
{
	return MaxCatchUpSteps;
}

float UEdenSimulationClockSubsystem::GetElapsedSimulationTimeSeconds() const
{
	return ElapsedSimulationTimeSeconds;
}

float UEdenSimulationClockSubsystem::GetAccumulatorSeconds() const
{
	return AccumulatorSeconds;
}

int32 UEdenSimulationClockSubsystem::GetLastStepsTaken() const
{
	return LastStepsTaken;
}

int32 UEdenSimulationClockSubsystem::GetLastDroppedSteps() const
{
	return LastDroppedSteps;
}

int32 UEdenSimulationClockSubsystem::GetSubscriberCount() const
{
	return Subscribers.Num();
}

bool UEdenSimulationClockSubsystem::IsClockConfigurationValid() const
{
	return FEdenFixedStepClockModel::IsValidFixedStepSeconds(FixedStepSeconds)
		&& FEdenFixedStepClockModel::IsValidMaxCatchUpSteps(MaxCatchUpSteps);
}

bool UEdenSimulationClockSubsystem::IsValidSubscriber(const UObject* Subscriber) const
{
	return Subscriber && Subscriber->GetClass()->ImplementsInterface(UEdenSimulationTickable::StaticClass());
}

bool UEdenSimulationClockSubsystem::ContainsSubscriber(
	const TArray<TWeakObjectPtr<UObject>>& SubscriberList,
	const UObject* Subscriber) const
{
	if (!Subscriber)
	{
		return false;
	}

	for (const TWeakObjectPtr<UObject>& ExistingSubscriber : SubscriberList)
	{
		if (ExistingSubscriber.Get() == Subscriber)
		{
			return true;
		}
	}

	return false;
}

bool UEdenSimulationClockSubsystem::RemoveSubscriberNow(const UObject* Subscriber)
{
	if (!Subscriber)
	{
		return false;
	}

	const int32 RemovedFromPendingRegistrations = PendingSubscriberRegistrations.RemoveAll(
		[Subscriber](const TWeakObjectPtr<UObject>& ExistingSubscriber)
		{
			return !ExistingSubscriber.IsValid() || ExistingSubscriber.Get() == Subscriber;
		});
	const int32 RemovedFromSubscribers = Subscribers.RemoveAll(
		[Subscriber](const TWeakObjectPtr<UObject>& ExistingSubscriber)
		{
			return !ExistingSubscriber.IsValid() || ExistingSubscriber.Get() == Subscriber;
		});

	return RemovedFromPendingRegistrations > 0 || RemovedFromSubscribers > 0;
}

void UEdenSimulationClockSubsystem::PruneInvalidSubscribers(TArray<TWeakObjectPtr<UObject>>& SubscriberList)
{
	SubscriberList.RemoveAll(
		[](const TWeakObjectPtr<UObject>& Subscriber)
		{
			const UObject* SubscriberObject = Subscriber.Get();
			return !SubscriberObject || !SubscriberObject->GetClass()->ImplementsInterface(UEdenSimulationTickable::StaticClass());
		});
}

void UEdenSimulationClockSubsystem::BuildSubscriberSnapshot()
{
	SubscriberSnapshot.Reset();

	for (const TWeakObjectPtr<UObject>& Subscriber : Subscribers)
	{
		if (Subscriber.IsValid())
		{
			SubscriberSnapshot.Add(Subscriber);
		}
	}
}

void UEdenSimulationClockSubsystem::FlushDeferredSubscriberMutations()
{
	for (const TWeakObjectPtr<UObject>& PendingUnregistration : PendingSubscriberUnregistrations)
	{
		if (const UObject* Subscriber = PendingUnregistration.Get())
		{
			RemoveSubscriberNow(Subscriber);
		}
	}
	PendingSubscriberUnregistrations.Reset();

	PruneInvalidSubscribers(PendingSubscriberRegistrations);
	for (const TWeakObjectPtr<UObject>& PendingRegistration : PendingSubscriberRegistrations)
	{
		UObject* Subscriber = PendingRegistration.Get();
		if (IsValidSubscriber(Subscriber) && !ContainsSubscriber(Subscribers, Subscriber))
		{
			Subscribers.Add(Subscriber);
		}
	}
	PendingSubscriberRegistrations.Reset();
}

void UEdenSimulationClockSubsystem::LogInvalidConfiguration()
{
	if (bLoggedInvalidConfiguration)
	{
		return;
	}

	UE_LOG(
		LogEdenSimClock,
		Error,
		TEXT("%s has invalid clock configuration. FixedStepSeconds=%f MaxCatchUpSteps=%d."),
		*GetNameSafe(this),
		FixedStepSeconds,
		MaxCatchUpSteps);
	bLoggedInvalidConfiguration = true;
}

void UEdenSimulationClockSubsystem::LogInvalidDeltaTime(float DeltaTimeSeconds)
{
	if (bLoggedInvalidDeltaTime)
	{
		return;
	}

	UE_LOG(
		LogEdenSimClock,
		Warning,
		TEXT("%s rejected invalid DeltaTimeSeconds=%f."),
		*GetNameSafe(this),
		DeltaTimeSeconds);
	bLoggedInvalidDeltaTime = true;
}

void UEdenSimulationClockSubsystem::LogDroppedSteps(int32 DroppedSteps)
{
	UE_LOG(
		LogEdenSimClock,
		Warning,
		TEXT("%s dropped %d fixed simulation step(s). Dropped time does not increase elapsed simulation time."),
		*GetNameSafe(this),
		DroppedSteps);
}
