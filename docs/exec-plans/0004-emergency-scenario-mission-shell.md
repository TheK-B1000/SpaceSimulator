# Emergency Scenario Mission Shell

## Status

Approved — Checkpoint A complete

## Prerequisite status

> [!CAUTION]
> ExecPlan 0003 (spacecraft resource simulation) is implemented and all automated validation passes (101 Eden tests, runtime composition/reset coverage). However, 0003 remains **Blocked** pending final hands-on PIE verification.
>
> - Do not mark 0003 Complete.
> - Do not create the `v0.2.0-resource-simulation` tag.
> - Do not modify the resource implementation as part of this task.
> - 0003 PIE verification is independent of Checkpoints A-G of 0004, but must be completed before 0004 Checkpoint H / manual milestone closeout.

## Repository state

| Field | Value |
|---|---|
| Branch | `plan/emergency-mission-shell` |
| Parent branch | `feature/spacecraft-resource-simulation` at `b19242e` |
| Resource baseline | Automated validation passing; PIE gate open |
| Checkpoint A status | ✅ Implemented and verified (30 tests, 131 total passing) |
| Checkpoint B status | ✅ Implemented and verified (6 tests, 137 total passing) |
| Checkpoint C status | ✅ Implemented and verified (3 tests, 140 total passing) |
| Checkpoint D status | ✅ Implemented and verified (6 tests, 146 total passing) |
| Checkpoint E status | ✅ Implemented and verified (3 tests, 149 total passing) |
| Checkpoint F status | ✅ Implemented and verified (7 tests, 156 total passing) |
| Working tree | Clean |

---

## 1. Problem and outcome

The verified resource simulation gives the spacecraft fuel, power, and thermal state, but there is no mission context: no objectives, no emergency events, no success/failure evaluation, and no deterministic restart. The PROJECT_SPEC requires "one emergency scenario" and "mission result" for the first vertical slice.

This milestone introduces:

- **`UEdenMissionSubsystem`** — a `UTickableWorldSubsystem` that owns mission lifecycle, active mission definition, mission elapsed time, objective runtime state, scheduled event execution, and mission outcome.
- **`FEdenMissionModel`** — a pure production model for deterministic timeline stepping, objective evaluation, and outcome resolution, testable without a world.
- **`UEdenMissionDefinitionDataAsset`** — a configuration-only Data Asset defining mission identifier, phases, objectives, scheduled events, and success/failure conditions. Never owns mutable runtime state.
- **Mission event commands** — deterministic, typed command structs that flow through existing resource component public APIs. The mission subsystem sends commands; resource components remain authoritative state owners.
- **Objective evaluation** — mission objectives observe authoritative resource snapshots and produce deterministic Pending/Active/Completed/Failed state.
- **Solar Event Emergency** — the first concrete scenario configured as a `UEdenMissionDefinitionDataAsset`, exercising warning → impact → recovery → resolution flow through the reusable framework.
- **Deterministic restart** — mission reset clears mission-owned state and mission-applied resource modifiers without silently owning resource reset.
- **Debug visibility** — mission state exposed through `ShowDebug EdenSystems` (extended) and `ShowDebug EdenMission`.
- **Project-specific logging** — via existing `LogEdenMission` category.

The outcome is a reusable mission/emergency framework where the first playable scenario demonstrates:

```
Normal operation → warning → timed disturbances → player response → objective evaluation → explicit success or failure → deterministic restart
```

---

## 2. Locked architecture decisions

### 2.1 Fixed-step ordering and effect latency

For each fixed simulation step:

```
1. Resource systems advance using modifiers already active
2. Mission subsystem observes completed authoritative resource state
3. Mission subsystem advances its own timeline
4. Mission subsystem evaluates objectives and outcome
5. Mission subsystem dispatches newly due events
6. Newly applied resource modifiers affect the NEXT fixed simulation step
```

**Key rules:**
- **One-step effect latency is intentional and locked.** Events dispatched at step $N$ apply external modifiers that take effect during resource simulation at step $N+1$. This prevents within-step feedback loops and gives deterministic, race-free semantics.
- **Do NOT rely on subscriber registration order.** In Checkpoint B, `UEdenSimulationClockSubsystem` will be enhanced with an explicit deterministic ordering/phase mechanism (e.g., explicit subscriber execution phases or explicit priority), with regression tests verifying that stepping order is strictly preserved regardless of when subscribers register.

### 2.2 Resource command names

Additive external modifiers on resource components:

