# Spacecraft Resource Simulation

## Status
Draft — awaiting review and explicit approval

Do not begin Checkpoint A, write resource simulation code, create resource Data Assets, or modify flight ownership boundaries until this revised plan is reviewed and explicitly approved.

## Problem and outcome

The verified six-axis flight shell lets the player fly a spacecraft, but the pawn has no concept of fuel consumption, electrical power, or thermal state. The PROJECT_SPEC requires "fuel, power, and thermal state" for the first vertical slice, and ARCHITECTURE.md defines authoritative component owners for each. Without resource simulation, there is no consumable cost to flight, no system state to monitor, no foundation for mission failures, and no data for telemetry.

This milestone introduces:

- **`UEdenSimulationClockSubsystem`** — a world-scoped subsystem that owns fixed-step simulation time and advances resource subscribers independently of the render frame rate.
- **`UEdenFuelSystemComponent`** — an actor component that owns fuel quantity, validates data-driven configuration, applies consumption, and produces explicit fuel state transitions.
- **`UEdenPowerSystemComponent`** — an actor component that owns generation rate, battery storage, demand, and availability, resolves a deterministic power budget, and produces power state transitions.
- **`UEdenThermalSystemComponent`** — an actor component that owns temperature state, applies a simple linear heat and dissipation model, and produces thermal state transitions.
- **`IEdenPropulsionDemandSource`** — a narrow C++ interface that lets fuel read normalized propulsion demand without coupling to the pawn or owning flight state.
- **Data-driven configuration** through per-system `UDataAsset` subclasses that hold capacities, rates, and thresholds. Data Assets contain configuration only, never runtime state.
- **Automated unit and integration tests** for resource calculations, state transitions, clock behavior, and cross-system integration.
- **Development-only debug visibility** locked to `ShowDebug EdenSystems`.
- **Project-specific logging** via existing `LogEdenSystems` and new `LogEdenSimClock`.

The outcome is a simulation layer where every resource has one authoritative owner, every transition is observable and logged, every calculation is deterministic and testable, and the system is completely independent from the render frame rate.

## Scope

### In scope

- `UEdenSimulationClockSubsystem` with fixed-step accumulator, configurable step size, bounded catch-up, overrun logging, pause, resume, and reset.
- `UEdenFuelSystemComponent` with data-driven capacity, consumption rate, warning/critical/depleted/recovered transitions, and reset.
- `UEdenPowerSystemComponent` with data-driven generation, storage capacity, demand tracking, budget resolution, warning/critical/depleted/recovered transitions, and reset.
- `UEdenThermalSystemComponent` with a simple linear thermal model, data-driven heat generation and dissipation rates in explicit `DegreesCelsiusPerSecond` units, warning/critical/overheated transitions, and reset.
- Per-system `UDataAsset` subclasses for capacities, rates, and thresholds.
- Separate state enums: `EEdenFuelState`, `EEdenPowerState`, and `EEdenThermalState`.
- Clock-driven fixed-step advance of all resource systems.
- C++ default subobject creation of Fuel, Power, and Thermal components on `AEdenSpacecraftPawn`.
- Blueprint assignment of configuration Data Assets and tuning only.
- Fuel consumption driven by normalized propulsion demand through `IEdenPropulsionDemandSource`, implemented by `UEdenFlightMovementComponent`.
- New `LogEdenSimClock` log category (`LogEdenSystems` already exists).
- Development-only debug visibility locked to `ShowDebug EdenSystems`.
- Unit tests for resource calculations, state transitions, edge cases, boundary conditions, NaN/Inf handling, and reset behavior.
- Integration tests for clock advancing multiple resource systems and fuel consumption from propulsion demand.
- Manual Unreal Editor verification for component composition, data asset assignment, PIE behavior, and debug visibility.
- Updated documentation: ARCHITECTURE.md state ownership table, RECOVER.md, REMEMBER.md.

### Out of scope

- Mission objectives or failure scenarios
- Docking gameplay
- Oxygen and life support
- Production HUD
- Telemetry transport
- EDEN OS integration
- Multiplayer
- Persistence
- Full power load management with consumer priority queues
- Radiator or active cooling mechanisms beyond the linear dissipation model
- Exponential cooling or more advanced thermal physics
- Fuel types or fuel transfer between tanks
- Shared `UEdenResourceComponentBase`
- Shared `EEdenResourceState`
- Pawn-owned `LastThrustFraction` or `GetCurrentThrustFraction()` API
- Direct coupling from `UEdenFuelSystemComponent` to `AEdenSpacecraftPawn` for propulsion demand

## Current repository baseline

