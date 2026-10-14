// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/EdenMissionModel.h"
#include "Core/EdenLogCategories.h"
#include "Algo/StableSort.h"


namespace EdenMissionModel
{
	void AddValidationError(TArray<FString>* OutErrors, const FString& ErrorMessage)
	{
		if (OutErrors)
		{
			OutErrors->Add(ErrorMessage);
		}
	}
}

bool FEdenMissionModel::ValidateDefinition(const FEdenMissionDefinitionConfig& Definition, TArray<FString>* OutErrors)
{
	bool bIsValid = true;

	if (Definition.MissionId.IsNone())
	{
		bIsValid = false;
		EdenMissionModel::AddValidationError(OutErrors, TEXT("MissionId must not be empty."));
	}

	const FEdenMissionInitialConditions& Initial = Definition.InitialConditions;
	if (Initial.bSetInitialTemperature && !FMath::IsFinite(Initial.InitialTemperatureCelsius))
	{
		bIsValid = false;
		EdenMissionModel::AddValidationError(OutErrors, TEXT("InitialConditions.InitialTemperatureCelsius must be finite."));
	}
	if (Initial.bSetBatteryDischargeLimit
		&& (!FMath::IsFinite(Initial.BatteryMaxDischargeKilowatts) || Initial.BatteryMaxDischargeKilowatts <= 0.0f))
	{
		bIsValid = false;
		EdenMissionModel::AddValidationError(OutErrors, TEXT("InitialConditions.BatteryMaxDischargeKilowatts must be finite and > 0."));
	}

	TSet<FName> SeenEventIds;
	for (const FEdenMissionEventConfig& Event : Definition.Events)
	{
		if (SeenEventIds.Contains(Event.EventId))
		{
			bIsValid = false;
			EdenMissionModel::AddValidationError(OutErrors, FString::Printf(TEXT("Duplicate EventId: %s"), *Event.EventId.ToString()));
		}
		SeenEventIds.Add(Event.EventId);

		if (!IsValidSimulationTime(Event.TriggerTimeSeconds))
		{
			bIsValid = false;
			EdenMissionModel::AddValidationError(OutErrors, FString::Printf(TEXT("Invalid TriggerTimeSeconds for EventId: %s"), *Event.EventId.ToString()));
		}

		const bool bCommandRequiresFiniteFloat =
			Event.CommandType == EEdenMissionCommandType::SetExternalHeatingRate
			|| Event.CommandType == EEdenMissionCommandType::SetExternalPowerDemand;

		if (bCommandRequiresFiniteFloat && !FMath::IsFinite(Event.FloatParameter))
		{
			bIsValid = false;
			EdenMissionModel::AddValidationError(
				OutErrors,
				FString::Printf(
					TEXT("FloatParameter must be finite for EventId '%s' command '%s'."),
					*Event.EventId.ToString(),
					*UEnum::GetValueAsString(Event.CommandType)));
		}

		if (Event.CommandType == EEdenMissionCommandType::SetPowerGeneration)
		{
			bIsValid = false;
			EdenMissionModel::AddValidationError(
				OutErrors,
				FString::Printf(
					TEXT("Unsupported command SetPowerGeneration for EventId '%s'."),
					*Event.EventId.ToString()));
		}
	}

	TSet<FName> SeenObjectiveIds;
	for (const FEdenMissionObjectiveConfig& Objective : Definition.Objectives)
	{
		if (SeenObjectiveIds.Contains(Objective.ObjectiveId))
		{
			bIsValid = false;
			EdenMissionModel::AddValidationError(OutErrors, FString::Printf(TEXT("Duplicate ObjectiveId: %s"), *Objective.ObjectiveId.ToString()));
		}
		SeenObjectiveIds.Add(Objective.ObjectiveId);

		if (!FMath::IsFinite(Objective.TargetValue))
		{
			bIsValid = false;
			EdenMissionModel::AddValidationError(
				OutErrors,
				FString::Printf(TEXT("TargetValue must be finite for ObjectiveId '%s'."), *Objective.ObjectiveId.ToString()));
		}

		const bool bRequiresNormalizedFraction =
			Objective.ObjectiveType == EEdenObjectiveType::RestorePowerAbove
			|| Objective.ObjectiveType == EEdenObjectiveType::MaintainFuelAbove
			|| Objective.ObjectiveType == EEdenObjectiveType::MaintainPowerAbove;

		if (!FMath::IsFinite(Objective.HoldSeconds) || Objective.HoldSeconds < 0.0f
			|| !FMath::IsFinite(Objective.DeadlineSeconds) || Objective.DeadlineSeconds < 0.0f)
		{
			bIsValid = false;
			EdenMissionModel::AddValidationError(
				OutErrors,
				FString::Printf(TEXT("HoldSeconds and DeadlineSeconds must be finite and >= 0 for ObjectiveId '%s'."), *Objective.ObjectiveId.ToString()));
		}

		if (bRequiresNormalizedFraction
			&& (Objective.TargetValue < 0.0f || Objective.TargetValue > 1.0f))
		{
			bIsValid = false;
			EdenMissionModel::AddValidationError(
				OutErrors,
				FString::Printf(
					TEXT("TargetValue for ObjectiveId '%s' must be a normalized fraction in [0,1]."),
					*Objective.ObjectiveId.ToString()));
		}
	}

	if (Definition.Objectives.Num() == 0)
	{
		bIsValid = false;
		EdenMissionModel::AddValidationError(OutErrors, TEXT("Missions must have at least one objective."));
	}

	return bIsValid;
}

