# Spacecraft Resource Simulation

## Status
Approved

## Implementation blocker

> [!CAUTION]
> The verified six-axis flight shell baseline is committed and tagged as `v0.1.0-flight-shell` on commit `ed7fb55`, and resource work is on `feature/spacecraft-resource-simulation`.
>
> Checkpoint A clock implementation was accepted and committed as `9bede83`. Checkpoint B fuel implementation was explicitly authorized and completed. Do not begin Checkpoint C or later power, thermal, asset, Blueprint, debug-visibility, pawn resource-component, propulsion-demand integration, mission, UI, telemetry, or EDEN OS work until separately authorized.

## Problem and outcome

The verified six-axis flight shell lets the player fly a spacecraft, but the pawn has no concept of fuel consumption, electrical power, or thermal state. The PROJECT_SPEC requires "fuel, power, and thermal state" for the first vertical slice, and ARCHITECTURE.md defines authoritative component owners for each. Without resource simulation, there is no consumable cost to flight, no system state to monitor, no foundation for mission failures, and no data for telemetry.

This milestone introduces:

- **`UEdenSimulationClockSubsystem`** — a `UTickableWorldSubsystem` that owns fixed-step simulation time and advances resource subscribers independently of the render frame rate. Core accumulator mathematics live in a pure `FEdenFixedStepClockModel` for world-independent testing.
- **`UEdenFuelSystemComponent`** — an actor component that owns fuel quantity, validates data-driven configuration, applies consumption, and produces explicit state transitions (warning, critical, depleted, recovered). Core fuel arithmetic lives in a pure `FEdenFuelModel`.
- **`UEdenPowerSystemComponent`** — an actor component that owns generation rate, battery storage, demand, and availability, resolves a deterministic power budget, and produces shortage/depletion transitions. Core power arithmetic lives in a pure `FEdenPowerModel`.
- **`UEdenThermalSystemComponent`** — an actor component that owns temperature state, applies heat generation and dissipation toward ambient, detects warning/critical thresholds, and produces thermal state transitions. Core thermal arithmetic lives in a pure `FEdenThermalModel`.
- **Data-driven configuration** through `UDataAsset` subclasses that hold capacities, rates, and thresholds. Data Assets contain configuration only, never runtime state. Data Assets participate in editor-time validation via Unreal's Data Validation facilities (`IsDataValid`), while components perform runtime validation before simulation begins.
- **Domain-specific state enums** — `EEdenFuelState`, `EEdenPowerState`, `EEdenThermalState` — because thermal failure is overheated, not depleted.
- **Automated unit and integration tests** for all resource calculations, state transitions, clock behavior, and cross-system integration.
- **Development-only debug visibility** via `ShowDebug EdenSystems`.
- **Project-specific logging** via existing `LogEdenSystems` category and new `LogEdenSimClock` category.

The outcome is a simulation layer where every resource has one authoritative owner, every transition is observable and logged, every calculation is deterministic and testable, and the system is completely independent from the render frame rate.

## Scope

### In scope

- `UEdenSimulationClockSubsystem` (`UTickableWorldSubsystem`) with fixed-step accumulator, configurable step size, bounded catch-up, overrun logging, pause, resume, and reset.
- `FEdenFixedStepClockModel` — pure production model for accumulator mathematics, testable without a world.
- `UEdenFuelSystemComponent` with data-driven capacity, consumption rate, warning/critical/depleted/recovered transitions, and reset.
- `FEdenFuelModel` — pure production model for fuel arithmetic.
- `UEdenPowerSystemComponent` with data-driven generation, storage capacity, demand tracking, budget resolution, warning/critical/depleted/recovered transitions, and reset.
- `FEdenPowerModel` — pure production model for power budget arithmetic.
- `UEdenThermalSystemComponent` with data-driven heat generation, dissipation toward ambient, warning/critical thresholds, overheated state, and reset.
- `FEdenThermalModel` — pure production model for thermal arithmetic.
- Domain-specific state enums: `EEdenFuelState`, `EEdenPowerState`, `EEdenThermalState`.
- Per-system `UDataAsset` subclasses with editor-time `IsDataValid` validation and runtime validation.
- Clock-driven fixed-step advance of all resource systems.
- `AEdenSpacecraftPawn` creates fuel, power, and thermal components as C++ default subobjects.
- `BP_EdenSpacecraftPawn` assigns data assets and tuning only; Blueprint cannot replace authoritative resource components.
- Fuel consumption demand via `IEdenPropulsionDemandSource` interface, not concrete pawn dependency.
- New `LogEdenSimClock` log category (note: `LogEdenSystems` already exists).
- `ShowDebug EdenSystems` for development-only resource state visibility.
- Unit tests for all pure models, component state transitions, edge cases, boundary conditions, NaN/Inf handling, validation ordering, and reset behavior.
- Integration tests for clock advancing multiple resource systems and cross-system coordination.
- Manual Unreal Editor verification steps for component composition, data asset creation, PIE behavior, and state visibility.
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
- Radiator or active cooling mechanisms beyond simple dissipation rate
- Fuel types or fuel transfer between tanks

## Current repository state

- Branch: `feature/spacecraft-resource-simulation`
- Clean flight-shell baseline: `v0.1.0-flight-shell` on commit `ed7fb55`
- Checkpoint A accepted commit: `9bede83`
- Expected pending changes after Checkpoint B implementation: fuel source, fuel tests, this ExecPlan, and `docs/RECOVER.md` only
- Active ExecPlan: `0003-spacecraft-resource-simulation.md` (Approved)
- Previous ExecPlan: `0002-six-axis-flight.md` (verified complete)
- `Source/EdenSpaceSimulator/Public/Systems/` contains Checkpoint B fuel types, fuel model, fuel config data asset, and fuel system component headers
- `Source/EdenSpaceSimulator/Private/Systems/` contains Checkpoint B fuel implementation files
- C++ simulation clock and fuel system checkpoints have been implemented; power, thermal, pawn resource composition, propulsion-demand integration, debug visibility, Blueprints, and Unreal assets are not implemented
- Existing log categories: `LogEden`, `LogEdenFlight`, `LogEdenSystems`, `LogEdenSimClock`, `LogEdenMission`, `LogEdenTelemetry`
- Module dependencies (`EdenSpaceSimulator.Build.cs`): `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`
- C++ standard: C++20, IWYU enforced
- Test patterns: `IMPLEMENT_SIMPLE_AUTOMATION_TEST` with `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`, `#if WITH_DEV_AUTOMATION_TESTS` guard, `Eden.Unit.*` naming hierarchy
- Existing flight types: `FEdenFlightInputCommand`, `FEdenFlightInputIntent`, `FEdenFlightMovementSettings`, `FEdenFlightVelocityState`, `FEdenFlightIntegrationResult`
- Pawn API: `ApplyFlightInputCommand()`, `ResetFlightState()`, `GetFlightMovementComponent()`, `GetRequiredCollisionRoot()`
- Movement component API: `MoveWithCommand()`, `ResetFlightMovement()`, `GetAngularVelocityLocalDegreesPerSecond()`, `MovementSettings`
- Pawn has ticking disabled (`PrimaryActorTick.bCanEverTick = false`) and `bReplicates = false`

## Architecture alignment

This plan follows `docs/ARCHITECTURE.md`, `ADR-0001`, and all guardrails in `AGENTS.md`.

### Dependency direction

```text
Presentation (ShowDebug, future HUD)
        |
        v
Application / Orchestration (Mission, future)
        |
        v
Simulation Domain (Clock, Fuel, Power, Thermal, Flight)
        |
        v
Unreal Engine primitives (UTickableWorldSubsystem, UActorComponent, UDataAsset)
```

Resource systems depend downward only. No resource component depends on UI widgets, mission logic, telemetry transport, or EDEN OS. Flight code may query resource state but cannot directly mutate resource internals. The fuel component does not include or depend on `AEdenSpacecraftPawn`; it reads propulsion demand through the `IEdenPropulsionDemandSource` interface.

### State ownership table

