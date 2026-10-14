# 0010 H.1 Rich Advisory Context

## Status

In progress — 2026-10-14. Implementation, Editor build and automation on `feature/0010-h1-rich-advisory-context`. No human PIE yet. Not merged.

## Problem and outcome

The EDEN OS advisory request (ExecPlan 0007, H.1) sends temperature, battery and fuel with their window deltas, but not the thermal limits, heat rates, operator modes or power balance. ProjectEden therefore cannot tell external heating from weak cooling, cannot see which cooling mode is already active, and had to assume default thresholds. Its old mitigation policy also recommended load shedding as thermal relief, although this simulator's load shed lowers cooling dissipation (ADR-0002). ProjectEden corrected that policy separately (ProjectEden commit `1f55190`).

Observable result: during an advisory evaluation, the H.1 `context` object carries up to 14 additional optional fields. They come from the same newest snapshot as the existing fields, and each is present only when its source was simulating. Without those sources the request body is byte-identical to today's.

## Scope

### In scope

- `windowDurationSeconds`, `thermalWarningCelsius`, `thermalCriticalCelsius`, `thermalAmbientCelsius`, `thermalAbsoluteMaxCelsius`, `heatGenerationDegreesCelsiusPerSecond`, `effectiveDissipationDegreesCelsiusPerSecond`, `externalHeatingRateDegreesCelsiusPerSecond`, `thermalControlMode`, `loadShedMode`, `propulsionPriority`, `powerGenerationKilowatts`, `powerDemandKilowatts`, `netPowerKilowatts`.
- Capture of thermal limits into each telemetry snapshot at the simulation step.
- Explicit source availability per snapshot, so default values from unavailable owners are never sent.
- Automation tests for serialization, omission, observed zero, non-finite omission, mode vocabulary, window duration, newest-snapshot alignment, threshold capture, and legacy byte identity.

### Out of scope

- Any change to ExecPlan 0007, its Checkpoint M human PIE gate, `main`, or `EdenSpaceSimulator 5.8`.
- Telemetry Export Schema v1, the advisory response, command proposals, or the external command boundary.
- Schema version changes. The fields are additive under schema version 1.
- Flight, visuals, HUD and mission content.

## Current repository state

Audited 2026-10-14 on `feature/0009-visual-flight-integration` @ `e0f8c96`, which contains `feature/0008-newtonian-flight` @ `a910663`.

| Item | Fact |
|---|---|
| H.1 serializer | `FEdenOsWireSerializationModel::BuildAdvisoryJsonV1`, explicit field-by-field `FString::Printf` |
| Context builder | `FEdenOsAdvisoryModel::BuildContext` copies Thermal, Power, Operator from the newest of at most 10 snapshots |
| Snapshot assembly | `UEdenTelemetrySubsystem::AssembleSnapshot` leaves default structs when an owner is unbound or disabled |
| Cadence | Snapshot every 5 fixed steps of 0.1 s (0.5 s); a full advisory window spans 4.5 s |
| Thermal config | Owned by `UEdenThermalSystemComponent` (`GetActiveThermalConfig`); not in any snapshot before this plan |
| Operator effects | `FEdenOperatorControlModel::ResolveIntent` unchanged since `main` `9738130`; propulsion adds no power demand or heat on 0008/0009 |
| ProjectEden | Accepts any finite scalar or string context field; spec in ProjectEden `docs/telemetry/h1-rich-advisory-context.md` |

## Architecture alignment

| Mutable state | Owner | This plan |
|---|---|---|
| Thermal config (limits) | `UEdenThermalSystemComponent` | Read only, copied by value into `FEdenTelemetrySnapshot::ThermalLimits` |
| Source availability | Telemetry subsystem, per snapshot | New `FEdenTelemetrySnapshot::Sources`, derived from owner enable flags |
| Advisory context | `FEdenOsAdvisoryModel` (value type) | Copies `Sources` and `ThermalLimits` from the newest snapshot and computes `WindowDurationSeconds` |
| Wire body | `FEdenOsWireSerializationModel` | Appends optional keys after the legacy keys |

Dependency direction is unchanged: telemetry reads owners, EDEN OS reads telemetry snapshots, and nothing writes back. ADR-0004 records the telemetry contract change.

## Alternatives considered

| Option | Verdict |
|---|---|
| **A. Capture limits and availability into the snapshot (chosen)** | The values belong to the observed step, a later config change cannot rewrite history, and the serializer stays pure. |
| B. Read live thermal config while serializing | Rejected. That breaks snapshot alignment: a threshold override after the snapshot would be reported as if it applied then. |
| C. Infer availability from non-default values | Rejected. 0.0 heating and `Normal` load shed are legitimate observations, so absence and zero would be confused. |
| D. Bump the H.1 schema version | Rejected. ProjectEden already passes unknown scalar fields through, and an additive optional field needs no version. |

## Milestones