bool FEdenMissionModel::IsValidSimulationTime(float TimeSeconds)
{
	return FMath::IsFinite(TimeSeconds) && TimeSeconds >= 0.0f;
}

bool FEdenMissionModel::CanTransition(EEdenMissionState Current, EEdenMissionState Target)
{
	if (Current == EEdenMissionState::Inactive && Target == EEdenMissionState::Ready) return true;
	if (Current == EEdenMissionState::Ready && Target == EEdenMissionState::Running) return true;
	if (Current == EEdenMissionState::Running && Target == EEdenMissionState::Succeeded) return true;
	if (Current == EEdenMissionState::Running && Target == EEdenMissionState::Failed) return true;
	if (Current == EEdenMissionState::Running && Target == EEdenMissionState::Inactive) return true;
	if (Current == EEdenMissionState::Succeeded && Target == EEdenMissionState::Inactive) return true;
	if (Current == EEdenMissionState::Failed && Target == EEdenMissionState::Inactive) return true;
	
	return false;
}

FEdenMissionRuntimeState FEdenMissionModel::InitializeRuntimeState(const FEdenMissionDefinitionConfig& Definition)
{
	FEdenMissionRuntimeState State;
	State.MissionState = EEdenMissionState::Ready;
	State.MissionPhase = EEdenMissionPhase::Nominal;
	State.MissionElapsedTimeSeconds = 0.0f;

	TMap<FName, float> EventTimes;
	for (const FEdenMissionEventConfig& EventConfig : Definition.Events)
	{
		FEdenMissionEventRuntime EventRuntime;
		EventRuntime.EventId = EventConfig.EventId;
		EventRuntime.EventState = EEdenMissionEventState::Pending;
		State.EventStates.Add(EventRuntime);
		EventTimes.Add(EventConfig.EventId, EventConfig.TriggerTimeSeconds);
	}

	Algo::StableSort(State.EventStates, [&EventTimes](const FEdenMissionEventRuntime& A, const FEdenMissionEventRuntime& B) {
		float TimeA = EventTimes.Contains(A.EventId) ? EventTimes[A.EventId] : 0.0f;
		float TimeB = EventTimes.Contains(B.EventId) ? EventTimes[B.EventId] : 0.0f;
		return TimeA < TimeB;
	});

	for (const FEdenMissionObjectiveConfig& ObjConfig : Definition.Objectives)
	{
		FEdenMissionObjectiveRuntime ObjRuntime;
		ObjRuntime.ObjectiveId = ObjConfig.ObjectiveId;
		ObjRuntime.State = ObjConfig.bActivateOnStart ? EEdenObjectiveState::Active : EEdenObjectiveState::Pending;
		State.ObjectiveStates.Add(ObjRuntime);
	}

	return State;
}

