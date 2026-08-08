// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Systems/EdenResourceDebugTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "EdenSimulationClockSubsystem.generated.h"

namespace EdenSimulationClockPriority
{
	constexpr int32 Default = 0;
	constexpr int32 Systems = 0;
	constexpr int32 Mission = 100;
	constexpr int32 Observers = 200;
	/** Advisory evaluation runs strictly after telemetry so it only ever sees settled state. */
	constexpr int32 Advisory = 300;
}

USTRUCT()
struct FEdenSimulationClockSubscriberEntry
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> Subscriber;

	UPROPERTY(Transient)
	int32 Priority = 0;

	UPROPERTY(Transient)
	int32 RegistrationOrder = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEdenSimulationClockOverrunSignature, int32, DroppedSteps);

UCLASS()
class EDENSPACESIMULATOR_API UEdenSimulationClockSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Simulation")
	bool RegisterSimulationTickable(UObject* Subscriber, int32 Priority = 0);

	UFUNCTION(BlueprintCallable, Category = "Eden|Simulation")
	bool UnregisterSimulationTickable(UObject* Subscriber);

	UFUNCTION(BlueprintCallable, Category = "Eden|Simulation")
	void PauseSimulation();

	UFUNCTION(BlueprintCallable, Category = "Eden|Simulation")
	void ResumeSimulation();

	UFUNCTION(BlueprintCallable, Category = "Eden|Simulation")
	void ResetSimulationClock();

	UFUNCTION(BlueprintCallable, Category = "Eden|Simulation")
	bool SetFixedStepSeconds(float NewFixedStepSeconds);

	UFUNCTION(BlueprintCallable, Category = "Eden|Simulation")
	bool SetMaxCatchUpSteps(int32 NewMaxCatchUpSteps);

	UFUNCTION(BlueprintPure, Category = "Eden|Simulation")
	bool IsSimulationPaused() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Simulation")
	float GetFixedStepSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Simulation")
	int32 GetMaxCatchUpSteps() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Simulation")
	float GetElapsedSimulationTimeSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Simulation")
	float GetAccumulatorSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Simulation")
	int32 GetLastStepsTaken() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Simulation")
	int32 GetLastDroppedSteps() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Simulation")
	int32 GetSubscriberCount() const;

	FEdenSimulationClockDebugSnapshot GetSimulationClockDebugSnapshot() const;

	UPROPERTY(BlueprintAssignable, Category = "Eden|Simulation")
	FEdenSimulationClockOverrunSignature OnSimulationClockOverrun;

private:
	bool IsClockConfigurationValid() const;
	bool IsValidSubscriber(const UObject* Subscriber) const;
	bool ContainsSubscriber(const TArray<FEdenSimulationClockSubscriberEntry>& SubscriberList, const UObject* Subscriber) const;
	bool RemoveSubscriberNow(const UObject* Subscriber);
	void PruneInvalidSubscribers(TArray<FEdenSimulationClockSubscriberEntry>& SubscriberList);
	void BuildSubscriberSnapshot();
	void FlushDeferredSubscriberMutations();
	void LogInvalidConfiguration();
	void LogInvalidDeltaTime(float DeltaTimeSeconds);
	void LogDroppedSteps(int32 DroppedSteps);

	UPROPERTY(EditAnywhere, Category = "Eden|Simulation", meta = (ClampMin = "0.000001", Units = "s"))
	float FixedStepSeconds = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Eden|Simulation", meta = (ClampMin = "1"))
	int32 MaxCatchUpSteps = 4;

	UPROPERTY(Transient)
	TArray<FEdenSimulationClockSubscriberEntry> Subscribers;

	UPROPERTY(Transient)
	TArray<FEdenSimulationClockSubscriberEntry> PendingSubscriberRegistrations;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UObject>> PendingSubscriberUnregistrations;

	UPROPERTY(Transient)
	TArray<FEdenSimulationClockSubscriberEntry> SubscriberSnapshot;

	float AccumulatorSeconds = 0.0f;
	float ElapsedSimulationTimeSeconds = 0.0f;
	int32 LastStepsTaken = 0;
	int32 LastDroppedSteps = 0;
	int32 NextRegistrationOrder = 0;
	bool bPaused = false;
	bool bIsStepping = false;
	bool bLoggedInvalidConfiguration = false;
	bool bLoggedInvalidDeltaTime = false;
};
