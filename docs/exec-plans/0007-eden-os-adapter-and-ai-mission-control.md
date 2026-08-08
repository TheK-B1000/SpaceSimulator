# EDEN OS Adapter and AI Mission Control

## Status

**In Progress - Checkpoint I accepted; Checkpoint J authorized next; Checkpoints K-M remain locked.**

Unreal lane execution remains locked to one checkpoint at a time. Unreal A/B/C/E/F/G/H/I are accepted (H timing amendment included). ProjectEden H.1 advisory API exists at `da38ecb`. Checkpoint I (`89d47da` + return-path proof `89761fd`) surfaces ProjectEden advisories as exactly one `EdenAdvisoryIssued` telemetry fact plus read-only HUD presentation. Do not begin Checkpoint K (or L/M) until J is explicitly accepted. Do not begin J implementation until the maintainer sends the Checkpoint J brief.

Checkpoint A remediation was accepted at `7a42fcf`. Checkpoint B was accepted and committed at `a63de4e`. Checkpoint C was accepted and committed at `f66cda3`; its corrective wire-contract alignment was accepted at `3f69f9b`. Checkpoint E implementation was committed at `32c8f9a`; the follow-up mission-level failure-isolation proof was accepted at `75fcd90`. Checkpoint F implementation commit `63768ab` plus corrective proof commit `5249a6a` are accepted.

## Prerequisite status

> [!CAUTION]
> **The ports-and-adapters seam this milestone depends on does not exist yet.**
>
> ExecPlan 0006 shipped its minimal scope: `UEdenTelemetrySubsystem`, `FEdenTelemetryExportModel`, `FEdenAfterActionModel`, and the AAR widget. There is **no `IEdenTelemetrySink`** anywhere in `Source/` — telemetry holds history in memory and writes JSON locally, with no delivery abstraction.
>
> Creating that seam is **0007's Checkpoint A**, not an inherited prerequisite.
>
> 0004 (`v0.3.0-emergency-mission`) and 0005 are Complete. 0006 is merged at `96cac73`.

---

## 1. Reserved vocabulary

Fixed for this plan and all downstream work. Both repositories previously called their own component "the simulator."

| Term | Means |
|---|---|
| **eden-sim** | ProjectEden's internal simulation engine (`packages/simulator`) |
| **mission environment** | Unreal / `EdenSpaceSimulator` |
| **mission session** | One externally-originated operational run |
| **telemetry ingestion** | Data flowing mission environment → ProjectEden |

"The simulator" unqualified means `eden-sim`.

---

## 2. Problem and outcome

Two finished systems share a name and nothing else. The mission environment produces a full operational record of a Solar Crisis; ProjectEden already models simulation runs, telemetry, alerts, and incidents. Neither knows the other exists.

```text
Mission environment (authoritative)
    → Telemetry Export Schema v1
    → IEdenTelemetrySink
    → EdenOsTelemetrySink
    ─── network boundary ───
    → FastAPI eden-api  → mission context → reasoning
    → advisory DTO
    ─── network boundary ───
    → validated DTO → telemetry event + HUD
```

EDEN advises and never secretly controls. EDEN OS being absent or broken changes nothing about whether the mission environment works.

---

## 3. Discovery — the Python side

Inspected at `B:\repo\ProjectEden`.

### Architecture

`packages/simulator` (`eden-sim`, CPU-bound, zero network/DB) and `packages/api` (`eden-api`, async FastAPI, depends on `eden-sim`). Layering is strict and already correct:

```text
Route (thin) → Service (business logic) → Repository/Provider Protocol → SQLAlchemy / Local / S3 / Email
```

`eden_api/dependencies.py` is the DI composition root. All `/api/*` routes register as sub-routers in `eden_api/routes/api.py`.

### Existing routes

```text
/health · /api/status · /api/auth/... · /api/simulator/...
/api/runs/... · /api/incidents/... · /api/runs/{id}/...
```

`/api/simulator/` today means *"submit and run an eden-sim simulation"* — the API executes the engine via `threaded_simulation_runner`. Telemetry ingestion is the opposite direction of control and must not share that namespace.

### Existing domain model

`SimulationRun`, `TelemetryState`, `Agent`, `AgentTelemetry`, `Sensor`, `SensorReading`, `Alert`, `Incident`, `Scenario`, `RunStatus`, `System`, `SensorType`, `Unit`, `AlertType`, `AlertStatus`, `Severity`, `IncidentType`, `IncidentStatus`.

ProjectEden already has a telemetry, alert, and incident domain. 0007 maps onto it rather than building a parallel one.

---

## 4. `SimulationRun` invariant audit

Per the locked rule (§5.2), `SimulationRun` was audited directly rather than deferred.

| Column | Constraint | Semantically true for a mission session? |
|---|---|---|
| `run_uid` | `String(36)`, unique | ✅ Mission environment supplies its own session UUID |
| `scenario_id` | FK, **NOT NULL** | ✅ "SolarEventEmergency" is a legitimate scenario row |
| `run_status_id` | FK, **NOT NULL** | ✅ Running/Succeeded/Failed/Aborted map cleanly |
| `seed` | **NOT NULL** | ❌ eden-sim determinism concept; meaningless for the mission environment |
| `ticks` | **NOT NULL** | ⚠️ Fixed-step count is analogous but unknown until completion |
| `started_at` | **NOT NULL** | ✅ |
| `ended_at` | **NOT NULL** | ❌ **Structural blocker** |
| `alerts_count` | **NOT NULL** | ❌ Aggregate unknown until completion |
| `highest_risk_system` | nullable | ✅ |
| `owner_id` | nullable FK | ✅ |

**Finding:** `SimulationRun` models a **completed batch run**, not a **live streaming session**. `ended_at` and `alerts_count` are `NOT NULL` at insert time, so a session created at `StartMission` and completed 50 seconds later cannot be represented without fabricating an end time and an alert count. `seed` is a pure eden-sim concept.

**Verdict against the locked rule:** the mismatch is confined to *batch-completion assumptions*, not to the telemetry domain itself. Three `NOT NULL` constraints and one irrelevant column — not "half the model."

The governing distinction is **not** "internal vs external telemetry." It is **batch-complete vs live session lifecycle**. The telemetry child models are already origin-agnostic; only the run header encodes eden-sim's batch assumptions.

### LOCKED — preferred ProjectEden persistence model

```text
SimulationRun gains:
- origin                = internal | mission_environment
- external_session_id   nullable / unique where applicable

Relax until completion:
- ended_at
- alerts_count
- ticks

Handle seed explicitly:
- nullable for mission_environment
- required by validation/domain policy for internal eden-sim runs
```

`seed` must **not** be given a placeholder such as `0`. That converts "not applicable" into fake data, and the telemetry domain's whole value is that its records are true.

ProjectEden enforces the original eden-sim invariants at its own boundary rather than in the schema:

```text
origin == internal
→ seed required
→ eden-sim batch lifecycle rules apply

origin == mission_environment
→ external_session_id required
→ live session may remain incomplete
→ completion populates ended_at / alerts_count / ticks
```

This preserves one telemetry domain without pretending the two producers share a lifecycle.

`TelemetryState`, `Alert`, `Incident`, and `SensorReading` are reused **unchanged** — they already hang off `simulation_run_id` and their shapes are origin-agnostic.

**Fallback, if ProjectEden rejects relaxing those constraints:** a separate `MissionSession` aggregate, which requires a nullable polymorphic parent on `TelemetryState` — strictly more invasive than relaxing four columns.

**The mission environment does not care which is chosen.** The wire contract exposes only `sessionId`; the persistence decision never crosses the boundary.

---

## 5. Locked decisions

### 5.1 Sink seam precedes all network code

```text
UEdenTelemetrySubsystem
        ↓
IEdenTelemetrySink
   ├── FEdenLocalJsonTelemetrySink   (existing 0006 export, refactored)
   └── FEdenOsTelemetrySink          (new, network)
```

The existing JSON exporter becomes the first sink and must keep passing its 0006 tests unchanged. No network code receives privileged access to telemetry internals.

### 5.2 Persistence reuse rule

> **Reuse the existing telemetry domain. Reuse `SimulationRun` itself only if its invariants remain semantically true for an externally authoritative mission environment.**

### 5.3 Routes and versioning

```text
POST /api/missions/sessions
POST /api/missions/sessions/{id}/telemetry
POST /api/missions/sessions/{id}/events
POST /api/missions/sessions/{id}/complete
POST /api/missions/sessions/{id}/advisory
```

No `/api/v1/` — ProjectEden does not version routes that way. Compatibility rides on the payload:

```json
{ "schemaVersion": 1 }
```

### 5.4 Authentication (development scope only)

| Locked | |
|---|---|
| Development / integration | Short-lived Bearer JWT supplied at runtime |
| Storage | Environment / configuration, outside source control |
| Forbidden | Hardcoded token · committed secret · cookie auth · invented API-key system |

For the end-to-end demo: authenticate through ProjectEden normally, obtain a JWT, inject it into the mission environment's EDEN connection configuration for that session.

> **Recorded limitation:** Production service-to-service authentication is unresolved and is **not** satisfied by embedding a long-lived user JWT in the packaged simulator. ProjectEden may add a machine/client credential mechanism later. 0007 does not attempt to solve machine identity.

### 5.5 Direction of authority is absolute

```text
ALLOWED     mission environment ──telemetry──> FastAPI
ALLOWED     FastAPI ──advisory DTO──> mission environment ──> HUD
FORBIDDEN   FastAPI ──> resource state
FORBIDDEN   FastAPI ──> mission state or outcome
FORBIDDEN   LLM output ──> any command without validation
```

### 5.6 Networking never touches the fixed step

