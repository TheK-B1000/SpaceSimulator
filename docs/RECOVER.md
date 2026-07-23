# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-07-23 |
| Branch | `feature/spacecraft-resource-simulation` |
| Clean baseline tag | `v0.1.0-flight-shell` on commit `ed7fb55` |
| Working tree | Checkpoint G documentation-only closeout changes are expected pending changes: `docs/ARCHITECTURE.md`, `docs/REMEMBER.md`, `docs/RECOVER.md`, and `docs/exec-plans/0003-spacecraft-resource-simulation.md`. |
| Active ExecPlan | `docs/exec-plans/0003-spacecraft-resource-simulation.md` (Blocked pending hands-on PIE verification) |
| Previous ExecPlan | `docs/exec-plans/0002-six-axis-flight.md` (verified complete) |
| Flight shell status | Committed, PIE-verified, and tagged as `v0.1.0-flight-shell`. |
| Resource implementation | Checkpoints A-F are accepted and committed: A `9bede83`, B `88788c0`, C `e410878`, D `5bce7ab`, E `9a4eca0`, F `867da77`. Checkpoint G automated validation, editor script verification, source-control review, and documentation updates are done. Mission, HUD/widgets, alerts, telemetry, networking, EDEN OS work, save/load, oxygen, additional resources, and fuel-based flight shutdown are not started. |
| Last successful validation | Repository validation: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`; build: `K:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild`; tests: `K:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject -Unattended -NoSplash -NullRHI -NoP4 "-ExecCmds=Automation RunTests Eden; Quit" "-TestExit=Automation Test Queue Empty" -Log`; editor scripts: same command with `scripts\Editor\VerifyFlightAssets.py`, `scripts\Editor\VerifyFlightRuntimeSmoke.py`, and `scripts\Editor\VerifyResourceAssets.py`; source search: `Get-ChildItem Source -Recurse -File | Select-String -Pattern 'LogTemp'`. |
| Last successful result | Repository validation passed. `Source` search found no `LogTemp`. `EdenSpaceSimulatorEditor` Win64 Development build passed. Automation log `Saved/Logs/EdenSpaceSimulator.log` reported 99 automation tests found for `Eden` and `**** TEST COMPLETE. EXIT CODE: 0 ****`; foundation, flight, simulation clock, fuel, power, thermal, systems integration, and debug tests reported success. `VerifyFlightAssets.py` logged `Flight asset verification passed.` `VerifyFlightRuntimeSmoke.py` logged `Flight runtime smoke passed` and showed the transient pawn stopped before the sandbox blocker with inward X velocity removed. `VerifyResourceAssets.py` logged `Resource asset verification passed.` |
| Next task | Complete hands-on PIE verification for ExecPlan 0003, then mark the plan Complete and commit/tag the milestone if the manual evidence passes. Do not begin the next gameplay milestone yet. |

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
6. Confirm Checkpoints A-F are committed through `867da77`.
7. Confirm the only pending files are Checkpoint G documentation updates unless a later session has added manual PIE evidence.
8. Do not begin the next gameplay milestone until the required hands-on PIE verification is completed and ExecPlan 0003 is marked Complete or explicitly accepted with documented residual risk.

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not run broad asset-fix, redirector, clean, or regeneration operations before a checkpoint.
- Do not delete `Content`, `Config`, or `Source`.
- Generated directories may be removed only when the editor is closed and the task requires a clean rebuild.
- Preserve logs or screenshots that provide the only evidence of a failure.
- If the build cannot be reproduced, mark the checkpoint unverified and investigate before adding features.
- If resource work must be abandoned, return to tag `v0.1.0-flight-shell`.

## Current Known Risks

- ExecPlan 0003 is blocked from Complete status only by hands-on PIE verification. Automated validation, editor script verification, documentation updates, and source-control review have been completed.
- Manual PIE verification remains pending for live configured resource behavior: confirm `ShowDebug EdenSystems` renders over the possessed pawn, displays clock/fuel/power/thermal values from the assigned Data Assets, sustained thrust consumes fuel, zero thrust does not consume propulsion fuel, power and thermal values change over time, threshold/recovery transitions are observable where reachable with current config, PIE stop/start resets clock/resource/flight state, no duplicate components exist, and the Output Log has no unexpected errors, project `LogTemp`, or per-frame resource spam.
- `UnrealEditor-Cmd.exe ... "-ExecCmds=ShowDebug EdenSystems; Quit" -Log` accepted `Cmd: ShowDebug EdenSystems; Quit`, but the command-line editor process did not exit on `Quit`; the spawned process was manually stopped.
- Exact machine-local Unreal Engine installation path remains machine-specific and must not be committed.
- Git prints a warning that it cannot access `C:\Users\K-B/.config/git/ignore`; this is outside the repository and did not block validation.
- `git diff --stat` with normal LFS filters hit `.git\lfs\tmp` access denied while cleaning `BP_EdenSpacecraftPawn.uasset`; use `git -c filter.lfs.process= -c filter.lfs.clean= -c filter.lfs.smudge= -c filter.lfs.required=false diff --stat` for review in this environment if the warning persists.
- The all-in-one `scripts/Validate-Project.ps1 -Build -RunTests ... -TestFilter Eden` command timed out twice at the `Build.bat ... -WaitMutex -FromMsBuild` step before compiler diagnostics. Direct repository validation, direct `Build.bat ... -NoMutex -FromMsBuild`, and direct `UnrealEditor-Cmd.exe` automation all passed.
- Engine `LogConsoleManager` may warn about `r.MotionVectorSimulation` on the render thread; that is residual engine noise, not Eden flight-shell ownership.
- Automation logs include expected `LogEdenSimClock` warnings from tests that intentionally exercise invalid fixed-step config, invalid subscribers, and overrun reporting.
- Automation logs include expected `LogEdenSystems` warnings and one expected invalid-config error from tests that intentionally exercise fuel sanitization and safe disable paths.
- Automation logs include expected `LogEdenSystems` warnings and expected invalid-config errors from tests that intentionally exercise power and thermal sanitization and safe disable paths.
- Automation logs include expected `LogEdenSystems` warnings and one expected multiple-source ambiguity error from Checkpoint D integration tests that intentionally exercise missing config, missing clock, missing propulsion source, and ambiguous propulsion source safe paths.
- The broad `Automation RunTests Eden` filter also listed a few engine/plugin tests whose names contain `Eden`/matching text and emitted engine/tooling `LogTemp` lines. Project source search found no `LogTemp` usage in `Source`.
- Unreal platform validation still reports non-Win64 SDK gaps for Android, Linux, LinuxArm64, and VisionOS; Win64 is valid and the requested build/tests passed.

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
- Checkpoint A accepted and committed as `9bede83`.
- Implemented Checkpoint B fuel scope:
  - `EEdenFuelState`
  - `FEdenFuelConfig`
  - `FEdenFuelStateSnapshot`
  - `FEdenFuelModel`
  - `UEdenFuelConfigDataAsset`
  - `UEdenFuelSystemComponent`
  - `Eden.Unit.Systems.Fuel.*` tests
- Verified Checkpoint B through repository validation, Win64 Development Editor build, foundation smoke, existing flight tests, existing SimClock tests, and new Fuel tests.
- Checkpoint B accepted and committed as `88788c0`.
- Implemented Checkpoint C power/thermal scope:
  - `EEdenPowerState`
  - `FEdenPowerConfig`
  - `FEdenPowerStateSnapshot`
  - `FEdenPowerModel`
  - `UEdenPowerConfigDataAsset`
  - `UEdenPowerSystemComponent`
  - `EEdenThermalState`
  - `FEdenThermalConfig`
  - `FEdenThermalStateSnapshot`
  - `FEdenThermalModel`
  - `UEdenThermalConfigDataAsset`
  - `UEdenThermalSystemComponent`
  - `Eden.Unit.Systems.Power.*` tests
  - `Eden.Unit.Systems.Thermal.*` tests
- Verified Checkpoint C through repository validation, Win64 Development Editor build, foundation smoke, existing flight tests, existing SimClock tests, existing Fuel tests, and new Power/Thermal tests.
- Checkpoint C accepted and committed as `e410878`.
- Implemented Checkpoint D resource integration scope:
  - `IEdenPropulsionDemandSource`
  - `UEdenFlightMovementComponent::GetPropulsionDemandNormalized()`
  - fuel, power, and thermal C++ default subobjects on `AEdenSpacecraftPawn`
  - fuel demand source discovery by interface with weak non-owning component reference
  - missing, invalid, and ambiguous propulsion source safe-zero behavior
  - missing clock safe-disable behavior for fuel, power, and thermal
  - `Eden.Integration.Systems.*` tests
- Verified Checkpoint D through repository validation, Win64 Development Editor build, foundation smoke, existing flight tests, existing SimClock tests, existing Fuel/Power/Thermal tests, and new Systems integration tests.
- Checkpoint D accepted and committed as `5bce7ab`.
- Implemented Checkpoint E development-only debug visibility scope:
  - `FEdenSimulationClockDebugSnapshot`
  - `FEdenFuelDebugSnapshot`
  - `FEdenPowerDebugSnapshot`
  - `FEdenThermalDebugSnapshot`
  - `FEdenSpacecraftSystemsDebugSnapshot`
  - read-only debug query methods on the clock and resource system components
  - pawn-level `GetEdenSystemsDebugSnapshot()`
  - non-shipping `AEdenSpacecraftPawn::DisplayDebug()` support for `ShowDebug EdenSystems`
  - `Eden.Unit.Systems.Debug.*` tests
- Verified Checkpoint E through repository validation, `git diff --check`, source `LogTemp` search, Win64 Development Editor build, existing Eden tests, and new debug tests.
- Command-line editor smoke accepted `ShowDebug EdenSystems`, but visual PIE overlay verification remains pending.
- Checkpoint E accepted and committed as `9a4eca0`.
- Implemented Checkpoint F Blueprint composition and configuration assets:
  - `Content/Eden/Data/Systems/DA_EdenFuelConfig.uasset`
  - `Content/Eden/Data/Systems/DA_EdenPowerConfig.uasset`
  - `Content/Eden/Data/Systems/DA_EdenThermalConfig.uasset`
  - assigned those assets to inherited `FuelSystem`, `PowerSystem`, and `ThermalSystem` on `BP_EdenSpacecraftPawn`
  - added `scripts/Editor/VerifyResourceAssets.py`
- Verified Checkpoint F through resource asset commandlet validation, existing flight asset commandlet regression validation, repository validation, Win64 Development Editor build, and full `Automation RunTests Eden`.
- Checkpoint F accepted and committed as `867da77`.
- Completed Checkpoint G automated closeout:
  - repository validation passed
  - Win64 Development Editor build passed
  - full `Automation RunTests Eden` passed with 99 tests
  - `VerifyFlightAssets.py`, `VerifyFlightRuntimeSmoke.py`, and `VerifyResourceAssets.py` passed
  - `Source` search found no `LogTemp`
  - `docs/ARCHITECTURE.md`, `docs/REMEMBER.md`, `docs/RECOVER.md`, and ExecPlan 0003 were updated

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

- Complete manual PIE verification for live configured resource behavior and `ShowDebug EdenSystems`.
- If manual verification passes, mark ExecPlan 0003 Complete, commit the Checkpoint G docs, and create a milestone tag.
- Keep mission, HUD/widgets, alerts, telemetry, networking, EDEN OS work, fuel-based flight shutdown, additional resources, and unrelated tuning untouched until their planned checkpoints.

### Next Clean Action

Complete the manual PIE verification gate for ExecPlan 0003. Do not begin the next milestone yet.