FEdenMissionStepResult FEdenMissionModel::StepTimeline(
	const FEdenMissionRuntimeState& CurrentState,
	const FEdenMissionDefinitionConfig& Definition,
	float FixedDeltaSeconds)
{
	FEdenMissionStepResult Result;
	Result.UpdatedState = CurrentState;

	if (CurrentState.MissionState != EEdenMissionState::Running)
	{
		return Result;
	}

	Result.UpdatedState.MissionElapsedTimeSeconds += FixedDeltaSeconds;

	TMap<FName, float> EventTimes;
	for (const FEdenMissionEventConfig& EventConfig : Definition.Events)
	{
		EventTimes.Add(EventConfig.EventId, EventConfig.TriggerTimeSeconds);
	}

	for (FEdenMissionEventRuntime& EventRuntime : Result.UpdatedState.EventStates)
	{
		if (EventRuntime.EventState == EEdenMissionEventState::Pending)
		{
			if (const float* TriggerTimePtr = EventTimes.Find(EventRuntime.EventId))
			{
				if (*TriggerTimePtr <= Result.UpdatedState.MissionElapsedTimeSeconds)
				{
					EventRuntime.EventState = EEdenMissionEventState::Executed;
					Result.NewlyTriggeredEventIds.Add(EventRuntime.EventId);
				}
			}
		}
	}

	return Result;
}

FEdenMissionRuntimeState FEdenMissionModel::ActivateObjective(const FEdenMissionRuntimeState& State, FName ObjectiveId)
{
	FEdenMissionRuntimeState NewState = State;
	for (FEdenMissionObjectiveRuntime& Obj : NewState.ObjectiveStates)
	{
		if (Obj.ObjectiveId == ObjectiveId)
		{
			if (Obj.State == EEdenObjectiveState::Pending)
			{
				Obj.State = EEdenObjectiveState::Active;
			}
			break;
		}
	}
	return NewState;
}

FEdenMissionRuntimeState FEdenMissionModel::CompleteObjective(const FEdenMissionRuntimeState& State, FName ObjectiveId)
{
	FEdenMissionRuntimeState NewState = State;
	for (FEdenMissionObjectiveRuntime& Obj : NewState.ObjectiveStates)
	{
		if (Obj.ObjectiveId == ObjectiveId)
		{
			if (Obj.State == EEdenObjectiveState::Active)
			{
				Obj.State = EEdenObjectiveState::Completed;
			}
			break;
		}
	}
	return NewState;
}

FEdenMissionRuntimeState FEdenMissionModel::FailObjective(const FEdenMissionRuntimeState& State, FName ObjectiveId)
{
	FEdenMissionRuntimeState NewState = State;
	for (FEdenMissionObjectiveRuntime& Obj : NewState.ObjectiveStates)
	{
		if (Obj.ObjectiveId == ObjectiveId)
		{
			if (Obj.State == EEdenObjectiveState::Active)
			{
				Obj.State = EEdenObjectiveState::Failed;
			}
			break;
		}
	}
	return NewState;
}

FEdenMissionRuntimeState FEdenMissionModel::EvaluateObjectives(
	const FEdenMissionRuntimeState& State,
	const FEdenMissionDefinitionConfig& Definition,
	float ThermalTemperatureCelsius,
	float PowerChargeFraction,
	float FuelFraction)
{
	// Legacy signature: temperature-only thermal observation, no trend, no hold timing.
	FEdenMissionObservations Observations;
	Observations.bHasThermal = ThermalTemperatureCelsius > TNumericLimits<float>::Lowest();
	Observations.TemperatureCelsius = ThermalTemperatureCelsius;
	Observations.PowerChargeFraction = PowerChargeFraction;
	Observations.FuelFraction = FuelFraction;
	return EvaluateObjectives(State, Definition, Observations, 0.0f);
}