```text
Simulation step → enqueue (bounded, non-blocking) → return
Async worker    → drain → HTTP → record result
```

No HTTP, DNS, or unbounded serialization inside `AdvanceSimulation`. Queue overflow drops with a counter, mirroring 0006's truncation-visibility rule. Checkpoint E uses one transport attempt per queued message; retry/backoff is deferred until a later checkpoint explicitly designs it.

### 5.7 EDEN converges on the human command path

```text
Human:  Input      → UEdenOperatorControlComponent
EDEN:   Network → validation → UEdenOperatorControlComponent
```

No private back door. Both paths terminate at the authoritative command API built in 0005.

### 5.8 Authority modes

```text
Observe            telemetry out only
Advisory           telemetry out, advisory in        ← default ceiling for 0007
AuthorizedControl  validated commands in             ← built and tested, disabled by default
```

### 5.9 Advisories are ordinary telemetry events

An advisory is recorded as an immutable telemetry event, exactly like any resource transition:

```text
EdenAdvisoryIssued
├── AdvisoryId
├── EvaluationId
├── Recommendation
├── Rationale
├── EvaluationSimulationTimeSeconds
├── ContextSnapshotSimulationTimeSeconds
└── TriggerReasons
```

**AMENDED 2026-08-08 to match the accepted H.1 contract.** `Severity`, `RecommendationCode`, `Confidence`, `RelatedObjectiveIds`, and `RelatedAlertIds` are **removed** from the 0007 event. §19.3 response v1 carries only `advisoryId`, `evaluationId`, `recommendation`, and `rationale`, so those fields had no source — recording them would have meant manufacturing facts. They may return in a later schemaVersion when a real product requirement and a real source exist.

#### Three distinct timestamps

The ordinary `FEdenTelemetryEvent::SimulationTimeSeconds` carries the **issuance** time, so no redundant field is needed:

| Fact | Meaning |
|---|---|
| `ContextSnapshotSimulationTimeSeconds` | when the state sent to ProjectEden was observed |
| `EvaluationSimulationTimeSeconds` | when Unreal decided an advisory was due and built the request |
| `FEdenTelemetryEvent::SimulationTimeSeconds` | when the validated response was accepted and the advisory issued |

```text
12.0  context snapshot observed
12.7  advisory evaluation due, request created
13.4  valid response accepted, EdenAdvisoryIssued emitted
```

None may be substituted for another.

**AAR latency semantics this enables (0006 consumes these later):**

```text
EDEN reasoning + transport latency = issued time - evaluation time
operator response time             = operator action time - issued time
```

Measuring operator response from *evaluation* time would charge the operator for time ProjectEden spent reasoning and on the wire.

These are **simulation-time** metrics. A paused simulation does not advance them, which is correct for mission and AAR semantics. Real wall-clock network latency, if ever wanted, belongs in separate transport telemetry and must not be smuggled into simulation time.

`TriggerReasons` records **why EDEN was asked to evaluate**, not only what it said. That provenance is what later makes it possible to study whether event-triggered or heartbeat-triggered advisories were more useful:

```text
00:10.0  Advisory evaluation   Trigger: PhaseChanged + AlertEntered
00:10.4  EDEN: Increase cooling capacity.
00:14.8  Advisory evaluation   Trigger: Heartbeat
00:14.9  EDEN: Thermal trend remains unfavorable.
```

Dependency direction stays correct — **0006 does not depend on EDEN OS**:

```text
0007 adapter → emits ordinary telemetry event → 0006 history → FEdenAfterActionModel
```

**0007 records facts only.** It does not define "followed" or "ignored." Whether a subsequent operator action semantically matches an advisory is later work for `FEdenAfterActionModel`, which can then derive:

```text
Advisories issued  ·  followed  ·  ignored  ·  median response time
```

### 5.10 Advisory cadence — LOCKED

EDEN advisory evaluation uses a **hybrid trigger model**. Pure event-driven has a blind spot: a temperature climbing 60 → 62 → 64 → 66 → 68 °C that crosses no threshold and triggers no phase change gives EDEN no reasoning opportunity despite a clearly deteriorating trend. Pure periodic is wasteful and semantically weaker.

```text
Immediate evaluation triggers
├── mission phase transition
├── alert transition or severity change
├── objective state transition
└── meaningful operator action

Periodic evaluation
└── every 5.0 seconds of SIMULATION time while a mission is Running
```

**Semantics:**

- Advisory evaluation occurs only from **settled** telemetry state (priority 200, post-mission).
- Multiple triggers within the same simulation step are **coalesced into one evaluation**, carrying all reasons.
- Trigger reasons are preserved in the advisory request and in the `EdenAdvisoryIssued` event.
- Cadence is **configuration**, not a constant: `UEdenOsConnectionSettings::AdvisoryHeartbeatSimulationSeconds = 5.0`.
- No render-frame-driven advisory requests.
- Heartbeat uses **simulation time, not wall-clock time** — the mission environment already owns deterministic simulation time.
- Advisory evaluation never blocks or alters authoritative simulation progression.

Coalescing matters concretely. Without it, $T{=}10.0$ produces three near-identical requests:

```text
T=10.0  phase → Impact
T=10.0  alert → Warning          →  ONE evaluation
T=10.0  objective → Active           reasons: [phase_changed,
                                               alert_entered,
                                               objective_changed]
```

One request with richer context, instead of three thin ones.

At 5.0 s against a 50-second Solar Crisis this yields roughly ten heartbeat opportunities plus event-triggered requests — enough to catch gradual trends without turning inference into telemetry spam.

**No premature deduplication.** A heartbeat fires and sends the settled context. State-hashing or revision-based suppression of redundant heartbeats is a later optimization, justified by profiling only. Clever deduplication before the basic connection works is how connections stop working.

### 5.11 Advisory context is derived from 0006 history, not shadowed

Each evaluation needs enough trend information for EDEN to reason, not just instantaneous state:

```text
current settled snapshot
+ previous sampled temperature, delta / trend
+ previous battery fraction, delta / trend
+ recent alert transitions
+ recent operator action
```

That context is **assembled from 0006 telemetry history**:

```text
0006 history → 0007 advisory context builder → FastAPI
```

**Not:**

```text
0007 maintains its own shadow temperature history
```

One owner. The adapter reads history; it never accumulates a second copy of state that 0006 already owns.

---

## 6. Milestone invariant — the architectural kill switch

Given identical seed, operator inputs, and scenario:

```text
EDEN disabled  ==  EDEN Observe  ==  EDEN Advisory
```

The authoritative simulation result **must be identical** across all three. Compared fields:

```text
mission outcome · objective states · fuel · battery · temperature
flight state · operator state · simulation timing
```

Telemetry and advisory bookkeeping are excluded from comparison. If Observe or Advisory changes any compared value, **the adapter has crossed the ownership boundary** and the milestone fails regardless of what else works.

This supersedes "HTTP returned 200" as the meaningful test.

---

## 7. State ownership additions

| State | Owner | Readers | Writers |
|---|---|---|---|
| Adapter connection state | `UEdenOsAdapterSubsystem` | HUD, debug | Adapter only |
| Outbound queue + drop counters | `UEdenOsAdapterSubsystem` | Debug, AAR | Adapter only |
| Delivery failure metrics | `UEdenOsAdapterSubsystem` | HUD, debug | Adapter only |
| Latest advisory | `UEdenOsAdapterSubsystem` | HUD | Adapter only (from validated DTO) |
| Authority mode | `UEdenOsConnectionSettings` | Adapter, router | Configuration only |
| Command allowlist / rate state | `UEdenExternalCommandRouter` | Debug | Router only |

**No existing ownership row changes.** Advisories are display data, never simulation truth.

---

## 8. Checkpoint breakdown

| # | Scope |
|---|---|
| **A** | Telemetry sink seam — `IEdenTelemetrySink`; existing JSON export becomes first sink |
| **B** | EDEN connection / config / auth contract — `UEdenOsConnectionSettings` |
| **C** | Export Schema v1 network DTO + serialization |
| **D** | ProjectEden mission-ingestion API — persistence-model decision included here |
| **E** | Async FastAPI sink — bounded queue / timeout mapping / disconnect |
| **F** | Session lifecycle — create → telemetry/events → complete |
| **G** | Observe mode end-to-end |
| **H** | Advisory request/response contract |
| **I** | Advisory telemetry events + HUD presentation |
| **J** | Authorized-command boundary — built and tested, disabled by default |
| **K** | Authority modes — Observe / Advisory / AuthorizedControl |
| **L** | Cross-project automated tests |
| **M** | PIE + FastAPI end-to-end closeout |

---

## 9. Three headline proofs

```text
1.  ProjectEden unavailable        → mission environment works normally
2.  Observe / Advisory enabled     → authoritative simulation is identical
3.  EDEN advisory → operator response → both appear in telemetry and AAR
```

Proof 1 covers the availability boundary, proof 2 the ownership boundary, proof 3 the value of the integration. All three must hold.

---

## 10. Test matrix

**Mission environment** — serialization round-trip; schema version negotiation; unsupported-version rejection; queue ordering under load; overflow drops and counts; request timeout; retry with backoff; server offline at start; server disappearing mid-mission; reconnect; malformed response; advisory with missing/extra fields; advisory with out-of-range confidence; command rejected in Observe/Advisory; command rejected when not allowlisted; command rejected when rate-limited; **the §6 invariant across all three modes**.

**ProjectEden** — valid telemetry accepted; unknown `schemaVersion` rejected; malformed payload rejected; duplicate sequence idempotent; out-of-order sequence handled; session creation; session completion; unauthorized session access returns **404 not 403** (matching the existing enumeration-prevention convention); advisory response conforms to schema. Standard `pytest` / `httpx` / `TestClient`.

