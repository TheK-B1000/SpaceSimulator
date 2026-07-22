# Six-Axis Flight Shell

## Status
Draft

This ExecPlan is created for review only. Do not implement the pawn, movement component, player controller, input assets, map, GameMode, or project setting changes until this plan is reviewed and explicitly approved for implementation.

## Problem and outcome
The verified foundation still opens into an Untitled Open World template map using the base GameModeBase. That is acceptable for foundation verification, but the first vertical slice needs a project-owned flight sandbox where a player can spawn into a simple spacecraft pawn and translate and rotate across all six degrees of freedom using Enhanced Input.

The outcome of the implementation phase will be a narrow flight shell:

- `L_FlightSandbox` exists as the authored editor map for flight iteration.
- The project default map points to `L_FlightSandbox`.
- A project-specific GameMode selects the project-specific pawn and controller.
- A C++ spacecraft pawn owns or references a flight movement component.
- A C++ flight movement component owns flight velocity state and applies six-axis movement commands.
- A C++ player controller or input command layer owns input intent and forwards validated commands to the pawn.
- Enhanced Input actions and mapping context exist as authored Unreal assets.
- Deterministic command shaping and clamping behavior has automation coverage under `Eden.Unit.Flight`.

## Scope
### In scope
- Add C++ flight shell types under the existing `EdenSpaceSimulator` runtime module.
- Add only source folders backed by real implementation:
  - `Source/EdenSpaceSimulator/Public/Flight`
  - `Source/EdenSpaceSimulator/Private/Flight`
  - `Source/EdenSpaceSimulator/Private/Tests`
- Implement the minimal player-controllable six-axis movement needed for a flight sandbox.
- Implement a stabilization or damping option only if it is simple, explicit, and unit-tested.
- Use project-specific log category `LogEdenFlight`.
- Create and save `Content/Eden/Maps/L_FlightSandbox.umap`.
- Create project input assets under `Content/Eden/Input`.
- Create Blueprint composition assets only where they are the right Unreal authoring surface, such as a pawn Blueprint or GameMode Blueprint that composes C++ classes and asset references.
- Update `Config/DefaultEngine.ini` only for the default map and project GameMode after assets exist.
- Add automation tests for deterministic command shaping, clamping, and reset behavior.
- Record editor/manual verification evidence for map, input, pawn spawn, GameMode, and default-map behavior.

### Out of scope
- Fuel, power, thermal, oxygen, resource drain, or fixed-step resource simulation.
- Mission objectives, docking scoring, failures, alerts, HUD, after-action review, telemetry export, networking, or EDEN OS integration.
- Production art, final cockpit meshes, audio, VFX, animation, or camera polish beyond a minimal usable debug camera.
- Multiplayer, possession switching, save/load, replay, or packaged build work.
- Moving or deleting existing authored assets.
- Core simulation rules in Level Blueprint or widget Blueprint.

## Current repository state
- Commit baseline: `0c294bb initial commit`.
- Working tree was clean when this plan was authored, aside from Git warning that `C:\Users\K-B/.config/git/ignore` cannot be accessed.
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

State ownership:

- Spacecraft transform and velocities: `UEdenFlightMovementComponent` and the Unreal movement/physics body.
- Player input intent: `AEdenFlightPlayerController` or equivalent input command layer.
- Pawn composition and component ownership: `AEdenSpacecraftPawn`.
- Map selection and class defaults: `L_FlightSandbox`, GameMode asset or C++ GameMode, and project config.
- UI display state: none introduced.
- Mission, resource, telemetry, and external service state: none introduced.

Dependency direction:

- Enhanced Input assets and player controller produce input commands.
- `AEdenSpacecraftPawn` exposes a narrow command API.
- `UEdenFlightMovementComponent` validates and applies movement commands.
- Tests exercise command shaping and movement math without requiring UI or mission systems.
- Blueprints compose C++ classes and assign assets; they do not own flight rules.

Mutable state introduced by this plan must have one owner. Any drift from the state ownership above requires updating this ExecPlan and likely `docs/ARCHITECTURE.md`; an ADR is only required if ownership, module boundaries, or the C++/Blueprint boundary changes from the accepted architecture.

## Alternatives considered
1. Use `UFloatingPawnMovement` directly.
   - Advantage: fast to wire and familiar.
   - Rejected for this slice because it hides too much of the flight command model and makes deterministic command shaping harder to test.

