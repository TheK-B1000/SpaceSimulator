# ADR-0001: Component-Oriented C++ Simulation With Blueprint Presentation

## Status

Accepted

## Date

2026-07-22

## Context

Eden Space Simulator must support real-time spacecraft control, fixed-step resource simulation, mission failures, UI, telemetry, and future EDEN OS integration. The project must remain understandable and testable while using Unreal Engine's actor, component, subsystem, reflection, and Blueprint ecosystems.

Placing all behavior in a pawn, Level Blueprint, or global manager would create tangled ownership and make automated testing difficult. Placing all content and presentation in C++ would slow iteration and misuse Unreal's content-authoring strengths.

## Decision

Use a component-oriented C++ architecture:

- Actor components own spacecraft system state.
- A movement component owns flight behavior.
- A world-scoped simulation clock owns fixed-step time.
- A mission subsystem owns mission progression.
- A telemetry subsystem consumes immutable snapshots.
- External EDEN OS communication uses a sink/adapter boundary.
- C++ owns reusable behavior and domain rules.
- Blueprints own composition, tuning, assets, and presentation.

Every mutable state value has one authoritative owner.

## Alternatives considered

### Blueprint-first architecture

Advantages:

- Fast visual iteration
- Low initial C++ setup

Rejected because:

- Core rules and ownership would be harder to test and review.
- Large Blueprint dependency graphs can hide blast radius.
- Future telemetry and backend integration would become presentation-coupled.

### Monolithic spacecraft actor

Advantages:

- Fewer files
- Simple initial access to all state

Rejected because:

- Violates single responsibility as systems grow.
- Encourages direct cross-system mutation.
- Makes unit testing and replacement difficult.
- Creates a likely high-blast-radius class.

### Standalone engine-independent simulation library from day one

Advantages:

- Maximum isolation and potential portability
- Strong pure C++ testing

Deferred because:

- It adds build and integration complexity before the domain stabilizes.
- Unreal components and data assets are useful boundaries for the first vertical slice.
- Pure calculation types can still be isolated within the Unreal module and extracted later if justified.

## Consequences

### Positive

- Clear state ownership
- Testable domain calculations
- Strong Unreal integration
- Blueprint iteration remains available
- Telemetry transport can change independently
- Smaller blast radius

### Negative

- More explicit interfaces and coordination
- Requires discipline around component communication
- Some integration tests need Unreal world setup
- Binary Blueprint assets still require careful source-control practices

### Risks and mitigations

- **Risk:** Too many tiny components.  
  **Mitigation:** Create components only for real state ownership or lifecycle boundaries.

- **Risk:** Overabstracted interfaces.  
  **Mitigation:** Introduce an interface only when multiple implementations, testing seams, or dependency inversion justify it.

- **Risk:** Blueprint logic duplicates C++ rules.  
  **Mitigation:** Review Blueprint responsibilities and expose narrow C++ APIs.

## Validation

The architecture is validated when:

- State ownership matches `docs/ARCHITECTURE.md`.
- UI can be replaced without changing domain behavior.
- A telemetry sink can fail without mutating simulation state.
- Resource calculations have automation tests.
- The first vertical slice can be completed without a monolithic manager.

## Supersedes / Superseded by

None.