**End-to-end** — full Solar Crisis against live FastAPI: session created at `StartMission`, warning at $T{=}5$, impact at $T{=}10$, advisory returned as temperature climbs, HUD displays it, operator responds, response event reaches FastAPI, terminal result delivered, AAR still available locally with both advisory and operator events present.

---

## 11. Proposed source layout

```text
Mission environment
Public/Telemetry/EdenTelemetrySink.h           (IEdenTelemetrySink — Checkpoint A)
Public/EdenOs/
    EdenOsConnectionSettings.h
    EdenOsAdapterSubsystem.h
    EdenOsTypes.h                               (connection snapshot, advisory DTO)
    EdenOsTelemetrySink.h
    EdenExternalCommandRouter.h
    EdenExternalCommandTypes.h
Private/EdenOs/                                 (mirrors)
Private/Tests/EdenOs*Tests.cpp

ProjectEden (packages/api)
eden_api/routes/missions.py                     registered in routes/api.py
eden_api/services/mission_session_service.py
eden_api/services/advisory_service.py
eden_api/schemas/mission_telemetry.py
eden_api/interfaces/repositories.py             (+ mission session Protocol)
eden_api/infrastructure/database/repositories/sqlalchemy_mission_session_repository.py
alembic/versions/xxxx_mission_session_origin.py
tests/routes/test_mission_routes.py
```

`EdenOs/` keeps adapter concerns out of `Telemetry/`, which stays transport-agnostic.

---

## 12. Out of scope

Python becoming simulation authority; FastAPI editing resource state; synchronous waits on AI; unvalidated LLM command execution; hardcoded hosts; committed credentials; per-render-frame telemetry; FastAPI schemas coupled to `UObject` layouts; EDEN OS being required for the game to function; AI logic inside Unreal widgets; WebSocket transport (HTTP first); production machine identity; multiplayer; autonomous control enabled by default; defining advisory "followed"/"ignored" semantics.

---

## 13. Open questions

**None blocking.** All design decisions are locked.

| Former question | Resolution |
|---|---|
| Persistence model | **Locked** — extension-first with `origin` discriminator, relaxed completion columns, explicit `seed` policy (§4) |
| JWT authentication | **Locked** — development scope only, with recorded production limitation (§5.4) |
| Advisory cadence | **Locked** — hybrid event + 5.0 s simulation-time heartbeat, coalesced (§5.10) |
| Advisories in AAR | **Locked** — recorded as ordinary telemetry events; 0007 records facts only (§5.9) |

The one item still owned elsewhere is ProjectEden's ratification of the §4 persistence model. It does not block mission-environment work, because the wire contract exposes only `sessionId`.

---

## 14. Execution parallelism

The contract is now specified well enough that both sides implement toward it independently rather than editing shared files.

```text
MISSION ENVIRONMENT              PROJECTEDEN
A  sink seam                     D  persistence + API ingestion
B  connection / auth config
C  schema + serialization
E  async transport foundation

              ↓ converge ↓

F  session lifecycle
G  Observe mode end-to-end
H  advisory contract
I  advisory events + HUD
J  command boundary (disabled)
K  authority modes
L  cross-project tests
M  PIE + FastAPI closeout
```

Both sides build against Export Schema v1 and the §5.3 routes. Neither needs the other running until Checkpoint F.

---

## 15. Unreal lane execution cadence

Locked sequence for this repository:

```text
Checkpoint A
-> user acceptance
Checkpoint B
-> user acceptance
Checkpoint C
-> user acceptance
Checkpoint E
-> user acceptance
Unreal lane complete
```

Rules:

- Implement one checkpoint at a time.
- Build, test, document, commit, and stop after each checkpoint.
- Do not treat checkpoint approval as completion of the Unreal lane.
- Do not implement ProjectEden Checkpoint D in this repository.
- Do not begin Checkpoint F until Unreal A/B/C/E are accepted and ProjectEden D is complete.

## 16. Progress log

2026-08-08: Checkpoint A implemented for review. Added `IEdenTelemetrySink`, `FEdenTelemetrySessionPayload`, `FEdenTelemetrySinkResult`, and `FEdenLocalJsonTelemetrySink`. Refactored `UEdenTelemetrySubsystem::WriteSessionJsonV1ToDisk()` to deliver through the local JSON sink while preserving `ExportTelemetry` behavior and the existing Telemetry Export Schema v1 builder.

2026-08-08: Checkpoint A tests added. `Eden.Unit.Telemetry.Sink.PayloadBuildsSameSchemaAsDirectExport` verifies the immutable payload overload produces the same JSON as the direct export API. `Eden.Unit.Telemetry.Export.FileSmokeWritesSavedTelemetry` now writes through `FEdenLocalJsonTelemetrySink`.

2026-08-08: Checkpoint A validation passed. Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`. `EdenSpaceSimulatorEditor` Win64 Development build passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -EngineRoot "K:\Program Files\Epic Games\UE_5.8"`. Focused telemetry automation passed via `UnrealEditor-Cmd.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.Telemetry; Quit" "-TestExit=Automation Test Queue Empty"` with 5 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****` in `Saved/Logs/Automation-0007A-Telemetry.log`. Full automation passed via `Automation RunTests Eden` with 196 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****` in `Saved/Logs/Automation-0007A-Eden.log`. `git diff --check` passed. `Source` search found no `LogTemp`.

2026-08-08: Checkpoint A scope guard confirmed. No EDEN OS settings, auth, network DTOs, async HTTP, session lifecycle, advisory handling, external command routing, Unreal asset edits, or ProjectEden changes were implemented.

2026-08-08: **Independent audit of `8209fbc` against baseline `96cac73` — Checkpoint A NOT ACCEPTED.**

Verified independently: build Succeeded (0 errors, 0 warnings); `Automation RunTests Eden.` → 189 found / 189 passed / 0 failed / 0 crashes / exit 0. The `196` recorded above used the bare `Eden` filter, which sweeps in 7 engine tests (`System.Mass.*`, `System.Plugins.TmvMedia.*`); 196 − 7 = 189 genuine `Eden.*` tests. Baseline was 188, so Checkpoint A added exactly **one** test.

Passed: pure-refactor boundary (zero HTTP/auth/advisory/connection code in `Telemetry/`); sink returns a result rather than `void`; local JSON production behavior preserved (same `Saved/Telemetry` directory, same `telemetry_<safe>.json` convention, same `ForceUTF8WithoutBOM` encoding, same path-or-empty return semantics).

**Failed — two locked gates:**

1. **Multi-sink registration absent.** `UEdenTelemetrySubsystem` exposes only `DeliverSessionToSink(IEdenTelemetrySink&)`, taking one sink per call. No sink storage of any kind — no list, no ownership model, no ordering guarantee, no duplicate policy, no unregistration. The seam cannot accommodate the second sink that justified its existence.
2. **Failure isolation absent.** With no fan-out loop, "SinkA fails, SinkB still receives" is unimplementable and untested.

**Also found:** the pre-existing smoke test stopped asserting the `telemetry_smoke-session.json` filename convention (weakened assertion); and the 5-arg `BuildSessionJsonV1` overload gained an `unknown-session` fallback it did not have at baseline (unrelated semantic drift in a checkpoint specified as a pure refactor).

Remediation authorized, scoped to Checkpoint A. Expected count after remediation: **≥194** `Eden.*` tests.

2026-08-08: Checkpoint A remediation implemented directly on `main`. Added plural non-owning `IEdenTelemetrySink` registration on `UEdenTelemetrySubsystem`, `RegisterTelemetrySink`, `UnregisterTelemetrySink`, deterministic registration-order fan-out, duplicate pointer/name rejection, stable unregistration, `FEdenScopedTelemetrySinkRegistration`, aggregate delivery records/counts, and failure-isolated continuation. `DeliverSessionToRegisteredSinks()` builds one immutable payload and delivers that same payload to all registered sinks. Registered sinks are explicitly non-owned and cleared on subsystem deinitialization.

2026-08-08: Checkpoint A remediation restored regression coverage. Reinstated the `telemetry_smoke-session.json` filename assertion, restored the direct 5-argument `BuildSessionJsonV1` empty-session behavior, kept subsystem-originated exports on `unknown-session` when needed, and retained local sink safe filenames for empty session IDs.

2026-08-08: Checkpoint A remediation tests added. Named sink tests now exist for registration/duplicate identity, registration-order fan-out, unregister/scoped registration behavior, partial failure continuation and aggregation, and one immutable payload delivered to all sinks. Added `Eden.Unit.Telemetry.Export.EmptySessionRegression`.

2026-08-08: Checkpoint A remediation validation passed. `git switch main`, `git pull`, and `git status` confirmed `main` was up to date and clean before remediation. Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`. `EdenSpaceSimulatorEditor` Win64 Development build passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -EngineRoot "K:\Program Files\Epic Games\UE_5.8"`. Focused telemetry automation passed via `UnrealEditor-Cmd.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.Telemetry.; Quit" "-TestExit=Automation Test Queue Empty"` with 11 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****` in `Saved/Logs/Automation-0007A-Remediation-Telemetry.log`. Full project automation passed via `Automation RunTests Eden.` with 195 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****` in `Saved/Logs/Automation-0007A-Remediation-Eden.log`. `git diff --check` passed. Source `LogTemp` search returned no matches. Network-scope search found no new HTTP/JWT/Bearer/Authorization/Advisory/EdenOs/socket/network implementation; matches were limited to pre-existing mission "no retry" log text and the existing telemetry export comment.

2026-08-08: Checkpoint A remediation accepted and pushed to `main` as `7a42fcf`. Checkpoint B began directly on `main` after explicit user approval.

