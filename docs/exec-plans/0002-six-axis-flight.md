# Six-Axis Flight Shell

## Status
Approved

This ExecPlan is approved as the implementation contract. Do not begin Checkpoint A, write flight code, create Unreal assets, edit project map settings, or implement pawn, movement, controller, GameMode, or input changes until implementation is explicitly authorized.

## Problem and outcome
The verified foundation still opens into an Untitled Open World template map using the base GameModeBase. That was acceptable for foundation verification, but the first vertical slice needs a project-owned flight sandbox where a player can spawn into a simple spacecraft pawn and translate and rotate across all six degrees of freedom using Enhanced Input.

The implementation outcome will be a narrow, kinematic flight shell:

- `L_FlightSandbox` exists as the authored editor map for flight iteration.
- `GameDefaultMap` and `EditorStartupMap` both point to `L_FlightSandbox`.
- A project-specific GameMode selects the project-specific Blueprint pawn and Blueprint controller.
- `AEdenSpacecraftPawn` creates a required `USphereComponent` collision root in C++, owns component composition, and exposes narrow command methods.
- `UEdenFlightMovementComponent` is a custom kinematic `UPawnMovementComponent` that owns linear velocity through the inherited movement-component velocity and owns angular velocity explicitly.
- `AEdenFlightPlayerController` owns current normalized input intent and forwards sanitized commands to the pawn.
- Enhanced Input actions and mapping context exist as authored Unreal assets.
- Movement uses swept collision-aware translation and detects blocking hits.
- Stabilization is a simple configurable damping toggle, not auto-leveling.
- Deterministic command sanitization, velocity integration, damping, clamping, and reset behavior have automation coverage under `Eden.Unit.Flight`.

## Scope
### In scope
- Add C++ flight shell types under the existing `EdenSpaceSimulator` runtime module:
  - `AEdenSpacecraftPawn`
  - `UEdenFlightMovementComponent`
  - `AEdenFlightPlayerController`
  - `AEdenFlightGameMode`
- Use a custom kinematic `UPawnMovementComponent` for this milestone.
- Add only source folders backed by real implementation:
  - `Source/EdenSpaceSimulator/Public/Flight`
  - `Source/EdenSpaceSimulator/Private/Flight`
  - `Source/EdenSpaceSimulator/Private/Tests`
- Implement local-space six-axis translation and pitch/yaw/roll command handling.
- Require `AEdenSpacecraftPawn` to create a `USphereComponent` in C++ as the collision root.
- Allow Blueprint tuning of the sphere collision root, but do not allow Blueprint replacement of the required movement root.
- Validate and set `UpdatedComponent` in `UEdenFlightMovementComponent`.
- Use `SafeMoveUpdatedComponent` or the appropriate Unreal swept movement API for translation.
- Detect blocking hits and avoid direct actor transform setting for collision movement.
- Apply rotation explicitly after sanitized angular integration.
- Implement a simple configurable stabilization toggle that damps linear and angular velocity toward zero when corresponding input is released.
- Use project-specific log category `LogEdenFlight`.
- Create and save `Content/Eden/Maps/L_FlightSandbox.umap` during the later asset checkpoint.
- Create Enhanced Input assets under `Content/Eden/Input`.
- Create Blueprint composition assets:
  - `BP_EdenSpacecraftPawn` for sphere-collision tuning, placeholder mesh, usable debug camera, and tuning.
  - `BP_EdenFlightPlayerController` for assigning Enhanced Input assets.
  - `BP_EdenFlightGameMode` for selecting the Blueprint pawn and controller.
- Update `Config/DefaultEngine.ini` only after `L_FlightSandbox` and the GameMode assets exist.
- Add automation tests for deterministic flight-domain behavior that does not require a loaded map.
- Record editor/manual verification evidence for map, input, collision, pawn possession, camera usability, GameMode, stabilization, reset, and default-map behavior.