| Field | Value |
|---|---|
| Branch for this work | `feature/spacecraft-resource-simulation` |
| Clean flight-shell tag | `v0.1.0-flight-shell` |
| Tagged commit | `ed7fb55` (`first flight`) |
| Previous active ExecPlan | `docs/exec-plans/0002-six-axis-flight.md` (verified complete) |
| Active ExecPlan for this milestone | `docs/exec-plans/0003-spacecraft-resource-simulation.md` (Draft) |
| Working-tree gate | Working tree must be clean before Checkpoint A begins |
| Systems directories | `Source/EdenSpaceSimulator/Public/Systems/` and `Private/Systems/` do not exist yet |
| Existing subsystems | None implemented yet |
| Existing log categories | `LogEden`, `LogEdenFlight`, `LogEdenSystems`, `LogEdenMission`, `LogEdenTelemetry` |
| Module dependencies | `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput` |
| C++ standard | C++20, IWYU enforced |
| Test patterns | `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, `Eden.Unit.*` / future `Eden.Integration.*` |
| Existing flight surface | `ApplyFlightInputCommand()`, `ResetFlightState()`, `GetFlightMovementComponent()`, `GetRequiredCollisionRoot()` |
| Movement surface | `MoveWithCommand()`, `ResetFlightMovement()`, angular velocity accessors, `MovementSettings` |
| Pawn tick / replication | `PrimaryActorTick.bCanEverTick = false`, `bReplicates = false` |

> [!IMPORTANT]
> Checkpoint A must not start until this Draft plan is explicitly approved **and** `git status` shows a clean working tree on `feature/spacecraft-resource-simulation`.

## Locked review decisions

These decisions supersede earlier draft text in this plan.

1. **Repository sequencing**
   - Commit and verify the six-axis flight shell before resource implementation.
   - Tag the verified flight shell as `v0.1.0-flight-shell`.
   - Implement resources on `feature/spacecraft-resource-simulation`.
   - Require a clean working tree before Checkpoint A.

2. **Propulsion demand ownership**
   - Do not couple `UEdenFuelSystemComponent` directly to `AEdenSpacecraftPawn`.
   - Add `IEdenPropulsionDemandSource`.
   - `UEdenFlightMovementComponent` implements `GetPropulsionDemandNormalized()`.
   - `UEdenFuelSystemComponent` stores a weak, non-owning reference to that interface.
   - Flight owns propulsion demand.
   - Fuel owns quantity and consumption.
   - Do **not** add `LastThrustFraction` or `GetCurrentThrustFraction()` on the pawn.

3. **Resource component composition**
   - Create `FuelSystem`, `PowerSystem`, and `ThermalSystem` as C++ default subobjects on `AEdenSpacecraftPawn`.
   - `BP_EdenSpacecraftPawn` assigns configuration Data Assets and tuning only.
   - Blueprint must not create, replace, or own authoritative resource components.

4. **No shared resource component base**
   - Do not add `UEdenResourceComponentBase`.
   - Keep readable duplication until a genuine stable abstraction emerges and is recorded in an ADR.

5. **Thermal model for the first vertical slice**
   - Use the simple linear thermal model.
   - Heat generation raises temperature.
   - Dissipation moves temperature toward ambient and cannot cross ambient.
   - Use explicit `DegreesCelsiusPerSecond` units.
   - Defer exponential cooling.

6. **Debug visibility**
   - Lock debug visibility to `ShowDebug EdenSystems`.

7. **Per-system state enums**
   - Replace shared `EEdenResourceState` with:
     - `EEdenFuelState`
     - `EEdenPowerState`
     - `EEdenThermalState`

8. **Event surface**
   - Use one `OnStateChanged(Previous, Current)` delegate per system.
   - Add terminal events such as `FuelDepleted`, `PowerDepleted`, and `ThermalOverheated` where useful.

## Architecture alignment

This plan follows `docs/ARCHITECTURE.md`, `ADR-0001`, and all guardrails in `AGENTS.md`.

### Dependency direction

```text
Presentation (future HUD, ShowDebug EdenSystems)
        |
        v
Application / Orchestration (Mission, future)
        |
        v
Simulation Domain (Clock, Fuel, Power, Thermal, Flight)
        |
        v
Unreal Engine primitives (UWorldSubsystem, UActorComponent, UDataAsset)
```

Resource systems depend downward only. No resource component depends on UI widgets, mission logic, telemetry transport, or EDEN OS. Fuel may read propulsion demand through `IEdenPropulsionDemandSource` only. Flight code must not mutate fuel internals.

### State ownership table

| State | Authoritative owner | Readers | Writers |
|---|---|---|---|
| Simulation elapsed time (seconds) | `UEdenSimulationClockSubsystem` | Resource systems, future mission, telemetry | Clock subsystem only |
| Fixed-step accumulator (seconds) | `UEdenSimulationClockSubsystem` | Internal only | Clock subsystem only |
| Pause state | `UEdenSimulationClockSubsystem` | Subscribers, UI | Clock subsystem only |
| Normalized propulsion demand `[0,1]` | `UEdenFlightMovementComponent` via `IEdenPropulsionDemandSource` | Fuel system | Flight movement only |
| Fuel quantity (kilograms) | `UEdenFuelSystemComponent` | Pawn query surface, mission, UI, telemetry | Fuel component only |
| Fuel state (`EEdenFuelState`) | `UEdenFuelSystemComponent` | Mission, UI, telemetry | Fuel component only |
| Fuel configuration | `UEdenFuelConfigDataAsset` | Fuel component | Content author only |
| Power generation rate (kilowatts) | `UEdenPowerSystemComponent` | Mission, UI, telemetry | Power component only |
| Battery charge (kilowatt-hours) | `UEdenPowerSystemComponent` | Mission, UI, telemetry | Power component only |
| Power demand (kilowatts) | `UEdenPowerSystemComponent` | Mission, UI, telemetry | Power component only |
| Power state (`EEdenPowerState`) | `UEdenPowerSystemComponent` | Mission, UI, telemetry | Power component only |
| Power configuration | `UEdenPowerConfigDataAsset` | Power component | Content author only |
| Temperature (Celsius) | `UEdenThermalSystemComponent` | Mission, UI, telemetry | Thermal component only |
| Thermal state (`EEdenThermalState`) | `UEdenThermalSystemComponent` | Mission, UI, telemetry | Thermal component only |
| Thermal configuration | `UEdenThermalConfigDataAsset` | Thermal component | Content author only |

### Commands, events, and snapshots

**Commands** (requests to change state):

```text
ConsumeFuel(AmountKilograms)
SetFuelQuantityKilograms(Value)          // reset/debug only
SetPowerGenerationEnabled(bEnabled)
SetBatteryChargeKilowattHours(Value)     // reset/debug only
SetHeatSourceActive(bActive)
SetTemperatureCelsius(Value)             // reset/debug only
ResetResourceState()
PauseSimulation() / ResumeSimulation()
ResetSimulationClock()
```

**Events** (facts that already occurred — multicast delegates):

```text
// One state-change delegate per system
OnFuelStateChanged(EEdenFuelState Previous, EEdenFuelState Current)
OnPowerStateChanged(EEdenPowerState Previous, EEdenPowerState Current)
OnThermalStateChanged(EEdenThermalState Previous, EEdenThermalState Current)