- **`UEdenThermalSystemComponent::SetExternalHeatingRateDegreesCelsiusPerSecond(float)`**
- **`UEdenPowerSystemComponent::SetExternalDemandKilowatts(float)`**

**Requirements:**
- Finite, non-negative values.
- Zero (`0.0f`) clears the modifier.
- Resource components remain authoritative state owners.
- Mission code never writes private resource state directly.

### 2.3 Solar Event Emergency timeline

Explicit absolute mission times (configured in Data Asset):

- **$T = 0\,\text{s}$**: Mission begins (Nominal phase). Survival objectives activate.
- **$T = 5\,\text{s}$**: Warning phase begins.
- **$T = 10\,\text{s}$**: Impact phase begins. External heating rate increased, power generation reduced, external power demand increased.
- **$T = 30\,\text{s}$**: Recovery phase begins. External heating rate cleared, power generation restored.
- **$T = 50\,\text{s}$**: Mission resolution deadline. Survival objectives evaluate for completion.

### 2.4 Restart ownership and division

- **`UEdenMissionSubsystem::ResetMission()`** owns resetting:
  - Mission lifecycle state (`Inactive`)
  - Mission elapsed time (`0.0s`)
  - Mission phase (`Nominal`)
  - Objective runtime states (`Pending`)
  - Event execution states (`Pending`)
  - Mission-applied external modifiers (`SetExternalHeatingRateDegreesCelsiusPerSecond(0.0f)`, `SetExternalDemandKilowatts(0.0f)`)
- **`UEdenMissionSubsystem` does NOT reset:**
  - Spacecraft velocity or transform
  - Flight input intent
  - Fuel quantity
  - Battery charge
  - Temperature
  - Simulation clock accumulator / elapsed time
- **Full scenario restart is coordinated above:**
  ```
  Pause clock
  → Stop / reset mission (clears mission state + external modifiers)
  → Reset flight + resources + clock through their existing owners
  → Reload / start mission
  → Resume clock
  ```

### 2.5 Development mission trigger

- For the first playable vertical slice, use developer console commands: `StartMission`, `RestartMission`.
- Configured default mission Data Asset assigned in GameMode or project configuration.
- No arbitrary string/path asset loading.
- No auto-start on map load (the sandbox remains independently flyable).
- Production mission-selection UI remains out of scope.

---

## 3. State ownership table

| Mutable state | Authoritative owner | Readers | Command direction |
|---|---|---|---|
| Simulation time, accumulator, pause | `UEdenSimulationClockSubsystem` | All systems, mission, debug | Clock only |
| Mission lifecycle state (`EEdenMissionState`) | `UEdenMissionSubsystem` | Debug, future HUD | Mission subsystem only |
| Mission elapsed time | `UEdenMissionSubsystem` | Objectives, debug | Mission subsystem only |
| Mission phase (`EEdenMissionPhase`) | `UEdenMissionSubsystem` | Objectives, debug | Mission subsystem only |
| Objective runtime state | `UEdenMissionSubsystem` | Debug, future HUD | Mission subsystem only |
| Event execution state | `UEdenMissionSubsystem` | Debug | Mission subsystem only |
| Mission outcome | `UEdenMissionSubsystem` | Debug, future HUD | Mission subsystem only |
| Mission-applied external heating rate | `UEdenThermalSystemComponent` | Mission (via snapshot) | Mission → Thermal command |
| Mission-applied external demand | `UEdenPowerSystemComponent` | Mission (via snapshot) | Mission → Power command |
| Fuel quantity, consumption, fuel state | `UEdenFuelSystemComponent` | Mission (via snapshot), debug | Fuel component only |
| Battery charge, generation, demand, power state | `UEdenPowerSystemComponent` | Mission (via snapshot), debug | Power component only |
| Temperature, heat gen, dissipation, thermal state | `UEdenThermalSystemComponent` | Mission (via snapshot), debug | Thermal component only |
| Propulsion demand | `UEdenFlightMovementComponent` | Fuel (via interface) | Movement component only |
| Spacecraft velocity/transform | `UEdenFlightMovementComponent` | Camera, UI | Movement component only |
| Presentation / debug display | View layer | Developer | Derived from snapshots |

---

## 4. Checkpoint breakdown

### Checkpoint A — Mission core types, state machine, pure model ✅ COMPLETE