### Out of scope
- Full rigid-body physics, forces, torques, orbital mechanics, N-body simulation, or physical mass/inertia modeling.
- Fuel, power, thermal, oxygen, resource drain, or fixed-step resource simulation.
- Mission objectives, docking scoring, failures, alerts, HUD, after-action review, telemetry export, networking, or EDEN OS integration.
- Production art, final cockpit meshes, audio, VFX, animation, or camera polish beyond a minimal usable debug camera.
- Multiplayer, possession switching, save/load, replay, or packaged build work.
- Moving or deleting existing authored assets.
- Core simulation rules in Level Blueprint, widget Blueprint, pawn Blueprint, controller Blueprint, or GameMode Blueprint.

## Current repository state
- Commit baseline observed before this revision: `0c294bb initial commit`.
- Working tree was clean before this task except for the plan file changes and Git warning that `C:\Users\K-B/.config/git/ignore` cannot be accessed.
- `docs/exec-plans/0001-project-foundation.md` is verified complete.
- Engine baseline is Unreal Engine 5.8.
- Primary runtime module is `EdenSpaceSimulator`.
- `EdenSpaceSimulator.uproject` explicitly enables `EnhancedInput`.
- `Config/DefaultInput.ini` uses `EnhancedPlayerInput` and `EnhancedInputComponent`.
- `Config/DefaultEngine.ini` currently sets `GameDefaultMap=/Engine/Maps/Templates/OpenWorld`.
- Recent editor verification reported an Untitled Open World map with the base GameModeBase. This is a known foundation-state artifact to replace during this milestone.
- `Content` currently has only visible collection/developer folders and no authored project `.uasset` or `.umap` gameplay assets.
- Project log categories exist in `Source/EdenSpaceSimulator/Public/Core/EdenLogCategories.h`.
- Foundation smoke test `Eden.Unit.Foundation.Smoke` exists and passed during foundation verification.
- Generated Unreal and IDE files are ignored; Unreal binary assets are tracked by Git LFS.

## Architecture alignment
This plan follows `docs/ARCHITECTURE.md` and `ADR-0001`.

Design discipline for this milestone:

- Keep responsibilities narrow and explicit.
- Prefer simple kinematic movement with collision-safe sweeps over premature physics complexity.
- Use Unreal ownership and lifetime conventions for components and reflected references.
- Keep C++ responsible for flight rules and Blueprints responsible for composition, tuning, and asset references.
- Keep every mutable runtime value owned by exactly one class.

State ownership is locked for this milestone:

- `AEdenFlightPlayerController` owns the current normalized input intent.
- `AEdenSpacecraftPawn` owns component composition, creates the required `USphereComponent` collision root in C++, and exposes narrow command methods.
- `UEdenFlightMovementComponent` owns linear velocity through the inherited `UPawnMovementComponent::Velocity` field; do not add a duplicate linear-velocity member.
- `UEdenFlightMovementComponent` owns angular velocity as explicit local body-axis degrees per second.
- `AEdenFlightPlayerController` clears input intent during controller reset and unpossession paths.
- `UEdenFlightMovementComponent` clears inherited linear velocity and explicit angular velocity during initialization and movement reset.
- The pawn's required sphere collision root owns collision shape configuration through Unreal component ownership.
- Blueprint assets own configuration references and authored defaults only. They never own runtime flight state or flight rules.
- Map selection and class defaults are owned by `L_FlightSandbox`, `BP_EdenFlightGameMode`, and project config after the asset checkpoint.
- UI display state, mission state, resource state, telemetry state, and external service state are not introduced.

Dependency direction:

- Enhanced Input assets feed `AEdenFlightPlayerController`.
- `AEdenFlightPlayerController` sanitizes and stores current normalized input intent.
- `AEdenFlightPlayerController` sends commands to `AEdenSpacecraftPawn`.
- `AEdenSpacecraftPawn` forwards commands through narrow methods to `UEdenFlightMovementComponent`.
- `UEdenFlightMovementComponent` validates `UpdatedComponent`, integrates inherited linear velocity and explicit angular velocity, performs swept movement, applies rotation, and reports blocking hits.
- Tests exercise flight-domain command sanitization and velocity integration without requiring UI, mission systems, or a loaded map.
- Blueprints compose C++ classes and assign assets; they do not own flight rules.

