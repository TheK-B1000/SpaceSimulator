# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-07-22 |
| Branch | `main` |
| Commit | `0047e41` |
| Working tree | Six-axis flight implementation is uncommitted. Planned C++ files, config changes, flight input assets, Blueprint assets, and `L_FlightSandbox` are present. |
| Active ExecPlan | `docs/exec-plans/0002-six-axis-flight.md` (verified complete) |
| Last successful validation | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden` |
| Last successful result | Repository validation passed, `EdenSpaceSimulatorEditor` Win64 Development build passed, and `Eden` automation tests passed. |
| Asset verification | `UnrealEditor-Cmd.exe ... -ExecutePythonScript=scripts\Editor\VerifyFlightAssets.py -NullRHI -Unattended` passed. |
| Runtime smoke | `UnrealEditor-Cmd.exe ... -ExecutePythonScript=scripts\Editor\VerifyFlightRuntimeSmoke.py -NullRHI -Unattended` passed. Transient pawn stopped at X=748.400003 and cleared inward X velocity. |
| Interactive PIE verification | Passed. `L_FlightSandbox` startup map, intended pawn/controller through `BP_EdenFlightGameMode`, possession, camera, six axes, `X` stabilization toggle, released-axis damping without auto-level, blocker stop without bounce, PIE stop/start reset, no `LogTemp`, no per-frame spam, and no unexpected errors. |
| Next task | Review the uncommitted flight working tree and commit when requested. Do not start the next simulation milestone without a new ExecPlan. |

## Recovery protocol

Run from the Git root:

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -5 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff
```

Then:

1. Read `AGENTS.md`.
2. Read all mandatory documents in its preflight order.
3. Read `docs/exec-plans/0002-six-axis-flight.md`.
4. Confirm no unrelated authored assets were added, removed, or regenerated.
5. Ensure `UE_ENGINE_ROOT` points at a local Unreal Engine 5.8 installation.
6. Run final validation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden
```

7. Optional commandlet verification:

```powershell
& "$env:UE_ENGINE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ".\EdenSpaceSimulator.uproject" -Unattended -NoSplash -NoP4 -NullRHI -ExecutePythonScript="scripts\Editor\VerifyFlightAssets.py" -AbsLog="$PWD\Saved\Logs\VerifyFlightAssets.log"
& "$env:UE_ENGINE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ".\EdenSpaceSimulator.uproject" -Unattended -NoSplash -NoP4 -NullRHI -ExecutePythonScript="scripts\Editor\VerifyFlightRuntimeSmoke.py" -AbsLog="$PWD\Saved\Logs\VerifyFlightRuntimeSmoke.log"
```

8. Compare actual state with this file. Correct stale recovery information before continuing.

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not run broad asset-fix, redirector, clean, or regeneration operations before a checkpoint.
- Do not delete `Content`, `Config`, or `Source`.
- Generated directories may be removed only when the editor is closed and the task requires a clean rebuild.
- Preserve logs or screenshots that provide the only evidence of a failure.
- If the build cannot be reproduced, mark the checkpoint unverified and investigate before adding features.

## Current Known Risks

- The flight shell working tree is still uncommitted relative to `0047e41`.
- The `Eden` automation filter can also match a few engine tests whose paths contain matching text; project flight and foundation tests still pass.
- Exact machine-local Unreal Engine installation path remains machine-specific and must not be committed.
- Git prints a warning that it cannot access `C:\Users\K-B/.config/git/ignore`; this is outside the repository and did not block validation.

## Session Handoff

### Completed

- Implemented Checkpoint A: flight command types, controller-owned input intent data, deterministic movement model, kinematic movement component, and `Eden.Unit.Flight` automation tests.
- Implemented Checkpoint B: `AEdenSpacecraftPawn`, `AEdenFlightPlayerController`, and `AEdenFlightGameMode`.
- Implemented Checkpoint C: `IA_FlightTranslate`, `IA_FlightRotate`, `IA_FlightStabilize`, `IMC_Flight`, `BP_EdenSpacecraftPawn`, `BP_EdenFlightPlayerController`, and `BP_EdenFlightGameMode`.
- Implemented Checkpoint D: `L_FlightSandbox`, a simple blocking cube, a PlayerStart, a light, `GameDefaultMap`, `EditorStartupMap`, and `GlobalDefaultGameMode`.
- Completed Checkpoint E commandlet and build/test validation.
- Completed interactive PIE verification for possession, camera, six-axis controls, stabilization, blocker response, PIE restart reset, and Output Log review.
- Marked ExecPlan `0002-six-axis-flight.md` verified complete.

### Validation

```text
Command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden
Result: Passed.
Evidence:
- Repository validation passed.
- EdenSpaceSimulatorEditor Win64 Development build passed.
- Eden.Unit.Flight.* tests passed.
- Eden.Unit.Foundation.Smoke passed.
```

```text
Command: UnrealEditor-Cmd.exe EdenSpaceSimulator.uproject -Unattended -NoSplash -NoP4 -NullRHI -ExecutePythonScript=scripts\Editor\VerifyFlightAssets.py -AbsLog=Saved\Logs\VerifyFlightAssets.log
Result: Passed.
```

```text
Command: UnrealEditor-Cmd.exe EdenSpaceSimulator.uproject -Unattended -NoSplash -NoP4 -NullRHI -ExecutePythonScript=scripts\Editor\VerifyFlightRuntimeSmoke.py -AbsLog=Saved\Logs\VerifyFlightRuntimeSmoke.log
Result: Passed.
Evidence:
- Transient BP_EdenSpacecraftPawn hit the sandbox blocker, stopped at X=748.400003, and cleared inward X velocity.
```

```text
Manual PIE verification in Unreal Engine 5.8
Result: Passed.
Evidence:
- L_FlightSandbox opens as the startup map
- BP_EdenFlightGameMode selects the intended pawn and controller; BP_EdenSpacecraftPawn spawns and is possessed
- Camera usable; W/S, A/D, Space/Left Ctrl, Mouse X/Y, Q/E all work
- X toggles stabilization; damps released axes; no auto-level
- Sandbox blocker stops inward movement without bounce
- PIE stop/start resets input intent and movement velocity
- Output Log contains no LogTemp, no repeated per-frame spam, and no unexpected errors
```

### Remaining Work

- Review the uncommitted flight working tree.
- Commit the verified flight shell when requested.
- Author the next ExecPlan before resource, mission, UI, telemetry, or EDEN OS work.

### Next Clean Action

Commit the flight shell when requested.