2. Use full Unreal physics with forces and torques immediately.
   - Advantage: physically expressive and closer to spacecraft feel.
   - Deferred because it increases tuning, mass/inertia, collision, and determinism complexity before the command model is proven.

3. Implement a custom `UPawnMovementComponent` named `UEdenFlightMovementComponent`.
   - Preferred because it gives clear state ownership, Unreal integration, testable command shaping, and a clean path to later physics refinement.

4. Put input bindings directly in the pawn.
   - Rejected because `docs/ARCHITECTURE.md` assigns player input intent to the controller or command layer. The pawn should receive commands, not own raw input interpretation.

5. Leave the default Open World map until later.
   - Rejected because the flight milestone needs a repeatable editor entry point and project-specific GameMode/pawn composition.

## Milestones
1. Confirm foundation baseline and review this plan.
2. Add flight C++ shell with command structs, pawn, controller, movement component, and GameMode.
3. Add deterministic automation tests under `Eden.Unit.Flight`.
4. Build the Win64 Development Editor target.
5. Create Enhanced Input assets and Blueprint composition assets in Unreal Editor.
6. Create and save `L_FlightSandbox`.
7. Configure default map and project GameMode after the assets exist.
8. Manually verify play-in-editor spawn, possession, six-axis input, stabilization behavior if included, and clean editor reload.
9. Update recovery documentation and review Git diff for accidental generated files or binary churn.

## Detailed steps
1. Re-run preflight from `AGENTS.md`, read active docs, inspect Git status, and confirm `0002-six-axis-flight.md` is still current.
2. Create C++ flight source files:
   - `Source/EdenSpaceSimulator/Public/Flight/EdenFlightTypes.h`
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
4. Define a small command model:
   - `FEdenFlightInputCommand` with explicit normalized translation and rotation vectors.
   - Translation axes: forward/back, right/left, up/down.
   - Rotation axes: pitch, yaw, roll.
   - Units and coordinate spaces documented in names or comments.
   - Clamp invalid inputs and guard against NaN before state changes.
5. Implement `UEdenFlightMovementComponent`:
   - Own linear and angular velocity state unless delegated to a physics body in a documented way.
   - Apply command input at engine movement cadence.
   - Avoid avoidable allocation in the update path.
   - Use `LogEdenFlight` for meaningful initialization, invalid command, and recovery logs.
   - Expose only tuning parameters needed for the sandbox, with safe defaults.
6. Implement `AEdenSpacecraftPawn`:
   - Own the flight movement component as a default subobject.
   - Provide a narrow command API consumed by the controller.
   - Add a minimal camera component only if required for usable editor verification.
   - Do not own mission, resource, telemetry, or UI state.
7. Implement `AEdenFlightPlayerController`:
   - Own input intent and Enhanced Input bindings.
   - Hold soft or Blueprint-assignable references to input mapping assets as appropriate.
   - Fail loudly with actionable logs if required input assets are missing.
   - Forward validated commands to the possessed `AEdenSpacecraftPawn`.
8. Implement `AEdenFlightGameMode`:
   - Select project-specific default pawn and player controller classes.
   - Avoid gameplay rules beyond class selection for this milestone.
9. Add automation tests:
   - `Source/EdenSpaceSimulator/Private/Tests/EdenFlightCommandTests.cpp`
   - Names under `Eden.Unit.Flight`.
   - Cover clamp behavior, NaN rejection or sanitization, zero input, max input, and reset behavior.
   - Add a light integration-style automation test only if it can run headless without fragile editor asset dependencies.
10. Run C++ validation before editor asset work:
    - Build target.
    - Run `Eden.Unit.Foundation` and `Eden.Unit.Flight`.
11. In Unreal Editor 5.8, create assets:
    - `Content/Eden/Input/IA_FlightTranslate.uasset`
    - `Content/Eden/Input/IA_FlightRotate.uasset`
    - `Content/Eden/Input/IA_FlightStabilize.uasset`, only if stabilization is included.
    - `Content/Eden/Input/IMC_Flight.uasset`
    - `Content/Eden/Blueprints/BP_EdenSpacecraftPawn.uasset`, if composition requires Blueprint asset references.
    - `Content/Eden/Blueprints/BP_EdenFlightGameMode.uasset`, if a Blueprint GameMode is preferable for project setting assignment.
    - `Content/Eden/Maps/L_FlightSandbox.umap`