Any drift from this ownership model requires updating this ExecPlan and likely `docs/ARCHITECTURE.md`. An ADR is required only if ownership, module boundaries, fixed-step policy, or the C++/Blueprint boundary changes from the accepted architecture.

## Movement model
Initial movement is locked to a custom kinematic `UPawnMovementComponent`.

Rules:

- Do not enable full rigid-body physics in this milestone.
- Do not simulate orbital mechanics.
- Do not apply rigid-body forces or torques.
- Do not directly set actor transforms for collision movement.
- The pawn must create a required `USphereComponent` in C++ and set it as the root component.
- `BP_EdenSpacecraftPawn` may tune the required sphere collision root, but may not replace it as the movement root.
- `UEdenFlightMovementComponent` must call `SetUpdatedComponent` or otherwise set `UpdatedComponent` to the required sphere collision root during initialization.
- `UEdenFlightMovementComponent` must validate that `UpdatedComponent` exists, points to the required sphere collision root, is movable, and is appropriate for swept movement before mutating velocity.
- Translation must use `SafeMoveUpdatedComponent` or the appropriate swept movement API.
- Movement must inspect `FHitResult` and detect blocking hits.
- Blocking-hit response is collision-safe sliding: remove inward velocity along the impact normal, preserve valid tangential velocity, and continue with Unreal-safe slide behavior where appropriate.
- Blocking-hit response must not bounce, apply damage, or trigger mission/resource effects in this milestone.
- Rotation must be applied explicitly from sanitized angular velocity. If rotation sweep is not supported for the selected API, document that limitation and keep translation collision-safe.
- Blocking-hit behavior must be deterministic and observable through return state or `LogEdenFlight`, without per-frame spam.

Command semantics:

- Translation command is a normalized local-space `FVector`.
- Translation command components use Unreal local axes:
  - `X`: forward positive, reverse negative.
  - `Y`: right positive, left negative.
  - `Z`: up positive, down negative.
- Rotation command is normalized pitch, yaw, and roll input.
- Rotation command component ordering is:
  - `X`: pitch input. Positive pitches nose up unless implementation evidence requires inversion for mouse feel, in which case the mapping asset handles inversion and C++ semantics stay documented.
  - `Y`: yaw input. Positive yaws right.
  - `Z`: roll input. Positive rolls right.
- C++ and input assets must use the same component ordering. Any editor-driven deviation must be recorded in the plan before implementation continues.
- Input values outside `[-1, 1]` are clamped.
- NaN and non-finite input are rejected or sanitized before mutating state.
- Invalid input logs actionable context at a bounded rate.

Velocity semantics:

- Linear velocity is world-space centimeters per second and is stored in the inherited `UPawnMovementComponent::Velocity` field.
- Angular velocity is local body-axis degrees per second and is stored by `UEdenFlightMovementComponent`.
- `MaxLinearSpeedCmPerSecond` clamps linear velocity magnitude.
- `LinearAccelerationCmPerSecondSquared` accelerates from local-space command intent into world-space inherited velocity while input is present.
- `LinearDecelerationCmPerSecondSquared` decelerates velocity when stabilization is enabled and the corresponding translation input is released.
- `MaxAngularSpeedDegreesPerSecond` clamps angular velocity magnitude or per-axis absolute angular speed. The choice must be documented in code and tests.
- `AngularAccelerationDegreesPerSecondSquared` accelerates angular velocity while rotation input is present.
- `AngularDecelerationDegreesPerSecondSquared` decelerates angular velocity when stabilization is enabled and the corresponding rotation input is released.
- `DeltaTime` must be positive and finite before integration.
- Zero, negative, NaN, or infinite `DeltaTime` must not mutate velocity or movement state.
- Where intended to be frame-rate independent, equivalent simulated time with different `DeltaTime` partitions must produce equivalent results within a documented tolerance.

