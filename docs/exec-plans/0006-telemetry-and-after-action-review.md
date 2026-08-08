# Telemetry and After-Action Review

## Status

**Approved — implementing (types, subsystem prio 200, AAR model, objective delegate, export v1 stub; presentation/PIE pending).**

## Prerequisite status

> [!IMPORTANT]
> 0006 depends on **0005 (Operator Systems Control + Mission HUD)** being complete, not merely on 0004.
>
> The interesting telemetry - operator commands, alert transitions, response latency - does not exist until 0005 creates it. Building 0006 first would produce a recorder whose most valuable channel is empty, and would force a schema revision the moment operator actions arrive.
>
> 0006 **design** is locked below. 0006 **implementation** must wait for 0005.

Locked product sequence:

```text
0004 Emergency mission shell
        |
0005 Operator systems + HUD
        |
0006 Telemetry + AAR
        |
0007 EDEN OS adapter / AI mission control
```

---

## 1. Problem and outcome

After 0005, the simulation produces operator decisions and their consequences. Nothing remembers them. The moment a mission ends, everything except the final `Succeeded`/`Failed` is gone.

This milestone builds the simulator's flight recorder:

```text
Flight / Fuel / Power / Thermal / Mission / Operator
                    -> immutable snapshots + transition events
             UEdenTelemetrySubsystem
                    -> bounded, ordered history
        After-Action Review  ·  JSON export  ·  future EDEN OS adapter
```

The outcome is that a completed Solar Crisis yields a reconstructable account of what happened, what the operator did, and when - as data, not as log text.

---

## 2. Locked architecture decisions (approved)

### 2.1 Telemetry observes; it never commands

Telemetry holds no authoritative state and issues no commands. It reads immutable snapshots through existing `BlueprintPure` accessors and subscribes to broadcast delegates.

Forbidden without exception:

```text
Telemetry -> set battery / temperature / fuel
Telemetry -> fail or complete an objective
Telemetry -> change mission phase or state
Telemetry -> issue an operator command
```

A telemetry defect must be capable of losing data. It must never be capable of changing the mission result.

### 2.2 Telemetry samples last in the fixed step

Checkpoint 0004-B gave the clock explicit subscriber priorities. Telemetry uses them:

```text
Priority   0   Systems      (fuel, power, thermal)
Priority 100   Mission      (timeline, objectives, outcome, dispatch)
Priority 200   Telemetry    (observation only)
```

Deterministic order:

```text
resources settle
-> mission observes / evaluates / dispatches
-> telemetry records final state of that simulation step
```

Every telemetry record means: *the settled state after step N*. No Schrodinger snapshots.

### 2.3 Simulation time is the clock; sequence number is the tiebreaker

Every record carries:

| Field | Purpose |
|---|---|
| `SimulationTimeSeconds` | Deterministic clock time. **Never wall time.** |
| `MissionElapsedTimeSeconds` | Mission-relative time for the AAR |
| `SequenceNumber` (`uint64`, monotonic) | Total ordering when several records share a timestamp |

Multiple events legitimately occur at the same fixed-step timestamp. Timestamp alone cannot order them; the sequence number can. Wall-clock UTC is recorded **once per session header**, never per record.

### 2.4 Two record types, different capture rules

- **`FEdenTelemetryEvent`** - discrete, transition-driven. **Lossless.** Losing an event loses a fact.
- **`FEdenTelemetrySnapshot`** - periodic sample of continuous state. **Decimated** (every Nth fixed step; proposed N=5, i.e. 2 Hz against the 0.1 s step). Losing a sample loses resolution, not a fact.

### 2.5 Observe every step for aggregates; store every Nth snapshot

Telemetry runs every fixed step at priority 200. Capture policy:

```text
every 0.1 s telemetry observation
      ├── update aggregate metrics
      ├── capture every event
      └── persist snapshot only every N steps (e.g. 0.5 s)
```

AAR extrema are therefore **simulation-step resolution**, not merely extrema of the retained snapshot ring. Presentation may say:

```text
Snapshot interval: 0.5 s
Peak recorded simulation temperature: 73.4 C
Lowest recorded battery: ...
```

Do **not** add authoritative running extrema to thermal/power ownership for 0006 v1. Aggregating inside telemetry avoids analytics-only derived state on resource owners.

### 2.6 History is bounded and truncation is encoded in the session

Fixed-capacity ring buffers, preallocated at initialization - no per-step heap allocation, per AGENTS.md. Separate capacities for events and snapshots.

Session truncation metadata (required):

```text
DroppedSnapshotCount
DroppedEventCount          // ideally always 0
HistoryTruncated
FirstAvailableSequence
LastAvailableSequence
```

On snapshot overflow: drop oldest, increment `DroppedSnapshotCount`, set `HistoryTruncated`.

On event overflow (extreme / should-not-happen): drop oldest, increment `DroppedEventCount`, set a **loud integrity flag**. Quiet event amputation is forbidden.

