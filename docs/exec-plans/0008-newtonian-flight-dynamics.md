# 0008 Newtonian Spacecraft Flight Dynamics

## Status

Approved 2026-10-12 — In progress. Checkpoint A provisionally approved (automation green; **human PIE pending**). Checkpoint B provisionally approved (automation green; **human PIE pending**). Checkpoint C implemented (automation green; **human PIE pending**). Branch `feature/0008-newtonian-flight`. Not mergeable until Checkpoint M (ExecPlan 0007) human PIE passes on `main`.

Fuel note until Phase 4: propulsion demand (translation **and**, from CP-C, attitude thrust) **drives fuel consumption**, but fuel availability does **not** yet constrain thrust or torque. An empty tank still produces full thrust.

## Problem and outcome

`UEdenFlightMovementComponent` (ExecPlan 0002, `v0.1.0-flight-shell`) is a kinematic, target-velocity shell. Input selects a target speed per axis and `FEdenFlightMovementModel::IntegrateAxisVector` moves velocity toward it at a fixed rate. There is no mass, force, torque, or inertia. Speed is hard-clamped at 12 m/s and 90 deg/s even with stabilization off, stabilization brakes for free, fuel depletion does not remove thrust, and flight advances on the render frame rather than the fixed-step clock.

Outcome when 0008 is complete:

- When the engines shut off, the spacecraft drifts indefinitely (assist off) — no artificial speed cap or damping.
- Linear acceleration is `F / m` with thrust in newtons and mass in kilograms, including propellant mass that decreases as fuel burns.
- Rotation obeys rigid-body dynamics: torque, a diagonal inertia tensor, angular momentum, and quaternion orientation integration.
- Flight Assist is a controller that can only command the real thrusters, within the same limits as the pilot, and its thrust consumes propellant.
- Fuel depletion, power state, load shed, and propulsion priority change what the spacecraft can physically do.
- A deterministic harness simulates 60 s at several render frame rates and matches analytical results within documented tolerances.

## Scope

### In scope

- Extending `FEdenFlightMovementModel` (pure math) and `UEdenFlightMovementComponent` (owner of flight state).
- New SI-unit settings: dry mass, per-direction thrust, RCS torque, inertia, assist tuning, specific impulse.
- Flight Assist ON/OFF semantics on the existing `IA_FlightStabilize` toggle.
- Resource coupling: propellant mass → flight mass; fuel/power state → thrust availability.
- Fixed-step flight integration.
- Deterministic physics test harness and migration of existing `Eden.Unit.Flight.*` tests.
- ADR-0003 for the decisions marked *ADR* below.

### Out of scope

- Gravity, orbital mechanics, planetary scale, floating origin.
- Individual thruster placement / thrust-vector geometry (per-axis lumped thrusters only).
- Chaos rigid-body simulation.
- Collision impulses, angular response to impact, damage.
- Docking gameplay, autopilot, EDEN flight commands. **EDEN gains no new control privileges in 0008.**
- Cockpit, Meshy assets, VFX, audio, HUD redesign (a small debug readout is in scope).
- Changing the EDEN OS wire schema or ProjectEden.

## Current repository state

Audited on branch `main` @ `9738130` (2026-10-12).