2026-08-08: Checkpoint B implemented for review. Added `UEdenOsConnectionSettings`, `FEdenOsConnectionConfig`, `FEdenOsConnectionConfigModel`, `FEdenOsConnectionSnapshot`, `FEdenOsValidationResult`, `EEdenOsConnectionState`, `EEdenOsAuthorityMode`, `UEdenOsAdapterSubsystem`, and `LogEdenOs`. `UEdenOsConnectionSettings` owns serialized configuration only; `UEdenOsAdapterSubsystem` is the single runtime owner of connection state and runtime-injected bearer JWT presence. No HTTP, FastAPI calls, async queue, retry/backoff, advisory request, command routing, ProjectEden changes, Unreal assets, or telemetry history duplication were implemented.

2026-08-08: Checkpoint B validation behavior added. `Eden.Unit.EdenOs.*` tests cover disabled config without URL/token, enabled empty URL rejection, malformed/unsupported URL rejection, invalid timeouts, invalid queue depth, invalid advisory heartbeat interval, absent JWT warning behavior, safe authority-mode default, deterministic connection snapshot defaults, redacted secret/log summaries, and subsystem-owned runtime snapshot/token-presence updates.

2026-08-08: Checkpoint B build-discovery issue diagnosed and resolved. `UnrealEditor.exe` automation initially found 0 `Eden.Unit.EdenOs.` tests because UBT had not regenerated the module source actions after the new source files were added. Running `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild` invalidated the makefile for `source file added` and compiled `EdenOsAdapterSubsystem.cpp`, `EdenOsConnectionSettings.cpp`, and `EdenOsConnectionConfigTests.cpp`. The final build passed with explicit `Compile [x64] EdenOsConnectionConfigTests.cpp`, relinked `UnrealEditor-EdenSpaceSimulator.dll`, and returned `Result: Succeeded`. Intermediate response/link manifests now include `EdenOsAdapterSubsystem.cpp.obj`, `EdenOsConnectionSettings.cpp.obj`, and `EdenOsConnectionConfigTests.cpp.obj`.

2026-08-08: Checkpoint B validation passed. Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`. Focused automation passed via `UnrealEditor.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.EdenOs.; Quit" "-TestExit=Automation Test Queue Empty" -Log`; `Saved/Logs/EdenSpaceSimulator.log` reported 11 tests found, all 11 `Eden.Unit.EdenOs.*` tests completed with `Result={Success}`, and `**** TEST COMPLETE. EXIT CODE: 0 ****`. Full automation passed via `Automation RunTests Eden.`; `Saved/Logs/EdenSpaceSimulator.log` reported 206 tests found, all new EdenOs tests included, and `**** TEST COMPLETE. EXIT CODE: 0 ****`. `git diff --check` passed. Source `LogTemp` search returned no matches. Source HTTP/network-scope search found no `FHttpModule`, `IHttpRequest`, HTTP module include, FastAPI call, WebSocket, socket, retry, or backoff implementation; matches were limited to pre-existing mission "no retry" log text. Secret scan found no realistic committed JWT; matches were limited to synthetic `test-token` literals in `EdenOsConnectionConfigTests.cpp`.

2026-08-08: Checkpoint B was accepted and pushed to `main` as `a63de4e`. Checkpoint C began directly on `main` after explicit user approval.

2026-08-08: Checkpoint C implemented for review. Added `EdenOsWireContract::CurrentSchemaVersion` plus route constants for `POST /api/missions/sessions`, `POST /api/missions/sessions/{id}/telemetry`, `POST /api/missions/sessions/{id}/events`, and `POST /api/missions/sessions/{id}/complete`. Added pure C++ EDEN OS wire DTOs and `FEdenOsWireSerializationModel` for session-create, telemetry-ingestion, event-ingestion, and session-complete JSON serialization. The serializer has no UObject lookup, world access, subsystem mutation, filesystem dependency, HTTP implementation, JWT handling, retry/backoff, or outbound queue.

2026-08-08: Checkpoint C schema relationship locked. The EDEN OS telemetry and completion envelopes carry `schemaVersion = 1` from the single `EdenOsWireContract::CurrentSchemaVersion` constant and embed the exact canonical `FEdenTelemetryExportModel::BuildSessionJsonV1(Payload)` output under `telemetry`, preserving 0006 Telemetry Export Schema v1 as the source export foundation instead of creating adapter-owned telemetry state. Session-create serialization omits fabricated `seed`, `endedAt`, `alertsCount`, and `ticks` fields. JWT remains connection state only and is not part of any wire payload.

2026-08-08: Checkpoint C tests added. `Eden.Unit.EdenOs.Wire.*` covers route/schema constants, session-create required fields only, telemetry wrapping of canonical Export Schema v1, event identity/type/time, completion terminal facts, unsupported/missing/malformed schema versions, missing identifiers, non-finite telemetry values, invalid event sequence metadata, JWT/token exclusion, and deterministic serialization.

2026-08-08: Checkpoint C validation passed. `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild` invalidated the makefile for `source file added`, explicitly compiled `EdenOsWireSerializationModel.cpp` and `EdenOsWireSerializationTests.cpp`, relinked `UnrealEditor-EdenSpaceSimulator.dll`, and returned `Result: Succeeded`. Focused automation passed via `UnrealEditor.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.EdenOs.Wire.; Quit" "-TestExit=Automation Test Queue Empty" -Log`; `Saved/Logs/EdenSpaceSimulator.log` reported 11 tests found, all 11 `Eden.Unit.EdenOs.Wire.*` tests completed with `Result={Success}`, and `**** TEST COMPLETE. EXIT CODE: 0 ****`. Full automation passed via `Automation RunTests Eden.`; the log reported 217 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****`. Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`. Final Win64 Development Editor build passed and was up to date. `git diff --check` passed. Source `LogTemp` search returned no matches. Source HTTP/network-scope search found no `FHttpModule`, `IHttpRequest`, HTTP module include, FastAPI call, WebSocket, socket, retry, or backoff implementation; matches were limited to pre-existing mission "no retry" log text. Secret scan found no realistic committed JWT; matches were limited to synthetic `test-token` literals in EdenOs tests and existing docs evidence.

2026-08-08: Checkpoint C was accepted at `f66cda3`. Checkpoint E began directly on `main` after explicit user approval.

2026-08-08: Checkpoint E implemented for review. Added `FEdenOsTelemetrySink`, `IEdenOsHttpTransport`, `FEdenOsUnrealHttpTransport`, `FEdenOsQueuedRequest`, `FEdenOsHttpRequestData`, `FEdenOsHttpResult`, `FEdenOsUrlModel`, and `FEdenOsTransportModel`. `FEdenOsTelemetrySink` plugs into the existing `IEdenTelemetrySink` seam and serializes through Checkpoint C before submitting immutable JSON to `UEdenOsAdapterSubsystem`. `UEdenTelemetrySubsystem` was not special-cased and remains unaware of HTTP/FastAPI details.

2026-08-08: Checkpoint E transport ownership and queue policy locked in code. `UEdenOsAdapterSubsystem` owns runtime connection state, bearer-token presence, pending count, dropped count, last error summary, the bounded outbound queue, one-in-flight pump, and HTTP callback handling. Queue depth uses `FEdenOsConnectionConfig::MaxQueueDepth` as the maximum outstanding messages, including the in-flight request. Overflow deterministically drops the newest message, increments `DroppedMessageCount`, and records a sanitized `LastErrorSummary`. Queue order is FIFO with one in-flight request; failure of one request starts the next without reordering. Queue entries own copied route/body strings and never hold references into mutable telemetry history.

2026-08-08: Checkpoint E HTTP semantics locked. Production transport uses Unreal `FHttpModule` / `IHttpRequest` asynchronously with `Content-Type: application/json` and `Accept: application/json`. When a runtime JWT exists, the production transport sends `Authorization: Bearer <token>` through the HTTP header only; JWT is never serialized into payload JSON, snapshots, logs, or failure summaries. URL construction is centralized through `FEdenOsUrlModel` using validated `BaseUrl` plus Checkpoint C route constants. HTTP success is strictly 2xx; non-2xx and network/start failures record failure. Unreal's HTTP API mapping in this checkpoint applies `RequestTimeoutSeconds` to `IHttpRequest::SetTimeout`; `ConnectionTimeoutSeconds` remains validated configuration but is not separately enforceable by the current Unreal request API.

2026-08-08: Checkpoint E callback lifetime strategy locked. HTTP callbacks capture `TWeakObjectPtr<UEdenOsAdapterSubsystem>` and the subsystem rejects callbacks after `Deinitialize()` by clearing `bAcceptTransportCallbacks`. `Deinitialize()` unregisters the owned EDEN OS telemetry sink from telemetry fan-out, clears the queue, clears in-flight state, clears transport ownership, and resets runtime connection state. No render-frame Tick or fixed-step pump was added; enqueue starts the pump if idle, and async completion starts the next queued request.

2026-08-08: Checkpoint E tests added. `Eden.Unit.EdenOs.Transport.*` covers deterministic URL joining, bearer-header construction without token exposure, FIFO order, queue max-depth enforcement, observable newest-message drop, non-2xx failure, network failure, success connection state, degraded state after prior success then failure, late completion safety, and request-timeout mapping. `Eden.Integration.EdenOs.FailingTransportDoesNotChangeSimulation` compares authoritative fuel, power, and thermal results with EDEN disabled versus failing transport. `Eden.Integration.EdenOs.FailingTransportPreservesAuthoritativeMissionResult` compares a deterministic world-backed mission run with EDEN disabled against the same mission/resources/clock run with `FEdenOsTelemetrySink` registered and fake HTTP transport failing every outbound delivery. It compares mission state, mission phase, mission elapsed time, mission event runtime states, objective runtime states, fuel kilograms/fraction/state, battery kWh/fraction/state, thermal temperature/state, telemetry history counts, clock elapsed time, and dropped clock steps. The same test proves the failing run registered exactly one EDEN sink, attempted one telemetry delivery, sent one HTTP request through the fake transport, and recorded the expected disconnected/offline adapter bookkeeping. `Eden.Integration.Telemetry.LocalSinkSurvivesEdenSinkFailure` proves local sink success and history preservation when an EDEN sink fails in the same fan-out.

