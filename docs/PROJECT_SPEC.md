# Eden Space Simulator Spec v0.1

## Problem

Build a compact, credible spacecraft operations simulator that allows a player to pilot a vehicle, manage critical systems, respond to failures, and produce meaningful telemetry. The project must serve as both a playable technical demonstration and a professional Unreal C++ portfolio system that can later integrate with EDEN OS.

## Users

- Player or trainee operating the spacecraft
- Developer extending simulation systems
- Reviewer evaluating architecture, testing, and engineering discipline
- Future EDEN OS service consuming telemetry and producing recommendations

## Constraints

- Engine: Unreal Engine 5.8
- Language: C++20 as supported by Unreal, with Blueprints for presentation and content
- Platform: Windows desktop first
- Source control: Git with Git LFS
- Initial mode: single-player
- Core domain logic must not depend on widgets or external network availability
- Machine-specific paths must not be committed
- Binary Unreal assets cannot be safely merged as plain text
- Development must follow `docs/SDLC.md`

## Performance targets

For the first vertical slice:

- Maintain a playable 60 FPS target on the development machine in the flight sandbox.
- Keep resource and mission simulation on an explicit fixed cadence, initially 10 Hz unless profiling or design requires another value.
- Avoid avoidable allocations in per-frame or fixed-step hot paths.
- Telemetry collection must not materially stall the game thread.
- Expensive diagnostics must be gated for development builds when appropriate.

These are initial engineering targets, not guarantees across all hardware.

## Scope

### In scope for the first vertical slice

- Six-degree-of-freedom spacecraft control
- Flight stabilization option
- Fuel, power, and thermal state
- One emergency scenario
- Docking or stabilization objective
- HUD for critical state
- Alerts and failure feedback
- Mission result
- Telemetry snapshots and local export or inspection
- Automated tests for deterministic domain behavior

### Out of scope

- Multiplayer
- Persistent online accounts
- Production cloud deployment
- Procedural universe generation
- Seamless atmosphere-to-space travel
- Full orbital mechanics
- Combat and weapons
- Economy, inventory, or crafting
- Generative AI as an authoritative controller
- Final art production

## Success criteria

Foundation:

- Repository hygiene and documentation are present.
- Win64 Development Editor target builds.
- Project automation smoke test passes.
- Required agent and recovery documents exist.

Flight:

- Player can translate and rotate across all six degrees of freedom.
- Input is based on Enhanced Input.
- Fuel use is observable and bounded.
- Flight controls have deterministic unit-tested calculations where practical.

Systems:

- Power and thermal state have one owner each.
- System state transitions are logged and visible.
- Invalid configuration is rejected or clamped with an actionable warning.
- Fixed-step simulation is independent of rendering frame rate.

Mission:

- A data-driven scenario can trigger at least one failure.
- Objective and failure conditions are explicit.
- The scenario can be restarted without restarting the editor.
- Mission outcome and key state transitions are recorded.

Telemetry:

- Immutable snapshots contain mission time, power, fuel, temperature, active failures, and mission status.
- A local sink can consume snapshots without accessing mutable simulation internals.
- A future network sink can be added without changing domain ownership.

## State ownership

| State | Authoritative owner |
|---|---|
| Spacecraft transform and velocities | Flight movement component / Unreal physics body |
| Player input intent | Player controller or input command layer |
| Fuel quantity and consumption | Fuel system component |
| Power generation, storage, and load | Power system component |
| Thermal energy or temperature | Thermal system component |
| Mission phase and objectives | Mission subsystem |
| Simulation time and fixed-step cadence | Simulation clock subsystem |
| Active failure definitions and lifecycle | Mission/failure orchestration layer |
| UI display state | Derived view data only, never authoritative |
| Telemetry history and delivery status | Telemetry subsystem and sink |
| External service connection state | Telemetry adapter |

## Feedback surfaces

- Project-specific structured Unreal log categories
- Development HUD or debug overlay
- Mission event timeline
- Automation test output
- Build and validation script exit codes
- Telemetry snapshots
- Explicit warning and failure states
- After-action summary

## Failure modes

| Failure | Required behavior |
|---|---|
| Invalid data asset configuration | Fail validation with asset and field context |
| Missing optional presentation asset | Log warning and preserve simulation where safe |
| Missing required simulation dependency | Fail loudly during initialization |
| Fixed-step overrun | Record warning and use a defined catch-up or drop policy |
| NaN or invalid physical value | Detect, log context, and enter a safe failure path |
| Fuel or power reaches zero | Produce an explicit state transition and mission consequence |
| Telemetry sink fails | Preserve simulation; record delivery failure without mutating domain state |
| External service unavailable | Continue local simulation and queue/drop according to adapter policy |
| Interrupted AI session | Recover through `docs/RECOVER.md`, Git state, and active ExecPlan |
| Build environment unavailable | Mark validation pending; never claim a successful build |

## Security and privacy

- Do not commit API keys, tokens, credentials, personal data, or machine-specific secrets.
- External telemetry must use explicit schemas and validation.
- Logs must not include secrets.
- Network integration must define timeout, retry, and failure behavior before production use.

## Definition of done

See `README.md`, `docs/SDLC.md`, and `docs/REVIEW.md`.
