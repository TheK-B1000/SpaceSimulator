// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdvisoryModel.h"

namespace EdenOsAdvisoryModelPrivate
{
	/** Newest-N window into a history array. Oldest entries are dropped first. */
	template <typename ElementType>
	void CopyNewestBounded(
		TConstArrayView<ElementType> Source,
		int32 MaxCount,
		TArray<ElementType>& OutWindow,
		bool& bOutTruncated)
	{
		const int32 SafeMax = FMath::Max(MaxCount, 1);
		const int32 CopyCount = FMath::Min(Source.Num(), SafeMax);
		const int32 StartIndex = Source.Num() - CopyCount;

		OutWindow.Reset(CopyCount);
		for (int32 Index = StartIndex; Index < Source.Num(); ++Index)
		{
			OutWindow.Add(Source[Index]);
		}

		bOutTruncated = bOutTruncated || (Source.Num() > CopyCount);
	}

	/** Trend across a snapshot window using a member-pointer selector, so no lambda plumbing is needed. */
	template <typename SelectorType>
	FEdenOsAdvisoryTrend BuildTrendFromWindow(
		const TArray<FEdenTelemetrySnapshot>& Window,
		SelectorType Selector)
	{
		FEdenOsAdvisoryTrend Trend;
		if (Window.Num() == 0)
		{
			return Trend;
		}

		Trend.bHasData = true;
		Trend.SampleCount = Window.Num();
		Trend.EarliestValue = Selector(Window[0]);
		Trend.LatestValue = Selector(Window.Last());
		Trend.Delta = Window.Num() > 1 ? (Trend.LatestValue - Trend.EarliestValue) : 0.0f;
		return Trend;
	}
}

bool FEdenOsAdvisoryModel::IsAdvisoryEvaluationPermitted(EEdenOsAuthorityMode AuthorityMode)
{
	// Observe is telemetry/lifecycle only. Advisory and AuthorizedControl both evaluate so that
	// Checkpoint L can accept an advisory before requesting a typed command proposal.
	return AuthorityMode == EEdenOsAuthorityMode::Advisory
		|| AuthorityMode == EEdenOsAuthorityMode::AuthorizedControl;
}

bool FEdenOsAdvisoryModel::IsMissionRunningForAdvisory(EEdenMissionState MissionState)
{
	return MissionState == EEdenMissionState::Running;
}

bool FEdenOsAdvisoryModel::TryGetTriggerReasonForEventType(
	EEdenTelemetryEventType EventType,
	EEdenOsAdvisoryTriggerReason& OutReason)
{
	switch (EventType)
	{
	case EEdenTelemetryEventType::PhaseChanged:
		OutReason = EEdenOsAdvisoryTriggerReason::MissionPhaseTransition;
		return true;
	case EEdenTelemetryEventType::AlertRaised:
	case EEdenTelemetryEventType::AlertCleared:
		OutReason = EEdenOsAdvisoryTriggerReason::AlertTransition;
		return true;
	case EEdenTelemetryEventType::ObjectiveStateChanged:
		OutReason = EEdenOsAdvisoryTriggerReason::ObjectiveTransition;
		return true;
	case EEdenTelemetryEventType::OperatorCommandIssued:
		OutReason = EEdenOsAdvisoryTriggerReason::OperatorAction;
		return true;
	default:
		// Mission lifecycle, scheduled events, and resource state transitions are recorded by
		// telemetry but are not locked advisory triggers. No speculative triggers.
		return false;
	}
}

void FEdenOsAdvisoryModel::AddTriggerReason(
	TArray<EEdenOsAdvisoryTriggerReason>& Reasons,
	EEdenOsAdvisoryTriggerReason Reason)
{
	if (Reasons.Contains(Reason))
	{
		return;
	}

	Reasons.Add(Reason);
	// Canonical ordering is ascending enum value, so the same settled step always yields the same
	// reason sequence regardless of the order events happened to arrive in.
	Reasons.Sort([](EEdenOsAdvisoryTriggerReason Left, EEdenOsAdvisoryTriggerReason Right)
	{
		return static_cast<uint8>(Left) < static_cast<uint8>(Right);
	});
}

TArray<EEdenOsAdvisoryTriggerReason> FEdenOsAdvisoryModel::DetectEventTriggers(
	TConstArrayView<FEdenTelemetryEvent> Events,
	int64 AfterSequence)
{
	TArray<EEdenOsAdvisoryTriggerReason> Reasons;

	for (const FEdenTelemetryEvent& Event : Events)
	{
		if (Event.SequenceNumber <= AfterSequence)
		{
			continue;
		}

		EEdenOsAdvisoryTriggerReason Reason = EEdenOsAdvisoryTriggerReason::MissionPhaseTransition;
		if (TryGetTriggerReasonForEventType(Event.EventType, Reason))
		{
			AddTriggerReason(Reasons, Reason);
		}
	}

	return Reasons;
}

bool FEdenOsAdvisoryModel::IsHeartbeatDue(
	float SimulationTimeSeconds,
	float LastEvaluationSimulationSeconds,
	bool bHasEvaluatedBefore,
	float HeartbeatIntervalSimulationSeconds)
{
	if (!bHasEvaluatedBefore)
	{
		// The first settled step of a running mission always gets one evaluation.
		return true;
	}

	if (!(HeartbeatIntervalSimulationSeconds > 0.0f) || !FMath::IsFinite(HeartbeatIntervalSimulationSeconds))
	{
		return false;
	}

	const float Elapsed = SimulationTimeSeconds - LastEvaluationSimulationSeconds;
	return Elapsed >= HeartbeatIntervalSimulationSeconds;
}