| State | Authoritative owner | Readers | Writers |
|---|---|---|---|
| Simulation elapsed time (seconds) | `UEdenSimulationClockSubsystem` | All resource systems, future mission, telemetry | Clock subsystem only |
| Fixed-step accumulator (seconds) | `UEdenSimulationClockSubsystem` | Internal only | Clock subsystem only |
| Pause state | `UEdenSimulationClockSubsystem` | All subscribers, UI | Clock subsystem only |
| Fuel quantity (kilograms) | `UEdenFuelSystemComponent` | Pawn, flight, mission, UI, telemetry | Fuel component only |
| Fuel state (`EEdenFuelState`) | `UEdenFuelSystemComponent` | Pawn, mission, UI, telemetry | Fuel component only |
| Fuel configuration (data asset) | `UEdenFuelConfigDataAsset` | Fuel component | Content author only |
| Propulsion demand (normalized) | `UEdenFlightMovementComponent` via `IEdenPropulsionDemandSource` | Fuel component | Flight movement component only |
| Power generation rate (kilowatts) | `UEdenPowerSystemComponent` | Mission, UI, telemetry | Power component only |
| Battery charge (kilowatt-hours) | `UEdenPowerSystemComponent` | Mission, UI, telemetry | Power component only |
| Power demand (kilowatts) | `UEdenPowerSystemComponent` | Mission, UI, telemetry | Power component only |
| Power state (`EEdenPowerState`) | `UEdenPowerSystemComponent` | Pawn, mission, UI, telemetry | Power component only |
| Power configuration (data asset) | `UEdenPowerConfigDataAsset` | Power component | Content author only |
| Temperature (Celsius) | `UEdenThermalSystemComponent` | Mission, UI, telemetry | Thermal component only |
| Thermal state (`EEdenThermalState`) | `UEdenThermalSystemComponent` | Pawn, mission, UI, telemetry | Thermal component only |
| Thermal configuration (data asset) | `UEdenThermalConfigDataAsset` | Thermal component | Content author only |

### Commands, events, and snapshots

**Commands** (requests to change state):
```
SetConsumptionDemandNormalized(float Demand)   // from IEdenPropulsionDemandSource or explicit
SetFuelQuantityKilograms(Value)                // reset/debug only
SetPowerGenerationEnabled(bEnabled)
SetBatteryChargeKilowattHours(Value)           // reset/debug only
SetHeatSourceActive(bActive)
SetTemperatureCelsius(Value)                   // reset/debug only
ResetResourceState()
PauseSimulation() / ResumeSimulation()
ResetSimulationClock()
```

**Events** (facts that already occurred — multicast delegates):

Each system emits one primary transition delegate and optional terminal events:
```
OnFuelStateChanged(EEdenFuelState PreviousState, EEdenFuelState NewState)
OnFuelDepleted                    // terminal convenience event
OnPowerStateChanged(EEdenPowerState PreviousState, EEdenPowerState NewState)
OnPowerDepleted                   // terminal convenience event
OnThermalStateChanged(EEdenThermalState PreviousState, EEdenThermalState NewState)
OnThermalOverheated               // terminal convenience event
OnSimulationClockOverrun(int32 DroppedSteps)
```

One state is derived per fixed step. If a value crosses multiple thresholds in a single step (e.g., Normal directly to Depleted), one `OnStateChanged(Normal, Depleted)` is emitted — no synthetic intermediate transitions.

**Snapshots** (future, not in this milestone): resource systems will expose read-only query methods returning values with explicit units. Full snapshot assembly is deferred to the telemetry milestone.

## Alternatives and tradeoffs

### Alternative 1: Single monolithic resource manager

A single `UEdenResourceManagerComponent` owns all fuel, power, and thermal state.

| Aspect | Assessment |
|---|---|
| Simplicity | Fewer classes initially |
| Single responsibility | Violated — three distinct domains in one class |
| Testability | Harder to test fuel logic without power/thermal coupling |
| Extension | Adding a new resource type requires modifying the manager |
| Architecture | Violates ADR-0001 component-oriented design |

**Rejected.** Violates single responsibility, makes testing harder, and creates a high-blast-radius class. Each resource domain has its own state, thresholds, transitions, and future coordination needs.

### Alternative 2: Separate components per resource (selected)

Each resource is a `UActorComponent` with one authoritative owner, its own data asset, and independent testable logic. Each has a companion pure production model (`FEdenFuelModel`, etc.) for world-independent testing.

| Aspect | Assessment |
|---|---|
| Single responsibility | Each component has one reason to change |
| Testability | Pure models tested without world setup; components tested with UObject lifecycle |
| Extension | New resource = new component + model, no existing class modified |
| Composition | C++ default subobjects; Blueprint assigns configuration |
| Architecture | Matches ADR-0001 and ARCHITECTURE.md |
| Cost | More files, explicit coordination for cross-system effects |

**Selected.** Follows the pattern established by `FEdenFlightMovementModel` + `UEdenFlightMovementComponent` and aligns with ADR-0001.

### Alternative 3: Shared base class `UEdenResourceComponentBase`

Factor common threshold/transition logic into a base class.

| Aspect | Assessment |
|---|---|
| DRY | Would reduce repeated threshold evaluation logic |
| Risk | Premature — each system has domain-specific state enums and different physics |
| AGENTS.md | "Do not create a base class merely because two snippets look similar. Abstract only after the shared concept and ownership are clear." |
| Domain correctness | Thermal failure is "Overheated", not "Depleted" — shared enum would be dishonest |

**Rejected.** Per AGENTS.md DRY guidance and the review finding that `EEdenResourceState` is domain-incorrect for thermal, each system uses its own state enum and readable transition logic. Extraction can be reconsidered in a future ADR after the systems stabilize.

### Alternative 4: Simulation clock as a component vs. `UTickableWorldSubsystem`

| Approach | Pros | Cons |
|---|---|---|
| `UTickableWorldSubsystem` | One per world, globally accessible, clear lifetime, automatic tick, no actor dependency, `Initialize`/`Deinitialize` lifecycle | Cannot be composed per-pawn for multi-vehicle |
| `UActorComponent` | Per-pawn composition, Blueprint-friendly | Multiple clocks would fragment time authority; violates "one owner" for simulation time |

**Selected: `UTickableWorldSubsystem`.** The first vertical slice is single-vehicle. Simulation time has one authoritative owner per ARCHITECTURE.md. Epic documents `UTickableWorldSubsystem` as a world-lifetime subsystem that automatically participates in ticking after initialization and stops during deinitialization. Multi-vehicle can be addressed in a future ADR if needed.

### Alternative 5: Per-system data assets vs. unified config asset

| Approach | Pros | Cons |
|---|---|---|
| Per-system (`UEdenFuelConfigDataAsset`, etc.) | Each system validates independently, smaller blast radius | More asset files |
| Unified (`UEdenSpacecraftResourceConfig`) | Single asset per spacecraft variant | Changes to one system's config require re-validating all; higher coupling |

**Selected: Per-system data assets.** Matches component-per-resource ownership, allows independent validation, and keeps blast radius small.

### Alternative 6: Subscriber storage mechanism

| Approach | Pros | Cons |
|---|---|---|
| `TArray<TWeakObjectPtr<UObject>>` with `IEdenSimulationTickable` cast | Unreal-safe; weak refs auto-invalidate; no dangling pointers; PIE/teardown safe | Cast per step (negligible with 3-5 subscribers) |
| `TArray<IEdenSimulationTickable*>` raw pointers | Slightly faster; no indirection | **Unsafe**: stale pointers on actor destruction, level transition, PIE teardown |
| Multicast delegate `OnSimulationStep` | Unreal-native, auto-cleanup | Less explicit; harder to debug ordering |

**Selected: weak UObject references with interface cast.** The clock stores `TArray<TWeakObjectPtr<UObject>>` and casts to `IEdenSimulationTickable` before each step. Invalid weak references are removed before stepping. This prevents dangling pointers during PIE teardown, level transitions, and unusual lifecycle ordering. Registration rejects duplicates. `Deinitialize` clears all subscribers.

Subscriber-list mutation is locked out during fixed-step iteration. The implementation for this milestone uses deferred registration/unregistration requests plus a stable weak-reference snapshot: prune invalid subscribers, copy valid weak references into a local snapshot, iterate the snapshot, then flush deferred adds/removes and prune again after the current fixed-step batch completes.

### Alternative 7: Fuel-thrust coupling

| Approach | Pros | Cons |
|---|---|---|
| `IEdenPropulsionDemandSource` interface on flight component | Fuel decoupled from concrete pawn; flight owns propulsion demand; fuel reads via weak non-owning reference | Requires interface definition |
| `AEdenSpacecraftPawn::GetCurrentThrustFraction()` | Simple, one method | **Fuel component depends on concrete pawn class** — violates architecture |
| `UEdenFuelSystemComponent::SetConsumptionDemandNormalized(float)` | Pure command; no interface needed | Requires an external caller to drive consumption each step |

**Selected: `IEdenPropulsionDemandSource`.** The flight movement component implements this interface and exposes `GetPropulsionDemandNormalized()`. The fuel component discovers propulsion demand sources on its owning actor during `BeginPlay`, requires exactly one valid source, and stores a `TWeakObjectPtr<UObject>` reference. The fuel component never includes or depends on `AEdenSpacecraftPawn`, `UEdenFlightMovementComponent`, or any concrete flight class.