2026-08-08: Checkpoint E validation passed. `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild` invalidated the makefile for added source and explicitly compiled `EdenOsAdapterSubsystem.cpp`, `EdenOsTelemetrySink.cpp`, `EdenOsTransport.cpp`, `EdenOsUnrealHttpTransport.cpp`, and `EdenOsTransportTests.cpp`; final build returned `Result: Succeeded`. Focused transport automation passed via `UnrealEditor.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.EdenOs.Transport.; Quit" "-TestExit=Automation Test Queue Empty" -Log`; `Saved/Logs/EdenSpaceSimulator.log` reported 10 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****`. Integration automation passed for `Eden.Integration.EdenOs.` with 1 test found and exit code 0, and `Eden.Integration.Telemetry.` with 1 test found and exit code 0. Full automation passed via `Automation RunTests Eden.` with 229 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****`. Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`. Final Win64 Development Editor build passed and was up to date. `git diff --check` passed with a line-ending warning for `EdenSpaceSimulator.Build.cs` only. Source `LogTemp` search returned no matches. Hardcoded localhost/API-version scan found no `localhost`, `127.0.0.1`, or implementation `/api/v1/` usage. Scope scan found no advisory, TriggerReasons, external command router, retry/backoff, WebSocket, or socket implementation; matches were limited to pre-existing mission "no retry" log text. Secret scan found no realistic committed JWT; matches were limited to synthetic `test-token` literals in EdenOs tests and existing docs evidence. Blocking/synchronous HTTP pattern scan found no HTTP wait/sleep pattern; matches were unrelated `Inf` mission validation test names.

2026-08-08: Checkpoint E corrective proof implemented after acceptance review found the original isolation test too narrow. The remediation changed only `EdenOsTransportTests.cpp` plus this ExecPlan/RECOVER evidence. UBT initially trusted stale test intermediates; only the `EdenOsTransportTests.cpp` intermediate object/response/dependency files under `Intermediate/Build/.../EdenSpaceSimulator/` were removed, causing UBT to invalidate the makefile for `EdenOsTransportTests.cpp.obj.rsp deleted` and explicitly compile `EdenOsTransportTests.cpp`. Corrective build passed via `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild`. Focused `Automation RunTests Eden.Integration.EdenOs.` found 2 tests, including `Eden.Integration.EdenOs.FailingTransportPreservesAuthoritativeMissionResult`, and passed with `**** TEST COMPLETE. EXIT CODE: 0 ****`. Focused `Automation RunTests Eden.Unit.EdenOs.Transport.` found 10 tests and passed. Full `Automation RunTests Eden.` found 230 tests and passed with exit code 0. Final repository validation passed. Final Win64 Development Editor build passed and reported target up to date after the corrective compile. `git diff --check` passed. Source `LogTemp` and secret scans returned no matches. Scope scan found no advisory, external command, retry/backoff implementation, hardcoded localhost, or blocking HTTP wait; matches were limited to the existing authority-mode contract/tests, pre-existing mission "no retry" log text, the route-version negative assertion, and the intended async Unreal HTTP calls. This evidence is the current Checkpoint E acceptance surface; the earlier 229-test result is superseded by the corrective proof.

2026-08-08: Checkpoint E was accepted at `75fcd90`. The Unreal lane checkpoints assigned before convergence (A/B/C/E) are accepted. Checkpoint F remains locked until ProjectEden Checkpoint D is complete and explicit Checkpoint F authorization is given.

2026-08-08: Checkpoint F was intentionally stopped before implementation after inspecting ProjectEden Checkpoint D at `B:\repo\ProjectEden`, commit `6442029 feat(missions): add external mission session ingestion`. The ProjectEden D request DTOs in `packages/api/eden_api/schemas/missions.py` did not match Unreal's accepted Checkpoint C wire DTOs for create, telemetry, event, and complete payloads. This was treated as a real contract conflict, not a Checkpoint F task.

2026-08-08: Narrow Checkpoint C corrective patch implemented for review. Unreal wire serialization now aligns with ProjectEden D request shapes: create emits `schemaVersion`, `sessionId`, `scenarioId`, and `startedAt`; telemetry emits `schemaVersion`, `sessionId`, `sequence`, `simulationTimeSeconds`, and canonical `telemetry`; events emit `schemaVersion`, `sessionId`, `eventId`, `eventType`, `sequence`, `simulationTimeSeconds`, and `payload`; complete emits `schemaVersion`, `sessionId`, `finalStatus`, `completedAt`, plus optional `finalSequence`, `ticks`, `alertsCount`, and `highestRiskSystem` only when known. The `finalStatus` enum maps exactly to ProjectEden literals `succeeded`, `failed`, and `aborted`. No lifecycle orchestration, HTTP transport, advisory, command, retry/backoff, FastAPI, DB, or ProjectEden code was changed.

2026-08-08: Corrective Checkpoint C semantic boundary recorded. Unreal canonical telemetry still owns its internal 0006 `missionId` value such as `SolarCrisis`; the ProjectEden create DTO now requires an explicit `ScenarioId` string, demonstrated with `SolarEventEmergency` in wire tests. The corrective patch does not rename canonical mission telemetry, does not introduce the future F mapping layer, and does not fabricate unknown completion facts.

2026-08-08: Corrective Checkpoint C validation passed. `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild` explicitly compiled `EdenOsWireSerializationModel.cpp` and `EdenOsWireSerializationTests.cpp`, relinked `UnrealEditor-EdenSpaceSimulator.dll`, and returned `Result: Succeeded`. Focused `Automation RunTests Eden.Unit.EdenOs.Wire.` via `UnrealEditor-Cmd.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.EdenOs.Wire.; Quit" "-TestExit=Automation Test Queue Empty" -Log` found 13 tests and passed with `**** TEST COMPLETE. EXIT CODE: 0 ****`. Full `Automation RunTests Eden.` found 232 tests and passed with exit code 0. Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`. `git diff --check` passed. Source `LogTemp` scan returned no matches. Added-source credential scan returned no matches. Added-diff HTTP/F-shaped scope scan found no `FHttpModule`, `IHttpRequest`, retry/backoff, advisory, authorized-control, trigger-reason, session-lifecycle, or external-session implementation changes.

2026-08-08: Corrective Checkpoint C was re-accepted at `3f69f9b`, and `main` was pushed to `origin/main` before Checkpoint F began. ProjectEden D remains accepted at `B:\repo\ProjectEden` commit `6442029`.

2026-08-08: Checkpoint F session lifecycle convergence implemented for review. Added `FEdenOsMissionLifecycleModel` to derive lifecycle create and completion DTOs from immutable telemetry session payloads without mutating mission, telemetry, transport, or simulation state. `FEdenOsTelemetrySink` now queues the accepted ProjectEden route sequence for one session: create once, telemetry on new sequence, events by event sequence, and complete only after an explicit terminal mission fact. The adapter owns runtime transport state and sink delivery bookkeeping; canonical 0006 telemetry remains the telemetry truth. `DefaultScenarioId` is now explicit connection configuration used by the create request; Blueprints/assets and ProjectEden source were not modified.

2026-08-08: Checkpoint F tests and verifier added. `Eden.Unit.EdenOs.Lifecycle.FinalStatusModel` covers terminal status derivation and non-terminal omission. `Eden.Unit.EdenOs.Lifecycle.OmitsCompletionUntilTerminalFact` prevents fabricated completion. `Eden.Unit.EdenOs.Lifecycle.DoesNotReplayDeliveredFacts` prevents duplicate lifecycle replay. `Eden.Integration.EdenOs.SessionLifecycleEmitsProjectEdenRouteSequence` exercises the real Unreal adapter/sink/transport queue path and writes `Saved/Automation/EdenOsMissionLifecycleRequests.json`. `scripts/Verify-EdenOsMissionContract.ps1` replays that Unreal-produced artifact through ProjectEden's real FastAPI routes, Pydantic schemas, service/repository layer, and isolated SQLite persistence.

2026-08-08: Checkpoint F validation passed. Initial build invalidated the UBT makefile for `source file added` and explicitly compiled `EdenOsMissionLifecycle.cpp`, `EdenOsTelemetrySink.cpp`, `EdenOsConnectionConfigTests.cpp`, `EdenOsLifecycleTests.cpp`, and `EdenOsTransportTests.cpp`; the build returned `Result: Succeeded`. Focused `Automation RunTests Eden.Unit.EdenOs.` passed with exit code 0. Focused `Automation RunTests Eden.Integration.EdenOs.` found 3 tests and passed with exit code 0 after updating the previous E isolation assertion to expect F's multi-request lifecycle fan-out. The ProjectEden verifier passed: it replayed 7 Unreal mission lifecycle requests through ProjectEden routes and verified persisted session, telemetry, events, and completion. Full `Automation RunTests Eden.` found 237 tests and passed with `**** TEST COMPLETE. EXIT CODE: 0 ****`. Repository validation passed. `VerifyFlightAssets.py`, `VerifyMissionAssets.py`, `VerifyOperatorAssets.py`, `VerifyResourceAssets.py`, and `VerifyFlightRuntimeSmoke.py` all passed. Final Win64 Development Editor build passed and reported target up to date. `git diff --check` passed. Source `LogTemp` search returned no matches. Secret scan matches were limited to synthetic test credentials and docs evidence. Scope scan matches were limited to the intended async Unreal HTTP transport from Checkpoint E, existing advisory configuration, route-version tests, and pre-existing mission "no retry" log text.