## Stabilization
Stabilization is included as a simple configurable toggle.

Definition:

- When enabled, stabilization damps linear velocity toward zero on translation axes whose corresponding input is released.
- When enabled, stabilization damps angular velocity toward zero on rotation axes whose corresponding input is released.
- When disabled, released input does not apply stabilization damping beyond any explicitly documented baseline deceleration.
- Stabilization does not auto-level the spacecraft.
- Stabilization does not rotate toward a world orientation.
- Stabilization does not inspect mission, docking, or UI state.
- Stabilization must be deterministic and covered by automation tests.

## Unreal composition
C++ classes are locked for this milestone:

- `AEdenSpacecraftPawn`
- `UEdenFlightMovementComponent`
- `AEdenFlightPlayerController`
- `AEdenFlightGameMode`

Blueprint composition assets are locked for this milestone:

- `BP_EdenSpacecraftPawn`: tunes the required C++ `USphereComponent` collision root, configures a placeholder mesh, contains a usable debug camera, and sets tuning defaults.
- `BP_EdenFlightPlayerController`: assigns Enhanced Input mapping context and input action assets.
- `BP_EdenFlightGameMode`: selects `BP_EdenSpacecraftPawn` and `BP_EdenFlightPlayerController`.

Blueprint constraints:

- No flight rules may live in these Blueprints.
- No Blueprint may own runtime flight velocity.
- No Blueprint may own current input intent.
- No Blueprint may directly apply movement transforms for core flight.
- No Blueprint may replace the required `USphereComponent` movement root.
- Blueprint graphs should remain minimal and limited to composition or editor-assigned references.

## Input assets
Create these assets during Checkpoint C:

- `Content/Eden/Input/IA_FlightTranslate.uasset`
  - Type: Axis3D.
  - Component ordering must match C++ translation command: `X` forward/reverse, `Y` right/left, `Z` up/down.
  - Initial keyboard mapping:
    - `W`: +X forward, no modifier required.
    - `S`: -X reverse, requires `Negate`.
    - `D`: +Y right, requires `Swizzle Input Axis Values` from X to Y.
    - `A`: -Y left, requires `Swizzle Input Axis Values` from X to Y and `Negate`.
    - `Space`: +Z up, requires `Swizzle Input Axis Values` from X to Z.
    - `Left Ctrl`: -Z down, requires `Swizzle Input Axis Values` from X to Z and `Negate`.
- `Content/Eden/Input/IA_FlightRotate.uasset`
  - Type: Axis3D.
  - Component ordering must match C++ rotation command: `X` pitch, `Y` yaw, `Z` roll.
  - Initial keyboard/mouse mapping:
    - `Mouse Y`: +X pitch, no swizzle required for scalar axis input. Add `Negate` only if editor verification shows the physical mouse direction feels inverted while preserving the C++ convention.
    - `Mouse X`: +Y yaw, requires `Swizzle Input Axis Values` from X to Y.
    - `E`: +Z roll right, requires `Swizzle Input Axis Values` from X to Z.
    - `Q`: -Z roll left, requires `Swizzle Input Axis Values` from X to Z and `Negate`.
- `Content/Eden/Input/IA_FlightStabilize.uasset`
  - Type: Digital.
  - Behavior: toggle stabilization enabled/disabled.
  - Initial keyboard mapping: `X`.
- `Content/Eden/Input/IMC_Flight.uasset`
  - Mapping context containing the flight translate, rotate, and stabilize mappings.

The chosen Axis3D component ordering and required `Negate` / `Swizzle Input Axis Values` modifiers are part of the contract between C++ and assets. Tests should cover the C++ side; manual verification must confirm the editor asset side.

