# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-07-22 |
| Branch | `feature/spacecraft-resource-simulation` |
| Clean baseline tag | `v0.1.0-flight-shell` on commit `ed7fb55` |
| Working tree | Draft ExecPlan `0003` and this recovery update are the only expected pending docs changes until Checkpoint A is approved. |
| Active ExecPlan | `docs/exec-plans/0003-spacecraft-resource-simulation.md` (Draft — awaiting approval) |
| Previous ExecPlan | `docs/exec-plans/0002-six-axis-flight.md` (verified complete) |
| Flight shell status | Committed, PIE-verified, and tagged as `v0.1.0-flight-shell`. |
| Resource implementation | Not started. Checkpoint A is blocked until the revised Draft is explicitly approved and the working tree is clean. |
| Next task | Review and approve ExecPlan 0003. Do not begin Checkpoint A yet. |

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
6. Do not start Checkpoint A unless the Draft plan is explicitly approved and `git status` is clean.

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not run broad asset-fix, redirector, clean, or regeneration operations before a checkpoint.
- Do not delete `Content`, `Config`, or `Source`.
- Generated directories may be removed only when the editor is closed and the task requires a clean rebuild.
- Preserve logs or screenshots that provide the only evidence of a failure.
- If the build cannot be reproduced, mark the checkpoint unverified and investigate before adding features.
- If resource work must be abandoned, return to tag `v0.1.0-flight-shell`.

## Current Known Risks

- ExecPlan 0003 is still Draft; implementation authorization has not been granted.
- Exact machine-local Unreal Engine installation path remains machine-specific and must not be committed.
- Git prints a warning that it cannot access `C:\Users\K-B/.config/git/ignore`; this is outside the repository and did not block validation.
- Engine `LogConsoleManager` may warn about `r.MotionVectorSimulation` on the render thread; that is residual engine noise, not Eden flight-shell ownership.

## Session Handoff

### Completed

- Verified six-axis flight shell on `main` through build, automation, asset/runtime commandlets, and interactive PIE.
- Annotated tag `v0.1.0-flight-shell` created on `ed7fb55`.
- Branch `feature/spacecraft-resource-simulation` created from that baseline.
- ExecPlan 0003 revised with locked review decisions and kept in Draft status.

### Locked decisions recorded in ExecPlan 0003

- `IEdenPropulsionDemandSource` on flight movement; fuel holds a weak non-owning reference.
- No pawn `LastThrustFraction` / `GetCurrentThrustFraction` API.
- Fuel/Power/Thermal created as C++ default subobjects; Blueprint assigns Data Assets/tuning only.
- No `UEdenResourceComponentBase`.
- Linear thermal model with `DegreesCelsiusPerSecond`; dissipation cannot cross ambient.
- Debug visibility locked to `ShowDebug EdenSystems`.
- Separate `EEdenFuelState`, `EEdenPowerState`, and `EEdenThermalState`.
- One `OnStateChanged(Previous, Current)` per system plus terminal events.

### Remaining Work

- Review and explicitly approve the revised ExecPlan 0003.
- Ensure a clean working tree.
- Only then authorize Checkpoint A.

### Next Clean Action

Approve or revise ExecPlan 0003. Do not begin Checkpoint A yet.