| Area | File | Relevant facts |
|---|---|---|
| Command | `Public/Flight/EdenFlightTypes.h` | `FEdenFlightInputCommand`: `TranslationInput` local (X fwd, Y right, Z up), `RotationInput` local (**X pitch, Y yaw, Z roll**), `bStabilizationEnabled` (default true). `FEdenFlightMovementSettings` in cm/s and deg/s: max linear 1200 cm/s, accel 2400, decel 1800; max angular 90 deg/s, accel 180, decel 135. |
| Model | `Private/Flight/EdenFlightMovementModel.cpp` | Per-axis `MoveScalarToward` target tracking; `ClampVectorMagnitude` always applied; release with stabilization → decel to 0; release without → coast. `RemoveInwardVelocity` for blocking hits. |
| Component | `Private/Flight/EdenFlightMovementComponent.cpp` | Owns `Velocity` (inherited, cm/s world), `AngularVelocityLocalDegreesPerSecond`, `PropulsionDemandNormalized`, `ThrustAuthority`, `bStabilizationAssistAvailable`. `MoveWithCommand` scales translation by authority, integrates, `SafeMoveUpdatedComponent` + `SlideAlongSurface`, then `ApplyRotation` via `FRotator(dPitch, dYaw, dRoll).Quaternion()` composed on the local side. Requires the C++ sphere root. Does not tick itself. |
| Driver | `Private/Flight/EdenFlightPlayerController.cpp:91` | `PlayerTick` → `AEdenSpacecraftPawn::ApplyFlightInputCommand(Command, DeltaTime)` → `MoveWithCommand`. Flight therefore runs at render cadence. |
| Clock | `Public/Core/EdenSimulationClockSubsystem.h` | World subsystem, `FixedStepSeconds = 0.1` (10 Hz), `MaxCatchUpSteps = 4`, priorities Systems 0 / Mission 100 / Observers 200 / Advisory 300. Step math in `FEdenFixedStepClockModel::CalculateSteps`. |
| Fuel | `Systems/EdenFuelModel.h`, `EdenFuelSystemComponent` | Consumes `demand × ConsumptionRateKilogramsPerSecond` per 10 Hz step; default capacity 100 kg, 1 kg/s. Reads demand only through `IEdenPropulsionDemandSource`. Broadcasts `OnFuelDepleted`. **Flight never reads fuel state: an empty tank still produces full thrust.** |
| Power | `Systems/EdenPowerModel.h`, `EdenPowerSystemComponent` | Battery kWh, demand/generation kW, `OnPowerDepleted`. **No coupling to thrust.** |
| Operator | `Operations/EdenOperatorControlModel.cpp:100-110` | Load Shed → `bStabilizationAssistAvailable = false`; Propulsion Reduced → `ThrustAuthority = ReducedThrustAuthority` (0.5). Pushed via `SetThrustAuthority` / `SetStabilizationAssistAvailable` (ADR-0002: movement owns these). |
| Telemetry / EDEN | `Telemetry/EdenTelemetrySubsystem.cpp:503-505`, `EdenOs/EdenOsWireSerializationModel.cpp:146` | Exports only `ThrustAuthority`, `bStabilizationAssistAvailable`, `PropulsionDemandNormalized`. No position/velocity on the wire. |
| Tests | `Private/Tests/EdenFlightCommandTests.cpp` (12 `Eden.Unit.Flight.*`), `EdenRuntimeCompositionTests.cpp:270-290`, `EdenOperatorScenarioTests.cpp`, `EdenExternalCommand*Tests.cpp` | Several flight tests assert kinematic values (accelerate-and-clamp, free damping) and must be migrated. Others assert only authority/assist/demand plumbing. |
| Content | `BP_EdenSpacecraftPawn`, `BP_EdenFlightPlayerController`, `IA_FlightStabilize` | The Blueprint may override `MovementSettings`; binary assets cannot be inspected from source — **must be checked in-editor in Checkpoint A**. |

Known debt carried in: rotation built from Euler deltas each frame (order-dependent at high combined rates); 0002 deliberately chose kinematics "over premature physics complexity".

## Architecture alignment

Dependency direction is unchanged: Input → `AEdenFlightPlayerController` → `AEdenSpacecraftPawn` → `UEdenFlightMovementComponent` → `FEdenFlightMovementModel`. Resource components still never call flight, and flight never calls concrete resource classes.

### State ownership

| Mutable state | Authoritative owner | Writers |
|---|---|---|
| Linear velocity (world) | `UEdenFlightMovementComponent::Velocity` (inherited, cm/s at the Unreal boundary) | Movement only |
| Angular velocity (body, rad/s) | `UEdenFlightMovementComponent` (new SI field; the existing deg/s getter/setter stays as a conversion) | Movement only |
| Orientation | `UpdatedComponent` transform (sphere root) | Movement only |
| Applied thrust / torque this step | `UEdenFlightMovementComponent` (transient, read-only getters) | Movement only |
| `ThrustAuthority`, `bStabilizationAssistAvailable` | Movement (unchanged, ADR-0002) | `UEdenOperatorControlComponent` |
| Resource thrust limit `[0,1]` (new) | Movement | Pawn composition only (from fuel/power events) — *ADR* |
| Propellant mass | `UEdenFuelSystemComponent` (unchanged) | Fuel only; movement reads it through a new read-only interface |
| Flight fixed-step accumulator | Movement | Movement only |
| Flight Assist intent | `FEdenFlightInputCommand::bStabilizationEnabled` (controller-owned intent, unchanged) | Controller |

### Interfaces

- `IEdenPropulsionDemandSource` keeps its signature. **`PropulsionDemandNormalized` will be defined as the applied thrust fraction, including assist thrust**, so propellant use follows real thruster work. Telemetry keeps the same field and range.
- New `IEdenFlightMassSource` (read-only: `GetPropellantMassKilograms()`), implemented by `UEdenFuelSystemComponent` and discovered by the movement component the same way fuel discovers its demand source. *ADR*
- `GetThrustAuthority()` keeps returning the **operator** value, so EDEN telemetry semantics do not change. A new `GetEffectiveThrustAuthority()` = operator × resource limit is for HUD and debug only. A telemetry schema bump is deferred.

### ADR impact

ADR-0003 "Newtonian flight model, assist semantics, and resource thrust limits" records: (1) a custom deterministic integrator rather than Chaos; (2) flight fixed-step cadence; (3) assist as a thrust-limited controller; (4) the composition `EffectiveAuthority = OperatorAuthority × ResourceThrustLimit`; (5) the mass-source interface. ADR-0002 stays valid. 0008 adds a second, independent multiplier and does not change the operator channel.