## Test design
Command sanitization and velocity integration must live in a small testable C++ type or functions that do not require a loaded map.

This helper must represent real flight-domain behavior used by `UEdenFlightMovementComponent`; do not create an abstraction solely for tests.

Planned test file:

- `Source/EdenSpaceSimulator/Private/Tests/EdenFlightCommandTests.cpp`

Planned test names:

- `Eden.Unit.Flight.CommandSanitizesZeroInput`
- `Eden.Unit.Flight.CommandClampsPositiveAndNegativeMaximumInput`
- `Eden.Unit.Flight.CommandClampsOutOfRangeInput`
- `Eden.Unit.Flight.CommandRejectsOrSanitizesNaNAndInfinity`
- `Eden.Unit.Flight.LinearVelocityAcceleratesAndClamps`
- `Eden.Unit.Flight.AngularVelocityAcceleratesAndClamps`
- `Eden.Unit.Flight.StabilizationDampsReleasedAxes`
- `Eden.Unit.Flight.ControllerResetClearsInputIntent`
- `Eden.Unit.Flight.MovementResetClearsLinearAndAngularVelocity`
- `Eden.Unit.Flight.EquivalentSimulatedTimeMatchesAcrossDeltaTimePartitions`

Coverage requirements:

- Zero input.
- Positive maximum input.
- Negative maximum input.
- Values outside normalized range.
- NaN and infinity.
- Acceleration and speed clamping.
- Stabilization damping.
- Controller input-intent reset behavior.
- Movement linear and angular velocity reset behavior.
- Equivalent simulated time using different `DeltaTime` partitions where the implementation is intended to be frame-rate independent.

## Alternatives considered
1. Use `UFloatingPawnMovement` directly.
   - Advantage: fast to wire and familiar.
   - Rejected because it hides too much of the flight command model and makes deterministic command shaping and velocity ownership less explicit.

2. Use full Unreal rigid-body physics with forces and torques immediately.
   - Advantage: physically expressive and closer to a later spacecraft model.
   - Rejected for this milestone because it expands tuning, mass/inertia, collision, determinism, and test complexity before the command model is proven.

3. Implement a custom kinematic `UPawnMovementComponent` named `UEdenFlightMovementComponent`.
   - Selected because it gives explicit velocity ownership, collision-aware movement through swept APIs, Unreal integration, and testable command shaping.

4. Put input bindings directly in the pawn.
   - Rejected because `docs/ARCHITECTURE.md` assigns player input intent to the controller or command layer. The pawn should receive commands, not own raw input interpretation.

5. Leave the default Open World map until later.
   - Rejected because the flight milestone needs a repeatable editor entry point and project-specific GameMode/pawn composition.

## Implementation checkpoints
Each checkpoint must build before continuing to the next checkpoint.

Checkpoint A: command model, movement component, and tests.

- Add flight command/velocity helper used by production movement code.
- Add `UEdenFlightMovementComponent`.
- Validate `UpdatedComponent` behavior against the required sphere collision root in code.
- Use inherited `UPawnMovementComponent::Velocity` for linear velocity; do not add duplicate linear-velocity storage.
- Add `Eden.Unit.Flight` tests for command sanitization, velocity integration, stabilization, controller input reset, movement velocity reset, and frame-rate partition behavior.
- Build and run relevant tests before continuing.

Checkpoint B: pawn, controller, and GameMode C++.

- Add `AEdenSpacecraftPawn` with a required C++ `USphereComponent` collision root and movement component composition.
- Add `AEdenFlightPlayerController` with current normalized input intent ownership and command forwarding.
- Add `AEdenFlightGameMode`.
- Build before continuing.

Checkpoint C: Unreal input and Blueprint assets.

- Create `IA_FlightTranslate`, `IA_FlightRotate`, `IA_FlightStabilize`, and `IMC_Flight`.
- Create `BP_EdenSpacecraftPawn`, `BP_EdenFlightPlayerController`, and `BP_EdenFlightGameMode`.
- Confirm Blueprint assets own configuration references only.
- Build and run tests before continuing.

