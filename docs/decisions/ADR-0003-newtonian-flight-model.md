# ADR-0003: Newtonian Flight Model, Flight Assist, and Flight Fixed Step

## Status

Proposed (ExecPlan 0008). Accepted when 0008 Checkpoint F passes.

## Date

2026-10-12

## Context

ExecPlan 0002 shipped a kinematic flight shell. Input set a target velocity, and velocity moved toward it at a fixed rate, with a hard speed cap and free braking. ExecPlan 0008 replaces that with physically consistent flight that later phases (rigid-body rotation, variable mass, docking, autopilot, EDEN pilots) can build on, without disturbing the 10 Hz resource and mission simulation that Checkpoint M validated.

## Decision

### Custom deterministic integrator, not Chaos

`FEdenFlightMovementModel` stays the pure, map-free owner of flight math. `UEdenFlightMovementComponent` stays the only owner of flight state and keeps sweep collision on the C++ sphere root. Chaos rigid bodies are not used for spacecraft flight.

### Layered flight pipeline

```text
FEdenFlightInputCommand (intent)
  -> flight controller: ResolveDirectThrust (assist off) | ResolveAssistThrust (assist on)
  -> FEdenFlightThrustEnvelope: per-axis +/- newtons x authority; ClampToEnvelope
  -> IntegrateTranslation: a = R F / m, exact for constant force over a step
```

Each layer can be replaced on its own. New spacecraft change `FEdenFlightPropulsionSettings`. New engine layouts reduce to a thrust envelope. New controllers (autopilot, docking, an EDEN pilot) produce a body-frame force request. None of them changes the dynamics.

### SI units internally

Model math uses metres, seconds, kilograms and newtons in `double`. The component converts to Unreal centimetres only at its boundary (`EdenFlightUnits`).

### Flight Assist is a thrust-limited controller

- Assist off: thrust = stick × available thrust. With no thrust, velocity is conserved exactly. There is no cap and no damping.
- Assist on: each body axis tracks `stick × AssistMaxSpeed` (0 when released) with force `m (v_target − v) / τ`, saturated by the envelope. It never assigns velocity. Its thrust counts toward propulsion demand, so it bills propellant.
- Assist availability is still owned by movement and commanded by operator control (ADR-0002). Load shed removes it, and the ship then drifts physically.

### Rigid-body rotation (Checkpoint B)

Rotation uses the same layering as translation:

```text
pilot stick (pitch, yaw, roll) -> MapRotationInputToBodyAxes -> MakePilotAttitudeRequest
  -> FEdenFlightAttitudeRequest { Torque | BodyRate | Orientation }
  -> ResolveAttitudeTorque -> FEdenFlightTorqueEnvelope (per-axis N*m x authority)
  -> IntegrateRotation (Euler's equations, RK4 on (q, w), renormalized quaternion)
```

- State is a world orientation quaternion `q` and a body-frame angular velocity `w` (rad/s). The inertia tensor is diagonal in body principal axes (`FEdenFlightAttitudeSettings::PrincipalMomentsOfInertiaKilogramMetersSquared`) and is validated against the rigid-body triangle inequality.
- Dynamics: `I w_dot = tau - w x (I w)` and `q_dot = q (x) (0, w) / 2`, using classical RK4 with constant torque over the 120 Hz step. With no torque, world angular momentum `R(q) I w` and energy `w.(I w)/2` are conserved to integration accuracy, including asymmetric tumbling.
- Assist off: `Torque` request, stick × available torque. Released controls give zero torque, and the ship keeps its angular momentum.
- Assist on: `BodyRate` request, stick × `AssistMaxAngularRate` (0 when released): `tau = I (w_target - w) / tau_a + w x (I w)`, saturated per axis. The gyroscopic feed-forward lets assist hold multi-axis rates. It is only a torque request, so it never writes `w`.
- `Orientation` request: an autopilot seam with no caller yet. The body-frame rotation-vector error `e` gives `w_target_i = sign(e_i) min(|e_i| / T_o, sqrt(alpha_i |e_i|), W_max)`, which approaches no faster than the actuators can brake, then resolves like `BodyRate`. Future autopilots and EDEN must use this same request type and envelope. No EDEN path to it exists in 0008.
- Input signs keep the kinematic shell's feel. Unreal `FRotator` +pitch, +yaw and +roll turn about body −Y, +Z and −X, and `MapRotationInputToBodyAxes` pins that mapping.
- The operator's effective thrust authority scales the torque envelope as well as the thrust envelope (RCS is propulsion). This extends ADR-0002, where authority scaled translation only.
- Translation is integrated with the orientation at the start of each step, then rotation advances. Rotation never changes linear momentum directly; it only redirects later thrust.