int64 FEdenOsAdvisoryModel::ResolveHighestObservedSequence(
	TConstArrayView<FEdenTelemetryEvent> Events,
	TConstArrayView<FEdenTelemetrySnapshot> Snapshots,
	int64 FallbackSequence)
{
	int64 Highest = FallbackSequence;

	for (const FEdenTelemetryEvent& Event : Events)
	{
		Highest = FMath::Max(Highest, Event.SequenceNumber);
	}

	for (const FEdenTelemetrySnapshot& Snapshot : Snapshots)
	{
		Highest = FMath::Max(Highest, Snapshot.SequenceNumber);
	}

	return Highest;
}

FEdenOsAdvisoryContext FEdenOsAdvisoryModel::BuildContext(
	const FEdenOsAdvisoryEvaluationInput& Input,
	const TArray<EEdenOsAdvisoryTriggerReason>& TriggerReasons)
{
	using namespace EdenOsAdvisoryModelPrivate;

	FEdenOsAdvisoryContext Context;
	Context.bIsValid = true;
	Context.SessionId = Input.SessionId;
	Context.SimulationTimeSeconds = Input.SimulationTimeSeconds;
	Context.MissionState = Input.MissionState;
	Context.TriggerReasons = TriggerReasons;
	Context.EvaluatedThroughSequence =
		ResolveHighestObservedSequence(Input.Events, Input.Snapshots, Input.LastEvaluatedSequence);
	Context.bUpstreamHistoryTruncated = Input.Metadata.bHistoryTruncated;

	// Bounded newest-N windows. Value copies, so the context cannot alias telemetry's arrays.
	CopyNewestBounded<FEdenTelemetrySnapshot>(
		Input.Snapshots,
		Input.Bounds.MaxSnapshots,
		Context.RecentSnapshots,
		Context.bContextTruncated);

	CopyNewestBounded<FEdenTelemetryEvent>(
		Input.Events,
		Input.Bounds.MaxEvents,
		Context.RecentEvents,
		Context.bContextTruncated);

	if (Context.RecentSnapshots.Num() > 0)
	{
		const FEdenTelemetrySnapshot& Latest = Context.RecentSnapshots.Last();

		// Snapshot time comes from the selected snapshot itself and is never fabricated from the
		// evaluation time; decimation means the two legitimately differ.
		Context.ContextSnapshotSimulationTimeSeconds = Latest.SimulationTimeSeconds;
		Context.MissionElapsedTimeSeconds = Latest.MissionElapsedTimeSeconds;
		Context.MissionPhase = Latest.Mission.MissionPhase;
		Context.ActiveMissionId = Latest.Mission.ActiveMissionId;
		Context.ObjectiveStates = Latest.Mission.ObjectiveSnapshots;
		Context.Fuel = Latest.Fuel;
		Context.Power = Latest.Power;
		Context.Thermal = Latest.Thermal;
		Context.Flight = Latest.Flight;
		Context.Operator = Latest.Operator;

		Context.TemperatureCelsiusTrend = BuildTrendFromWindow(
			Context.RecentSnapshots,
			[](const FEdenTelemetrySnapshot& Snapshot) { return Snapshot.Thermal.TemperatureCelsius; });

		Context.BatteryChargeFractionTrend = BuildTrendFromWindow(
			Context.RecentSnapshots,
			[](const FEdenTelemetrySnapshot& Snapshot) { return Snapshot.Power.ChargeFraction; });

		Context.FuelFractionTrend = BuildTrendFromWindow(
			Context.RecentSnapshots,
			[](const FEdenTelemetrySnapshot& Snapshot) { return Snapshot.Fuel.FuelFraction; });
	}

	return Context;
}

FEdenOsAdvisoryEvaluationResult FEdenOsAdvisoryModel::Evaluate(const FEdenOsAdvisoryEvaluationInput& Input)
{
	FEdenOsAdvisoryEvaluationResult Result;
	Result.NewLastEvaluatedSequence = Input.LastEvaluatedSequence;
	Result.NewLastEvaluationSimulationSeconds = Input.LastEvaluationSimulationSeconds;

	if (!IsAdvisoryEvaluationPermitted(Input.AuthorityMode))
	{
		return Result;
	}

	if (!IsMissionRunningForAdvisory(Input.MissionState))
	{
		return Result;
	}

	// Collect every trigger for this settled step, then build exactly one context from all of them.
	TArray<EEdenOsAdvisoryTriggerReason> Reasons = DetectEventTriggers(Input.Events, Input.LastEvaluatedSequence);

	const bool bHeartbeatDue = IsHeartbeatDue(
		Input.SimulationTimeSeconds,
		Input.LastEvaluationSimulationSeconds,
		Input.bHasEvaluatedBefore,
		Input.HeartbeatIntervalSimulationSeconds);

	if (bHeartbeatDue)
	{
		AddTriggerReason(Reasons, EEdenOsAdvisoryTriggerReason::Heartbeat);
	}

	if (Reasons.Num() == 0)
	{
		return Result;
	}

	Result.bShouldEvaluate = true;
	Result.Context = BuildContext(Input, Reasons);
	Result.NewLastEvaluatedSequence = Result.Context.EvaluatedThroughSequence;
	Result.NewLastEvaluationSimulationSeconds = Input.SimulationTimeSeconds;
	return Result;
}