## Alternatives considered

### Integrator

| Option | Verdict |
|---|---|
| **A. Extend the custom integrator in `FEdenFlightMovementModel` (chosen)** | Deterministic, map-free and unit-testable, fits the fixed-step clock, and keeps sweep collision and the sphere-root invariant. Costs more code for the rotational math. |
| B. Chaos rigid body (`SetSimulatePhysics`, `AddForce` / `AddTorqueInRadians`) | Rejected. Results depend on the physics substep configuration and async physics, so they can't be bit-reproducible in map-free tests. Collision would leave the movement component's ownership. Mass and inertia would come from the collision shape unless overridden. And it would replace a working, tested system rather than extending it. |

### Fixed-step cadence

| Option | Verdict |
|---|---|
| A. Register flight on the existing 10 Hz clock | Rejected. Flight would move in visible 100 ms jumps, and input latency would reach 100 ms. Interpolating the render pose would add another step of delay. |
| B. Lower the global clock to 120 Hz | Rejected. It changes the accepted resource/mission cadence. With `MaxCatchUpSteps = 4`, steps would be dropped below 30 fps. It also re-baselines Checkpoint M. |
| **C. Flight-owned fixed step (default 1/120 s) using the existing `FEdenFixedStepClockModel::CalculateSteps`, gated by the world clock's pause state (recommended)** | Flight becomes frame-rate independent with about 8 ms latency, and resources/missions stay untouched. Fuel keeps sampling demand at 10 Hz. Phase 4 averages demand over the 12 flight steps in each clock step, so propellant accounting is exact. *ADR* — **needs reviewer approval because it differs from the literal "integrate with the existing clock".** |

### Flight Assist

| Option | Verdict |
|---|---|
| Keep the free velocity decay | Rejected. It breaks conservation and costs nothing. |
| **Velocity-tracking controller saturated by real thrust (chosen)** | Assist ON keeps the current feel (stick = commanded velocity), but every newton comes from the same thrusters and is billed to propellant. |

## Physics specification

Internal units are SI and `double` throughout the model: m, s, kg, N, N·m, rad. Conversion to Unreal centimetres happens only in the component (`× 100`).

### Frames

Body frame = Unreal actor local: +X forward, +Y right, +Z up. Rotation input remains (X pitch, Y yaw, Z roll). The model maps it to body angular axes: pitch → about +Y, yaw → about +Z, roll → about +X. Signs are pinned by tests against the current PIE feel.

### Translation (Phase 1)

Per body axis `i ∈ {x,y,z}`, with input `u_i ∈ [-1,1]`, authority `A = OperatorAuthority × ResourceLimit`, and per-direction thrust ratings `F⁺_i`, `F⁻_i` (N):

```text
Assist OFF:  F_i = u_i · (u_i ≥ 0 ? F⁺_i : F⁻_i) · A
Assist ON:   v_des,i = (|u_i| > tol) ? u_i · V_assist : 0
             F_req,i = m · (v_des,i − v_i) / τ
             F_i     = clamp(F_req,i, −F⁻_i·A, +F⁺_i·A)
a_world = R(q) · F_body / m
x_{n+1} = x_n + v_n·Δt + ½·a·Δt²
v_{n+1} = v_n + a·Δt
```

`m = DryMass + PropellantMass` (propellant is fixed at its initial value until Phase 4). With constant `R` and `F` over a step, this update is exact. Assist-off momentum is conserved exactly, because zero force gives `v_{n+1} = v_n`. The assist controller is a discrete first-order lag `(1 − Δt/τ)`: it is stable when `Δt < 2τ` and monotone when `Δt < τ`. Settings validation enforces `τ ≥ 4·Δt_flight`.

Assist-ON speed limit `V_assist` applies only through counter-thrust and is never a clamp. A ship moving faster than `V_assist` when assist turns on decelerates at the available thrust.

Starting design values (not a real vehicle; tunable in `BP_EdenSpacecraftPawn`):

| Parameter | Default |
|---|---|
| `DryMassKilograms` | 10 000 kg |
| `ForwardThrustNewtons` / `ReverseThrustNewtons` | 100 000 / 50 000 N |
| `LateralThrustNewtons` (±Y) / `VerticalThrustNewtons` (±Z) | 20 000 / 20 000 N |
| `AssistMaxSpeedMetersPerSecond` | 100 m/s |
| `AssistResponseTimeSeconds` (τ) | 0.2 s (saturated proportional: brakes at full available thrust, then settles exponentially with no overshoot) |

Sanity check: 100 kN / 10 000 kg = 10 m/s²; 1 s of full forward thrust from rest → 10 m/s and 5 m.

### Rotation (Phase 2)