2026-08-08: Checkpoint F live cross-process corrective proof implemented after acceptance review found the route replay verifier insufficient by itself. Added sanitized adapter delivery records for completed outbound HTTP attempts (`MessageType`, route path, sequence, HTTP status, success flag, error summary, and response body only; no request body, bearer token, or authorization header). Added `Eden.External.EdenOs.LiveProjectEdenMissionLifecycle`, an opt-in automation test that uses the real `UEdenOsAdapterSubsystem`, `FEdenOsTelemetrySink`, `FEdenOsUnrealHttpTransport`, and Unreal `FHttpModule`; it skips when `EDEN_OS_LIVE_E2E_BASE_URL` or `EDEN_OS_LIVE_E2E_BEARER_JWT` is absent. Added `scripts/Run-EdenOsLiveE2E.ps1` to create an isolated ProjectEden SQLite database, apply Alembic head through ProjectEden, start a real `uvicorn eden_api.main:app` listener on loopback, register/login through ProjectEden auth, inject the short-lived bearer token into Unreal only through process environment, run the live Unreal automation test, and query the real DB after completion.

2026-08-08: Checkpoint F live cross-process validation passed via `.\scripts\Run-EdenOsLiveE2E.ps1 -ProjectEdenRoot 'B:\repo\ProjectEden' -EngineRoot 'K:\Program Files\Epic Games\UE_5.8' -Port 8791`. ProjectEden Alembic upgraded the isolated SQLite DB through accepted D migration `a6b7c8d9e0f1 add_mission_environment_sessions`, the real FastAPI server started at `http://127.0.0.1:8791`, `/health` returned 200, `/api/auth/register` returned 201, and `/api/auth/login` returned 200. Unreal live automation found 1 test and passed with `**** TEST COMPLETE. EXIT CODE: 0 ****`. Sanitized Unreal evidence in `Saved/Automation/EdenOsLiveE2E/20260808-110140/UnrealLiveE2E.json` recorded `transport = FEdenOsUnrealHttpTransport`, session `dfc2dd9f-42ea-4082-f2ad-8f89818e2532`, scenario `SolarEventEmergency`, mission state `Succeeded`, adapter state `Connected`, zero pending and dropped messages, and 10 successful real HTTP deliveries: create 201, telemetry 202, seven event 202 responses, and complete 200. The create response body contained the public ProjectEden session id and `status: running`; the completion response contained `status: succeeded`.

2026-08-08: Checkpoint F live persistence proof passed. `Saved/Automation/EdenOsLiveE2E/20260808-110140/ProjectEdenDbEvidence.json` verified exactly one `mission_environment` `SimulationRun` for external session `dfc2dd9f-42ea-4082-f2ad-8f89818e2532`; `origin = mission_environment`; `external_session_id` matched the Unreal session; `scenarioId = SolarEventEmergency`; `seed` was null; `ended_at` was populated only after completion; DB run status was `completed`; Unreal mission state was `Succeeded`; terminal result mapping matched; one `MissionTelemetryPayload`, one `TelemetryState`, and seven `MissionEnvironmentEvent` rows belonged to the same persisted run; `alertsCount = 1`; `ticks` and `highestRiskSystem` remained null because Unreal did not supply those facts. ProjectEden uvicorn access logs showed the real create, telemetry, seven event, and complete requests. The runtime bearer token was never printed or written to tracked files.

2026-08-08: Checkpoint F regression validation after live proof passed. `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild` invalidated the makefile for the working set and explicitly compiled `EdenOsAdapterSubsystem.cpp`, `EdenOsTransport.cpp`, `EdenOsUnrealHttpTransport.cpp`, `EdenOsConnectionConfigTests.cpp`, `EdenOsLifecycleTests.cpp`, and `EdenOsTransportTests.cpp`, then linked successfully. Focused `Automation RunTests Eden.Unit.EdenOs.` passed with exit code 0. Focused `Automation RunTests Eden.Integration.EdenOs.` passed with exit code 0. The existing `scripts/Verify-EdenOsMissionContract.ps1 -ProjectEdenRoot 'B:\repo\ProjectEden'` replay verifier still passed, replaying 7 Unreal-produced requests through ProjectEden TestClient routes and SQLite persistence. Full `Automation RunTests Eden.` found 238 tests; the external live test skipped cleanly because live env vars were unset, and the suite passed with `**** TEST COMPLETE. EXIT CODE: 0 ****`. Repository validation passed. Final Win64 Development Editor build passed and reported target up to date. `git diff --check` passed. Source `LogTemp` and token scans returned no matches. Scope scans found no G work; matches were limited to accepted authority-mode contract/tests, pre-existing mission "no retry" text, intended async Unreal HTTP transport, and intended test seams.

2026-08-08: Checkpoint F accepted. Commit `63768ab feat(eden): converge mission session lifecycle` plus corrective proof commit `5249a6a test(eden): prove live ProjectEden mission lifecycle` close the 0007 plumbing/convergence phase. The accepted evidence proves the real cross-process path from Unreal mission telemetry through `FEdenOsTelemetrySink`, `UEdenOsAdapterSubsystem`, `FEdenOsUnrealHttpTransport`, Unreal `FHttpModule`, ProjectEden FastAPI auth/routes/service/repository, and isolated SQLite persistence. `main` was pushed to `origin/main` after F acceptance. Checkpoint G Observe mode is authorized next; Checkpoints H-M remain locked.

2026-08-08: Checkpoint G implemented for review. Added an explicit Observe-mode runtime integration proof in `EdenOsTransportTests.cpp`. The mission isolation probe now compares flight and operator telemetry snapshots in addition to mission outcome, mission phase, mission/objective runtime states, fuel, battery, temperature, telemetry history counts, and clock timing. `Eden.Integration.EdenOs.ObserveModePreservesAuthoritativeMissionResult` runs the same deterministic mission with EDEN disabled and EDEN Observe enabled, verifies the Observe adapter sends lifecycle telemetry through create/telemetry/events/complete routes, verifies the connection snapshot reports `Observe`, verifies no advisory or command route is touched, and proves the compared authoritative simulation truth remains identical. The existing failing-transport isolation proof now also benefits from the expanded flight/operator comparison surface.

2026-08-08: Checkpoint G live Observe proof implemented. `scripts/Run-EdenOsLiveE2E.ps1` now accepts `-AuthorityMode Advisory|Observe` and injects `EDEN_OS_LIVE_E2E_AUTHORITY_MODE` only into the live Unreal automation process. `Eden.External.EdenOs.LiveProjectEdenMissionLifecycle` applies that authority mode to runtime config and writes sanitized evidence with `authorityMode`; no bearer token, request body, or authorization header is written to evidence. No ProjectEden code, advisory request/response contract, HUD advisory events, external command router, retry/backoff, WebSocket, Blueprint asset, or gameplay feature was added.

2026-08-08: Checkpoint G validation passed. Initial build explicitly recompiled `EdenOsTransportTests.cpp` and returned `Result: Succeeded`. Focused `Automation RunTests Eden.Integration.EdenOs.` found 4 tests, including `Eden.Integration.EdenOs.ObserveModePreservesAuthoritativeMissionResult`, and passed with `**** TEST COMPLETE. EXIT CODE: 0 ****` in `Saved/Logs/Automation-0007G-IntegrationEdenOs.log`. Focused `Automation RunTests Eden.Unit.EdenOs.` found 38 tests and passed. Live Observe E2E passed via `.\scripts\Run-EdenOsLiveE2E.ps1 -ProjectEdenRoot 'B:\repo\ProjectEden' -EngineRoot 'K:\Program Files\Epic Games\UE_5.8' -Port 8792 -AuthorityMode Observe`; Unreal evidence `Saved/Automation/EdenOsLiveE2E/20260808-113526/UnrealLiveE2E.json` recorded `authorityMode = Observe`, mission state `Succeeded`, adapter state `Connected`, zero pending/dropped messages, create 201, telemetry 202, seven event 202 responses, and complete 200. `ProjectEdenDbEvidence.json` verified one `mission_environment` run, external session id match, `seed = null`, `ended_at` populated, DB status `completed`, one telemetry payload/state, seven events, `alertsCount = 1`, `ticks = null`, and `highestRiskSystem = null`.

2026-08-08: Checkpoint G regression and hygiene validation passed. Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`. Full `Automation RunTests Eden.` found 239 tests and passed with `**** TEST COMPLETE. EXIT CODE: 0 ****`; the external live test skipped in the full suite because live env vars were unset. Editor verifiers passed: `VerifyFlightAssets.py`, `VerifyMissionAssets.py`, `VerifyOperatorAssets.py`, `VerifyResourceAssets.py`, and `VerifyFlightRuntimeSmoke.py`. Final Win64 Development Editor build passed and reported target up to date. `git diff --check` passed with the existing LF/CRLF warning on `scripts/Run-EdenOsLiveE2E.ps1`. Source `LogTemp` search returned no matches. Token scan found only existing runtime-header/test-script references and synthetic `test-token` usage. Added-diff scope scan found only authority-mode setup and negative assertions that Observe does not call advisory or command routes. Checkpoint G is ready for acceptance; Checkpoints H-M remain locked.

2026-08-08: Checkpoint I accepted. Implementation commit `89d47da feat(eden): surface ProjectEden advisories` plus corrective evidence commit `89761fd test(eden): prove live advisory return path`. Live Advisory E2E evidence `Saved/Automation/EdenOsLiveE2E/20260808-140801/` proves the full return path in one run: ProjectEden `/advisories` 201 → `evaluationId` correlation → exactly one local `EdenAdvisoryIssued` → recommendation/rationale/triggerReasons preserved → three clocks (`issued 0.7`, `evaluation 0.4`, `context snapshot 0.4`) with `issued >= evaluation >= snapshot` → read-only presentation matches response → ProjectEden `MissionAdvisory` persistence (`advisoryCount = 1`). Integration `Eden.Integration.EdenOs.` 6/6; full `Automation RunTests Eden.` 265/265; Win64 Development Editor build and repository validation passed. No fabricated confidence/severity/recommendationCode. Checkpoint J (authorized-command boundary, built and tested, disabled by default) is authorized next; Checkpoints K-M remain locked. Do not begin K.

---

## 17. Checkpoint acceptance protocol

Applies to every remaining checkpoint in both lanes.

Each checkpoint is audited against **two independent questions**:

```text
1. Does what was built work?
   → repository validation, Win64 Development Editor build,
     full `Automation RunTests Eden.`  (trailing dot — bare `Eden`
     sweeps in engine tests and inflates the count by ~7)