// Terminal / high-signal events where useful
OnFuelDepleted
OnPowerDepleted
OnThermalOverheated

// Clock
OnSimulationClockOverrun(int32 DroppedSteps)
```

Do not create separate entered/exited delegates for every warning and critical transition. `OnStateChanged` is the primary transition surface; terminal events cover irreversible or operator-critical outcomes that are useful without inspecting the full state pair.

**Snapshots** (future, not in this milestone): resource systems will expose read-only query methods returning values with explicit units. Full snapshot assembly is deferred to the telemetry milestone.

## Alternatives and tradeoffs

### Alternative 1: Single monolithic resource manager

Rejected. Violates single responsibility, makes testing harder, and creates a high-blast-radius class.

### Alternative 2: Separate components per resource (selected)

Selected. Matches ADR-0001 and the flight component model. Resource components are created as C++ default subobjects on `AEdenSpacecraftPawn`; Blueprint assigns Data Assets and tuning only.

### Alternative 3: Shared base class `UEdenResourceComponentBase`

**Rejected for this milestone.** Per AGENTS.md DRY guidance and review decision 4, keep readable duplication until a genuine stable abstraction emerges and is recorded in an ADR.

### Alternative 4: Simulation clock as component vs subsystem

**Selected: `UWorldSubsystem`.** Simulation time has one authoritative owner per world for the first vertical slice.

### Alternative 5: Per-system data assets vs unified config asset

**Selected: Per-system data assets.** Matches component ownership and keeps blast radius small.

### Alternative 6: Subscriber registration mechanism

**Selected: `TArray` of `IEdenSimulationTickable*` with explicit register/unregister.** Unregister in `EndPlay`.

### Alternative 7: Fuel reads thrust through the pawn

Rejected by review decision 2. That approach would introduce pawn mediation and encourage `LastThrustFraction` / `GetCurrentThrustFraction()` ownership confusion.

### Alternative 8: Fuel holds a weak reference to `IEdenPropulsionDemandSource` (selected)

Selected. Flight owns demand. Fuel owns quantity and consumption. The interface keeps the dependency narrow and non-owning.

## Update-loop design

### Fixed-step accumulator

```text
UEdenSimulationClockSubsystem::Tick(float DeltaTime)
    |
    if (bPaused) return
    |
    if (!IsValidDeltaTime(DeltaTime)) return
    |
    Accumulator += DeltaTime
    StepCount = 0
    |
    while (Accumulator >= FixedStepSeconds && StepCount < MaxCatchUpSteps)
        |
        for each registered IEdenSimulationTickable:
            Subscriber->AdvanceSimulation(FixedStepSeconds)
        Accumulator -= FixedStepSeconds
        ElapsedSimulationTimeSeconds += FixedStepSeconds
        StepCount++
    |
    if (Accumulator >= FixedStepSeconds)
        DroppedSteps = FMath::FloorToInt32(Accumulator / FixedStepSeconds)
        Accumulator = FMath::Fmod(Accumulator, FixedStepSeconds)
        Log overrun warning with DroppedSteps (LogEdenSimClock)
        Broadcast OnSimulationClockOverrun(DroppedSteps)
```

### Parameters

| Parameter | Default | Rationale |
|---|---|---|
| `FixedStepSeconds` | 0.1 (10 Hz) | Per PROJECT_SPEC initial target |
| `MaxCatchUpSteps` | 4 | Bounds worst-case to 0.4s of simulation per frame |

### DeltaTime validation

Reject zero, negative, NaN, and infinite values. The clock does not advance on invalid DeltaTime and logs a bounded warning.

### Subscriber interface

```cpp
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEdenSimulationTickable : public UInterface
{
	GENERATED_BODY()
};

class IEdenSimulationTickable
{
	GENERATED_BODY()

public:
	virtual void AdvanceSimulation(float FixedDeltaSeconds) = 0;
};
```

Resource components implement `IEdenSimulationTickable` and register with the clock during `BeginPlay`, unregister during `EndPlay`.

## Resource system design

### Propulsion demand contract

```cpp
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEdenPropulsionDemandSource : public UInterface
{
	GENERATED_BODY()
};