Body-frame diagonal inertia `I = diag(Ixx, Iyy, Izz)`, with defaults from a solid box `I = m/12 · (b² + c²)` for 20 × 10 × 5 m. Torque per axis `τ_i = u_i · TorqueRating_i · A`. Euler's rigid-body equations:

```text
ω̇ = I⁻¹ · (τ − ω × (I·ω))            (body frame)
q_{n+1} = normalize(q_n ⊗ exp(½·ω·Δt))  (body-frame increment, exponential map)
```

`ω` is integrated with RK4 per step, because of the gyroscopic term. Orientation uses the exponential map, which is exact for constant ω over the step. With `τ = 0`, world-frame angular momentum `L = R·I·ω` and rotational energy `½ωᵀIω` must be conserved within tolerance over 60 s. That includes intermediate-axis tumbling when `Ixx ≠ Iyy ≠ Izz`. Assist ON tracks `ω_des = u · Ω_assist` with the same saturated lag and the per-axis torque limits.

### Assist cost and resources (Phases 3–4)

```text
PropulsionDemandNormalized = clamp( Σ|F_i| / F_ref , 0, 1 ),  F_ref = ForwardThrustNewtons
ṁ = Σ|F_i| / (Isp · g₀)           g₀ = 9.80665 m/s²
```

Phase 4 sets `ConsumptionRateKilogramsPerSecond = F_ref / (Isp·g₀)`, so the existing fuel model bills `demand × rate = ṁ` without changing its API. RCS torque is billed through an equivalent force term `|τ_i| / LeverArm_i`. Resource thrust limit: fuel `Depleted` → 0. Power `Depleted` → assist unavailable and limit = `UnpoweredThrustLimit` (default 0, because valves and controllers need power). Load shed still disables assist (ADR-0002), and the ship then drifts physically.

## Milestones

Each checkpoint lands as a separate commit on `feature/0008-newtonian-flight`. Each must build, pass `Eden.` tests, and be accepted before the next starts.

| CP | Phase | Deliverable | Acceptance |
|---|---|---|---|
| 0 | — | This plan approved; ADR-0003 drafted; `BP_EdenSpacecraftPawn` flight-setting overrides checked | Reviewer approval |
| A | 1 + 5 core | Newtonian translation, assist-ON thrust-limited velocity tracking, assist-OFF momentum; **120 Hz flight fixed step (pulled forward from CP-E)**; flight dynamics snapshot; rotation math unchanged but stepped at 120 Hz | Tests T1–T6, T18–T20 pass; Blueprint guard test passes; existing suite green after migration; PIE: drift on release with assist off |
| B | 2 | Inertia tensor, torque, RK4 ω, quaternion integration; deg/s API kept | T7–T10 pass; PIE: rotation keeps spinning with assist off, settles with assist on |
| C | 3 | Assist as a saturated controller on both linear and angular axes; assist thrust reported in demand | T11–T13 pass |
| D | 4 | `IEdenFlightMassSource`, variable mass, resource thrust limit, Isp-derived fuel rate | T14–T17 pass; Solar Crisis scenario tests still pass |
| E | 5 | Flight fixed step (1/120 s) with accumulator and pause gating; driver changed from per-frame integrate to accumulate | T18–T20 pass |
| F | 6 | Deterministic harness complete; full regression; human PIE checklist; docs (REMEMBER, RECOVER, ADR accepted) | All T* pass; PIE checklist PASS |

Merge rule: `feature/0008-newtonian-flight` is not merged to `main` until **Checkpoint M (ExecPlan 0007) human PIE has passed on the `main` baseline** and 0008 CP-F is accepted.

## Detailed steps

### Checkpoint A — Newtonian translation (Phase 1)

1. `Public/Flight/EdenFlightTypes.h`
   - Add `USTRUCT FEdenFlightPropulsionSettings` with `DryMassKilograms`, `ForwardThrustNewtons`, `ReverseThrustNewtons`, `LateralThrustNewtons`, `VerticalThrustNewtons`, `AssistMaxSpeedMetersPerSecond`, `AssistResponseTimeSeconds` (explicit `Units` meta, `ClampMin`).
   - Leave the linear fields of `FEdenFlightMovementSettings` in place for one checkpoint, marked deprecated in comments. Angular fields remain in use until CP-B.
2. `Public/Flight/EdenFlightMovementModel.h/.cpp`
   - Add `FEdenFlightTranslationState { FVector PositionDeltaMeters; FVector VelocityWorldMetersPerSecond; }` and `FEdenFlightTranslationResult` (state, applied body force N, demand, sanitization flags).
   - Add `static FEdenFlightTranslationResult IntegrateTranslation(const FEdenFlightTranslationState&, const FEdenFlightInputCommand&, const FEdenFlightPropulsionSettings&, double MassKilograms, double Authority, const FQuat& BodyRotation, double DeltaSeconds)`, implementing the equations above, with input sanitization reused from `SanitizeCommand`.
   - Add `static bool ValidatePropulsionSettings(const FEdenFlightPropulsionSettings&, double FlightStepSeconds, TArray<FString>* OutErrors)`.
   - Split the existing `IntegrateVelocity` so it keeps doing angular integration only (removed in CP-B).