**Scope:**
- `Public/Missions/EdenMissionTypes.h` — all enums (`EEdenMissionState`, `EEdenMissionPhase`, `EEdenMissionCommandType`, `EEdenObjectiveType`, `EEdenObjectiveState`, `EEdenMissionEventState`), config structs, runtime structs, snapshot struct.
- `Public/Missions/EdenMissionModel.h` — pure production model (`FEdenMissionModel`) with static validation, lifecycle transition rules, initialization, timeline stepping, objective operations, outcome evaluation, reset, snapshot creation.
- `Private/Missions/EdenMissionModel.cpp` — pure model implementation.
- `Private/Tests/EdenMissionModelTests.cpp` — 30 unit tests under `Eden.Unit.Mission.*`.

**Evidence:**
- Build: Win64 Development Editor build passed cleanly (0 errors, 0 warnings).
- Automation: 131 tests passed (30 new `Eden.Unit.Mission.*` tests, 101 existing foundation/flight/systems/runtime tests).

---

### Checkpoint B — Mission subsystem and simulation clock deterministic integration ✅ COMPLETE

**Scope:**
- Enhance `UEdenSimulationClockSubsystem` with explicit deterministic subscriber ordering (`RegisterSimulationTickable(UObject* Subscriber, int32 Priority = 0)`). Priority 0 (Systems) strictly steps before Priority 100 (Mission). Equal priority preserves registration order via stable sort.
- `Public/Missions/EdenMissionSubsystem.h` and `Private/Missions/EdenMissionSubsystem.cpp` — `UWorldSubsystem`, implements `IEdenSimulationTickable`, lifecycle state machine (`LoadMission`, `StartMission`, `AbortMission`, `ResetMission`), clock registration, event step execution.
- `Eden.Unit.SimClock.PriorityOrdersSubscribersDeterministically` and `Eden.Unit.SimClock.EqualPriorityPreservesRegistrationOrder` clock regression tests.
- `Eden.Integration.Mission.MissionSubsystemReceivesSimulationSteps`, `Eden.Integration.Mission.MultipleSimulationStepsCatchUp`, `Eden.Integration.Mission.ClockOrdersSystemsBeforeMission`, and `Eden.Integration.Mission.LifecycleTransitionsViaSubsystem`.

**Evidence:**
- Build: Win64 Development Editor target built with 0 errors, 0 warnings.
- Automation: 137 tests passed (6 new tests in Checkpoint B, 131 existing tests, 0 failures, 0 errors).

---

### Checkpoint C — Mission definition Data Asset and validation ✅ COMPLETE

**Scope:**
- `Public/Missions/EdenMissionDefinitionDataAsset.h` and `Private/Missions/EdenMissionDefinitionDataAsset.cpp` — `UDataAsset` subclass holding `FEdenMissionDefinitionConfig MissionDefinition;`, with helper accessors (`GetMissionId`, `GetDisplayName`, `GetMissionDefinition`).
- Editor-time `IsDataValid(FDataValidationContext& Context)` override wrapping `FEdenMissionModel::ValidateDefinition`.
- Unit tests: `Eden.Unit.Mission.DataAsset.DataAssetValidatesValidConfig`, `Eden.Unit.Mission.DataAsset.DataAssetRejectsInvalidConfig`, `Eden.Unit.Mission.DataAsset.DataAssetAccessorsReturnValues`.

**Evidence:**
- Build: Win64 Development Editor target built with 0 errors, 0 warnings.
- Automation: 140 tests passed (3 new tests in Checkpoint C, 137 existing tests, 0 failures, 0 errors).

---

### Checkpoint D — External modifier APIs on resource components ✅ COMPLETE

**Scope:**
- `UEdenThermalSystemComponent::SetExternalHeatingRateDegreesCelsiusPerSecond(float)` and `ClearExternalHeatingRate()`.
- `UEdenPowerSystemComponent::SetExternalDemandKilowatts(float)` and `ClearExternalDemand()`.
- Updated `FEdenThermalModel` (`FEdenThermalStateSnapshot`, `FEdenThermalStepResult`, `MakeSnapshot`, `Step`) to integrate `ExternalHeatingRateDegreesCelsiusPerSecond` into heat step equations and snapshots.
- Updated `FEdenPowerModel` (`FEdenPowerStateSnapshot`, `FEdenPowerStepResult`, `MakeSnapshot`, `Step`) to integrate `ExternalDemandKilowatts` into net power equations and snapshots.
- Added 6 unit tests across `EdenThermalSystemTests.cpp` and `EdenPowerSystemTests.cpp`:
  - `Eden.Unit.Systems.Thermal.ExternalHeatingRateIncreasesTotalHeating`
  - `Eden.Unit.Systems.Thermal.ClearExternalHeatingRateResetsToZero`
  - `Eden.Unit.Systems.Thermal.ExternalHeatingRateSanitizesNegativeAndNaN`
  - `Eden.Unit.Systems.Power.ExternalDemandIncreasesTotalDemand`
  - `Eden.Unit.Systems.Power.ClearExternalDemandResetsToZero`
  - `Eden.Unit.Systems.Power.ExternalDemandSanitizesNegativeAndNaN`