| CP | Deliverable | Acceptance |
|---|---|---|
| A | Types, capture, context and serializer | Editor build passes on the pinned toolset |
| B | `Eden.Unit.EdenOs.RichContext.*` tests | New tests pass; full `Eden.` suite has no new failures |
| C | Human PIE with live ProjectEden | A human sees enriched advisories in Advisory mode. Not self-certified. |

## Detailed steps

1. `Systems/EdenThermalModel.h/.cpp`: `FEdenThermalLimitsSnapshot` and `FEdenThermalModel::MakeLimitsSnapshot`, which is invalid when `ValidateConfig` fails.
2. `Telemetry/EdenTelemetryTypes.h`: `FEdenTelemetrySourceAvailability`; add `Sources` and `ThermalLimits` to `FEdenTelemetrySnapshot`. Export Schema v1 is untouched because the exporter is explicit.
3. `Telemetry/EdenTelemetrySubsystem.cpp` `AssembleSnapshot`: set each flag from `IsThermalSimulationEnabled`, `IsPowerSimulationEnabled` and `IsOperatorControlEnabled`; capture limits only when thermal is enabled.
4. `EdenOs/EdenOsAdvisoryTypes.h`, `EdenOsAdvisoryModel.cpp`: copy `Sources` and `ThermalLimits` from the newest snapshot. `WindowDurationSeconds` is newest minus oldest bounded-window time, valid with at least two snapshots and a positive finite span.
5. `EdenOs/EdenOsWireSerializationModel.cpp`: `BuildRichContextLines`, which uses exact mode vocabulary (`Off`/`Nominal`/`Boost`/`Emergency`, `Normal`/`Shed`, `Full`/`Reduced`), `%.6f` numbers and finite-only output.
6. `Private/Tests/EdenOsRichAdvisoryContextTests.cpp`.

## Validation

```powershell
.\scripts\Validate-Project.ps1 -Build
.\scripts\Validate-Project.ps1 -RunTests -TestFilter "Eden.Unit.EdenOs.RichContext"
.\scripts\Validate-Project.ps1 -RunTests -TestFilter "Eden."
```

ProjectEden side: `pytest tests/` in `packages/api`, which includes enriched H.1 contexts generated from the simulator's own equations.

Human PIE (Milestone C, not done): run Advisory mode against a local ProjectEden. Confirm the request body has the rich keys during Solar Crisis. Toggle `T`/`L`/`P` and confirm the modes change in the next request. Confirm the advisory text never recommends load shedding or reduced propulsion for heat.

## Failure modes and rollback

- A rich value is serialized from an unavailable owner. Detected by `LegacyBodyIsByteIdenticalWithoutSources`.
- A non-finite value reaches ProjectEden, which would reject it with 422. Detected by `ObservedZeroIsSentAndNonFiniteIsOmitted`.
- Rollback: delete the branch. `feature/0008-newtonian-flight`, `feature/0009-visual-flight-integration` and `main` are unchanged. The serializer change is isolated to code that runs only when sources are flagged available.

## Progress log

- 2026-10-14: Preflight read (AGENTS, IMPRINT, REMEMBER, PROJECT_SPEC, ARCHITECTURE, SDLC, REVIEW, RECOVER, 0009, ADR-0002). The workspace was on `feature/0009-visual-flight-integration` @ `e0f8c96`, clean except the untracked 0009 draft. The branch was cut there with `git switch -c` so no file on disk changed. Editor not running.
- 2026-10-14: Milestone A and B code written.
- 2026-10-14: `Validate-Project.ps1 -Build -EngineRoot "K:\Program Files\Epic Games\UE_5.8"`: Win64 Development Editor build succeeded (VS 2022 14.44.35207, 12 actions, 214 s). The registry lookup for engine association `5.8` failed, so `-EngineRoot` must be passed.
- 2026-10-14: `-RunTests -TestFilter "Eden.Unit.EdenOs.RichContext"`: 8/8 passed.
- 2026-10-14: `-RunTests -TestFilter "Eden."`: 404/404 passed, exit 0. The 6 `LogEdenOs` warnings come from existing negative-path tests (synthetic transport failures, invalid JSON, convergence failure).

## Decision log

- 2026-10-14: Branch from the checked-out 0009 tip rather than checking out 0008. Switching branches would rewrite the visual work on disk. The telemetry files touched here are disjoint from the 0009 visual commits, so the commits can be cherry-picked onto 0008 or `main` later.
- 2026-10-14: Thermal rates are gated on thermal availability, and thresholds additionally on a valid config. Operator modes are the operator intent snapshot. During startup deferral (`IsIntentApplyPending`) the modes can lead the applied rates by up to one step.

## Acceptance evidence

- Milestone A: Editor build passed (progress log, 2026-10-14).
- Milestone B: 8/8 RichContext tests and 404/404 `Eden.` tests passed.
- Milestone C (human PIE): **not done**. Not self-certified.

## Handoff

Next clean task: human PIE (Milestone C) with live ProjectEden, then cherry-pick onto the branch that will carry 0007 after its Checkpoint M PIE.