class IEdenPropulsionDemandSource
{
	GENERATED_BODY()

public:
	virtual float GetPropulsionDemandNormalized() const = 0;
};
```

- `UEdenFlightMovementComponent` implements `IEdenPropulsionDemandSource`.
- `GetPropulsionDemandNormalized()` returns a clamped `[0,1]` scalar derived from current flight propulsion intent / command magnitude owned by flight.
- `UEdenFuelSystemComponent` stores a `TWeakInterfacePtr<IEdenPropulsionDemandSource>` or equivalent weak, non-owning reference.
- Fuel resolves the demand source during initialization from the flight movement component on the same pawn without treating the pawn as the demand owner.
- Missing or expired demand source yields zero consumption and an actionable log, not a crash.

### Fuel system

**State**: `FuelQuantityKilograms` (float, >= 0), `EEdenFuelState`.

**Fixed-step behavior**:

1. Query `GetPropulsionDemandNormalized()` from the weak demand-source reference.
2. Subtract `ConsumptionRateKilogramsPerSecond * PropulsionDemandNormalized * FixedDeltaSeconds`.
3. Clamp to `[0, CapacityKilograms]`.
4. Evaluate thresholds and transition state if changed.
5. Broadcast `OnFuelStateChanged(Previous, Current)` on change; broadcast `OnFuelDepleted` when entering depleted.

Flight code never calls `ConsumeFuel()` as part of normal movement.

### Power system

**State**: `BatteryChargeKilowattHours` (float, >= 0), `GenerationRateKilowatts`, `DemandKilowatts`, `EEdenPowerState`.

**Fixed-step behavior**:

1. `NetPowerKilowatts = GenerationRateKilowatts - DemandKilowatts`.
2. `BatteryChargeKilowattHours += NetPowerKilowatts * (FixedDeltaSeconds / 3600.0)`.
3. Clamp to `[0, BatteryCapacityKilowattHours]`.
4. Evaluate thresholds and transition state if changed.
5. Broadcast `OnPowerStateChanged(Previous, Current)` on change; broadcast `OnPowerDepleted` when entering depleted.

**Demand model for this milestone**: configurable baseline demand from the data asset. Full consumer priority queues remain out of scope.

### Thermal system

**State**: `TemperatureCelsius` (float), `EEdenThermalState`.

**Locked linear model**:

1. Apply heat generation:
   `TemperatureCelsius += HeatGenerationRateDegreesCelsiusPerSecond * FixedDeltaSeconds`.
2. Apply dissipation toward ambient using `DissipationRateDegreesCelsiusPerSecond * FixedDeltaSeconds`.
3. Dissipation may move temperature toward ambient but must not cross ambient in a single application.
4. Clamp to configured absolute min/max bounds after generation and dissipation.
5. Evaluate thresholds and transition state if changed.
6. Broadcast `OnThermalStateChanged(Previous, Current)` on change; broadcast `OnThermalOverheated` when entering overheated.

Exponential cooling is deferred.

### Per-system state enums

```cpp
UENUM(BlueprintType)
enum class EEdenFuelState : uint8
{
	Normal,
	Warning,
	Critical,
	Depleted
};

UENUM(BlueprintType)
enum class EEdenPowerState : uint8
{
	Normal,
	Warning,
	Critical,
	Depleted
};