**Evidence:**
- Build: Win64 Development Editor target built with 0 errors, 0 warnings.
- Automation: 146 tests passed (6 new tests in Checkpoint D, 140 existing tests, 0 failures, 0 errors).

---

### Checkpoint E — Mission event execution and command dispatch ✅ COMPLETE

**Scope:**
- Event execution within `UEdenMissionSubsystem::AdvanceSimulation` dispatching configured commands (`SetMissionPhase`, `SetExternalHeatingRate`, `ClearExternalHeatingRate`, `SetExternalPowerDemand`, `ClearExternalPowerDemand`, `SetPowerGeneration`, `ActivateObjective`).
- Resource component finding and command dispatch to `UEdenThermalSystemComponent` and `UEdenPowerSystemComponent`.
- Mission-applied external modifier clearing on `AbortMission` and `ResetMission`.
- Command logging via `LogEdenMission`.
- Added 3 integration tests in `EdenMissionSubsystemTests.cpp`:
  - `Eden.Integration.Mission.MissionEventCommandsReachThermal`
  - `Eden.Integration.Mission.MissionEventCommandsReachPower`
  - `Eden.Integration.Mission.MissionResetClearsExternalModifiers`

**Evidence:**
- Build: Win64 Development Editor target built with 0 errors, 0 warnings.
- Automation: 149 tests passed (3 new tests in Checkpoint E, 146 existing tests, 0 failures, 0 errors).

---

### Checkpoint F — Objective and outcome evaluation ✅ COMPLETE

**Scope:**
- Objective evaluation in `FEdenMissionModel::EvaluateObjectives` against live resource snapshots:
  - `SurviveUntilTime`: Completed when `MissionElapsedTimeSeconds >= TargetValue`.
  - `KeepTemperatureBelow`: Failed when `ThermalTemperatureCelsius >= TargetValue`.
  - `RestorePowerAbove`: Completed when `PowerBatteryChargeKilowattHours >= TargetValue`.
  - `MaintainFuelAbove`: Failed when `FuelQuantityKilograms <= TargetValue`.
- Live component queries in `UEdenMissionSubsystem::AdvanceSimulation` across `UEdenThermalSystemComponent`, `UEdenPowerSystemComponent`, and `UEdenFuelSystemComponent`.
- Outcome resolution in `FEdenMissionModel::EvaluateOutcome` transitioning mission state to `Succeeded` or `Failed` with state change delegate broadcasts.
- Added 4 unit tests in `EdenMissionModelTests.cpp`:
  - `Eden.Unit.Mission.Objective.EvaluateObjectivesSurviveUntilTime`
  - `Eden.Unit.Mission.Objective.EvaluateObjectivesKeepTemperatureBelow`
  - `Eden.Unit.Mission.Objective.EvaluateObjectivesRestorePowerAbove`
  - `Eden.Unit.Mission.Objective.EvaluateObjectivesMaintainFuelAbove`
- Added 3 integration tests in `EdenMissionSubsystemTests.cpp`:
  - `Eden.Integration.Mission.DeterministicSuccessScenario`
  - `Eden.Integration.Mission.DeterministicFailureScenario`
  - `Eden.Integration.Mission.ResourceStateChangeTriggersObjectiveFailure`

**Evidence:**
- Build: Win64 Development Editor target built with 0 errors, 0 warnings.
- Automation: 156 tests passed (7 new tests in Checkpoint F, 149 existing tests, 0 failures, 0 errors).

---

### Checkpoint G — Solar Event Emergency Data Asset and runtime integration

**Scope:**
- `Content/Eden/Data/Missions/DA_SolarEventEmergency.uasset`.
- Full end-to-end integration tests, restart tests, abort tests.
- Runtime composition tests.

---

### Checkpoint H — Debug visibility, console triggers, and manual PIE closeout

**Scope:**
- `ShowDebug EdenMission` implementation.
- Extended `ShowDebug EdenSystems` with mission summary.
- Developer console commands: `StartMission`, `RestartMission`.
- VerifyMissionAssets.py editor script.
- Manual PIE verification gate.
- Final documentation updates (ARCHITECTURE.md, REMEMBER.md, RECOVER.md).
