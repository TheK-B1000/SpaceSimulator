# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-07-23 |
| Branch | `feature/spacecraft-resource-simulation` |
| Clean baseline tag | `v0.1.0-flight-shell` on commit `ed7fb55` |
| Working tree | Checkpoint A source, tests, ExecPlan progress, and this recovery update are the expected pending changes. |
| Active ExecPlan | `docs/exec-plans/0003-spacecraft-resource-simulation.md` (Approved) |
| Previous ExecPlan | `docs/exec-plans/0002-six-axis-flight.md` (verified complete) |
| Flight shell status | Committed, PIE-verified, and tagged as `v0.1.0-flight-shell`. |
| Resource implementation | Checkpoint A clock scope implemented and locally verified. Fuel, power, thermal, Data Assets, pawn resource components, propulsion-demand integration, ShowDebug, Blueprints, and Unreal assets are not started. |
| Last successful validation | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot "K:\Program Files\Epic Games\UE_5.8" -TestFilter Eden` |
| Last successful result | Repository validation passed, `EdenSpaceSimulatorEditor` Win64 Development build passed, `Eden.Unit.Foundation.Smoke` passed, existing `Eden.Unit.Flight.*` tests passed, and all new `Eden.Unit.SimClock.*` tests passed. |
| Next task | Review and accept Checkpoint A. Do not begin Checkpoint B yet. |

## Recovery protocol

Run from the Git root:

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -5 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator tag -l "v0.1.0*"
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff
```

Then:

1. Read `AGENTS.md`.
2. Read all mandatory documents in its preflight order.
3. Read `docs/exec-plans/0003-spacecraft-resource-simulation.md`.
4. Confirm the current branch is `feature/spacecraft-resource-simulation`.
5. Confirm the clean baseline tag `v0.1.0-flight-shell` exists.
6. Confirm Checkpoint A is still the only implementation scope in the diff.
7. Do not start Checkpoint B unless the maintainer explicitly authorizes it and `git status` is clean.

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not run broad asset-fix, redirector, clean, or regeneration operations before a checkpoint.
- Do not delete `Content`, `Config`, or `Source`.
- Generated directories may be removed only when the editor is closed and the task requires a clean rebuild.
- Preserve logs or screenshots that provide the only evidence of a failure.
- If the build cannot be reproduced, mark the checkpoint unverified and investigate before adding features.
- If resource work must be abandoned, return to tag `v0.1.0-flight-shell`.

## Current Known Risks

- Checkpoint A is implemented but not yet reviewed or accepted by the maintainer.
- Exact machine-local Unreal Engine installation path remains machine-specific and must not be committed.
- Git prints a warning that it cannot access `C:\Users\K-B/.config/git/ignore`; this is outside the repository and did not block validation.
- Engine `LogConsoleManager` may warn about `r.MotionVectorSimulation` on the render thread; that is residual engine noise, not Eden flight-shell ownership.
- Automation logs include expected `LogEdenSimClock` warnings from tests that intentionally exercise invalid fixed-step config, invalid subscribers, and overrun reporting.

## Session Handoff

### Completed

- Verified six-axis flight shell on `main` through build, automation, asset/runtime commandlets, and interactive PIE.
- Annotated tag `v0.1.0-flight-shell` created on `ed7fb55`.
- Branch `feature/spacecraft-resource-simulation` created from that baseline.
- ExecPlan 0003 revised with locked review decisions, final approval clarifications, and Approved status.
- Implemented Checkpoint A clock scope:
  - `LogEdenSimClock`
  - `FEdenFixedStepClockModel`
  - `UEdenSimulationTickable` / `IEdenSimulationTickable`
  - `UEdenSimulationClockSubsystem`
  - `Eden.Unit.SimClock.*` tests
- Verified Checkpoint A through repository validation, Win64 Development Editor build, foundation smoke, existing flight tests, and new SimClock tests.

### Locked decisions recorded in ExecPlan 0003

- `IEdenPropulsionDemandSource` on flight movement; fuel holds a weak non-owning reference.
- Fuel requires exactly one valid `IEdenPropulsionDemandSource` on the owning actor; no source or invalid source uses zero demand, and multiple sources are an ambiguity error.
- No pawn `LastThrustFraction` / `GetCurrentThrustFraction` API.
- `UEdenSimulationClockSubsystem` supports Game and PIE worlds, with GamePreview only if automated verification requires it; Editor, EditorPreview, Inactive, and None are excluded.
- Clock subscriber-list mutation during stepping is prevented through deferred registration/unregistration and stable weak-reference snapshot iteration.
- Validation directly covers `InitialFuelFraction`, `InitialChargeFraction`, `InitialTemperatureCelsius`, positive finite `FixedStepSeconds`, and `MaxCatchUpSteps > 0`.
- Fuel/Power/Thermal created as C++ default subobjects; Blueprint assigns Data Assets/tuning only.
- No `UEdenResourceComponentBase`.
- Linear thermal model with `DegreesCelsiusPerSecond`; dissipation cannot cross ambient.
- Debug visibility locked to `ShowDebug EdenSystems`.
- Separate `EEdenFuelState`, `EEdenPowerState`, and `EEdenThermalState`.
- One `OnStateChanged(Previous, Current)` per system plus terminal events.

### Remaining Work

- Review and accept Checkpoint A.
- Authorize Checkpoint B only when ready.
- Keep fuel, power, thermal, resource Data Assets, pawn resource components, propulsion-demand integration, ShowDebug, Blueprints, and Unreal assets untouched until their planned checkpoints.

### Next Clean Action

Review Checkpoint A. Do not begin Checkpoint B yet.