Ownership rule:
- Flight owns movement and propulsion demand.
- Fuel owns fuel quantity and consumption.
- Fuel reads a normalized demand value through a stable interface.

## Update-loop design

### Pure accumulator model

`FEdenFixedStepClockModel` is a pure production struct (not test-only) that encapsulates fixed-step accumulator mathematics. It is used by both `UEdenSimulationClockSubsystem` and unit tests.

```cpp
struct FEdenFixedStepClockModel
{
    static bool IsValidDeltaTime(float DeltaTimeSeconds);

    // Returns the number of steps taken (0..MaxCatchUpSteps).
    // OutAccumulator is the remaining sub-step time.
    // OutDroppedSteps is nonzero when the accumulator overflowed MaxCatchUpSteps.
    static int32 CalculateSteps(
        float DeltaTimeSeconds,
        float FixedStepSeconds,
        int32 MaxCatchUpSteps,
        float& InOutAccumulatorSeconds,
        int32& OutDroppedSteps);
};
```

### Fixed-step tick flow

```text
UEdenSimulationClockSubsystem::Tick(float DeltaTime)
    |
    if (bPaused) return               // paused frames do not accumulate
    |
    if (!FEdenFixedStepClockModel::IsValidDeltaTime(DeltaTime))
        Log bounded warning (LogEdenSimClock)
        return
    |
    StepsTaken = FEdenFixedStepClockModel::CalculateSteps(
        DeltaTime, FixedStepSeconds, MaxCatchUpSteps,
        Accumulator, DroppedSteps)
    |
    Prune invalid weak references from subscriber list
    |
    Create stable weak-reference snapshot for this fixed-step batch
    |
    for i in [0, StepsTaken):
        for each valid subscriber in snapshot (cast TWeakObjectPtr to IEdenSimulationTickable):
            Subscriber->AdvanceSimulation(FixedStepSeconds)
        ElapsedSimulationTimeSeconds += FixedStepSeconds
    |
    Flush deferred registration/unregistration requests
    Prune invalid weak references after stepping
    |
    if (DroppedSteps > 0)
        Log overrun warning with DroppedSteps (LogEdenSimClock)
        Broadcast OnSimulationClockOverrun(DroppedSteps)
        // Dropped time does NOT increase ElapsedSimulationTimeSeconds
```

### Subsystem lifecycle

```text
Initialize(FSubsystemCollectionBase& Collection)
    Call Super::Initialize(Collection)
    Set defaults (FixedStepSeconds, MaxCatchUpSteps, Accumulator = 0, Elapsed = 0, bPaused = false)

Deinitialize()
    Clear all subscriber weak references
    Call Super::Deinitialize()

Tick(float DeltaTime)
    As described above

GetStatId()
    Return STAT group identifier for profiling

DoesSupportWorldType(const EWorldType::Type WorldType)
    Return true for Game and PIE
    Return true for GamePreview only if required for automated verification
    Return false for Editor, EditorPreview, Inactive, and None
```

### Parameters

| Parameter | Default | Rationale |
|---|---|---|
| `FixedStepSeconds` | 0.1 (10 Hz) | Per PROJECT_SPEC initial target |
| `MaxCatchUpSteps` | 4 | Bounds worst-case to 0.4s of simulation per frame; prevents spiral of death |

Validation:
- `FixedStepSeconds` must be positive and finite.
- `MaxCatchUpSteps` must be greater than zero.

### Clock semantics (locked)

- Paused frames do not accumulate into the accumulator.
- Dropped overrun time does not increase `ElapsedSimulationTimeSeconds`.
- Equivalent-partition tests apply only when `MaxCatchUpSteps` is not exceeded.
- Overrun tests assert dropped-step count and accumulator state through return values, not log-string parsing.

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

Using a `UINTERFACE` enables Unreal's `Cast<>` and reflection. The `CannotImplementInterfaceInBlueprint` meta tag keeps this as a C++-only simulation contract — Blueprints must not own simulation step behavior.

### Subscriber registration rules

- Components register during `BeginPlay` and unregister during `EndPlay`.
- The clock stores `TWeakObjectPtr<UObject>` — it does not own subscribers.
- Duplicate registration is rejected silently or with a bounded log.
- Invalid weak references are pruned before each stepping pass and after flushing deferred mutations.
- Registration and unregistration requests made during fixed-step iteration are deferred until the current fixed-step batch completes.
- Fixed-step iteration uses a stable weak-reference snapshot so the active subscriber list is not mutated while subscribers are being advanced.
- `Deinitialize` clears all subscriber references.
- A subscriber destroyed mid-iteration is safe because weak pointers detect invalidation.

