// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "EdenOs/EdenOsAdvisoryTypes.h"
#include "EdenOs/EdenOsTypes.h"
#include "Missions/EdenMissionTypes.h"
#include "Telemetry/EdenTelemetryTypes.h"

/**
 * Everything one advisory evaluation may inspect.
 *
 * All history arrives as views into accepted 0006 telemetry history. The adapter contributes only
 * its own cursors (LastEvaluatedSequence, LastEvaluationSimulationSeconds) — never a copy of
 * simulation or telemetry truth.
 */
struct EDENSPACESIMULATOR_API FEdenOsAdvisoryEvaluationInput
{
	TConstArrayView<FEdenTelemetryEvent> Events;
	TConstArrayView<FEdenTelemetrySnapshot> Snapshots;
	FEdenTelemetrySessionMetadata Metadata;
	FString SessionId;

	/** Settled simulation time for the step being evaluated. */
	float SimulationTimeSeconds = 0.0f;

	/** Authoritative mission state, read from the settled snapshot. */
	EEdenMissionState MissionState = EEdenMissionState::Inactive;

	EEdenOsAuthorityMode AuthorityMode = EEdenOsAuthorityMode::Advisory;
	float HeartbeatIntervalSimulationSeconds = 5.0f;
	FEdenOsAdvisoryContextBounds Bounds;

	/** Adapter cursor: highest telemetry sequence already folded into a previous evaluation. */
	int64 LastEvaluatedSequence = 0;

	/** Adapter cursor: simulation time of the previous evaluation. */
	float LastEvaluationSimulationSeconds = 0.0f;

	/** False until the first evaluation of a mission has occurred. */
	bool bHasEvaluatedBefore = false;
};

struct EDENSPACESIMULATOR_API FEdenOsAdvisoryEvaluationResult
{
	/** True when this settled step produced exactly one advisory evaluation. */
	bool bShouldEvaluate = false;

	FEdenOsAdvisoryContext Context;

	/** Cursors the caller should store when bShouldEvaluate is true. */
	int64 NewLastEvaluatedSequence = 0;
	float NewLastEvaluationSimulationSeconds = 0.0f;
};

/**
 * Pure advisory trigger, cadence, and context model.
 *
 * Deliberately free of world, subsystem, transport, and clock dependencies so that every
 * deterministic rule in Checkpoint H is provable without a running mission or a live server.
 */
struct EDENSPACESIMULATOR_API FEdenOsAdvisoryModel
{
	/**
	 * Advisory evaluation is permitted in Advisory and AuthorizedControl.
	 * Observe never evaluates. Checkpoint L requires Accepted advisories under AuthorizedControl
	 * before command-proposal automation may run.
	 */
	static bool IsAdvisoryEvaluationPermitted(EEdenOsAuthorityMode AuthorityMode);

	/** Heartbeat and event triggers apply only while the mission is Running. */
	static bool IsMissionRunningForAdvisory(EEdenMissionState MissionState);

	/** Maps one telemetry event type onto its trigger reason. Returns false for non-triggering events. */
	static bool TryGetTriggerReasonForEventType(
		EEdenTelemetryEventType EventType,
		EEdenOsAdvisoryTriggerReason& OutReason);

	/**
	 * Collects trigger reasons from telemetry events newer than AfterSequence.
	 * Duplicate reasons within the same evaluation collapse to one entry.
	 * Result is sorted into canonical ascending-enum order.
	 */
	static TArray<EEdenOsAdvisoryTriggerReason> DetectEventTriggers(
		TConstArrayView<FEdenTelemetryEvent> Events,
		int64 AfterSequence);

	/** Simulation-time heartbeat. The first evaluation of a mission is always due. */
	static bool IsHeartbeatDue(
		float SimulationTimeSeconds,
		float LastEvaluationSimulationSeconds,
		bool bHasEvaluatedBefore,
		float HeartbeatIntervalSimulationSeconds);

	/** Highest sequence number present across events and snapshots; falls back to the current cursor. */
	static int64 ResolveHighestObservedSequence(
		TConstArrayView<FEdenTelemetryEvent> Events,
		TConstArrayView<FEdenTelemetrySnapshot> Snapshots,
		int64 FallbackSequence);

	/** Inserts a reason if absent and keeps the array in canonical order. */
	static void AddTriggerReason(
		TArray<EEdenOsAdvisoryTriggerReason>& Reasons,
		EEdenOsAdvisoryTriggerReason Reason);

	/** Builds the immutable context. Callers pass the already-coalesced reason set. */
	static FEdenOsAdvisoryContext BuildContext(
		const FEdenOsAdvisoryEvaluationInput& Input,
		const TArray<EEdenOsAdvisoryTriggerReason>& TriggerReasons);

	/**
	 * Full settled-step evaluation: gate on mode and mission state, collect and coalesce every
	 * trigger for this step, and build at most ONE context.
	 */
	static FEdenOsAdvisoryEvaluationResult Evaluate(const FEdenOsAdvisoryEvaluationInput& Input);
};