2. Was everything the checkpoint required actually built?
   → source and architecture inspection against the checkpoint's
     locked acceptance criteria
```

**Question 1 cannot answer question 2.** Green tests prove that implemented behavior works; they say nothing about required behavior that was never implemented. Checkpoint A passed question 1 completely — clean build, 189/189 passing, correct pure-refactor boundary, plausible commit title — while failing question 2 on the single capability the checkpoint existed to deliver.

Supporting rules:

- **Diff pre-existing tests against the checkpoint's baseline commit.** Mechanical routing through a new seam is acceptable and must be identified as such. A weakened or removed assertion is a failure.
- **A test count is a smell test, not authority.** Source inspection decides.
- **Named acceptance tests must exist as named tests.** Extra assertions folded into an unrelated existing test do not satisfy named coverage.
- **A checkpoint declared "pure refactor" carries no semantic improvements**, however harmless. Behavior changes ride their own checkpoint.

### 17.1 Git operating model — LOCKED 2026-08-08

0007 proceeds **directly on `main`**. No checkpoint branches, no merge choreography.

```text
main
 → repair/implement checkpoint
 → build + tests + audit
 → commit
 → explicit approval
 → next checkpoint
```

The governing rule:

> **`main` may contain work-in-progress checkpoint code, but the next checkpoint does not begin until the current checkpoint passes acceptance.**

Acceptance is controlled by **tests plus audit**, not by branch topology.

**Why this changed.** Branch topology was never the control mechanism, and pretending otherwise produced a false sense of one. Checkpoint A was audited, rejected on two locked gates, and merged to `main` anyway via PR #5 (`f8edfb0`) with no remediation. The branch gate did not hold, and `main`'s suite reports 189/189 green over a seam that cannot register a second sink — a true statement about code that does not meet its checkpoint contract.

Removing the branch ceremony removes the illusion. What remains is the thing that actually caught the defect: source inspection against locked acceptance criteria (§17).

**Consequence to keep in view:** a green suite on `main` no longer implies any checkpoint was accepted. Approval is recorded in §16, and §16 is the authority on what has been accepted — not the merge history, and not the build badge.

---

## 18. Checkpoint H — advisory contract and context

**Ready for acceptance review. Not accepted.**

### 18.1 Advisory trigger model

Locked triggers only; no speculative additions. Triggers are derived from **telemetry events**, not from diffing state:

| Telemetry event | Trigger reason |
|---|---|
| `PhaseChanged` | `MissionPhaseTransition` |
| `AlertRaised` / `AlertCleared` | `AlertTransition` |
| `ObjectiveStateChanged` | `ObjectiveTransition` |
| `OperatorCommandIssued` | `OperatorAction` |
| *(cadence)* | `Heartbeat` |

`MissionStarted/Succeeded/Failed/Aborted`, `ScheduledEventFired`, and `ResourceStateTransition` are recorded by telemetry but are **not** triggers.

**This is what keeps §5.11 satisfiable.** Detecting "the phase changed" by comparing against a remembered previous phase would require the adapter to shadow mission state. Telemetry already records transitions as immutable events, so the adapter needs only a **sequence cursor** — a position in telemetry's history, never a copy of it.

### 18.2 Deterministic TriggerReason ordering

Canonical order is ascending enum value:

```text
MissionPhaseTransition(0) → AlertTransition(1) → ObjectiveTransition(2)
→ OperatorAction(3) → Heartbeat(4)
```

`AddTriggerReason` de-duplicates then re-sorts, so arrival order cannot affect the emitted sequence.

### 18.3 Same-step coalescing

All triggers for one settled step produce **exactly one** evaluation carrying every reason. Duplicate reasons within an evaluation collapse; reasons are never discarded. Deduplication never spans simulation steps — the cursor only advances past events already folded into an evaluation.

### 18.4 Heartbeat

Consumes the existing Checkpoint B setting `UEdenOsConnectionSettings::AdvisoryHeartbeatSimulationSeconds` (default 5.0). No second constant was introduced. Simulation time only — no wall clock, `FPlatformTime`, frame time, or HTTP timing. A paused clock cannot advance it. The first evaluation of a running mission is always due; terminal missions stop evaluating.

### 18.5 Settled-state ordering

```text
Systems 0 → Mission 100 → Telemetry 200 → Advisory 300
```

`EdenSimulationClockPriority::Advisory = 300` was added, and `UEdenOsAdapterSubsystem` now implements `IEdenSimulationTickable` purely to observe.

### 18.6 Context ownership and bounds

Context is built from accepted 0006 telemetry history only. All fields are **value copies**, so no context aliases telemetry's mutable arrays and later simulation cannot alter an already-built context.

Bounds: newest-`MaxSnapshots` (10) and newest-`MaxEvents` (20), oldest dropped first, with `bContextTruncated` recording that it happened and `bUpstreamHistoryTruncated` mirroring 0006's own integrity flag.

Trend semantics are deliberately minimal and documented rather than invented: `LatestValue`, `EarliestValue`, `Delta` across the bounded window, plus `SampleCount`. No forecasting, no prediction, no ML.

### 18.7a Corrective amendment — evaluation time separated from snapshot time

**Why H was amended.** Drafting the H.1 request schema (§19.2) exposed a semantic ambiguity already present inside accepted H: one field carried two different facts.

```text
BEFORE
  Input.SimulationTimeSeconds = SettledSnapshot.SimulationTimeSeconds
  → FEdenOsAdvisoryContext::SimulationTimeSeconds was the SNAPSHOT time
    while its name and meaning implied the EVALUATION-DUE time

AFTER
  SimulationTimeSeconds                 = clock elapsed time, read live on the settled step
  ContextSnapshotSimulationTimeSeconds  = timestamp of the newest snapshot used