## Propulsion demand interface

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
    // Returns 0.0 (no thrust) to 1.0 (full thrust).
    virtual float GetPropulsionDemandNormalized() const = 0;
};
```

`UEdenFlightMovementComponent` implements this interface. The implementation returns the magnitude of the last translation input vector, clamped to `[0, 1]`.

`UEdenFuelSystemComponent` discovers demand sources on its owning actor during `BeginPlay` without depending on the concrete pawn class:
1. Inspect the owner actor's components for implementations of `UEdenPropulsionDemandSource`.
2. Require exactly one valid `IEdenPropulsionDemandSource` on the spacecraft actor.
3. No source: log a bounded warning via `LogEdenSystems` and use zero demand.
4. One source: store a non-owning `TWeakObjectPtr<UObject> PropulsionDemandSource`.
5. Multiple sources: report an ambiguity error via `LogEdenSystems`, do not select silently, and use zero demand until the configuration is corrected.
6. Invalid source or expired weak reference: safely use zero demand.

During `AdvanceSimulation`, the fuel component reads:
```cpp
float Demand = 0.0f;
if (PropulsionDemandSource.IsValid())
{
    if (const IEdenPropulsionDemandSource* Source =
        Cast<IEdenPropulsionDemandSource>(PropulsionDemandSource.Get()))
    {
        Demand = Source->GetPropulsionDemandNormalized();
    }
}
```

The fuel component never includes `EdenSpacecraftPawn.h` or `EdenFlightMovementComponent.h`.

## Resource system design

### Pure production models

Each resource system has a companion pure production model. These are not test-only abstractions — they are the authoritative calculation types used by the components in production. This follows the established `FEdenFlightMovementModel` pattern.

| Model | Responsibility |
|---|---|
| `FEdenFixedStepClockModel` | Accumulator stepping, validation, overrun calculation |
| `FEdenFuelModel` | Fuel consumption, clamping, state derivation from quantity and thresholds |
| `FEdenPowerModel` | Net power budget, battery charge/drain, clamping, state derivation |
| `FEdenThermalModel` | Heat generation, dissipation toward ambient, clamping, state derivation |

### Domain-specific state enums

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

Thermal failure is `Overheated`, not `Depleted`. Each system has its own enum so domain concepts are honest and do not share a false abstraction.

### Fuel system

**State**: `FuelQuantityKilograms` (float, >= 0), `EEdenFuelState`.

**Configured initial value**: `InitialFuelFraction` (float, 0-1, default 1.0). Initial fuel = `CapacityKilograms * InitialFuelFraction`.

**Fixed-step behavior** (delegated to `FEdenFuelModel`):
1. Read propulsion demand from `IEdenPropulsionDemandSource` (0.0 to 1.0).
2. Subtract `ConsumptionRateKilogramsPerSecond * PropulsionDemand * FixedDeltaSeconds`.
3. Clamp to `[0, CapacityKilograms]`.
4. Derive new `EEdenFuelState` from quantity and thresholds.
5. If state changed: fire `OnFuelStateChanged(Previous, New)`, log transition. If new state is `Depleted`, also fire `OnFuelDepleted`.

**Reset**: Restores to `CapacityKilograms * InitialFuelFraction` and `EEdenFuelState::Normal`.

### Power system

**State**: `BatteryChargeKilowattHours` (float, >= 0), `EEdenPowerState`.

**Configured initial value**: `InitialChargeFraction` (float, 0-1, default 1.0). Initial charge = `BatteryCapacityKilowattHours * InitialChargeFraction`.

**Fixed-step behavior** (delegated to `FEdenPowerModel`):
1. Calculate net power: `NetPowerKilowatts = GenerationRateKilowatts - DemandKilowatts`.
2. Apply to battery: `BatteryChargeKilowattHours += NetPowerKilowatts * (FixedDeltaSeconds / 3600.0)`.
3. Clamp to `[0, BatteryCapacityKilowattHours]`.
4. Derive new `EEdenPowerState` from charge fraction and thresholds.
5. If state changed: fire `OnPowerStateChanged(Previous, New)`, log transition. If new state is `Depleted`, also fire `OnPowerDepleted`.

**Reset**: Restores to `BatteryCapacityKilowattHours * InitialChargeFraction` and `EEdenPowerState::Normal`.

**Demand model for this milestone**: A simple configurable baseline demand from the data asset. Full load management with consumer priorities is out of scope.

### Thermal system

**State**: `TemperatureCelsius` (float), `EEdenThermalState`.

**Configured initial value**: `InitialTemperatureCelsius` (float, default = `AmbientTemperatureCelsius`).

**Fixed-step behavior** (delegated to `FEdenThermalModel`):
1. Apply heat generation: `TemperatureCelsius += HeatGenerationDegreesCelsiusPerSecond * FixedDeltaSeconds`.
2. Apply dissipation toward ambient:
   ```
   DissipationDelta = DissipationDegreesCelsiusPerSecond * FixedDeltaSeconds
   if (TemperatureCelsius > AmbientTemperatureCelsius)
       TemperatureCelsius -= DissipationDelta
       TemperatureCelsius = FMath::Max(TemperatureCelsius, AmbientTemperatureCelsius)  // do not cross ambient
   else if (TemperatureCelsius < AmbientTemperatureCelsius)
       TemperatureCelsius += DissipationDelta
       TemperatureCelsius = FMath::Min(TemperatureCelsius, AmbientTemperatureCelsius)  // do not cross ambient
   ```
3. Clamp to `[AbsoluteMinTemperatureCelsius, AbsoluteMaxTemperatureCelsius]` as final safety bounds.
4. Derive new `EEdenThermalState` from temperature and thresholds.
5. If state changed: fire `OnThermalStateChanged(Previous, New)`, log transition. If new state is `Overheated`, also fire `OnThermalOverheated`.

**Reset**: Restores to `InitialTemperatureCelsius` and `EEdenThermalState::Normal`.

**Unit naming**: Rates are explicitly `DegreesCelsiusPerSecond` — a simplified temperature-change rate, not a physical heat energy model. This is acceptable for the first vertical slice and the naming is honest.

### State transition rules

- One new state is derived per fixed step by comparing the current resource value against thresholds.
- If a value crosses multiple thresholds in a single step (e.g., Normal directly to Depleted), one `OnStateChanged(OldState, NewState)` is emitted with the actual old and new states. No synthetic intermediate transitions.
- Recovery transitions use the same mechanism: if a value returns above a threshold, the new state is derived and one `OnStateChanged` is emitted.
- Logs include: component name, actor name, previous state, new state, and current value with units.
- No per-frame logging. Only state transitions and configuration validation produce log output.

### Data asset configuration

Each system has its own `UDataAsset` subclass with both editor-time (`IsDataValid`) and runtime (`BeginPlay`) validation.

**`UEdenFuelConfigDataAsset`**:
| Field | Type | Constraint | Default |
|---|---|---|---|
| `CapacityKilograms` | float | > 0 | 100.0 |
| `ConsumptionRateKilogramsPerSecond` | float | >= 0 | 1.0 |
| `InitialFuelFraction` | float | [0, 1] | 1.0 |
| `WarningThresholdFraction` | float | (0, 1) | 0.25 |
| `CriticalThresholdFraction` | float | [0, 1) | 0.10 |

Validation ordering: `0 <= CriticalThresholdFraction < WarningThresholdFraction <= 1`.

**`UEdenPowerConfigDataAsset`**:
| Field | Type | Constraint | Default |
|---|---|---|---|
| `BatteryCapacityKilowattHours` | float | > 0 | 10.0 |
| `GenerationRateKilowatts` | float | >= 0 | 2.0 |
| `BaselineDemandKilowatts` | float | >= 0 | 1.5 |
| `InitialChargeFraction` | float | [0, 1] | 1.0 |
| `WarningThresholdFraction` | float | (0, 1) | 0.25 |
| `CriticalThresholdFraction` | float | [0, 1) | 0.10 |

Validation ordering: `0 <= CriticalThresholdFraction < WarningThresholdFraction <= 1`.

**`UEdenThermalConfigDataAsset`**:
| Field | Type | Constraint | Default |
|---|---|---|---|
| `AmbientTemperatureCelsius` | float | see ordering | 20.0 |
| `InitialTemperatureCelsius` | float | [AbsoluteMin, AbsoluteMax] | 20.0 (= ambient) |
| `HeatGenerationDegreesCelsiusPerSecond` | float | >= 0 | 0.5 |
| `DissipationDegreesCelsiusPerSecond` | float | >= 0 | 0.3 |
| `WarningThresholdCelsius` | float | see ordering | 60.0 |
| `CriticalThresholdCelsius` | float | see ordering | 80.0 |
| `AbsoluteMinTemperatureCelsius` | float | see ordering | -273.15 |
| `AbsoluteMaxTemperatureCelsius` | float | see ordering | 150.0 |

Validation ordering: `AbsoluteMinTemperatureCelsius <= AmbientTemperatureCelsius < WarningThresholdCelsius < CriticalThresholdCelsius <= AbsoluteMaxTemperatureCelsius`.

**Validation**: Each data asset implements `IsDataValid` for editor-time checking. Each component also validates its data asset during `BeginPlay`. Invalid configuration (missing asset, zero capacity, threshold ordering violation, etc.) produces an actionable `UE_LOG` with actor name, component name, asset name, and field context, and enters a safe failure state where the system does not advance simulation.

Direct validation coverage is required for:
- `InitialFuelFraction` in `[0, 1]`.
- `InitialChargeFraction` in `[0, 1]`.
- `InitialTemperatureCelsius` inside `[AbsoluteMinTemperatureCelsius, AbsoluteMaxTemperatureCelsius]`.
- `FixedStepSeconds` positive and finite.
- `MaxCatchUpSteps` greater than zero.

## Component composition (locked)

`AEdenSpacecraftPawn` creates fuel, power, and thermal system components as **C++ default subobjects** in its constructor, alongside the existing `RequiredCollisionRoot` and `FlightMovementComponent`.

```cpp
FuelSystemComponent = CreateDefaultSubobject<UEdenFuelSystemComponent>(TEXT("FuelSystem"));
PowerSystemComponent = CreateDefaultSubobject<UEdenPowerSystemComponent>(TEXT("PowerSystem"));
ThermalSystemComponent = CreateDefaultSubobject<UEdenThermalSystemComponent>(TEXT("ThermalSystem"));
```

This guarantees the authoritative resource owners exist whenever the project spacecraft spawns.

`BP_EdenSpacecraftPawn` assigns configuration Data Assets and tuning values to these components. Blueprint cannot create, remove, or replace the authoritative resource components.

## Failure modes

| Failure | Required behavior |
|---|---|
| Missing data asset reference | Log error with component, actor, and expected asset type. Do not advance simulation for that system. |
| Zero or negative capacity | Reject during `IsDataValid` and `BeginPlay`. Log field name, value, and asset name. |
| Initial fuel or charge fraction outside `[0, 1]` | Reject during `IsDataValid` and `BeginPlay`. Log field name, value, and expected range. |
| Initial temperature outside absolute thermal bounds | Reject during `IsDataValid` and `BeginPlay`. Log initial value, absolute min, absolute max, and asset context. |
| Fuel/power threshold ordering violation (`Critical >= Warning`) | Reject. Log both values, expected ordering, and asset context. |
| Thermal threshold ordering violation | Reject. Log full ordering chain and asset context. |
| NaN or Inf in resource value | Detect at start of fixed step. Log context with previous value. Clamp to safe value. |
| Invalid clock configuration | Reject non-positive/non-finite `FixedStepSeconds` and `MaxCatchUpSteps <= 0`. Clock does not advance and logs actionable context. |
| Fixed-step overrun (> MaxCatchUpSteps) | Drop excess accumulator time (does not increase elapsed time), log warning with dropped count via `LogEdenSimClock`, broadcast `OnSimulationClockOverrun`. |
| Fuel reaches zero | Derive `EEdenFuelState::Depleted`. Fire `OnFuelStateChanged` + `OnFuelDepleted`. Log transition. Do not crash. |
| Power reaches zero | Derive `EEdenPowerState::Depleted`. Fire `OnPowerStateChanged` + `OnPowerDepleted`. Log transition. |
| Temperature reaches `AbsoluteMaxTemperatureCelsius` | Derive `EEdenThermalState::Overheated`. Fire `OnThermalStateChanged` + `OnThermalOverheated`. Log transition. |
| Propulsion demand source not found on actor | Fuel component logs warning during `BeginPlay`. Consumption proceeds with zero demand. |
| Multiple propulsion demand sources found on actor | Fuel component reports ambiguity via `LogEdenSystems`, does not choose silently, and uses zero demand until fixed. |
| Propulsion demand source weak reference expires or no longer implements the interface | Fuel component safely uses zero demand. |
| Clock subsystem unavailable | Resource components log error in `BeginPlay` and do not register. They remain inert. |
| DeltaTime is zero, negative, NaN, or Inf | Clock rejects via `FEdenFixedStepClockModel::IsValidDeltaTime`. Does not advance. Logs bounded warning. |
| Subscriber registers or unregisters during stepping | Clock defers mutation until the current fixed-step batch completes; active iteration uses a stable weak-reference snapshot. |
| Subscriber destroyed during stepping | Weak pointer detects invalidation. Snapshot entry is skipped safely and pruned after stepping. |
| PIE teardown | `Deinitialize` clears all subscriber weak references. `EndPlay` unregisters individual components. |

## Performance considerations

- **Fixed-step at 10 Hz**: 3 resource components × 10 steps/second = 30 component updates/second. Negligible CPU cost.
- **No per-frame allocation**: Resource model calculations use stack-local arithmetic. No `FString` formatting, `TArray` allocation, or asset loading in the fixed-step path.
- **No per-frame logging**: Only state transitions and configuration validation log. High-frequency values visible only through `ShowDebug EdenSystems`.
- **Delegate cost**: Multicast delegates fire only on state transitions, not every step.
- **Accumulator bound**: MaxCatchUpSteps = 4 prevents simulation spiral-of-death on hitches.
- **Subscriber array**: Weak pointer array with typical 3-5 subscribers. Weak-validity check is negligible. No hash maps or priority queues.
- **Development-only debug**: `ShowDebug EdenSystems` uses Unreal's built-in `ShowDebug` framework. No shipping overhead.
- **Log string formatting**: Only on state transitions and errors. `UE_LOG` format strings use compile-time literals.

## Test matrix

### Unit tests (`Eden.Unit.SimClock.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Unit.SimClock.FixedStepAdvancesSubscribers` | `FEdenFixedStepClockModel::CalculateSteps` returns correct step count; mock subscriber receives correct call count |
| `Eden.Unit.SimClock.AccumulatorHandlesPartialFrames` | Steps fire only when accumulator >= step size; remainder preserved |
| `Eden.Unit.SimClock.CatchUpBounded` | MaxCatchUpSteps limits steps per call |
| `Eden.Unit.SimClock.OverrunDropsExcessSteps` | OutDroppedSteps is nonzero; accumulator remainder is sub-step; elapsed time excludes dropped time |
| `Eden.Unit.SimClock.PausePreventsAdvance` | Paused state produces zero steps and no accumulator growth |
| `Eden.Unit.SimClock.ResetClearsAccumulatorAndTime` | Reset zeroes elapsed time, accumulator, and step count |
| `Eden.Unit.SimClock.InvalidDeltaTimeRejected` | Zero, negative, NaN, Inf rejected by `IsValidDeltaTime` |
| `Eden.Unit.SimClock.InvalidFixedStepConfigRejected` | Non-positive, NaN, and Inf `FixedStepSeconds` rejected |
| `Eden.Unit.SimClock.MaxCatchUpStepsRequiresPositiveValue` | `MaxCatchUpSteps <= 0` rejected |
| `Eden.Unit.SimClock.WorldTypeSupportIsLocked` | `DoesSupportWorldType` supports Game/PIE, optionally GamePreview for automation, and excludes Editor, EditorPreview, Inactive, None |
| `Eden.Unit.SimClock.SubscriberMutationDeferredDuringStep` | Registration/unregistration during stepping does not mutate the active iteration list |
| `Eden.Unit.SimClock.ElapsedTimeAccumulatesCorrectly` | Elapsed time equals steps × step size (not raw DeltaTime sum) |
| `Eden.Unit.SimClock.EquivalentTimeMatchesBelowCatchUpCap` | Different DeltaTime splits produce same elapsed time and step count when MaxCatchUpSteps is not exceeded |

### Unit tests (`Eden.Unit.Systems.Fuel.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Unit.Systems.Fuel.ConsumptionReducesQuantity` | `FEdenFuelModel`: fuel decreases by rate × demand × time |
| `Eden.Unit.Systems.Fuel.QuantityClampsAtZero` | Cannot go negative |
| `Eden.Unit.Systems.Fuel.QuantityClampsAtCapacity` | Cannot exceed capacity |
| `Eden.Unit.Systems.Fuel.WarningStateAtThreshold` | Normal → Warning derived at threshold crossing |
| `Eden.Unit.Systems.Fuel.CriticalStateAtThreshold` | Warning → Critical derived at threshold crossing |
| `Eden.Unit.Systems.Fuel.DepletedStateAtZero` | Depleted derived at zero quantity |
| `Eden.Unit.Systems.Fuel.DirectNormalToDepletedTransition` | Value crossing multiple thresholds in one step emits one transition |
| `Eden.Unit.Systems.Fuel.RecoveryStateAboveThreshold` | Depleted → Normal when refueled above warning |
| `Eden.Unit.Systems.Fuel.ResetRestoresInitialValue` | Reset returns to `Capacity * InitialFraction` and Normal state |
| `Eden.Unit.Systems.Fuel.NaNAndInfRejected` | Invalid values detected and clamped |
| `Eden.Unit.Systems.Fuel.ZeroCapacityRejected` | Config validation rejects zero capacity |
| `Eden.Unit.Systems.Fuel.ThresholdOrderingValidated` | `Critical >= Warning` rejected; `Critical < Warning` accepted |
| `Eden.Unit.Systems.Fuel.InitialFuelFractionValidated` | `InitialFuelFraction` outside `[0, 1]` rejected |
| `Eden.Unit.Systems.Fuel.PropulsionDemandSourceCardinalityHandled` | Zero source uses zero demand, exactly one source is retained weakly, multiple sources report ambiguity, invalid source uses zero demand |
| `Eden.Unit.Systems.Fuel.NoConsumptionWhenDepleted` | Depleted state does not subtract further |

### Unit tests (`Eden.Unit.Systems.Power.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Unit.Systems.Power.NetPositiveBudgetChargesBattery` | `FEdenPowerModel`: generation > demand charges battery |
| `Eden.Unit.Systems.Power.NetNegativeBudgetDrainsBattery` | Demand > generation drains battery |
| `Eden.Unit.Systems.Power.BatteryClampsAtCapacity` | Cannot overcharge |
| `Eden.Unit.Systems.Power.BatteryClampsAtZero` | Cannot go negative |
| `Eden.Unit.Systems.Power.WarningStateAtThreshold` | Threshold transition derived at correct level |
| `Eden.Unit.Systems.Power.CriticalStateAtThreshold` | Threshold transition derived at correct level |
| `Eden.Unit.Systems.Power.DepletedStateAtZero` | Zero charge derives Depleted |
| `Eden.Unit.Systems.Power.DirectNormalToDepletedTransition` | Multi-threshold crossing emits one transition |
| `Eden.Unit.Systems.Power.RecoveryState` | Recovery restores state |
| `Eden.Unit.Systems.Power.ResetRestoresInitialValue` | Reset returns to `Capacity * InitialFraction` and Normal state |
| `Eden.Unit.Systems.Power.NaNAndInfRejected` | Invalid values detected and clamped |
| `Eden.Unit.Systems.Power.ThresholdOrderingValidated` | Ordering violation rejected |
| `Eden.Unit.Systems.Power.InitialChargeFractionValidated` | `InitialChargeFraction` outside `[0, 1]` rejected |

### Unit tests (`Eden.Unit.Systems.Thermal.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Unit.Systems.Thermal.HeatGenerationIncreasesTemperature` | `FEdenThermalModel`: temperature rises with generation |
| `Eden.Unit.Systems.Thermal.DissipationMovesTowardAmbient` | Temperature approaches ambient, does not cross it |
| `Eden.Unit.Systems.Thermal.DissipationDoesNotCrossAmbient` | Dissipation clamps at ambient in a single step |
| `Eden.Unit.Systems.Thermal.TemperatureClampsAtAbsoluteBounds` | Min/max safety bounds enforced |
| `Eden.Unit.Systems.Thermal.WarningStateAtThreshold` | Warning threshold derives correctly |
| `Eden.Unit.Systems.Thermal.CriticalStateAtThreshold` | Critical threshold derives correctly |
| `Eden.Unit.Systems.Thermal.OverheatedStateAtMax` | Max temperature derives Overheated |
| `Eden.Unit.Systems.Thermal.DirectNormalToOverheatedTransition` | Multi-threshold crossing emits one transition |
| `Eden.Unit.Systems.Thermal.RecoveryState` | Cooling below threshold recovers |
| `Eden.Unit.Systems.Thermal.ResetRestoresInitialTemperature` | Reset returns to configured initial temperature and Normal |
| `Eden.Unit.Systems.Thermal.NaNAndInfRejected` | Invalid values detected and clamped |
| `Eden.Unit.Systems.Thermal.ThresholdOrderingValidated` | `AbsMin <= Ambient < Warning < Critical <= AbsMax` enforced |
| `Eden.Unit.Systems.Thermal.InitialTemperatureInsideAbsoluteBoundsValidated` | `InitialTemperatureCelsius` outside absolute min/max bounds rejected |

### Integration tests (`Eden.Integration.Systems.*`)

| Test name | What it verifies |
|---|---|
| `Eden.Integration.Systems.ClockAdvancesAllResources` | Clock drives fuel, power, thermal simultaneously via weak subscribers |
| `Eden.Integration.Systems.FuelDepletionFromDemand` | Sustained propulsion demand depletes fuel through clock |
| `Eden.Integration.Systems.FuelUsesExactlyOneDemandSource` | Component discovery does not depend on pawn class and rejects multiple demand sources without selecting silently |
| `Eden.Integration.Systems.ResourceResetClearsAllState` | Full reset restores all systems to configured initial values |
| `Eden.Integration.Systems.MissingDataAssetHandledGracefully` | Missing config does not crash, system remains inert |
| `Eden.Integration.Systems.PIERestartResetsClockAndResources` | Simulated PIE restart (Deinitialize + Initialize) resets all state |

### Manual editor verification

```text
1. Open EdenSpaceSimulator.uproject in Unreal Engine 5.8.
2. Confirm project loads into L_FlightSandbox.
3. Open BP_EdenSpacecraftPawn in the Blueprint editor.
4. Confirm FuelSystem, PowerSystem, and ThermalSystem components are visible as
   C++ default subobjects (not Blueprint-added).