FEdenMissionRuntimeState FEdenMissionModel::EvaluateObjectives(
	const FEdenMissionRuntimeState& State,
	const FEdenMissionDefinitionConfig& Definition,
	const FEdenMissionObservations& Observations,
	float FixedDeltaSeconds)
{
	if (State.MissionState != EEdenMissionState::Running)
	{
		return State;
	}

	TMap<FName, const FEdenMissionObjectiveConfig*> ObjectiveConfigs;
	for (const FEdenMissionObjectiveConfig& Objective : Definition.Objectives)
	{
		ObjectiveConfigs.Add(Objective.ObjectiveId, &Objective);
	}

	const float StepSeconds = FMath::IsFinite(FixedDeltaSeconds) ? FMath::Max(0.0f, FixedDeltaSeconds) : 0.0f;
	// Mission time and hold timers accumulate in float steps; resolve thresholds on the nearest step.
	const float TimeTolerance = StepSeconds > 0.0f ? 0.5f * StepSeconds : 1.0e-4f;
	FEdenMissionRuntimeState NewState = State;
	const float Now = NewState.MissionElapsedTimeSeconds;
	for (FEdenMissionObjectiveRuntime& ObjectiveRuntime : NewState.ObjectiveStates)
	{
		if (ObjectiveRuntime.State != EEdenObjectiveState::Active)
		{
			continue;
		}

		const FEdenMissionObjectiveConfig* const* ConfigPtr = ObjectiveConfigs.Find(ObjectiveRuntime.ObjectiveId);
		if (!ConfigPtr || !*ConfigPtr)
		{
			continue;
		}

		const FEdenMissionObjectiveConfig& Config = **ConfigPtr;
		auto Complete = [&]()
		{
			ObjectiveRuntime.State = EEdenObjectiveState::Completed;
			ObjectiveRuntime.ResolvedAtSeconds = Now;
		};
		auto Fail = [&](const FString& Reason)
		{
			ObjectiveRuntime.State = EEdenObjectiveState::Failed;
			ObjectiveRuntime.ResolvedAtSeconds = Now;
			ObjectiveRuntime.FailureReason = Reason;
		};

		switch (Config.ObjectiveType)
		{
		case EEdenObjectiveType::SurviveUntilTime:
			if (Now >= Config.TargetValue)
			{
				Complete();
			}
			break;

		case EEdenObjectiveType::KeepTemperatureBelow:
			// Equality is valid; fail only when temperature exceeds the limit.
			if (Observations.TemperatureCelsius > Config.TargetValue)
			{
				Fail(FString::Printf(TEXT("temperature %.2f C exceeded %.0f C at T=%.1f s"), Observations.TemperatureCelsius, Config.TargetValue, Now));
			}
			break;

		case EEdenObjectiveType::RestorePowerAbove:
			if (Observations.PowerChargeFraction >= Config.TargetValue)
			{
				Complete();
			}
			break;

		case EEdenObjectiveType::MaintainFuelAbove:
			// Equality is valid; fail only when fuel fraction drops below the limit.
			if (Observations.FuelFraction < Config.TargetValue)
			{
				Fail(FString::Printf(TEXT("propellant %.0f%% fell below %.0f%% at T=%.1f s"), Observations.FuelFraction * 100.0f, Config.TargetValue * 100.0f, Now));
			}
			break;

		case EEdenObjectiveType::MaintainPowerAbove:
			if (Observations.PowerChargeFraction < Config.TargetValue)
			{
				Fail(FString::Printf(TEXT("battery %.0f%% fell below %.0f%% at T=%.1f s"), Observations.PowerChargeFraction * 100.0f, Config.TargetValue * 100.0f, Now));
			}
			break;

		case EEdenObjectiveType::RecoverThermalStability:
		{
			const bool bRecovering = Observations.bHasThermal
				&& Observations.ExternalHeatingDegreesCelsiusPerSecond <= 0.0f
				&& Observations.TemperatureCelsius < Config.TargetValue
				&& Observations.RealizedNetDegreesCelsiusPerSecond <= StableTrendToleranceDegreesCelsiusPerSecond;
			// Any break resets the hold: the conditions must hold continuously.
			ObjectiveRuntime.HoldElapsedSeconds = bRecovering ? ObjectiveRuntime.HoldElapsedSeconds + StepSeconds : 0.0f;
			if (bRecovering && ObjectiveRuntime.HoldElapsedSeconds >= Config.HoldSeconds - TimeTolerance)
			{
				Complete(); // Completion this step wins over a deadline reached this same step.
			}
			else if (Config.DeadlineSeconds > 0.0f && Now >= Config.DeadlineSeconds - TimeTolerance)
			{
				Fail(FString::Printf(
					TEXT("recovery not achieved by T=%.0f s (%.2f C, %+.2f C/s, stable for %.1f/%.1f s)"),
					Config.DeadlineSeconds,
					Observations.TemperatureCelsius,
					Observations.RealizedNetDegreesCelsiusPerSecond,
					ObjectiveRuntime.HoldElapsedSeconds,
					Config.HoldSeconds));
			}
			break;
		}

		default:
			break;
		}
	}

	return NewState;
}

