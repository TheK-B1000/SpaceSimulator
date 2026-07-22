# Roadmap

The roadmap delivers complete vertical capabilities rather than a wide field of half-built systems.

## Phase 0: Professional foundation

Deliverables:

- Agent contract and project documentation
- Git LFS and repository hygiene
- Build and validation scripts
- Source and content organization
- Project-specific logging
- Minimal automation smoke test
- Verified Win64 Development Editor build

Exit criteria:

- Bootstrap acceptance criteria pass.
- `docs/RECOVER.md` contains a verified checkpoint.

## Phase 1: Flight shell

Deliverables:

- Spacecraft pawn
- Six-degree-of-freedom movement component
- Enhanced Input actions and mapping context
- Cockpit and external camera support
- Optional stabilization
- Developer flight telemetry
- Unit tests for command processing and deterministic calculations

Exit criteria:

- Player can translate and rotate in all axes.
- Movement remains stable across expected frame-rate variation.
- No art dependency is required for functionality.

## Phase 2: Resource simulation

Deliverables:

- Fixed-step simulation clock
- Fuel system
- Power system
- Thermal system
- State-transition events
- Configuration data assets
- Unit and integration tests

Exit criteria:

- Systems have one authoritative owner each.
- Equal simulated time produces expected equivalent state where intended.
- Depletion and critical states are visible and logged.

## Phase 3: Mission and failure orchestration

Deliverables:

- Mission definition
- Mission subsystem
- Solar-event or equipment-failure scenario
- Objectives
- Success and failure conditions
- Restart/reset
- Event timeline

Exit criteria:

- A complete emergency scenario can be started, completed, failed, and restarted.
- Mission truth is independent from UI.

## Phase 4: Operator UI and after-action review

Deliverables:

- HUD
- Alerts
- System controls
- Mission status
- After-action summary
- Accessibility and input review

Exit criteria:

- Player can understand system state and consequences without developer logs.
- UI contains no authoritative simulation state.

## Phase 5: Telemetry boundary

Deliverables:

- Immutable telemetry schema
- Telemetry subsystem
- Local sink or export
- Delivery status and bounded history
- Integration tests

Exit criteria:

- Telemetry can be consumed without accessing mutable domain internals.
- Sink failure does not corrupt or stop local simulation.

## Phase 6: EDEN OS adapter

Deliverables:

- Transport decision ADR
- HTTP or WebSocket adapter
- Schema versioning
- Timeout, retry, and backpressure behavior
- Authentication and secret handling
- Offline behavior
- End-to-end demo

Exit criteria:

- EDEN OS can consume simulator telemetry.
- Network loss is observable and recoverable.
- Domain code remains transport-independent.

## Phase 7: Polish and portfolio release

Deliverables:

- Packaged Windows build
- Performance pass
- Automated build/test workflow
- Architecture diagrams
- Demonstration video
- Technical write-up
- Known limitations and future work

Exit criteria:

- A new machine can build the project from documented steps.
- The project can be explained, demonstrated, tested, and recovered.