5. Confirm each component has a data asset reference slot.
6. Create DA_TestFuelConfig, DA_TestPowerConfig, DA_TestThermalConfig data assets
   in Content/Eden/Config/ with reasonable test values.
7. Validate data assets using Edit > Data Validation. Confirm valid assets pass and
   intentionally invalid assets (e.g., Critical >= Warning, InitialFuelFraction > 1,
   InitialChargeFraction < 0, InitialTemperatureCelsius outside absolute bounds)
   produce actionable errors.
8. Assign data assets to the resource components on BP_EdenSpacecraftPawn.
9. Press Play.
10. Fly with W to apply thrust. Run ShowDebug EdenSystems. Observe fuel decreasing.
11. Wait for fuel to reach warning threshold. Confirm log output for state transition.
12. Continue until fuel depleted. Confirm log output for depletion.
13. Verify power system battery drains over time if demand > generation.
14. Verify thermal system temperature changes over time. Verify dissipation moves
    toward ambient and does not cross it.
15. Stop Play, start Play again. Confirm all resource state resets to configured
    initial values (not hardcoded assumptions).
16. Open Output Log. Confirm no LogTemp, no per-frame spam, and state transitions
    are logged with component name, actor name, previous state, new state, and
    value with units.
17. Confirm ShowDebug EdenSystems shows fuel, power, and thermal values.
18. Review source control for unintended changes.
```

## Source layout

```text
Source/EdenSpaceSimulator/
+-- Public/
|   +-- Core/
|   |   +-- EdenLogCategories.h            [MODIFY] add LogEdenSimClock
|   |   +-- EdenSimulationTickable.h       [NEW] IEdenSimulationTickable UInterface
|   |   +-- EdenFixedStepClockModel.h      [NEW] pure accumulator math
|   |   +-- EdenSimulationClockSubsystem.h [NEW] UTickableWorldSubsystem
|   +-- Flight/
|   |   +-- EdenFlightMovementComponent.h  [MODIFY] implement IEdenPropulsionDemandSource
|   |   +-- EdenPropulsionDemandSource.h   [NEW] IEdenPropulsionDemandSource UInterface
|   |   +-- EdenSpacecraftPawn.h           [MODIFY] add resource component default subobjects
|   |   +-- (others unchanged)
|   +-- Systems/                            [NEW directory]
|       +-- EdenResourceTypes.h             [NEW] EEdenFuelState, EEdenPowerState, EEdenThermalState
|       +-- EdenFuelModel.h                 [NEW] pure fuel math
|       +-- EdenPowerModel.h                [NEW] pure power math
|       +-- EdenThermalModel.h              [NEW] pure thermal math
|       +-- EdenFuelSystemComponent.h       [NEW]
|       +-- EdenPowerSystemComponent.h      [NEW]
|       +-- EdenThermalSystemComponent.h    [NEW]
|       +-- EdenFuelConfigDataAsset.h       [NEW]
|       +-- EdenPowerConfigDataAsset.h      [NEW]
|       +-- EdenThermalConfigDataAsset.h    [NEW]
+-- Private/
    +-- Core/
    |   +-- EdenLogCategories.cpp           [MODIFY] define LogEdenSimClock
    |   +-- EdenFixedStepClockModel.cpp     [NEW]
    |   +-- EdenSimulationClockSubsystem.cpp [NEW]
    +-- Flight/
    |   +-- EdenFlightMovementComponent.cpp [MODIFY] implement GetPropulsionDemandNormalized
    |   +-- EdenSpacecraftPawn.cpp          [MODIFY] create resource default subobjects
    +-- Systems/                            [NEW directory]
    |   +-- EdenFuelModel.cpp               [NEW]
    |   +-- EdenPowerModel.cpp              [NEW]
    |   +-- EdenThermalModel.cpp            [NEW]
    |   +-- EdenFuelSystemComponent.cpp     [NEW]
    |   +-- EdenPowerSystemComponent.cpp    [NEW]
    |   +-- EdenThermalSystemComponent.cpp  [NEW]
    |   +-- EdenFuelConfigDataAsset.cpp     [NEW]
    |   +-- EdenPowerConfigDataAsset.cpp    [NEW]
    |   +-- EdenThermalConfigDataAsset.cpp  [NEW]
    +-- Tests/
        +-- EdenSimClockTests.cpp           [NEW]
        +-- EdenFuelSystemTests.cpp         [NEW]
        +-- EdenPowerSystemTests.cpp        [NEW]
        +-- EdenThermalSystemTests.cpp      [NEW]
        +-- EdenResourceIntegrationTests.cpp [NEW]