EEdenMissionState FEdenMissionModel::EvaluateOutcome(
	const FEdenMissionRuntimeState& State,
	const FEdenMissionDefinitionConfig& Definition)
{
	if (State.MissionState != EEdenMissionState::Running)
	{
		return State.MissionState;
	}

	TMap<FName, const FEdenMissionObjectiveConfig*> ObjectiveConfigs;
	for (const FEdenMissionObjectiveConfig& Objective : Definition.Objectives)
	{
		ObjectiveConfigs.Add(Objective.ObjectiveId, &Objective);
	}

	bool bHasRequired = false;
	bool bHasRequiredCompleteSeeking = false;
	bool bAllCompleteSeekingCompleted = true;
	bool bAllConstraintsHeld = true;

	for (const FEdenMissionObjectiveRuntime& ObjState : State.ObjectiveStates)
	{
		const FEdenMissionObjectiveConfig* const* ConfigPtr = ObjectiveConfigs.Find(ObjState.ObjectiveId);
		if (!ConfigPtr || !*ConfigPtr || !(*ConfigPtr)->bRequired)
		{
			continue;
		}

		bHasRequired = true;
		const FEdenMissionObjectiveConfig& Config = **ConfigPtr;

		if (ObjState.State == EEdenObjectiveState::Failed)
		{
			return EEdenMissionState::Failed;
		}

		const bool bFailOnlyConstraint =
			Config.ObjectiveType == EEdenObjectiveType::KeepTemperatureBelow
			|| Config.ObjectiveType == EEdenObjectiveType::MaintainFuelAbove
			|| Config.ObjectiveType == EEdenObjectiveType::MaintainPowerAbove;

		if (bFailOnlyConstraint)
		{
			if (ObjState.State != EEdenObjectiveState::Active
				&& ObjState.State != EEdenObjectiveState::Completed)
			{
				bAllConstraintsHeld = false;
			}
			continue;
		}

		bHasRequiredCompleteSeeking = true;
		if (ObjState.State != EEdenObjectiveState::Completed)
		{
			bAllCompleteSeekingCompleted = false;
		}
	}

	if (!bHasRequired)
	{
		return EEdenMissionState::Running;
	}

	// Complete-seeking required objectives (SurviveUntilTime / RestorePowerAbove) must Complete.
	// Fail-only constraints may remain Active; they block success only by Failed.
	if (bHasRequiredCompleteSeeking && bAllCompleteSeekingCompleted && bAllConstraintsHeld)
	{
		return EEdenMissionState::Succeeded;
	}

	return EEdenMissionState::Running;
}

FString FEdenMissionModel::DescribeOutcome(
	const FEdenMissionRuntimeState& State,
	const FEdenMissionDefinitionConfig& Definition,
	EEdenMissionState Outcome)
{
	if (Outcome == EEdenMissionState::Succeeded)
	{
		return FString::Printf(TEXT("all required objectives met at T=%.1f s"), State.MissionElapsedTimeSeconds);
	}
	if (Outcome == EEdenMissionState::Failed)
	{
		for (const FEdenMissionObjectiveConfig& Config : Definition.Objectives)
		{
			if (!Config.bRequired)
			{
				continue;
			}
			const FEdenMissionObjectiveRuntime* Runtime = State.ObjectiveStates.FindByPredicate(
				[&Config](const FEdenMissionObjectiveRuntime& Candidate) { return Candidate.ObjectiveId == Config.ObjectiveId; });
			if (Runtime && Runtime->State == EEdenObjectiveState::Failed)
			{
				return FString::Printf(TEXT("%s failed: %s"), *Config.ObjectiveId.ToString(),
					Runtime->FailureReason.IsEmpty() ? TEXT("objective failed") : *Runtime->FailureReason);
			}
		}
		return TEXT("a required objective failed");
	}
	return FString();
}

FEdenMissionRuntimeState FEdenMissionModel::ResetRuntimeState()
{
	return FEdenMissionRuntimeState();
}

FEdenMissionStateSnapshot FEdenMissionModel::MakeSnapshot(const FEdenMissionRuntimeState& State, FName MissionId)
{
	FEdenMissionStateSnapshot Snapshot;
	Snapshot.MissionState = State.MissionState;
	Snapshot.MissionPhase = State.MissionPhase;
	Snapshot.MissionElapsedTimeSeconds = State.MissionElapsedTimeSeconds;
	Snapshot.ActiveMissionId = MissionId;
	Snapshot.ObjectiveSnapshots = State.ObjectiveStates;
	Snapshot.TerminalReason = State.TerminalReason;
	return Snapshot;
}
