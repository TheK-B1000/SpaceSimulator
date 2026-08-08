# Telemetry and After-Action Review

## Status

**Draft — design only, not approved, not started.**

## Prerequisite status

> [!IMPORTANT]
> 0006 depends on **0005 (Operator Systems Control + Mission HUD)** being complete, not merely on 0004.
>
> The interesting telemetry — operator commands, alert transitions, response latency — does not exist until 0005 creates it. Building 0006 first would produce a recorder whose most valuable channel is empty, and would force a schema revision the moment operator actions arrive.
>
> 0006 **design** may proceed now. 0006 **implementation** must wait for 0005.

---

## 1. Problem and outcome

After 0005, the simulation produces operator decisions and their consequences. Nothing remembers them. The moment a mission ends, everything except the final `Succeeded`/`Failed` is gone.

This milestone builds the simulator's flight recorder:

```text
Flight / Fuel / Power / Thermal / Mission / Operator
                    ↓ immutable snapshots + transition events
             UEdenTelemetrySubsystem
                    ↓ bounded, ordered history
        After-Action Review  ·  JSON export  ·  future EDEN OS adapter
```

The outcome is that a completed Solar Crisis yields a reconstructable account of what happened, what the operator did, and when — as data, not as log text.

---

## 2. Locked architecture decisions (proposed)

### 2.1 Telemetry observes; it never commands

Telemetry holds no authoritative state and issues no commands. It reads immutable snapshots through existing `BlueprintPure` accessors and subscribes to existing broadcast delegates.

Forbidden without exception:

```text
Telemetry → set battery / temperature / fuel
Telemetry → fail or complete an objective
Telemetry → change mission phase or state
Telemetry → issue an operator command
```

A telemetry defect must be capable of losing data. It must never be capable of changing the mission result.

### 2.2 Telemetry samples last in the fixed step

Checkpoint 0004-B gave the clock explicit subscriber priorities. Telemetry uses them:

```text
Priority   0   Systems      (fuel, power, thermal)
Priority 100   Mission      (timeline, objectives, outcome, dispatch)
Priority 200   Telemetry    (observation only)
```

Registering strictly after mission guarantees every sample reflects **fully settled** state for that step — post-resource, post-objective, post-outcome, post-dispatch. No sample can catch a half-stepped world.

### 2.3 Simulation time is the clock; sequence number is the tiebreaker

Every record carries:

| Field | Purpose |
|---|---|
| `SimulationTimeSeconds` | Deterministic clock time. **Never wall time.** |
| `MissionElapsedTimeSeconds` | Mission-relative time for the AAR |
| `SequenceNumber` (`uint64`, monotonic) | Total ordering when several records share a timestamp |

Multiple events legitimately occur at the same fixed-step timestamp (phase change, external demand, external heating all at $T=10.0$). Timestamp alone cannot order them; the sequence number can. Wall-clock UTC is recorded **once per session header**, never per record.

### 2.4 Two record types, different capture rules

- **`FEdenTelemetryEvent`** — discrete, transition-driven. **Never decimated.** Losing an event loses a fact.
- **`FEdenTelemetrySnapshot`** — periodic sample of continuous state. **Decimated** (every Nth fixed step; proposed N=5, i.e. 2 Hz against the 0.1 s step). Losing a sample loses resolution, not a fact.

### 2.5 History is bounded and its truncation is visible

Fixed-capacity ring buffers, preallocated at initialization — no per-step heap allocation, per AGENTS.md. Separate capacities for events and snapshots.

On overflow: drop oldest, increment `DroppedEventCount` / `DroppedSnapshotCount`. The AAR must **state** when its history was truncated rather than silently presenting a partial record as complete. Silent truncation is a lie told with real data.

### 2.6 History outlives the mission that produced it

This is the decision most easily got wrong. The AAR is generated *after* a mission reaches `Succeeded`/`Failed`, so:

- `UEdenMissionSubsystem::ResetMission()` **must not** clear telemetry history.
- History is cleared **only** by explicit `ClearHistory()` or by `LoadMission` starting a *new* mission.

Telemetry deliberately outlives its mission. Wiring it to mission reset would destroy the record exactly when the review needs it.

### 2.7 Domain composition inside, versioned schema at the boundary

`FEdenTelemetrySnapshot` **composes** the existing domain snapshot structs rather than re-flattening their fields:

```cpp
struct FEdenTelemetrySnapshot
{
    float    SimulationTimeSeconds;
    float    MissionElapsedTimeSeconds;
    uint64   SequenceNumber;

    FEdenFuelStateSnapshot     Fuel;
    FEdenPowerStateSnapshot    Power;
    FEdenThermalStateSnapshot  Thermal;
    FEdenMissionStateSnapshot  Mission;
    FEdenFlightStateSnapshot   Flight;
    FEdenOperatorStateSnapshot Operator;   // 0005
};
```

One definition per domain; adding a resource field does not require editing telemetry.

The **exporter** owns the versioned wire schema and maps domain structs onto it. Schema version lives at the export boundary, not in the domain. This is what lets 0007's EDEN OS adapter consume a stable contract while the simulation keeps evolving.

### 2.8 Sink failure is contained

```cpp
class IEdenTelemetrySink
{
    virtual EEdenSinkResult ReceiveSnapshot(const FEdenTelemetrySnapshot&) = 0;
    virtual EEdenSinkResult ReceiveEvent(const FEdenTelemetryEvent&) = 0;
};
```

A sink returns a result; it never throws and never mutates simulation state. Delivery failures are counted and logged separately from domain state, per ARCHITECTURE.md §12. A failing sink must not stall the fixed step — a slow or broken sink is disabled after a bounded failure count rather than retried indefinitely inside the simulation tick.

---

## 3. State ownership additions

| State | Owner | Readers | Writers |
|---|---|---|---|
| Telemetry event history | `UEdenTelemetrySubsystem` | AAR, export, sinks | Telemetry subsystem only |
| Telemetry snapshot history | `UEdenTelemetrySubsystem` | AAR, export, sinks | Telemetry subsystem only |
| Sequence counter | `UEdenTelemetrySubsystem` | — | Telemetry subsystem only |
| Dropped-record counters | `UEdenTelemetrySubsystem` | AAR | Telemetry subsystem only |
| Sink registration list | `UEdenTelemetrySubsystem` | — | Telemetry subsystem only |
| Sink delivery failure counts | `UEdenTelemetrySubsystem` | Debug, AAR | Telemetry subsystem only |
| Derived AAR metrics | `FEdenAfterActionModel` (pure, computed) | AAR view | Computed, never stored as truth |

Every row is telemetry-owned. **No existing ownership row changes** — that is the point.

---

## 4. Event capture sources

All bind to delegates that already exist; none introduce polling.

| Source | Delegate | Events |
|---|---|---|
| Mission | `OnMissionStateChanged` | `MissionStarted`, `MissionSucceeded`, `MissionFailed`, `MissionAborted` |
| Mission | `OnMissionPhaseChanged` | `PhaseChanged` |
| Mission | `OnMissionEventTriggered` | `ScheduledEventFired` (+ command type and payload) |
| Fuel / Power / Thermal | `OnStateChanged(Prev, Current)` | `ResourceStateTransition` |
| Operator (0005) | operator intent change | `OperatorCommandIssued` |
| Alerts (0005) | alert raised / cleared | `AlertRaised`, `AlertCleared` |
| Objectives | objective runtime transition | `ObjectiveActivated/Completed/Failed` |

Objective transitions currently have no delegate — mission owns objective runtime state internally. **0006 will need a mission-side objective-transition broadcast**, or telemetry must diff successive mission snapshots. Diffing is inference, not observation; a delegate is the correct fix. Flagged as open question #2.

---

## 5. After-Action Review

`FEdenAfterActionModel` is a **pure** model computing derived metrics from recorded history — no world, fully unit-testable, matching `FEdenMissionModel`'s precedent.

Derived metrics:

| Metric | Derivation |
|---|---|
| Duration | Last minus first mission-relative timestamp |
| Peak temperature | Max over snapshot history |
| Lowest battery | Min `ChargeFraction` over snapshot history |
| Fuel remaining | Final `FuelFraction` |
| Response to impact | First `OperatorCommandIssued` after the impact event, minus impact timestamp |
| Operator action list | Filtered event history |
| Objective results | Final objective states |
| Critical events | Events at or above a severity threshold |

Note that peak/min are computed from **decimated** snapshots, so they are extrema *of the samples*, not guaranteed true extrema. Either label them as sampled, or have resource components expose running peak/min as authoritative state. Flagged as open question #3.

---

## 6. Checkpoint breakdown

Follows the proposed A–I with two adjustments, noted below.