```

### Modifications to existing files

| File | Change | Rationale |
|---|---|---|
| [EdenLogCategories.h](file:///k:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator/Source/EdenSpaceSimulator/Public/Core/EdenLogCategories.h) | Add `LogEdenSimClock` declaration | New log category for simulation clock |
| [EdenLogCategories.cpp](file:///k:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator/Source/EdenSpaceSimulator/Private/Core/EdenLogCategories.cpp) | Define `LogEdenSimClock` | Match declaration |
| [EdenFlightMovementComponent.h](file:///k:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator/Source/EdenSpaceSimulator/Public/Flight/EdenFlightMovementComponent.h) | Implement `IEdenPropulsionDemandSource` | Expose propulsion demand without concrete pawn coupling |
| [EdenFlightMovementComponent.cpp](file:///k:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator/Source/EdenSpaceSimulator/Private/Flight/EdenFlightMovementComponent.cpp) | Implement `GetPropulsionDemandNormalized()` | Return last translation input magnitude |
| [EdenSpacecraftPawn.h](file:///k:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator/Source/EdenSpaceSimulator/Public/Flight/EdenSpacecraftPawn.h) | Add fuel, power, thermal component `TObjectPtr` members and accessors | C++ default subobjects for resource systems |
| [EdenSpacecraftPawn.cpp](file:///k:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator/Source/EdenSpaceSimulator/Private/Flight/EdenSpacecraftPawn.cpp) | `CreateDefaultSubobject` for resource components | Guarantee authoritative owners exist |

No changes to `EdenSpaceSimulator.Build.cs` are expected — existing module dependencies are sufficient.

## Implementation checkpoints

Each checkpoint must build and pass tests before continuing.

### Checkpoint A: Simulation clock, propulsion demand interface, and tests

1. Add `LogEdenSimClock` log category to existing log categories files.
2. Add `IEdenSimulationTickable` UInterface in `Public/Core/EdenSimulationTickable.h`.
3. Add `FEdenFixedStepClockModel` pure production model in `Public/Core/EdenFixedStepClockModel.h` + `.cpp`.
4. Add `UEdenSimulationClockSubsystem` as a `UTickableWorldSubsystem` with:
   - `Initialize` / `Deinitialize` calling parent implementations.
   - `Tick` using `FEdenFixedStepClockModel`.
   - `GetStatId` for profiling.
   - `DoesSupportWorldType` filtering: Game and PIE only, with GamePreview only if required by automated verification; Editor, EditorPreview, Inactive, and None excluded.
   - Weak-pointer subscriber storage; duplicate rejection; invalid-reference pruning; deferred mutation during stepping; stable weak-reference snapshot iteration; full cleanup in `Deinitialize`.
   - Config validation for positive finite `FixedStepSeconds` and `MaxCatchUpSteps > 0`.
   - Pause, resume, reset.
5. Add `IEdenPropulsionDemandSource` UInterface in `Public/Flight/EdenPropulsionDemandSource.h`.
6. Implement `IEdenPropulsionDemandSource` on `UEdenFlightMovementComponent`.
7. Add `Eden.Unit.SimClock.*` tests in `Private/Tests/EdenSimClockTests.cpp` — testing `FEdenFixedStepClockModel` directly for most tests.
8. Build and run tests.

**Exit criteria**: Clock subsystem and pure model build. All `Eden.Unit.SimClock.*` tests pass, including world-type filtering, clock config validation, and subscriber mutation safety. Existing `Eden.Unit.Flight.*` and `Eden.Unit.Foundation.Smoke` tests still pass.

### Checkpoint B: Resource types, fuel model, and fuel system

1. Add `EEdenFuelState`, `EEdenPowerState`, `EEdenThermalState` enums in `Public/Systems/EdenResourceTypes.h`.
2. Add `FEdenFuelModel` pure production model in `Public/Systems/EdenFuelModel.h` + `.cpp`.
3. Add `UEdenFuelConfigDataAsset` with `IsDataValid` editor-time validation in `Public/Systems/EdenFuelConfigDataAsset.h` + `.cpp`.
4. Add `UEdenFuelSystemComponent` implementing `IEdenSimulationTickable` in `Public/Systems/EdenFuelSystemComponent.h` + `.cpp`. Component discovers `IEdenPropulsionDemandSource` implementations on its owning actor during `BeginPlay`, requires exactly one valid source, retains it as a weak non-owning component reference, logs and uses zero demand for no source, reports ambiguity and uses zero demand for multiple sources, and safely uses zero demand for invalid/expired sources.
5. Add `Eden.Unit.Systems.Fuel.*` tests in `Private/Tests/EdenFuelSystemTests.cpp` — testing `FEdenFuelModel` directly for pure arithmetic, component for lifecycle.
6. Build and run all `Eden` tests.

**Exit criteria**: Fuel model and system build. All `Eden.Unit.Systems.Fuel.*` tests pass including threshold ordering validation, `InitialFuelFraction` validation, and propulsion demand source cardinality handling. Existing tests still pass.

### Checkpoint C: Power model, thermal model, and their systems

1. Add `FEdenPowerModel` and `UEdenPowerConfigDataAsset` (with `IsDataValid`) and `UEdenPowerSystemComponent`.
2. Add `FEdenThermalModel` and `UEdenThermalConfigDataAsset` (with `IsDataValid`) and `UEdenThermalSystemComponent`.
3. Thermal model implements dissipation toward ambient (cannot cross ambient in a single step).
4. Add `Eden.Unit.Systems.Power.*` and `Eden.Unit.Systems.Thermal.*` tests including threshold ordering, `InitialChargeFraction`, `InitialTemperatureCelsius`, and dissipation-does-not-cross-ambient tests.
5. Build and run all `Eden` tests.

**Exit criteria**: All three pure models and resource systems build and pass unit tests, including initial value validation for fuel, power, and thermal configuration.

### Checkpoint D: Pawn integration and clock wiring

1. Add fuel, power, thermal component default subobjects to `AEdenSpacecraftPawn` constructor.
2. Wire resource components to register with `UEdenSimulationClockSubsystem` during `BeginPlay` and unregister during `EndPlay`.
3. Fuel system discovers exactly one valid `IEdenPropulsionDemandSource` on its owning actor without depending on the concrete pawn class.
4. Add `Eden.Integration.Systems.*` integration tests including PIE restart simulation.
5. Build and run all `Eden` tests.

**Exit criteria**: All unit and integration tests pass. Resource components advance through the clock. Fuel consumption scales with a single valid propulsion demand source, ambiguous sources fail safely, and PIE restart resets all state.

### Checkpoint E: ShowDebug EdenSystems

1. Implement `ShowDebug EdenSystems` using Unreal's `ShowDebug` framework. Display current fuel, power, and thermal values with units and states.
2. Ensure debug display is development-build only (inherent in `ShowDebug` framework).
3. Build and run all `Eden` tests.

**Exit criteria**: `ShowDebug EdenSystems` works in development builds. No shipping overhead. No per-frame log spam.

### Checkpoint F: Blueprint composition and data assets

1. In Unreal Editor, confirm `BP_EdenSpacecraftPawn` shows the C++ default subobject resource components.
2. Create `DA_TestFuelConfig`, `DA_TestPowerConfig`, `DA_TestThermalConfig` data asset instances in `Content/Eden/Config/`.
3. Run Data Validation on assets in the editor.
4. Assign data assets to the resource components on the pawn Blueprint.
5. PIE test: fly, run `ShowDebug EdenSystems`, observe fuel consumption, verify state transitions in log.
6. PIE test: stop and restart — verify reset behavior restores configured initial values.

**Exit criteria**: Resource systems function in the editor with C++ default subobject composition and data-driven configuration.

### Checkpoint G: Full validation, documentation, and recovery

1. Run `scripts/Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden`.
2. Complete manual editor verification checklist.
3. Update `docs/ARCHITECTURE.md` state ownership table with resource system entries.
4. Update `docs/REMEMBER.md` with new durable facts about resource systems.
5. Update `docs/RECOVER.md` with implementation evidence and next checkpoint.
6. Review Git status and diff for unrelated changes.

**Exit criteria**: All builds pass, all tests pass, manual verification recorded, documentation current.

## Rollback plan

| Checkpoint | Rollback action |
|---|---|
| A (Clock + Interface) | Revert new clock, model, and interface files plus `LogEdenSimClock` addition. Revert `IEdenPropulsionDemandSource` from flight component. No existing behavior affected. |
| B (Fuel) | Revert new fuel files. Clock and interface remain intact and tested. |
| C (Power/Thermal) | Revert new power/thermal files. Clock and fuel remain intact. |
| D (Pawn integration) | Revert resource default subobject additions to pawn. Revert integration tests. Individual systems still work standalone with manual advance. |
| E (ShowDebug) | Revert ShowDebug implementation. Resource simulation still works without debug visibility. |
| F (Blueprint) | Delete data asset instances. C++ default subobjects remain but have no assigned configuration (safe: components log error and remain inert). |
| G (Docs) | Revert documentation to checkpoint F state. |

At any checkpoint, the project can be reverted to the previous checkpoint's state. The flight shell remains fully functional regardless of resource simulation state because the flight movement component's `IEdenPropulsionDemandSource` implementation is a read-only query that does not alter flight behavior.

## Verification plan

### Automated tests

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden
```