3. `Public/Flight/EdenFlightMovementComponent.h` / `.cpp`
   - Add `UPROPERTY(EditAnywhere) FEdenFlightPropulsionSettings PropulsionSettings`.
   - In `MoveWithCommand`: compute `Authority`, call `IntegrateTranslation`, convert m → cm for `Velocity` and the sweep delta, and set `PropulsionDemandNormalized` from applied force.
   - Remove the linear `ClampVectorMagnitude` path. Collision keeps `RemoveInwardVelocity` + `SlideAlongSurface` (normal momentum is deliberately lost on impact — recorded in ADR-0003).
   - Add read-only `GetAppliedBodyForceNewtons()`, `GetSpeedMetersPerSecond()`, `GetMassKilograms()`.
4. Debug: add a `Flight:` line to `ShowDebug EdenSystems` (`AEdenSpacecraftPawn::DisplayDebug`) showing speed m/s, applied force kN, mass kg, assist on/off, effective authority.
5. Tests: create `Private/Tests/EdenFlightDynamicsTests.cpp` (T1–T6). Migrate `Eden.Unit.Flight.LinearVelocityAcceleratesAndClamps` → `…AssistTracksCommandedVelocity` and `StabilizationDampsReleasedAxes` → linear-assist expectations. Keep the angular expectations unchanged until CP-B.
6. Content: set `PropulsionSettings` defaults on `BP_EdenSpacecraftPawn` if it overrides movement. Record before/after values here.

### Checkpoints B–F

Same pattern, detailed in this file when each checkpoint opens. Primary files: `EdenFlightTypes.h`, `EdenFlightMovementModel.*`, `EdenFlightMovementComponent.*`, `EdenSpacecraftPawn.cpp` (resource-limit wiring, CP-D), `EdenFuelSystemComponent.*` (implements `IEdenFlightMassSource`, CP-D), `EdenFlightPlayerController.cpp` (driver semantics, CP-E), `DA_EdenFuelConfig` (Isp-derived rate, CP-D), and `docs/decisions/ADR-0003-*.md`.

## Validation

### Deterministic physics harness

`Private/Tests/EdenFlightSimulationHarness.h/.cpp` (test-only) drives the pure model, without a world:

- Input: initial state, settings, and a **scripted input timeline in simulation time** (`TArray<{StartSeconds, Command}>`).
- Frame source: `Fixed(Hz)` for 30, 60, 144 and 240 Hz, plus `Jittered(seed, meanHz, ±%)` with a seeded `FRandomStream`.
- Pipeline (from CP-E): frame Δt → `FEdenFixedStepClockModel::CalculateSteps` at 1/120 s → model step with the command active at that step's sim time. Before CP-E, it steps the model directly with each frame's Δt.
- Output: per-step trace (t, x, v, q, ω, F, τ, demand, mass) plus a final-state comparison.
- Equality modes: **sim-time-scripted input → final state bit-identical across all frame sources** (CP-E onward). **Frame-sampled input** (as the real controller does) → difference bounded by one flight step of command latency, with that tolerance asserted.

### Test catalogue (`Eden.Unit.Flight.Dynamics.*` unless noted)