UENUM(BlueprintType)
enum class EEdenThermalState : uint8
{
	Normal,
	Warning,
	Critical,
	Overheated
};
```

Do not introduce a shared `EEdenResourceState`.

Transition rules:

- Fuel/power warning and critical thresholds are capacity fractions.
- Thermal warning/critical/overheated thresholds are absolute Celsius values.
- Transitions are evaluated once per fixed step, not per frame.
- State changes fire `OnStateChanged(Previous, Current)` and log via `LogEdenSystems`.
- Logs include component name, actor name, previous state, new state, and current value with units.
- No per-frame logging.

### Component composition on the pawn

`AEdenSpacecraftPawn` creates these C++ default subobjects:

- `FuelSystem` → `UEdenFuelSystemComponent`
- `PowerSystem` → `UEdenPowerSystemComponent`
- `ThermalSystem` → `UEdenThermalSystemComponent`

Rules:

- C++ owns creation and authoritative runtime behavior.
- `BP_EdenSpacecraftPawn` assigns `UEdenFuelConfigDataAsset`, `UEdenPowerConfigDataAsset`, `UEdenThermalConfigDataAsset`, and tuning only.
- Blueprint must not create, replace, or own authoritative resource components.
- Existing flight movement and required sphere collision root ownership remain unchanged.

### Data asset configuration

**`UEdenFuelConfigDataAsset`**:

| Field | Type | Constraint | Default |
|---|---|---|---|
| `CapacityKilograms` | float | > 0 | 100.0 |
| `ConsumptionRateKilogramsPerSecond` | float | >= 0 | 1.0 |
| `WarningThresholdFraction` | float | (0, 1), > CriticalThresholdFraction | 0.25 |
| `CriticalThresholdFraction` | float | (0, 1), < WarningThresholdFraction | 0.10 |

**`UEdenPowerConfigDataAsset`**:

| Field | Type | Constraint | Default |
|---|---|---|---|
| `BatteryCapacityKilowattHours` | float | > 0 | 10.0 |
| `GenerationRateKilowatts` | float | >= 0 | 2.0 |
| `BaselineDemandKilowatts` | float | >= 0 | 1.5 |
| `WarningThresholdFraction` | float | (0, 1), > CriticalThresholdFraction | 0.25 |
| `CriticalThresholdFraction` | float | (0, 1), < WarningThresholdFraction | 0.10 |

**`UEdenThermalConfigDataAsset`**:

| Field | Type | Constraint | Default |
|---|---|---|---|
| `AmbientTemperatureCelsius` | float | — | 20.0 |
| `HeatGenerationRateDegreesCelsiusPerSecond` | float | >= 0 | 0.5 |
| `DissipationRateDegreesCelsiusPerSecond` | float | >= 0 | 0.3 |
| `WarningThresholdCelsius` | float | < CriticalThresholdCelsius | 60.0 |
| `CriticalThresholdCelsius` | float | > WarningThresholdCelsius | 80.0 |
| `OverheatThresholdCelsius` | float | >= CriticalThresholdCelsius | 100.0 |
| `AbsoluteMinTemperatureCelsius` | float | — | -273.15 |
| `AbsoluteMaxTemperatureCelsius` | float | > AbsoluteMinTemperatureCelsius | 150.0 |

**Validation**: Each component validates its data asset during `BeginPlay`. Invalid configuration produces an actionable `UE_LOG` and enters a safe failure state where that system does not advance simulation.

## Failure modes

| Failure | Required behavior |
|---|---|
| Missing data asset reference | Log error with component, actor, and expected asset type. Do not advance that system. |
| Zero or negative capacity | Reject during validation. Log field name, value, and asset name. |
| Invalid threshold ordering | Reject. Log both values and asset context. |
| NaN or Inf in resource value | Detect at start of fixed step. Log context. Clamp to safe value. |
| Fixed-step overrun | Drop excess steps, log via `LogEdenSimClock`, broadcast overrun. |
| Fuel reaches zero | Transition to Depleted. Fire `OnFuelStateChanged` and `OnFuelDepleted`. |
| Power reaches zero | Transition to Depleted. Fire `OnPowerStateChanged` and `OnPowerDepleted`. |
| Temperature reaches overheat threshold | Transition to Overheated. Fire `OnThermalStateChanged` and `OnThermalOverheated`. |
| Missing or expired propulsion demand source | Fuel logs warning and consumes zero. Other systems continue. |
| Clock subsystem unavailable | Resource components log error in `BeginPlay` and remain inert. |
| Invalid DeltaTime | Clock rejects and does not advance. Log bounded warning. |
| Blueprint attempts to replace authoritative resource components | Treat as authoring error. C++ default subobjects remain the owners. |

## Performance considerations

- Fixed-step at 10 Hz keeps resource update cost negligible.
- No per-frame allocation in the fixed-step path.
- No per-frame logging.
- Delegates fire only on state transitions and terminal events.
- `MaxCatchUpSteps = 4` bounds hitch catch-up.
- `ShowDebug EdenSystems` is development-only and must not add shipping overhead or Output Log spam.

## Test matrix

### Unit tests (`Eden.Unit.SimClock.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Unit.SimClock.FixedStepAdvancesSubscribers` | Subscribers receive FixedDeltaSeconds |
| `Eden.Unit.SimClock.AccumulatorHandlesPartialFrames` | Steps fire only when accumulator >= step size |
| `Eden.Unit.SimClock.CatchUpBounded` | MaxCatchUpSteps limits steps per frame |
| `Eden.Unit.SimClock.OverrunDropsExcessAndLogs` | Excess accumulator is discarded after max steps |
| `Eden.Unit.SimClock.PausePreventsAdvance` | Paused clock does not advance subscribers |
| `Eden.Unit.SimClock.ResetClearsAccumulatorAndTime` | Reset zeroes elapsed time and accumulator |
| `Eden.Unit.SimClock.InvalidDeltaTimeRejected` | Zero, negative, NaN, Inf rejected |
| `Eden.Unit.SimClock.ElapsedTimeAccumulatesCorrectly` | Elapsed time equals total fixed steps × step size |
| `Eden.Unit.SimClock.EquivalentTimeMatchesAcrossPartitions` | Different DeltaTime splits produce same elapsed time and step count |

### Unit tests (`Eden.Unit.Systems.Fuel.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Unit.Systems.Fuel.ConsumptionScalesWithPropulsionDemand` | Fuel decreases by rate × demand × time |
| `Eden.Unit.Systems.Fuel.ZeroDemandConsumesNothing` | Missing/zero demand yields no consumption |
| `Eden.Unit.Systems.Fuel.QuantityClampsAtZero` | Cannot go negative |
| `Eden.Unit.Systems.Fuel.QuantityClampsAtCapacity` | Cannot exceed capacity |
| `Eden.Unit.Systems.Fuel.WarningTransitionAtThreshold` | Normal → Warning |
| `Eden.Unit.Systems.Fuel.CriticalTransitionAtThreshold` | Warning → Critical |
| `Eden.Unit.Systems.Fuel.DepletedTransitionAtZero` | Critical → Depleted and terminal event |
| `Eden.Unit.Systems.Fuel.RecoveryTransitionAboveThreshold` | Recovery restores state through `OnFuelStateChanged` |
| `Eden.Unit.Systems.Fuel.ResetRestoresFullCapacity` | Reset returns to full and Normal |
| `Eden.Unit.Systems.Fuel.NaNAndInfRejected` | Invalid values detected and clamped |
| `Eden.Unit.Systems.Fuel.ZeroCapacityRejected` | Config validation rejects zero capacity |

### Unit tests (`Eden.Unit.Systems.Power.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Unit.Systems.Power.NetPositiveBudgetChargesBattery` | Generation > demand charges battery |
| `Eden.Unit.Systems.Power.NetNegativeBudgetDrainsBattery` | Demand > generation drains battery |
| `Eden.Unit.Systems.Power.BatteryClampsAtCapacity` | Cannot overcharge |
| `Eden.Unit.Systems.Power.BatteryClampsAtZero` | Cannot go negative |
| `Eden.Unit.Systems.Power.WarningTransition` | Threshold transition fires |
| `Eden.Unit.Systems.Power.CriticalTransition` | Threshold transition fires |
| `Eden.Unit.Systems.Power.DepletedTransition` | Zero charge fires depleted and terminal event |
| `Eden.Unit.Systems.Power.RecoveryTransition` | Recovery restores state |
| `Eden.Unit.Systems.Power.ResetRestoresFullCharge` | Reset returns to full and Normal |
| `Eden.Unit.Systems.Power.NaNAndInfRejected` | Invalid values detected and clamped |

### Unit tests (`Eden.Unit.Systems.Thermal.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Unit.Systems.Thermal.HeatGenerationIncreasesTemperature` | Temperature rises with generation |
| `Eden.Unit.Systems.Thermal.DissipationMovesTowardAmbient` | Dissipation moves toward ambient |
| `Eden.Unit.Systems.Thermal.DissipationDoesNotCrossAmbient` | Dissipation cannot cross ambient |
| `Eden.Unit.Systems.Thermal.TemperatureClampsAtBounds` | Min/max bounds enforced |
| `Eden.Unit.Systems.Thermal.WarningTransition` | Warning threshold fires |
| `Eden.Unit.Systems.Thermal.CriticalTransition` | Critical threshold fires |
| `Eden.Unit.Systems.Thermal.OverheatedTransition` | Overheat threshold fires terminal event |
| `Eden.Unit.Systems.Thermal.RecoveryTransition` | Cooling below threshold recovers |
| `Eden.Unit.Systems.Thermal.ResetRestoresAmbient` | Reset returns to ambient and Normal |
| `Eden.Unit.Systems.Thermal.NaNAndInfRejected` | Invalid values detected and clamped |

### Integration tests (`Eden.Integration.Systems.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Integration.Systems.ClockAdvancesAllResources` | Clock drives fuel, power, thermal simultaneously |
| `Eden.Integration.Systems.FuelDepletionFromPropulsionDemand` | Sustained propulsion demand depletes fuel through the clock |
| `Eden.Integration.Systems.ResourceResetClearsAllState` | Full reset restores all systems |
| `Eden.Integration.Systems.MissingDataAssetHandledGracefully` | Missing config does not crash; system remains inert |

### Manual editor verification

```text
1. Open EdenSpaceSimulator.uproject in Unreal Engine 5.8.
2. Confirm project loads into L_FlightSandbox.
3. Open BP_EdenSpacecraftPawn.
4. Confirm FuelSystem, PowerSystem, and ThermalSystem exist as C++ default subobjects.
5. Confirm Blueprint has not created duplicate authoritative resource components.
6. Assign DA_TestFuelConfig, DA_TestPowerConfig, and DA_TestThermalConfig.
7. Press Play.
8. Fly with thrust and confirm fuel decreases via ShowDebug EdenSystems.
9. Confirm fuel warning/critical/depleted transitions appear in Output Log through OnFuelStateChanged / OnFuelDepleted.
10. Confirm power and thermal values change over fixed-step time.
11. Confirm ShowDebug EdenSystems is the supported debug visibility path.
12. Stop Play, start Play again, and confirm resource state resets.
13. Confirm Output Log has no LogTemp and no per-frame spam.
14. Review source control for unintended changes.
```

## Source layout

```text
Source/EdenSpaceSimulator/
+-- Public/
|   +-- Core/
|   |   +-- EdenLogCategories.h                 [MODIFY] add LogEdenSimClock
|   |   +-- EdenSimulationTickable.h            [NEW]
|   |   +-- EdenSimulationClockSubsystem.h      [NEW]
|   |   +-- EdenPropulsionDemandSource.h        [NEW] IEdenPropulsionDemandSource
|   +-- Flight/
|   |   +-- EdenFlightMovementComponent.h       [MODIFY] implement IEdenPropulsionDemandSource
|   |   +-- EdenSpacecraftPawn.h                [MODIFY] create Fuel/Power/Thermal default subobjects
|   +-- Systems/                                [NEW directory]
|       +-- EdenFuelTypes.h                     [NEW] EEdenFuelState
|       +-- EdenPowerTypes.h                    [NEW] EEdenPowerState
|       +-- EdenThermalTypes.h                  [NEW] EEdenThermalState
|       +-- EdenFuelSystemComponent.h           [NEW]
|       +-- EdenPowerSystemComponent.h          [NEW]
|       +-- EdenThermalSystemComponent.h        [NEW]
|       +-- EdenFuelConfigDataAsset.h           [NEW]
|       +-- EdenPowerConfigDataAsset.h          [NEW]
|       +-- EdenThermalConfigDataAsset.h        [NEW]
+-- Private/
    +-- Core/
    |   +-- EdenLogCategories.cpp               [MODIFY]
    |   +-- EdenSimulationClockSubsystem.cpp    [NEW]
    +-- Flight/
    |   +-- EdenFlightMovementComponent.cpp     [MODIFY] GetPropulsionDemandNormalized()
    |   +-- EdenSpacecraftPawn.cpp              [MODIFY] create default subobjects; no thrust-fraction API
    +-- Systems/                                [NEW directory]
    |   +-- EdenFuelSystemComponent.cpp         [NEW]
    |   +-- EdenPowerSystemComponent.cpp        [NEW]
    |   +-- EdenThermalSystemComponent.cpp      [NEW]
    |   +-- EdenFuelConfigDataAsset.cpp         [NEW]
    |   +-- EdenPowerConfigDataAsset.cpp        [NEW]
    |   +-- EdenThermalConfigDataAsset.cpp      [NEW]
    +-- Tests/
        +-- EdenSimClockTests.cpp               [NEW]
        +-- EdenFuelSystemTests.cpp             [NEW]
        +-- EdenPowerSystemTests.cpp            [NEW]
        +-- EdenThermalSystemTests.cpp          [NEW]
        +-- EdenResourceIntegrationTests.cpp    [NEW]