### Dedicated 120 Hz flight fixed step

Flight advances in fixed steps (`FEdenFlightStepSettings::FixedStepHz`, default 120) from world delta time, using a `double` accumulator. It does not advance while the world or `UEdenSimulationClockSubsystem` is paused. Resources and missions stay on the 10 Hz clock. Catch-up is bounded (`MaxStepsPerUpdate`, default 60 = 0.5 s). Steps beyond that are counted, logged, and exposed in `FEdenFlightDynamicsSnapshot::DroppedFlightStepCount`, never silently discarded.

### Telemetry contract preserved

`GetThrustAuthority()` keeps operator semantics. `GetEffectiveThrustAuthority()` reports the product of all limits. `PropulsionDemandNormalized` = Σ|applied force| / forward thrust, clamped to [0, 1], so a full forward burn reads 1.0 as before. Attitude torque is **not** counted in demand yet (see Consequences).

Richer dynamics live in `FEdenFlightStateSnapshot::Dynamics`, in-process only:

- translation: velocity, acceleration, applied and available thrust, mass
- rotation: orientation, body rates, angular acceleration, applied and available torque, world angular momentum, rotational energy
- assist state and step counters

The EDEN OS wire format and Telemetry Export v1 are unchanged.

### Fuel: demand versus availability

Until ExecPlan 0008 Phase 4:

- Propulsion demand **drives fuel consumption** (`UEdenFuelSystemComponent` reads it through `IEdenPropulsionDemandSource`).
- Fuel **availability does not yet constrain thrust or torque**. An empty tank still produces full thrust.

Phase 4 adds fuel- and power-derived limits to `GetEffectiveThrustAuthority()`, propellant mass, and Isp-based mass flow.

## Alternatives considered

### Chaos rigid body with AddForce / AddTorque

Rejected. Results depend on physics substepping and async physics, so they can't be reproduced bit-for-bit in world-free tests. Ownership of collision and transform would move to the physics scene. It would also replace a tested system rather than extend it.

### Register flight on the 10 Hz world clock

Rejected. Motion would update in 100 ms jumps with up to 100 ms of input latency. Render interpolation would add more delay.

### Raise the world clock to 120 Hz

Rejected. It changes the accepted resource and mission cadence that Checkpoint M validated. It also drops steps below 30 fps with the current `MaxCatchUpSteps = 4`.

### Keep assist as free velocity decay

Rejected. It violates momentum conservation, costs nothing, and can't be reasoned about by an AI pilot.

## Consequences

### Positive

- Physically consistent, deterministic flight. The same scripted 60 s profile is bit-identical at 30, 60, 144 and 240 Hz and with jittered frame rates.
- A clear seam for future propulsion types and pilots.
- Truthful demand and capability data for HUD, telemetry and EDEN.

### Negative

- Assist braking now consumes propellant, which slightly changes resource balance during manual flight.
- Collision response is still inelastic slide (normal momentum is removed). There is no angular response to impact.
- Input is still sampled once per render frame, so a command can take effect up to one frame late.
- Fuel samples demand at 10 Hz from the latest flight step. Averaging over flight steps arrives in 0008 Checkpoint D.
- Attitude control is not yet billed to propellant, and empty tanks do not yet disable thrust or torque (Phase 4).
- The inertia tensor is constant and the centre of mass is fixed. Propellant depletion does not shift either.
- Torque is lumped per body axis. There are no individual thruster positions, and torque produces no translation coupling.
- Per-axis decoupled orientation control is a seam, not a time-optimal autopilot. Large multi-axis slews converge but do not follow the shortest-time path.
- RK4 conserves angular momentum and energy to integration accuracy, not exactly. The tests bound the drift at under 1e-6 relative over 60 s.

## Validation

- `Eden.Unit.Flight.Dynamics.*`: analytic results, conservation, envelope, assist, accumulator, frame-rate independence, frame-sampled bounds.
- `Eden.Unit.Flight.Rotation.*`: constant torque, input signs, torque-free spin, asymmetric angular-momentum and energy conservation, release, assist stabilization and rate tracking, authority saturation, orientation request, thrust redirection, 60 s tumble frame-rate independence, invalid inputs.
- `Eden.Integration.Flight.*`: pause gating for translation and rotation, in-world fixed step, Blueprint guard.
- Migrated `Eden.Unit.Flight.*` and the full `Eden.` regression.

## Supersedes / Superseded by

Partially supersedes ExecPlan 0002's kinematic translation decision. ADR-0002 remains in force.