The AAR must **state** when history was truncated rather than silently presenting a partial record as complete.

### 2.7 Three separate history / mission operations

```text
ResetMission()
    resets mission runtime only
    does NOT clear telemetry / AAR history

ClearHistory()
    explicitly destroys telemetry / AAR history

LoadMission(new mission)
    closes / discards previous active recording according to policy
    then starts a new telemetry session
```

AAR data must not disappear because gameplay state was reset.

### 2.8 Domain composition inside, versioned schema at the boundary

Internally, compose domain snapshots:

```cpp
struct FEdenTelemetrySnapshot
{
    float    SimulationTimeSeconds;
    float    MissionElapsedTimeSeconds;
    uint64   SequenceNumber;

    FEdenFlightStateSnapshot   Flight;
    FEdenFuelStateSnapshot     Fuel;
    FEdenPowerStateSnapshot    Power;
    FEdenThermalStateSnapshot  Thermal;
    FEdenMissionStateSnapshot  Mission;
    FEdenOperatorStateSnapshot Operator;   // 0005
};
```

Externally: **Telemetry Export Schema v1**. The exporter/adapter translates the evolving internal model into a stable wire contract. That prevents EDEN OS from welding onto Unreal structs.

### 2.9 Sink failure is contained

```cpp
class IEdenTelemetrySink
{
    virtual EEdenSinkResult ReceiveSnapshot(const FEdenTelemetrySnapshot&) = 0;
    virtual EEdenSinkResult ReceiveEvent(const FEdenTelemetryEvent&) = 0;
};
```

A sink returns a result; it never throws and never mutates simulation state. Delivery failures are counted and logged separately from domain state, per ARCHITECTURE.md section 12. A failing sink must not stall the fixed step - disable after a bounded failure count rather than retry indefinitely inside the simulation tick.

### 2.10 Objective-state delegate is a Checkpoint dependency

Do **not** infer objective transitions by comparing sampled snapshots.

Mission owns objective state and must emit:

```cpp
OnObjectiveStateChanged(
    FName ObjectiveId,
    EEdenObjectiveState PreviousState,
    EEdenObjectiveState NewState
)
```

Telemetry subscribes:

```text
Mission objective changes
        |
OnObjectiveStateChanged
        |
Telemetry event
        |
AAR
```

This touches mission code and **strengthens** command/event/snapshot separation. It is an explicit 0006 checkpoint dependency, not an internal telemetry workaround.

---

## 3. State ownership additions

| State | Owner | Readers | Writers |
|---|---|---|---|
| Telemetry event history | `UEdenTelemetrySubsystem` | AAR, export, sinks | Telemetry subsystem only |
| Telemetry snapshot history | `UEdenTelemetrySubsystem` | AAR, export, sinks | Telemetry subsystem only |
| Per-step aggregate extrema | `UEdenTelemetrySubsystem` | AAR | Telemetry subsystem only |
| Sequence counter | `UEdenTelemetrySubsystem` | - | Telemetry subsystem only |
| Truncation / integrity metadata | `UEdenTelemetrySubsystem` | AAR | Telemetry subsystem only |
| Sink registration list | `UEdenTelemetrySubsystem` | - | Telemetry subsystem only |
| Sink delivery failure counts | `UEdenTelemetrySubsystem` | Debug, AAR | Telemetry subsystem only |
| Derived AAR metrics | `FEdenAfterActionModel` (pure, computed) | AAR view | Computed, never stored as truth |

Every row is telemetry-owned (except the mission-owned `OnObjectiveStateChanged` emission). **No resource ownership rows change** for extrema.

---

## 4. Event capture sources

Bind to delegates; do not poll for transitions.

| Source | Delegate | Events |
|---|---|---|
| Mission | `OnMissionStateChanged` | `MissionStarted`, `MissionSucceeded`, `MissionFailed`, `MissionAborted` |
| Mission | `OnMissionPhaseChanged` | `PhaseChanged` |
| Mission | `OnMissionEventTriggered` | `ScheduledEventFired` (+ command type and payload) |
| Mission | **`OnObjectiveStateChanged` (new, required)** | `ObjectiveActivated` / `Completed` / `Failed` / etc. |
| Fuel / Power / Thermal | `OnStateChanged(Prev, Current)` | `ResourceStateTransition` |
| Operator (0005) | operator intent change | `OperatorCommandIssued` |
| Alerts (0005) | alert raised / cleared | `AlertRaised`, `AlertCleared` |

---

## 5. After-Action Review

Split deliberately:

```text
H  FEdenAfterActionModel
   pure transformation: telemetry session -> AAR result

I  AAR presentation
   AAR result -> UI

J  automated / manual closeout
```

`FEdenAfterActionModel` is pure - no world, fully unit-testable - matching `FEdenMissionModel`.

Unit-testable questions without Unreal UI:

```text
What was response latency?
What was maximum temperature?
How long was the craft critical?
Which operator action came first?
Which objectives failed?
Was telemetry history truncated?
```

Derived metrics (illustrative):

| Metric | Derivation |
|---|---|
| Duration | Last minus first mission-relative timestamp |
| Peak recorded simulation temperature | Max from **per-step aggregates** |
| Lowest recorded battery | Min `ChargeFraction` from **per-step aggregates** |
| Fuel remaining | Final `FuelFraction` |
| Response to impact | First `OperatorCommandIssued` after impact, minus impact timestamp |
| Operator action list | Filtered event history |
| Objective results | Final objective states + transition events |
| Critical events | Events at or above a severity threshold |
| Truncation / integrity | Session metadata |

---

## 6. Checkpoint breakdown

| # | Scope | Gate |
|---|---|---|
| **A** | `FEdenTelemetryEvent`, `FEdenTelemetrySnapshot`, `EEdenTelemetryEventType`, sequence/timestamp semantics, session truncation metadata | Unit tests |
| **B** | `UEdenTelemetrySubsystem`, clock registration at priority 200, ring buffers, per-step aggregates + decimated snapshot store | Unit + ordering tests |
| **C** | Snapshot assembly from flight + resources + mission + operator | Integration tests |
| **D0** | Mission `OnObjectiveStateChanged` delegate (ownership-correct emission) | Unit + mission tests |
| **D** | Event capture: delegate binding for all sources in section 4 | Integration tests |
| **E** | History buffering: bounds, drop counters, `ResetMission` / `ClearHistory` / `LoadMission` semantics (section 2.7) | Unit tests |
| **F** | `IEdenTelemetrySink` + local logging sink + failure containment | Unit tests with a deliberately failing sink |
| **G** | JSON export with versioned wire schema (`Telemetry Export Schema v1`); `Saved/Telemetry/` | Round-trip tests |
| **H** | `FEdenAfterActionModel` pure metrics | Unit tests on synthetic histories |
| **I** | AAR presentation surface | Manual PIE |
| **J** | Full automation + manual PIE closeout | Hands-on gate |

---

## 7. Test matrix

**Unit** - sequence numbers strictly increase; identical timestamps remain ordered by sequence; ring buffer drops oldest and increments counters; aggregates update every step while snapshots store every Nth; events are never decimated; event drop raises integrity flag; AAR metrics correct over synthetic histories; AAR reports truncation when drop counters are non-zero; JSON round-trips; schema version present.

**Integration** - telemetry steps after mission at priority 200; every sample reflects settled state; all section 4 delegates produce exactly one event per transition; `ResetMission` does **not** clear history; `ClearHistory` does; `LoadMission` closes prior recording per policy and starts a new session; a failing sink neither stalls the step nor mutates state.

**Scenario** - record a full Solar Crisis run and assert the resulting history reconstructs the known timeline; assert an operator run and a passive run produce materially different AARs.

**Manual PIE** - AAR renders after mission end, values match observed play, export file written and well-formed, no per-frame log spam, clean Output Log.

---

## 8. Proposed source layout

```text
Public/Telemetry/
    EdenTelemetryTypes.h          (event, snapshot, event-type enum, session metadata)
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

`Public/Telemetry/` and `Private/Telemetry/` already appear in ARCHITECTURE.md section 10 as planned directories.

Mission change required by Checkpoint D0:

```text
Public/Missions/EdenMissionSubsystem.h   (+ OnObjectiveStateChanged)
Private/Missions/...                     (emit on objective transitions)
```

---

## 9. Out of scope

EDEN OS adapter and transport (0007); network delivery; live streaming; replay or playback from telemetry; persistence across sessions beyond file export; docking (0008); analytics dashboards; compression; database storage; telemetry-driven gameplay of any kind; authoritative resource-owned running extrema for analytics.

---

## 10. Open questions (remaining)

Resolved by approval:

1. ~~Objective transition delegate~~ -> **Locked:** add `OnObjectiveStateChanged` (Checkpoint D0). Do not diff snapshots.
2. ~~Sampled vs true extrema~~ -> **Locked:** update aggregates every fixed step inside telemetry; store snapshots every N; do not move extrema ownership onto resource components.
3. ~~History vs mission reset~~ -> **Locked:** `ResetMission` / `ClearHistory` / `LoadMission` are three separate operations (section 2.7).
4. ~~AAR H/I split~~ -> **Locked:** H model / I presentation / J closeout.
5. ~~0006 depends on 0005~~ -> **Locked.**

Still open before implementation:

1. **Snapshot decimation rate.** N=5 (2 Hz) proposed. Confirm before Checkpoint B fixes buffer sizes.
2. **Export trigger.** Automatic on terminal mission state with a config toggle, or explicit console command?
3. **AAR surface.** Separate post-mission modal (recommended) vs reuse of the 0005 HUD stack.