```

### Modifications to existing files

| File | Change | Rationale |
|---|---|---|
| `EdenLogCategories.h/.cpp` | Add `LogEdenSimClock` | Clock-specific logging |
| `EdenFlightMovementComponent.h/.cpp` | Implement `IEdenPropulsionDemandSource` and `GetPropulsionDemandNormalized()` | Flight owns propulsion demand |
| `EdenSpacecraftPawn.h/.cpp` | Create Fuel/Power/Thermal C++ default subobjects and expose getters | C++ owns authoritative resource component creation |

Explicitly **not** modifying the pawn to add `LastThrustFraction` or `GetCurrentThrustFraction()`.

No `EdenSpaceSimulator.Build.cs` changes are expected.

## Implementation checkpoints

Each checkpoint must build and pass tests before continuing. Checkpoint A requires an approved plan and a clean working tree.

### Checkpoint A: Simulation clock subsystem and tests

1. Confirm working tree is clean.
2. Add `LogEdenSimClock`.
3. Add `IEdenSimulationTickable`.
4. Add `UEdenSimulationClockSubsystem`.
5. Add `Eden.Unit.SimClock.*` tests.
6. Build and run tests.

**Exit criteria**: Clock builds; all `Eden.Unit.SimClock.*` tests pass; existing flight and foundation tests still pass.

### Checkpoint B: Fuel types, demand interface, and fuel system

1. Add `IEdenPropulsionDemandSource`.
2. Add `EEdenFuelState` and `UEdenFuelConfigDataAsset`.
3. Add `UEdenFuelSystemComponent` with weak demand-source reference and `OnFuelStateChanged` / `OnFuelDepleted`.
4. Add `Eden.Unit.Systems.Fuel.*` tests, including zero-demand and propulsion-scaled consumption.
5. Build and run all `Eden` tests.

**Exit criteria**: Fuel system builds and passes tests without pawn thrust-fraction APIs.

### Checkpoint C: Power and thermal systems

1. Add `EEdenPowerState`, power config/component, and `OnPowerStateChanged` / `OnPowerDepleted`.
2. Add `EEdenThermalState`, thermal config/component, linear thermal model with ambient non-crossing dissipation, and `OnThermalStateChanged` / `OnThermalOverheated`.
3. Add power and thermal unit tests, including `DissipationDoesNotCrossAmbient`.
4. Build and run all `Eden` tests.

**Exit criteria**: All three resource systems build and pass unit tests.

### Checkpoint D: Pawn default subobjects and flight demand wiring

1. Create Fuel/Power/Thermal C++ default subobjects on `AEdenSpacecraftPawn`.
2. Implement `GetPropulsionDemandNormalized()` on `UEdenFlightMovementComponent`.
3. Wire fuel's weak demand-source reference to the flight movement component.
4. Register/unregister resource components with the simulation clock.
5. Add `Eden.Integration.Systems.*` tests.
6. Build and run all `Eden` tests.

**Exit criteria**: Resource components advance through the clock. Fuel consumption scales with propulsion demand. No pawn `GetCurrentThrustFraction` API exists.

### Checkpoint E: Debug visibility

1. Implement development-only `ShowDebug EdenSystems`.
2. Do not add alternate project debug console commands as the supported path.
3. Guard against shipping overhead and Output Log spam.
4. Build and run all `Eden` tests.

**Exit criteria**: `ShowDebug EdenSystems` works in development builds and is the locked debug visibility path.

### Checkpoint F: Blueprint Data Asset assignment

1. Create `DA_TestFuelConfig`, `DA_TestPowerConfig`, and `DA_TestThermalConfig` under `Content/Eden/Config/`.
2. Assign those assets on `BP_EdenSpacecraftPawn` only; do not create Blueprint-owned authoritative resource components.
3. PIE-verify fuel/power/thermal behavior and `ShowDebug EdenSystems`.
4. PIE stop/start reset verification.

**Exit criteria**: Resource systems function with Blueprint-assigned Data Assets and C++-owned components.

### Checkpoint G: Full validation, documentation, and recovery

1. Run `scripts/Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden`.
2. Complete the manual editor verification checklist.
3. Update `docs/ARCHITECTURE.md`, `docs/REMEMBER.md`, and `docs/RECOVER.md`.
4. Review Git status and diff for unrelated changes.

**Exit criteria**: Build, tests, manual verification, and documentation are complete.

## Rollback plan

| Checkpoint | Rollback action |
|---|---|
| A (Clock) | Revert new clock files and `LogEdenSimClock`. |
| B (Fuel / demand interface) | Revert fuel files and propulsion-demand interface additions. |
| C (Power/Thermal) | Revert power/thermal files. |
| D (Pawn / flight wiring) | Revert pawn default-subobject and flight demand-source changes. |
| E (Debug) | Revert `ShowDebug EdenSystems` additions. |
| F (Blueprint assets) | Remove test Data Asset assignments/instances only. |
| G (Docs) | Revert documentation to checkpoint F state. |

The tagged flight shell `v0.1.0-flight-shell` remains the recovery baseline for leaving resource work entirely.

## Verification plan

### Automated tests

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden
```