| # | Scope | Gate |
|---|---|---|
| **A** | `FEdenTelemetryEvent`, `FEdenTelemetrySnapshot`, `EEdenTelemetryEventType`, sequence/timestamp semantics | Unit tests |
| **B** | `UEdenTelemetrySubsystem`, clock registration at priority 200, ring buffers | Unit + ordering tests |
| **C** | Snapshot assembly from flight + resources + mission + operator | Integration tests |
| **D** | Event capture: delegate binding for all sources in §4 | Integration tests |
| **E** | History buffering: bounds, drop counters, reset semantics (§2.6) | Unit tests |
| **F** | `IEdenTelemetrySink` + local logging sink + failure containment | Unit tests with a deliberately failing sink |
| **G** | JSON export with versioned wire schema; `Saved/Telemetry/` | Round-trip tests |
| **H** | `FEdenAfterActionModel` derived metrics | Unit tests on synthetic histories |
| **I** | AAR presentation surface | Manual PIE |
| **J** | Full automation + manual PIE closeout | Hands-on gate |

**Adjustment 1** — AAR is split: `H` is the pure metric model (testable without a world), `I` is presentation. Combining them would make the metrics only testable through UI.

**Adjustment 2** — a `J` closeout checkpoint is added, matching the structure of 0003 and 0004 rather than folding verification into the last feature checkpoint.

---

## 7. Test matrix

**Unit** — sequence numbers strictly increase; identical timestamps remain ordered by sequence; ring buffer drops oldest and increments counters; decimation samples every Nth step exactly; events are never decimated; AAR metrics correct over synthetic histories; AAR reports truncation when drop counters are non-zero; JSON round-trips; schema version present.

**Integration** — telemetry steps after mission at priority 200; every sample reflects settled state; all §4 delegates produce exactly one event per transition; mission reset does **not** clear history; new `LoadMission` **does** clear history; a failing sink neither stalls the step nor mutates state; a sink disabled after repeated failures is reported.

**Scenario** — record a full Solar Crisis run and assert the resulting history reconstructs the known timeline; assert an operator run and a passive run produce materially different AARs. That difference is the milestone's proof: it demonstrates the recorder captured *decisions*, not just physics.

**Manual PIE** — AAR renders after mission end, values match observed play, export file written and well-formed, no per-frame log spam, clean Output Log.

---

## 8. Proposed source layout

```text
Public/Telemetry/
    EdenTelemetryTypes.h          (event, snapshot, event-type enum)
    EdenTelemetrySubsystem.h
    EdenTelemetrySink.h           (IEdenTelemetrySink)
    EdenTelemetryLogSink.h
    EdenTelemetryJsonExporter.h
    EdenAfterActionModel.h        (pure)
    EdenAfterActionTypes.h
Private/Telemetry/                (mirrors)
Private/Tests/
    EdenTelemetryTypesTests.cpp
    EdenTelemetrySubsystemTests.cpp
    EdenTelemetrySinkTests.cpp
    EdenTelemetryExportTests.cpp
    EdenAfterActionModelTests.cpp
    EdenTelemetryScenarioTests.cpp

Content/Eden/UI/WBP_EdenAfterActionReview.uasset
Saved/Telemetry/                  (runtime output, not tracked)
```

`Public/Telemetry/` and `Private/Telemetry/` already appear in ARCHITECTURE.md §10 as planned directories.

---

## 9. Out of scope

EDEN OS adapter and transport (0007); network delivery; live streaming; replay or playback from telemetry; persistence across sessions beyond file export; docking (0008); analytics dashboards; compression; database storage; telemetry-driven gameplay of any kind.

---

## 10. Open questions for approval

1. **Snapshot decimation rate.** N=5 (2 Hz) proposed as the balance between AAR resolution and bounded memory. A 50 s mission then holds ~100 snapshots — cheap. Confirm before Checkpoint B fixes the buffer sizes.
2. **Objective transition delegate.** Objective runtime state has no broadcast today. Recommend adding one to `UEdenMissionSubsystem` rather than having telemetry diff snapshots — diffing infers transitions instead of observing them, and would miss any transition that resolves within one decimation interval. This means 0006 touches mission code.
3. **Sampled vs. true extrema.** "Peak temperature" from decimated samples can miss a real spike between samples. Either label AAR extrema as sampled, or have thermal/power own authoritative running peak/min. The second is more honest and costs two floats per component; it also changes 0004-owned code.
4. **Export trigger.** Automatic on mission end, or explicit console command? Automatic risks writing files during every PIE run; explicit risks losing the record. Recommend automatic on terminal state, with a config toggle.
5. **AAR surface.** Reuse the 0005 HUD stack (UMG) or a separate review screen? Recommend separate — the AAR is a post-mission modal view with different lifetime and input handling than the operator HUD.