Checkpoint D: map and project configuration.

- Create and save `L_FlightSandbox`.
- Add a simple blocking object for collision sweep verification.
- Configure `GameDefaultMap`, `EditorStartupMap`, and project GameMode only after assets exist.
- Build and run tests before continuing.

Checkpoint E: full validation, documentation, and recovery update.

- Run repository validation, build, and `Eden` automation tests.
- Complete manual editor verification.
- Update `docs/RECOVER.md`.
- Review Git status and diff for generated files, unrelated assets, and binary churn.

## Detailed steps
1. Re-run preflight from `AGENTS.md`, read active docs, inspect Git status, and confirm this revised plan is still current.
2. Create C++ flight source files:
   - `Source/EdenSpaceSimulator/Public/Flight/EdenFlightTypes.h`
   - `Source/EdenSpaceSimulator/Public/Flight/EdenFlightMovementModel.h`
   - `Source/EdenSpaceSimulator/Private/Flight/EdenFlightMovementModel.cpp`
   - `Source/EdenSpaceSimulator/Public/Flight/EdenFlightMovementComponent.h`
   - `Source/EdenSpaceSimulator/Private/Flight/EdenFlightMovementComponent.cpp`
   - `Source/EdenSpaceSimulator/Public/Flight/EdenSpacecraftPawn.h`
   - `Source/EdenSpaceSimulator/Private/Flight/EdenSpacecraftPawn.cpp`
   - `Source/EdenSpaceSimulator/Public/Flight/EdenFlightPlayerController.h`
   - `Source/EdenSpaceSimulator/Private/Flight/EdenFlightPlayerController.cpp`
   - `Source/EdenSpaceSimulator/Public/Flight/EdenFlightGameMode.h`
   - `Source/EdenSpaceSimulator/Private/Flight/EdenFlightGameMode.cpp`
3. Keep C++ public headers narrow:
   - Use `#pragma once`.
   - Prefer forward declarations.
   - Include only required public types.
   - Use `UPROPERTY` and `TObjectPtr` for reflected UObject references that require garbage collection.
4. Implement the flight-domain model:
   - Sanitized normalized translation command.
   - Sanitized normalized rotation command.
   - Velocity integration.
   - Stabilization damping.
   - Reset behavior.
   - Finite `DeltaTime` validation.
5. Implement `UEdenFlightMovementComponent`:
   - Derive from `UPawnMovementComponent`.
   - Own linear velocity through inherited `Velocity`; do not create duplicate linear-velocity state.
   - Own explicit local body-axis angular velocity.
   - Clear inherited linear velocity and explicit angular velocity during initialization and reset.
   - Set and validate `UpdatedComponent` against the required sphere collision root.
   - Use swept movement for translation.
   - Detect blocking hits.
   - Slide on blocking hits by removing inward velocity along the impact normal.
   - Apply explicit rotation.
   - Log invalid configuration and blocking-hit state transitions with `LogEdenFlight`.
6. Implement `AEdenSpacecraftPawn`:
   - Own component composition.
   - Create the required `USphereComponent` collision root in C++.
   - Prevent Blueprint composition from replacing the movement root.
   - Own the flight movement component as a default subobject.
   - Provide narrow command methods consumed by the controller.
   - Support `BP_EdenSpacecraftPawn` containing a usable debug camera.
   - Do not own mission, resource, telemetry, UI, input intent, or velocity state.
7. Implement `AEdenFlightPlayerController`:
   - Own current normalized input intent.
   - Bind Enhanced Input actions supplied by Blueprint-assigned configuration.
   - Sanitize input before forwarding commands.
   - Clear input intent during reset and unpossession paths.
   - Fail loudly with actionable logs if required input assets are missing.
   - Forward commands to the possessed `AEdenSpacecraftPawn`.