| ID | Test | Expected (tolerance) |
|---|---|---|
| T1 | IdealizedConstantMassThrustFromRest | **Idealized: constant mass, no propellant use.** 60 s at 100 kN / 10 t: v = 600 m/s, x = 18 000 m (rel 1e-9). Variable mass is tested separately in T14/T15. |
| T2 | MomentumConservedAssistOff | 10 s burn, then 50 s coast: v constant (abs 1e-12 m/s), x linear in t |
| T3 | PerDirectionThrustRatings | Reverse uses 50 kN, lateral/vertical 20 kN; authority 0.5 halves a |
| T4 | ThrustFollowsOrientation | Rotate body 90° yaw, then thrust: acceleration along new +X (abs 1e-9) |
| T5 | AssistTracksCommandedVelocity | Settles to `u·V_assist` within 5τ; no overshoot; force never exceeds rating |
| T6 | AssistBrakingIsThrustLimited | From 300 m/s, assist on, no input: deceleration ≤ F⁻/m every step; stops at analytically predicted time ±1 step |
| T7 | TorqueFreeSpinStable | Single principal axis spin: ω constant for 60 s (rel 1e-9) |
| T8 | AngularMomentumConserved | Tumbling, τ = 0, unequal I: \|L_world\| and E_rot drift < 1e-6 relative over 60 s |
| T9 | QuaternionStaysNormalized | \|q\| − 1 < 1e-12 after 60 s at max rates |
| T10 | ConstantTorqueAngularAccel | τ/I matches ω̇ (rel 1e-9); legacy deg/s getter agrees |
| T11 | AssistOffNoAngularDamping | Released stick keeps ω |
| T12 | AssistDemandBillsPropellant | Braking with assist produces nonzero demand; idle at rest produces zero |
| T13 | LoadShedRemovesAssist | `Eden.Integration` — Shed → assist off → ship drifts |
| T14 | MassDecreasesWithBurn | m(t) = m₀ − ṁt; a(t) = F/m(t) (Tsiolkovsky Δv = Isp·g₀·ln(m₀/m₁), rel 1e-6) |
| T15 | FuelDepletedRemovesThrust | `Eden.Integration` — empty tank → zero applied force, ship coasts |
| T16 | PowerDepletedLimitsPropulsion | `Eden.Integration` — limit applied, assist unavailable |
| T17 | OperatorAuthorityContractUnchanged | `GetThrustAuthority()` still equals operator value; telemetry fields unchanged |
| T18 | FrameRateIndependence60s | Harness: scripted 60 s profile (burn/rotate/coast/assist) identical at 30/60/144/240/jittered |
| T19 | FrameSampledInputBounded | Harness: controller-style sampling within one-step latency bound |
| T20 | PauseFreezesFlight | Clock paused → no flight steps; resumes without a jump |

Regression: `scripts/Validate-Project.ps1 -Build -RunTests -TestFilter Eden.` must stay green at every checkpoint (baseline 340/340). The existing `EdenRuntimeCompositionTests` composition assertions (nonzero demand under thrust; zero demand clears fuel use) must hold unchanged.

### Manual PIE checklist (CP-F, on `L_FlightSandbox`)

1. Assist off: tap W, release — ship drifts at constant speed (`ShowDebug EdenSystems` speed steady).
2. Assist off: tap yaw, release — rotation continues.
3. Assist on: release all — linear and angular motion settle using thrust; demand > 0 while settling.
4. `P` Reduced → acceleration visibly halves.
5. `L` Shed → assist unavailable → ship drifts.
6. Drain fuel (`SetFuelQuantityKilograms` via debug) → no thrust.
7. `StartMission` Solar Crisis runs to completion; Output Log clean.
8. Frame cap test: `t.MaxFPS 30` vs `144` — same 10 s burn yields the same speed readout.

### Performance

The model costs O(1) per flight step. At 120 Hz that is 120 sweeps per second per ship (`SafeMoveUpdatedComponent`), which is negligible for one ship. Catch-up uses `MaxCatchUpSteps` (flight default 8). Below 15 fps, simulation time slows rather than spiralling. Verify with `stat game` that the movement component stays under 0.1 ms per frame.

## Failure modes and rollback

| Risk | Detection | Mitigation / rollback |
|---|---|---|
| Flight feel regresses (too sluggish or too twitchy) | PIE checklist | All values are data in `PropulsionSettings`; assist ON preserves the stick-equals-velocity feel |
| Blueprint overrides old `MovementSettings` and the new fields stay default | CP-0 in-editor inspection | Record and migrate explicitly in CP-A step 6 |
| Assist controller oscillates | T5/T6, settings validation `τ ≥ 4Δt` | Reject invalid settings with a logged warning and fall back to defaults |
| Propellant drains much faster or slower than the Solar Crisis tuning expects | Solar Crisis scenario tests in CP-D | Tune Isp / `DA_EdenFuelConfig`; the scenario's `ConservePropellant` objective is reviewed explicitly |
| EDEN contract drift | T17, `EdenOsWireSerializationTests` | Wire fields keep semantics; no schema change in 0008 |
| Checkpoint M baseline disturbed | All 0008 work is on a branch; the Editor copy (`EdenSpaceSimulator 5.8`) stays on `main` | Merge only after M PIE PASS |
| Long-distance precision | Harness at 18 km | UE5 large-world coordinates (double); floating origin is out of scope |

Rollback: each checkpoint is one commit; `git revert` restores the previous accepted checkpoint. `v0.1.0-flight-shell` and `main` remain the kinematic reference.

## Progress log

- 2026-10-12: Audited flight, clock, fuel, power, operator, telemetry, and tests on `main` @ `9738130`. Created branch `feature/0008-newtonian-flight`. Drafted this plan. No production code changed.
- 2026-10-12: Plan approved and committed (`d2aa929`). Checkpoint A implemented:
  - `FEdenFlightPropulsionSettings`, `FEdenFlightStepSettings` (`FixedStepHz`, stored as a rate so the step is exact in double), and `FEdenFlightDynamicsSnapshot`.
  - Model layers: thrust envelope, direct/assist controllers, `IntegrateTranslation`, `CalculateFixedSteps`, and `StepFlight`, shared by the component and the harness.
  - Component: 120 Hz loop with pause gating and counted drops, SI↔cm conversion, and the dynamics snapshot.
  - Telemetry: in-process `Flight.Dynamics`.
  - Debug: `ShowDebug EdenSystems` flight lines.
  - Tests: harness `Private/Tests/EdenFlightSimulationHarness.h`, `Eden.Unit.Flight.Dynamics.*`, `Eden.Integration.Flight.*`, and migrated `Eden.Unit.Flight.*`.
  - Docs: ADR-0003 (Proposed).