Expected: `EdenSpaceSimulatorEditor` Win64 Development builds. All new `Eden.Unit.SimClock.*`, `Eden.Unit.Systems.*`, `Eden.Integration.Systems.*`, plus existing `Eden.Unit.Flight.*` and `Eden.Unit.Foundation.Smoke` tests pass.

### Manual verification

As described in the manual editor verification section above.

### Git review

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff
```

Expected: only planned files appear. No generated files tracked. No unrelated asset changes.

## Acceptance evidence

This section will be populated after implementation and verification.

Required evidence:

- [ ] Clean working tree confirmed before Checkpoint A
- [ ] Build log: `EdenSpaceSimulatorEditor` Win64 Development passes
- [ ] Test log: all `Eden.Unit.SimClock.*` tests pass
- [ ] Test log: all `Eden.Unit.Systems.Fuel.*` tests pass
- [ ] Test log: all `Eden.Unit.Systems.Power.*` tests pass
- [ ] Test log: all `Eden.Unit.Systems.Thermal.*` tests pass
- [ ] Test log: all `Eden.Integration.Systems.*` tests pass
- [ ] Test log: existing `Eden.Unit.Flight.*` and `Eden.Unit.Foundation.Smoke` tests still pass
- [ ] Manual PIE: fuel consumption visible during flight via `ShowDebug EdenSystems`
- [ ] Manual PIE: state transitions logged through per-system `OnStateChanged` and terminal events
- [ ] Manual PIE: power drain visible over time
- [ ] Manual PIE: thermal change visible over time; dissipation does not cross ambient
- [ ] Manual PIE: PIE restart resets all resource state
- [ ] Manual PIE: no LogTemp, no per-frame spam
- [ ] Manual PIE: Blueprint only assigns Data Assets; C++ owns resource components
- [ ] Documentation: RECOVER.md updated
- [ ] Documentation: REMEMBER.md updated with new durable facts
- [ ] Documentation: ARCHITECTURE.md state ownership table updated
- [ ] Git: diff reviewed, no unrelated changes

## Decision log

2026-07-22: Drafted initial ExecPlan 0003 for review. No implementation performed.
2026-07-22: Review decisions locked:
- Tag verified flight shell as `v0.1.0-flight-shell` and implement on `feature/spacecraft-resource-simulation` with a clean working-tree gate before Checkpoint A.
- Use `IEdenPropulsionDemandSource` implemented by `UEdenFlightMovementComponent`; fuel stores a weak non-owning reference; remove proposed pawn thrust-fraction API.
- Create Fuel/Power/Thermal as C++ default subobjects; Blueprint assigns Data Assets/tuning only.
- Do not add `UEdenResourceComponentBase`.
- Use simple linear thermal model with `DegreesCelsiusPerSecond` units; dissipation moves toward ambient and cannot cross it; defer exponential cooling.
- Lock debug visibility to `ShowDebug EdenSystems`.
- Replace shared `EEdenResourceState` with `EEdenFuelState`, `EEdenPowerState`, and `EEdenThermalState`.
- Use one `OnStateChanged(Previous, Current)` per system plus terminal events where useful.

## Progress log

2026-07-22: Drafted ExecPlan 0003 for review. No implementation performed.
2026-07-22: Created annotated tag `v0.1.0-flight-shell` on `ed7fb55`.
2026-07-22: Created branch `feature/spacecraft-resource-simulation` from the tagged baseline.
2026-07-22: Revised this Draft plan with locked review decisions and the clean repository baseline. No Checkpoint A implementation performed.

## Handoff

Revised Draft is ready for review.

Next clean actions:

1. Review and explicitly approve this revised ExecPlan.
2. Ensure the working tree is clean.
3. Only then authorize Checkpoint A.

Do not begin resource simulation implementation from this Draft by assumption.
