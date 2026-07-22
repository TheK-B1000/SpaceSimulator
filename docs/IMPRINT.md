# IMPRINT

## Project identity

Eden Space Simulator is an AI-assisted spacecraft operations simulator built to demonstrate serious simulation engineering, operator decision-making, telemetry, and future EDEN OS integration.

It is not initially a galaxy-scale open-world game. Its power comes from a small, credible simulation loop with clean architecture and measurable behavior.

## Product promise

The player should be able to understand:

- What the spacecraft or habitat is doing
- Why a system changed state
- What action is available
- What consequence followed
- Whether the mission succeeded
- What the telemetry says happened

The codebase should provide the same clarity to the engineer.

## Non-negotiable engineering principles

1. **Theory before code**  
   Understand and document the system, data flow, state owners, failure modes, and blast radius before implementation.

2. **One owner for each truth**  
   Every mutable state value has one authoritative owner.

3. **Cognitive debt stays at zero**  
   Do not leave code in the repository that the maintainer cannot explain.

4. **C++ spine, Blueprint skin**  
   C++ owns reusable behavior, contracts, simulation logic, and tests. Blueprints own composition, tuning, content, presentation, and iteration.

5. **Simulation and presentation remain separate**  
   UI, effects, audio, and network adapters observe or command the simulation through explicit boundaries.

6. **Professional evidence**  
   "It works" means acceptance criteria, build evidence, test evidence, logs, and manual verification where applicable.

7. **Small vertical slices**  
   Deliver a complete thin capability before expanding breadth.

8. **No silent failure**  
   Failures must produce actionable logs, explicit state, or visible operator feedback.

9. **No architecture by accident**  
   Important patterns are recorded in `docs/ARCHITECTURE.md` and ADRs.

10. **Recovery is a feature**  
    Every substantial session leaves the repository recoverable by another engineer or a fresh Codex session.

## First vertical slice

The first vertical slice is a docking and emergency-response trainer:

- Six-degree-of-freedom movement
- A small set of spacecraft resources
- One data-driven failure scenario
- Clear HUD and alerts
- Success and failure conditions
- Telemetry and after-action output

## Explicitly deferred

Until the first vertical slice is complete:

- Multiplayer
- Procedural galaxies
- Seamless planetary landing
- N-body orbital simulation
- Combat
- Crafting and economy systems
- Massive open worlds
- Production backend integration
- Generative AI controlling authoritative simulation state

## Definition of success

The simulator is successful when it is understandable, reproducible enough to test, stable enough to demonstrate, and architected so that EDEN OS integration is an adapter rather than a rewrite.