- 2026-10-12: Deviation — the linear fields of `FEdenFlightMovementSettings` were removed immediately rather than kept deprecated for one checkpoint. Only tests referenced them, and the Blueprint does not override them.
- 2026-10-12: Deviation — `AssistResponseTimeSeconds` default 0.2 s (the plan draft said 0.5 s), for crisper settling. The controller saturates at full thrust, then settles exponentially.
- 2026-10-12: Test migration. `Eden.Unit.Flight.LinearVelocityAcceleratesAndClamps` was removed (superseded by `Dynamics.AssistTracksCommandedVelocity` / `MomentumConservedAssistOff`). `StabilizationDampsReleasedAxes` became `AngularStabilizationDampsReleasedAxes` (linear parts moved to Dynamics). `Eden.Integration.Runtime.BlueprintPawnUsesRuntimeSystems` now releases controls with assist off to prove zero demand and zero fuel use, and separately asserts that assist braking bills demand.
- 2026-10-12: Checkpoint B implemented:
  - `FEdenFlightAttitudeSettings` (diagonal inertia from a 20×10×5 m, 10 t box; roll/pitch/yaw torque 80/180/200 kN·m; assist 60 deg/s, τ 0.2 s; orientation hold 1 s).
  - Model: `FEdenFlightTorqueEnvelope`, `FEdenFlightAttitudeRequest`, `MapRotationInputToBodyAxes`, `ResolveAttitudeTorque`, `IntegrateRotation`, angular momentum and energy helpers.
  - `StepFlight` integrates rotation after translation.
  - Component state is now SI body rates (`Get/SetAngularVelocityBodyRadiansPerSecond`).
  - Rotation fields added to the dynamics snapshot, plus a `ShowDebug EdenSystems` attitude line.
  - Tests: `Eden.Unit.Flight.Rotation.*` and an extended `Eden.Integration.Flight.FixedStepFollowsSimulationPause`.
  - ADR-0003 updated.
- 2026-10-12: Deviation (CP-B). `FEdenFlightMovementSettings` and `UEdenFlightMovementComponent::MovementSettings` (kinematic rotation) were removed and replaced by `FEdenFlightAttitudeSettings` / `AttitudeSettings`. `Get/SetAngularVelocityLocalDegreesPerSecond` were replaced by `Get/SetAngularVelocityBodyRadiansPerSecond`. Only tests used them, and the Blueprint did not override them. `InputReleaseTolerance` became the model constant `FEdenFlightMovementModel::InputReleaseTolerance` (0.001, unchanged value).
- 2026-10-12: Test migration (CP-B). `Eden.Unit.Flight.AngularVelocityAcceleratesAndClamps` and `AngularStabilizationDampsReleasedAxes` were removed (kinematic) and superseded by `Rotation.ConstantTorqueAngularAcceleration`, `AssistTracksCommandedRate`, `AssistStabilizesThroughTorque` and `AssistOffConservesSpinAfterRelease`. `InvalidDeltaTimeDoesNotMutateVelocity` and `EquivalentSimulatedTimeMatchesAcrossDeltaTimePartitions` now check rigid-body rotation.
- 2026-10-12: Checkpoint C implemented:
  - Attitude torque is billed as RCS thrust: `ThrusterOutput = Σ|F| + Σ|τᵢ|/leverArmᵢ`, with lever arms 5/10/10 m (`FEdenFlightAttitudeSettings::ThrusterLeverArmsMeters`). `PropulsionDemandNormalized = ThrusterOutput / F_fwd`, clamped to [0, 1].
  - `Dynamics.ThrusterOutputNewtons` (unclamped) is added to the snapshot and the debug line.
  - T11 is `Rotation.AssistOffConservesSpinAfterRelease` (CP-B). T12 is `Dynamics.AssistBrakingIsThrustLimited` / `AssistAvailabilityAndIdleCost` (CP-A) plus the new `Rotation.AttitudeThrustBillsPropellant`. T13 is the new `Eden.Integration.Flight.OperatorControlConstrainsPhysicalFlight`, using the real Blueprint pawn: load shed means drift and spin with zero propellant; normal load means assist brakes and burns propellant; reduced priority scales physical thrust and torque.
  - The assist controllers were already saturated on all six axes after CP-A and CP-B, so CP-C added no controller change.