Expected: `EdenSpaceSimulatorEditor` Win64 Development builds. All `Eden.Unit.SimClock.*`, `Eden.Unit.Systems.*`, `Eden.Integration.Systems.*`, `Eden.Unit.Flight.*`, and `Eden.Unit.Foundation.Smoke` tests pass.

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

Current pre-implementation evidence:

- [x] Implementation blocker cleared: flight shell committed, `v0.1.0-flight-shell` tag exists on `ed7fb55`, feature branch `feature/spacecraft-resource-simulation` exists, and resource implementation has not started.

Required implementation evidence:
- [x] Checkpoint A source: `LogEdenSimClock`, `FEdenFixedStepClockModel`, `UEdenSimulationTickable` / `IEdenSimulationTickable`, and `UEdenSimulationClockSubsystem` implemented.
- [x] Checkpoint A behavior: Game and PIE world support; Editor, EditorPreview, GamePreview, Inactive, and None excluded; fixed-step and catch-up config validation; pause/resume/reset; elapsed time; dropped-step reporting; weak subscribers; duplicate rejection; invalid subscriber handling; deferred mutation; stable snapshot iteration; `Deinitialize` cleanup.
- [x] Repository validation: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1` passed.
- [x] Build log: `EdenSpaceSimulatorEditor` Win64 Development passed through `scripts/Validate-Project.ps1 -Build -RunTests -EngineRoot "K:\Program Files\Epic Games\UE_5.8" -TestFilter Eden`.
- [x] Test log: all `Eden.Unit.SimClock.*` tests passed, including `FEdenFixedStepClockModel` accumulator tests, equivalent partitions below the catch-up cap, overrun dropped-step state, world-type filtering, invalid config, invalid subscriber handling, duplicate rejection, and deferred subscriber mutation.
- [x] Checkpoint B source: `EEdenFuelState`, `FEdenFuelConfig`, `FEdenFuelStateSnapshot`, `FEdenFuelModel`, `UEdenFuelConfigDataAsset`, and `UEdenFuelSystemComponent` implemented without propulsion-demand, pawn, asset, Blueprint, power, thermal, debug, mission, UI, telemetry, or EDEN OS work.
- [x] Checkpoint B behavior: configured initial fuel fraction, reset, positive finite capacity validation, nonnegative finite consumption-rate validation, initial fraction validation, strict critical/warning threshold ordering, quantity clamping, one final state derivation per step, `OnFuelStateChanged`, `OnFuelDepleted` only on entering Depleted, NaN/infinity/excessive consumption handling, recovery, safe missing/invalid config disable, and safe clock register/unregister paths.
- [x] Test log: all `Eden.Unit.Systems.Fuel.*` tests passed, including threshold ordering, `InitialFuelFraction`, NaN/infinity demand, excessive consumption, reset, recovery, missing/invalid config disable, one direct multi-threshold transition, and depleted-entry semantics.
- [ ] Test log: all `Eden.Unit.Systems.Power.*` tests pass (including threshold ordering)
- [ ] Test log: all `Eden.Unit.Systems.Thermal.*` tests pass (including dissipation-does-not-cross-ambient and threshold ordering)
- [ ] Test log: all `Eden.Integration.Systems.*` tests pass (including PIE restart)
- [x] Test log: existing `Eden.Unit.Flight.*` and `Eden.Unit.Foundation.Smoke` tests still pass under the `Eden` automation filter.
- [ ] Manual PIE: fuel consumption visible during flight via `ShowDebug EdenSystems`
- [ ] Manual PIE: state transitions logged on threshold crossings with previous/new state
- [ ] Manual PIE: power drain visible over time
- [ ] Manual PIE: thermal change visible over time; dissipation moves toward ambient
- [ ] Manual PIE: PIE restart resets all resource state to configured initial values
- [ ] Manual PIE: no LogTemp, no per-frame spam
- [ ] Manual PIE: `ShowDebug EdenSystems` shows fuel, power, and thermal values
- [ ] Manual: Data Validation in editor catches invalid threshold ordering
- [x] Documentation: RECOVER.md updated
- [ ] Documentation: REMEMBER.md updated with new durable facts
- [ ] Documentation: ARCHITECTURE.md state ownership table updated
- [x] Git: diff reviewed, no unrelated changes

## Decision log

2026-07-22: Drafted ExecPlan 0003. No implementation.
2026-07-22: Revised per review feedback — 14 corrections applied:
  1. Flight shell commit is an explicit implementation blocker.
  2. Clock locked to `UTickableWorldSubsystem` with `Initialize`/`Deinitialize`/`Tick`/`GetStatId`/`DoesSupportWorldType`.
  3. Added `FEdenFixedStepClockModel` pure production model.
  4. Subscriber storage changed from raw pointers to `TWeakObjectPtr<UObject>` with pruning, duplicate rejection, and `Deinitialize` cleanup.
  5. Threshold validation ordering corrected and made explicit for fuel/power and thermal.
  6. Replaced shared `EEdenResourceState` with domain-specific `EEdenFuelState`, `EEdenPowerState`, `EEdenThermalState`. Thermal terminal state is `Overheated`, not `Depleted`.
  7. Simplified to one `OnStateChanged(Previous, New)` delegate per system plus optional terminal events. No synthetic intermediate transitions.
  8. Thermal dissipation corrected: moves toward ambient, cannot cross ambient in a single step. Rates named `DegreesCelsiusPerSecond` — honest simplified model.
  9. Removed fuel-to-pawn coupling. Added `IEdenPropulsionDemandSource` interface on flight movement component. Fuel never includes or depends on concrete pawn.
  10. Locked resource components as C++ default subobjects on `AEdenSpacecraftPawn`. Blueprint assigns configuration only.
  11. Added pure production models: `FEdenFuelModel`, `FEdenPowerModel`, `FEdenThermalModel`.
  12. Defined configured initial values (`InitialFuelFraction`, `InitialChargeFraction`, `InitialTemperatureCelsius`) and reset semantics.
  13. Locked debug visibility to `ShowDebug EdenSystems`.
  14. Clarified clock semantics: paused frames do not accumulate, dropped time does not increase elapsed, equivalent-partition tests apply below catch-up cap, overrun tests use return values not log strings.
2026-07-22: Applied final approval clarifications:
  1. `UEdenSimulationClockSubsystem::DoesSupportWorldType` is locked to Game and PIE, with GamePreview only where automated verification requires it, and Editor, EditorPreview, Inactive, and None excluded.
  2. `UEdenFuelSystemComponent` requires exactly one valid `IEdenPropulsionDemandSource` on the owning actor; no source and invalid source use zero demand, one source is retained weakly, and multiple sources report ambiguity without silent selection.
  3. Clock subscriber-list mutation during fixed-step iteration is prevented through deferred registration/unregistration plus stable weak-reference snapshot iteration.
  4. Validation and tests must directly cover `InitialFuelFraction`, `InitialChargeFraction`, `InitialTemperatureCelsius`, `FixedStepSeconds`, and `MaxCatchUpSteps`.
2026-07-22: Updated repository baseline after the flight shell commit: `v0.1.0-flight-shell` on `ed7fb55`; resource work proceeds on `feature/spacecraft-resource-simulation`.
2026-07-22: ExecPlan 0003 status changed to Approved. No implementation performed.

## Progress log

2026-07-22: Drafted ExecPlan 0003 for review. No implementation performed.
2026-07-22: Revised ExecPlan 0003 per review feedback (14 corrections). No implementation performed.
2026-07-22: Applied final approval clarifications, updated the committed flight-shell baseline, and marked the plan Approved. No implementation performed.
2026-07-23: Implemented Checkpoint A clock scope only. Added `FEdenFixedStepClockModel`, `UEdenSimulationTickable` / `IEdenSimulationTickable`, `UEdenSimulationClockSubsystem`, `LogEdenSimClock`, and `Eden.Unit.SimClock.*` automation coverage. Did not implement fuel, power, thermal systems, resource Data Assets, propulsion-demand integration, pawn resource components, debug visibility, Blueprints, or Unreal assets.
2026-07-23: Validation passed: repository validation, `EdenSpaceSimulatorEditor` Win64 Development build, `Eden.Unit.Foundation.Smoke`, existing `Eden.Unit.Flight.*`, and all new `Eden.Unit.SimClock.*` tests through `scripts/Validate-Project.ps1 -Build -RunTests -EngineRoot "K:\Program Files\Epic Games\UE_5.8" -TestFilter Eden`.
2026-07-23: Checkpoint A accepted and committed as `9bede83`.
2026-07-23: Implemented Checkpoint B fuel scope only. Added `EEdenFuelState`, `FEdenFuelConfig`, `FEdenFuelStateSnapshot`, `FEdenFuelModel`, `UEdenFuelConfigDataAsset`, `UEdenFuelSystemComponent`, and `Eden.Unit.Systems.Fuel.*` automation coverage. Did not implement propulsion-demand integration, pawn fuel component composition, power, thermal, debug visibility, Blueprint or Data Asset instances, Unreal map/config changes, mission, UI, telemetry, or EDEN OS work.
2026-07-23: Validation passed: repository validation, `EdenSpaceSimulatorEditor` Win64 Development build, `Eden.Unit.Foundation.Smoke`, existing `Eden.Unit.Flight.*`, existing `Eden.Unit.SimClock.*`, and all new `Eden.Unit.Systems.Fuel.*` tests through `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot "K:\Program Files\Epic Games\UE_5.8" -TestFilter Eden`. Automation log reported 54 tests found for `Eden` and `**** TEST COMPLETE. EXIT CODE: 0 ****`.

## Handoff

Checkpoint B fuel implementation is ready for review and acceptance.

Do not begin Checkpoint C or any power, thermal, propulsion-demand integration, pawn resource component composition, debug visibility, Blueprint, Unreal asset, map/config, mission, UI, telemetry, or EDEN OS work until separately authorized. Before the next checkpoint starts, confirm the working tree is clean and still on `feature/spacecraft-resource-simulation`.