8. Implement `AEdenFlightGameMode`:
   - Provide the C++ base for the Blueprint GameMode.
   - Avoid gameplay rules beyond class defaults for this milestone.
9. Add automation tests in `Source/EdenSpaceSimulator/Private/Tests/EdenFlightCommandTests.cpp`.
10. Run Checkpoint A and B builds/tests before opening the editor for assets.
11. In Unreal Editor 5.8, create input and Blueprint composition assets from the locked asset list.
12. Create `L_FlightSandbox` with a simple blocking object and a usable player start.
13. Update `Config/DefaultEngine.ini` after assets exist:
    - Set `GameDefaultMap=/Game/Eden/Maps/L_FlightSandbox`.
    - Set `EditorStartupMap=/Game/Eden/Maps/L_FlightSandbox`.
    - Set default GameMode to `BP_EdenFlightGameMode`.
14. Complete manual editor verification.
15. Update `docs/RECOVER.md` with implementation evidence and remaining risks.

## Validation
Repository validation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1
```

Expected result: pass.

Build and automation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden
```

Expected result:

- `EdenSpaceSimulatorEditor` Win64 Development builds at every checkpoint.
- `Eden.Unit.Foundation.Smoke` passes.
- New `Eden.Unit.Flight.*` tests pass.

Manual editor verification:

```text
Open EdenSpaceSimulator.uproject in Unreal Engine 5.8.
Confirm project reloads into L_FlightSandbox after planned map configuration.
Press Play.
Confirm the project-specific pawn is possessed through BP_EdenFlightGameMode.
Confirm camera usability for basic flight testing.
Confirm W/S moves forward/reverse.
Confirm A/D moves left/right.
Confirm Space/Left Ctrl moves up/down.
Confirm Mouse Y pitches.
Confirm Mouse X yaws.
Confirm Q/E rolls.
Confirm IA_FlightStabilize toggles damping behavior.
Confirm swept movement blocks against a simple blocking object.
Confirm blocking hits are detected without direct actor transform tunneling.
Stop Play, start Play again, and confirm input intent and velocities reset.
Review Output Log for LogEdenFlight messages.
Confirm Output Log is free of LogTemp and per-frame spam.
Save only intentional assets and review source control status.
```

Expected result: editor verification evidence is recorded in `docs/RECOVER.md`, and binary asset changes are limited to the planned map, input, and Blueprint assets.