- 2026-10-12: Balance note (CP-C). Rotating now burns propellant: full yaw reads 0.2 demand, so 0.2 kg/s at the default 1 kg/s rate. Decision 2 still holds; there is no capacity or rate re-tune. Phase 4 calibrates.
- 2026-10-12: Correction to the CP-B report. CP-B added **12** `Eden.Unit.Flight.Rotation.*` tests, not 13. With the two removed kinematic tests the net is +10 (354 → 364); the run diff shows no other test removed.
- 2026-10-12: Feel change (CP-B), to tune in PIE. With the default inertia and torque, angular acceleration is about 27–44 deg/s² (the kinematic shell used 180 deg/s²), and assist tops out at 60 deg/s (was 90).

## Decision log

- 2026-10-12 (CP-B): Rotation uses the same layering as translation. The attitude request (`Torque` | `BodyRate` | `Orientation`) resolves to torque inside `FEdenFlightTorqueEnvelope`, then `IntegrateRotation` (RK4 on Euler's equations and the quaternion kinematics). The `Orientation` mode is the autopilot/EDEN seam and has no caller in 0008, so EDEN gains no control privileges.
- 2026-10-12 (CP-B): Operator effective thrust authority now scales attitude torque as well as thrust (RCS is propulsion). This extends ADR-0002, where authority scaled translation only.
- 2026-10-12 (CP-B): Attitude torque is not yet billed in `PropulsionDemandNormalized`, to avoid rebalancing (Decision 2). Billing via equivalent thrust arrives in Phase 3/4.
- 2026-10-12 (CP-B): Rate-assist torque includes gyroscopic feed-forward `w x (I w)` so multi-axis rates hold without drift. It is still saturated by the envelope.

- 2026-10-12: Extend the custom integrator rather than adopt Chaos (determinism, testability, existing ownership).
- 2026-10-12 (approved, Decision 1): dedicated 120 Hz flight fixed step; resources and missions stay at 10 Hz on `UEdenSimulationClockSubsystem`. Flight advances only while the world and the simulation clock are both unpaused, from the same world delta time. Flight keeps its own `double` accumulator (the clock model's `float` accumulator is not precise enough over long sessions). Catch-up is bounded (default 60 steps = 0.5 s per update) to avoid a death spiral. Any time beyond that is **counted, logged as a warning, and exposed in the flight dynamics snapshot** — never silently discarded. The fixed step moves from CP-E into CP-A, because the frame-rate harness needs it.
- 2026-10-12 (approved, Decision 2): keep the 100 kg fuel capacity and existing balance in Step 1. Phase 4 calibrates capacity, Isp, thrust, and mass together; makes propellant use change mass and available thrust; and stops fuel-dependent engines on an empty tank. Propulsion parameters stay data-driven per spacecraft.
- 2026-10-12: `GetThrustAuthority()` keeps operator semantics; effective authority is a new getter.
- 2026-10-12: Architecture layers so new ships, engines, controllers, and AI pilots plug in without rewriting physics: **intent** (`FEdenFlightInputCommand`) → **flight controller** (assist off: direct; assist on: velocity-hold), producing a body-frame force request → **thrust envelope** (`FEdenFlightThrustEnvelope`, from propulsion settings × authority; future engines aggregate into it) → **dynamics** (`IntegrateTranslation`, which only knows force, mass, orientation and time).
- 2026-10-12: `PropulsionDemandNormalized` = Σ|applied body force| / `ForwardThrustNewtons`, clamped to [0, 1]. A full forward burn reads 1.0 exactly as before (no rebalance). Assist thrust now counts, so assist braking bills propellant. Combined multi-axis burns above 1.0 are clamped; Phase 4 replaces this with an unclamped mass flow ṁ = Σ|F|/(Isp·g₀).
- 2026-10-12: The EDEN wire and Telemetry Export v1 serialize flight fields by hand, so new dynamics data lives in `FEdenFlightStateSnapshot::Dynamics` in-process only. Wire and export JSON are unchanged.
- 2026-10-12: `BP_EdenSpacecraftPawn` / `BP_EdenFlightPlayerController` name tables contain no `MovementSettings` or flight tuning properties: no Blueprint overrides of the C++ flight model. A guard test (`Eden.Integration.Flight.SpacecraftBlueprintUsesValidFlightModel`) keeps it that way.

## Acceptance evidence

None yet.

## Handoff

- Open question for the reviewer: approve fixed-step option C (flight-owned 120 Hz) versus registering on the 10 Hz world clock.
- Open question: the default `DA_EdenFuelConfig` capacity of 100 kg is about 1 % of dry mass, so propellant mass will barely change acceleration. Re-tune capacity and Isp in CP-D, or keep the scenario balance?
- Next clean task after approval: Checkpoint 0 in-editor inspection, then Checkpoint A.