12. Configure `L_FlightSandbox`:
    - Use an empty or simple sandbox scene.
    - Place only necessary debug geometry and player start.
    - Set world override GameMode if needed.
    - Do not put core flight rules in Level Blueprint.
13. Update `Config/DefaultEngine.ini` after `L_FlightSandbox` exists:
    - Set `GameDefaultMap=/Game/Eden/Maps/L_FlightSandbox`.
    - Set editor startup map to `L_FlightSandbox` if desired and documented.
    - Set default GameMode to the project-specific GameMode asset or C++ class.
14. Verify in editor:
    - Project opens to `L_FlightSandbox`.
    - PIE spawns the project pawn and controller.
    - Translate and rotate work across all six axes.
    - Invalid or missing input assets log actionable errors.
    - Stopping and restarting PIE does not preserve stale movement state.
15. Update `docs/RECOVER.md` with verification evidence and remaining risks during the implementation session.

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

- `EdenSpaceSimulatorEditor` Win64 Development builds.
- `Eden.Unit.Foundation.Smoke` passes.
- New `Eden.Unit.Flight.*` tests pass.

Manual editor verification:

```text
Open EdenSpaceSimulator.uproject in Unreal Engine 5.8.
Confirm default editor/game map is L_FlightSandbox after planned map creation.
Press Play.
Confirm the project-specific pawn is possessed through the project-specific GameMode.
Confirm keyboard/mouse or controller mappings translate and rotate the pawn across six axes.
Stop Play, start Play again, and confirm movement state resets.
Review Output Log for LogEdenFlight messages and absence of new LogTemp reliance.
Save assets intentionally and review source control status.
```

Expected result: editor verification evidence is recorded in `docs/RECOVER.md`, and binary asset changes are limited to the planned map/input/Blueprint assets.

Git review:

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff
```

Expected result: generated files remain ignored, no unrelated assets are changed, and only planned text/binary assets appear.

## Failure modes and rollback
- C++ build fails: revert the latest C++ increment, inspect compiler output, and preserve the plan and logs.
- Automation tests fail: fix the command model or tests before opening the editor for asset work.
- Input assets are missing or assigned incorrectly: controller logs an actionable failure and pawn remains safe.
- `L_FlightSandbox` is accidentally saved with unrelated content: close editor, inspect source control status, and revert only the unintended map asset change after confirming no authored work is lost.
- Default map or GameMode config points to a missing asset: restore the previous `DefaultEngine.ini` map settings or correct the asset path before ending the session.
- Movement state persists across PIE restarts: treat as a state ownership bug in the movement component or pawn reset path.
- Per-frame logs spam the Output Log: reduce logging to state transitions and invalid input events.
- Unreal Editor cannot safely create binary assets in the environment: implement and verify C++ only, document exact editor steps, and leave asset creation pending.

## Progress log
2026-07-22: Drafted this ExecPlan for review only. No flight code, input assets, map assets, GameMode assets, or config changes were implemented.

## Decision log
2026-07-22: Plan `L_FlightSandbox` and project-specific pawn/GameMode setup as part of the flight milestone, not foundation.
2026-07-22: Prefer a custom `UPawnMovementComponent`-style `UEdenFlightMovementComponent` so velocity ownership and deterministic command shaping remain explicit.
2026-07-22: Keep raw input interpretation in a player controller or command layer, not the pawn.
2026-07-22: Keep resource, mission, telemetry, UI, and EDEN OS work out of this milestone.

## Acceptance evidence
Pending. This file is the only artifact created for this task.

Implementation acceptance criteria for a future approved pass:

- `L_FlightSandbox` exists and is the default map.
- Project-specific GameMode and pawn are used in PIE.
- Enhanced Input assets exist and are wired intentionally.
- Player can translate and rotate across all six axes.
- Movement command shaping has `Eden.Unit.Flight` automation coverage.
- Build and `Eden` automation tests pass.
- Editor verification is recorded.
- No core flight rules live in Level Blueprint.
- No unrelated authored assets are modified.

## Handoff
Review this plan before implementation. The next clean action is plan review, not code. After approval, implement in small increments: C++ command model and tests first, then editor-authored assets and map/default GameMode configuration.