```

Both values were always real and reproducible; the claim attached to one of them was wrong. Serializing that across a process boundary would have told ProjectEden that trigger and observation were simultaneous when they are not.

**Ownership split now explicit:**

```text
Simulation clock  owns evaluation timing
Telemetry         owns observation history and its timestamps
H evaluator       combines evaluation time + latest bounded telemetry context
```

**Why the two diverge.** Telemetry stamps snapshots with the same `GetElapsedSimulationTimeSeconds()` the evaluator now reads, so the values are directly comparable and evaluation time is always ≥ snapshot time. They differ solely because snapshots are **decimated**: on a step that records no snapshot, the newest available observation is older than the evaluation.

**Heartbeat corrected.** Cadence now derives from live clock time rather than snapshot timestamps, so the interval is exactly `AdvisoryHeartbeatSimulationSeconds` at fixed-step granularity instead of being quantized to the snapshot decimation interval. No second constant was introduced; no adapter-owned elapsed-time state was added — the evaluator reads the clock, it does not own one.

### 18.7 Known limitation — snapshot lag versus event immediacy

Telemetry **decimates snapshots** (every 5 steps) but does **not** decimate events. An event-triggered evaluation therefore carries resource and mission fields from the most recent *snapshot*, which may be up to one decimation interval old, while the trigger itself is current.

This is deterministic and bounded, not a race. But EDEN reasoning must not assume the context's state fields are simultaneous with its trigger. Worth revisiting in I if advisory quality depends on tighter coupling.

### 18.8 BLOCKER — no ProjectEden advisory contract exists

Per §12 and §22, ProjectEden was inspected before any wire contract was invented. `packages/api/eden_api/routes/` contains `missions.py` with the accepted D/F ingestion routes and **no advisory route or schema anywhere**.

Therefore H implemented the **internal, transport-neutral** contract only. No advisory request/response DTO, no server route, and no ProjectEden change was made. `/api/simulator` was not reused and no `/api/v1/` was introduced.

**A minimal cross-project advisory API contract must be proposed and accepted before the advisory wire/response work can proceed.** That is a prerequisite for Checkpoint I, not a defect in H.

### 18.9 Scope guard

`EdenAdvisoryIssued` is **not** emitted; no HUD, widget, or presentation change; no command router, no `AuthorizedControl` execution; no followed/ignored semantics. `AuthorizedControl` remains contract-only and disabled by default.

### 18.10 Shared G/H fixture change — ratified

> Shared G/H isolation fixture changed from 0.5 s to 0.7 s because the prior value caused the first decimated telemetry snapshot to coincide with mission termination, preventing any settled Running-state advisory evaluation. Existing G non-interference assertions remain unchanged and pass.

**Accepted 2026-08-08.** Both sides of every probe comparison use the same fixture, and all five `Eden.Integration.EdenOs.` tests remain green, so this is a fixture correction rather than a weakening of G.

### 18.11 Checkpoint H acceptance

**ACCEPTED 2026-08-08** at commit `285016c`.

```text
Build                       0 errors / 0 warnings
Eden.Integration.EdenOs.    5 / 5
Full Automation Eden.       255 / 255, 0 failures, 0 crashes, exit 0
Advisory unit tests         14
Advisory integration test   1
Shadow simulation history   none
I/J/K scope leakage         none
```

The sequence-cursor architecture is accepted as the resolution to §19: a cursor is the reader's position in history owned elsewhere, not a shadow copy of it.

```text
Telemetry   owns facts and history
EdenOs H    owns cursor, evaluation bookkeeping, immutable derived context
```

---

## 19. Checkpoint H.1 — ProjectEden advisory API contract

**IMPLEMENTED in ProjectEden (`da38ecb` trigger-contract fix). Unlocks Checkpoint I.**

ProjectEden serves `POST /api/missions/sessions/{session_id}/advisories` with the locked §19.2/§19.3 schemas and the five §19.2a trigger strings.

### 19.1 Route

```text
POST /api/missions/sessions/{session_id}/advisories
```

Stays under the mission-session resource established in D/F. **Not** `/api/simulator` (opposite direction of control) and **no** `/api/v1/` (payload versioning only).

### 19.2 Request v1

```json
{
  "schemaVersion": 1,
  "evaluationId": "...",
  "simulationTimeSeconds": 12.5,
  "contextSnapshotSimulationTimeSeconds": 12.0,
  "triggerReasons": ["objective_transition", "heartbeat"],
  "context": { "...": "bounded H context" }
}
```

| Field | Meaning |
|---|---|
| `evaluationId` | Generated by the mission environment; makes evaluation idempotent |
| `simulationTimeSeconds` | When the advisory evaluation became **due** |
| `contextSnapshotSimulationTimeSeconds` | Timestamp of the newest telemetry snapshot used to build context |
| `triggerReasons` | Every same-step coalesced reason, canonical order preserved |
| `context` | The bounded immutable H context |

#### 19.2a Trigger reason wire vocabulary — LOCKED

H stopped before the wire contract, so `EEdenOsAdvisoryTriggerReason` has **never been serialized**. Neither repository can inspect the other for these names; the contract defines them, and both implement against this table.

| `EEdenOsAdvisoryTriggerReason` | Wire value | Canonical order |
|---|---|---|
| `MissionPhaseTransition` | `mission_phase_transition` | 0 |
| `AlertTransition` | `alert_transition` | 1 |
| `ObjectiveTransition` | `objective_transition` | 2 |
| `OperatorAction` | `operator_action` | 3 |
| `Heartbeat` | `heartbeat` | 4 |

snake_case of the enum name. `triggerReasons` is emitted in canonical ascending order (§18.2), is never empty, and never contains duplicates. Any value outside this table is rejected deterministically; the vocabulary is closed for 0007.

No JWT in the body. No commands. No control authority.

**The two timestamps are deliberately separate.** Per §18.7 they can differ by up to one snapshot decimation interval, and reasoning must never assume they are simultaneous.

### 19.3 Response v1

```json
{
  "schemaVersion": 1,
  "advisoryId": "...",
  "evaluationId": "...",
  "recommendation": "...",
  "rationale": "..."
}
```

Four concepts only. Severity, confidence, and structured recommended-action wait for a demonstrated product requirement.

The response is **information only**. It carries no executable command contract:

```text
advisory response → immutable recommendation fact → I telemetry event + HUD
NOT
advisory response → operator mutation
```

### 19.4 Server behavior

```text
valid authenticated request      → 200
unknown mission session          → 404
unsupported schemaVersion        → 4xx
duplicate evaluationId           → idempotent, same result
completed session                → explicit policy, preferably reject
malformed / non-finite context   → 422 / 4xx
```

ProjectEden should persist the advisory evaluation and result where that fits its existing service/repository layering, since I will need a durable fact to correlate with `EdenAdvisoryIssued`.

### 19.4a H.1 acceptance criteria — locked before implementation reports

Audited in this order. Recorded in advance so acceptance is measured against the contract, not against whatever was built.

| # | Gate |
|---|---|
| 1 | **Scope** — `POST /api/missions/sessions/{session_id}/advisories`, plural exactly; no `/api/simulator`; no `/api/v1/` |
| 2 | **Request contract** — `schemaVersion`, `evaluationId`, `simulationTimeSeconds`, `contextSnapshotSimulationTimeSeconds`, `triggerReasons`, `context` |
| 3 | **Dual-time semantics** — 0.7/0.5 accepted and stored independently; snapshot > evaluation rejected; NaN/Infinity rejected |
| 4 | **Trigger vocabulary** — exactly the five §19.2a values; nothing else silently accepted |
| 5 | **Session policy** — auth required; owner-scoped unknown session → 404; mission_environment requirement; completed-session policy enforced |
| 6 | **Idempotency** — same session + `evaluationId` → same advisory, no second reasoner invocation, no duplicate row; DB unique constraint present |
| 7 | **Reasoner boundary** — route does not reason; service orchestrates; repository persists; Protocol vendor-independent; deterministic implementation clearly identified as a stub |
| 8 | **Response contract** — the four fields only; no executable action, control authority, or operator-mutation payload |
| 9 | **Persistence** — advisory linked to correct session, both timestamps, trigger reasons, context, recommendation, rationale, `evaluationId`, schema version |
| 10 | **Migration** — one Alembic head, clean upgrade, downgrade per repo policy, no historical migration modified |
| 11 | **Regression** — focused H.1 tests, mission D/F tests, DB/migration tests, full API suite, simulator suite, focused Ruff |
| 12 | **Security/scope** — no JWT persisted or logged; no API-key auth; no commands; no `AuthorizedControl`; no Unreal changes; no Checkpoint I work |

**The load-bearing test.** Through the real route → service → repository → database stack:

```text
POST advisory   simulationTimeSeconds = 0.7
                contextSnapshotSimulationTimeSeconds = 0.5
   → both persisted as distinct facts
   → reasoner invoked once
   → durable advisory returned
repeat same evaluationId
   → same advisory returned
   → reasoner invocation count unchanged
   → row count unchanged
```

If that passes end to end, H.1's skeleton is load-bearing. It is the regression test for `d6a07b6`, and it must not be satisfied by a fixture where the two timestamps are equal.

### 19.4b H.1 audit — `c2ba6d6` NOT ACCEPTED

Audited 2026-08-08 against §19.4a. Architecture cleared; one wire-contract defect.

**Cleared:** plural route shape; route → service → reasoner → repository → DB layering; vendor-agnostic `AdvisoryReasoner` Protocol with the deterministic implementation honestly labelled a stub; `(session_id, evaluationId)` idempotency with no second reasoner invocation; 0.7/0.5 persisted distinctly; owner-scoped 404; completed-session 409; validation 422s; full API 319 passed; simulator 25 passed; Ruff/diff/credential scans; no I/J/K leakage.

**Migration topology — verified by execution, not by suite label:**

```text
alembic heads          b7c8d9e0f1a2 (head)     exactly one
fresh DB upgrade       → b7c8d9e0f1a2          OK
downgrade -1           → a6b7c8d9e0f1          OK, advisory table dropped,
                                               D/F session tables retained
re-upgrade             → b7c8d9e0f1a2          OK, clean round trip
c2ba6d6 under alembic/ one new file, +71        no historical migration modified
```

Real DDL carries the dual timestamp columns, `uq_mission_advisory_session_eval`, and the `snapshot <= evaluation` check — the invariant is enforced at storage, not only in Pydantic.

**REJECTED — trigger vocabulary defect:**

```text
implemented   meaningful_operator_action
locked        operator_action            (§19.2a)
```

Compounding it, `operator_action` and `mission_phase_transition` appear in **no test**; coverage exercises only `alert_transition`, `objective_transition`, and `heartbeat`. Unknown-value rejection is correctly proven (`warp_core_breach` → 422), so the vocabulary is closed — but closure cannot reveal a wrong spelling inside the closed set.

**Root cause, recorded so the lesson survives:** the original H.1 brief listed the trigger in prose as "meaningful operator action". §19.2a, which fixed the wire spelling, was written afterwards and did not reach the implementer. The contract and the brief disagreed; the implementation followed the brief. Left unfixed, Unreal would send `operator_action` in Checkpoint I and receive a 422 — a break visible only once both repos are wired.

**Remaining gate:** rename to `operator_action` with no alias, plus a test exercising all five locked values through the real request-validation path, keeping the unknown-rejection test. No architectural change.

### 19.5 REQUIRED H amendment before H.1 can be implemented faithfully

H as accepted **cannot populate the two timestamps distinctly.**

`EdenOsAdapterSubsystem.cpp:480` sets `Input.SimulationTimeSeconds` from `SettledSnapshot.SimulationTimeSeconds`, so `FEdenOsAdvisoryContext::SimulationTimeSeconds` is in fact the *context snapshot* time. Emitting it as `simulationTimeSeconds` would assert exactly the simultaneity §19.2 forbids.

Second effect: heartbeat cadence is measured against snapshot time, so it is quantized to the snapshot decimation interval (0.5 s) rather than the fixed step (0.1 s). Deterministic and bounded, but not literally "every 5.0 s of simulation time."

Required amendment (small — the accessor already exists):

- Read true settled-step time from `UEdenSimulationClockSubsystem::GetElapsedSimulationTimeSeconds()`.
- Add `ContextSnapshotSimulationTimeSeconds` to `FEdenOsAdvisoryContext`, sourced from the newest snapshot.
- Keep `SimulationTimeSeconds` as the true evaluation-due time.
- Drive the heartbeat from true step time so the interval is exact.
- Add tests proving the two timestamps differ when a trigger fires between snapshots.