Git review:

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff
```

Expected result: generated files remain ignored, no unrelated assets are changed, and only planned text/binary assets appear.

## Failure modes and rollback
- C++ build fails: stop at the current checkpoint, inspect compiler output, and revert only the latest C++ increment if needed.
- `UpdatedComponent` is missing or invalid: movement component must refuse to mutate velocity and log actionable context.
- Swept movement is bypassed accidentally: treat as a high-severity architecture and collision-safety bug.
- Blocking hits are not detected: stop before asset work and fix the movement component.
- Automation tests fail: fix the command model or tests before opening the editor for asset work.
- Input assets are missing or assigned incorrectly: controller logs an actionable failure and pawn remains safe.
- Axis3D ordering differs between C++ and assets: update the asset or revise this plan before continuing.
- `L_FlightSandbox` is accidentally saved with unrelated content: close editor, inspect source control status, and revert only the unintended map asset change after confirming no authored work is lost.
- Default map or GameMode config points to a missing asset: restore the previous `DefaultEngine.ini` map settings or correct the asset path before ending the session.
- Movement state persists across PIE restarts: treat as a state ownership bug in controller intent reset, movement velocity reset, or pawn initialization.
- Stabilization auto-levels or rotates toward world orientation: remove that behavior; it is outside the locked definition.
- Per-frame logs spam the Output Log: reduce logging to state transitions and invalid input events.
- Unreal Editor cannot safely create binary assets in the environment: implement and verify C++ only, document exact editor steps, and leave asset creation pending.

## Progress log
2026-07-22: Drafted initial ExecPlan for review only. No flight code, input assets, map assets, GameMode assets, or config changes were implemented.
2026-07-22: Revised the plan after directional approval to lock kinematic swept movement, exact state ownership, movement semantics, stabilization behavior, Blueprint composition, input mapping, tests, manual verification, and A-E implementation checkpoints. No implementation was performed.

## Decision log
2026-07-22: Plan `L_FlightSandbox` and project-specific pawn/GameMode setup as part of the flight milestone, not foundation.
2026-07-22: Use a custom kinematic `UPawnMovementComponent` for initial flight.
2026-07-22: Do not enable full rigid-body physics, force simulation, torques, or orbital mechanics in this milestone.
2026-07-22: Require a C++ `USphereComponent` collision root and swept movement with blocking-hit detection.
2026-07-22: Keep raw input interpretation and current normalized input intent in `AEdenFlightPlayerController`, not the pawn.
2026-07-22: Keep linear velocity in inherited `UPawnMovementComponent::Velocity` and angular velocity in `UEdenFlightMovementComponent`; do not duplicate linear-velocity ownership.
2026-07-22: Define stabilization as deterministic velocity damping when corresponding input is released, not auto-leveling.
2026-07-22: Use Blueprints only for composition references, authored defaults, placeholder presentation, and asset assignment.
2026-07-22: Lock `IA_FlightTranslate` and `IA_FlightRotate` to Axis3D with documented Negate and Swizzle modifiers.
2026-07-22: Set both `GameDefaultMap` and `EditorStartupMap` to `L_FlightSandbox` during the map/config checkpoint.
2026-07-22: Keep resource, mission, telemetry, UI, and EDEN OS work out of this milestone.

## Acceptance evidence
Pending. This file is the only artifact revised for this task.

Implementation acceptance criteria for a future approved pass:

- `L_FlightSandbox` exists and is set as both `GameDefaultMap` and `EditorStartupMap`.
- `BP_EdenFlightGameMode` selects `BP_EdenSpacecraftPawn` and `BP_EdenFlightPlayerController`.
- `AEdenSpacecraftPawn` creates the required `USphereComponent` collision root in C++.
- `BP_EdenSpacecraftPawn` tunes the required sphere root, has a placeholder mesh, contains a usable debug camera, and does not replace the movement root.
- Enhanced Input assets exist and follow the documented Axis3D component ordering, including required Negate and Swizzle modifiers.
- `IA_FlightStabilize` is mapped to `X`.
- Player can translate and rotate across all six axes.
- Movement uses swept translation and detects blocking hits.
- `UEdenFlightMovementComponent` validates `UpdatedComponent`.
- Blocking-hit response slides by removing inward velocity along the impact normal, with no bounce or damage.
- Controller input intent and movement velocity reset under their separate owners on initialization and PIE restart.
- Stabilization toggle damps released-axis velocity and does not auto-level.
- Movement command shaping and velocity integration have `Eden.Unit.Flight` automation coverage.
- Build and `Eden` automation tests pass at each checkpoint.
- Manual editor verification is recorded.
- No core flight rules live in Blueprints or Level Blueprint.
- No unrelated authored assets are modified.

## Handoff
This plan is approved as the implementation contract. The next clean action is explicit implementation authorization for Checkpoint A, not implementation by assumption.

When implementation is explicitly approved, proceed checkpoint by checkpoint:

1. Checkpoint A: command model, movement component, and tests.
2. Checkpoint B: pawn, controller, and GameMode C++.
3. Checkpoint C: Unreal input and Blueprint assets.
4. Checkpoint D: map and project configuration.
5. Checkpoint E: full validation, documentation, and recovery update.

Build before moving past each checkpoint. Do not create Unreal assets before the C++ movement model and tests are building. Do not start resource simulation, mission gameplay, UI, telemetry export, networking, or EDEN OS integration from this plan.
