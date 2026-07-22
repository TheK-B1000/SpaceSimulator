# Project Foundation Bootstrap

## Status
Verified complete

## Problem and outcome
The repository has the project documentation, Unreal descriptor, module scaffold, and validation script needed for a professional Unreal foundation, but the current tree is still a raw bootstrap state. Generated IDE files are visible as untracked files, the validation script fails in this checkout because Git requires a safe-directory override, the local Unreal Engine 5.8 installation is not discoverable from this shell, project logging categories do not exist yet, and there is no `Eden.Unit.Foundation` automation smoke test.

The outcome is a clean, documented foundation for the first vertical slice without implementing flight movement, resource simulation, mission gameplay, UI, networking, or EDEN OS integration.

Local foundation verification is now complete: repository validation, Win64 Development Editor build, `Eden.Unit.Foundation.Smoke`, Enhanced Input enablement, and Unreal Engine 5.8 project open all passed. No authored `.uasset` or `.umap` files were modified during verification.

## Scope
### In scope
- Record this bootstrap as the active ExecPlan.
- Preserve the existing `EdenSpaceSimulator` runtime module.
- Add only foundation C++ under the intended layout:
  - `Source/EdenSpaceSimulator/Public/Core`
  - `Source/EdenSpaceSimulator/Private/Core`
  - `Source/EdenSpaceSimulator/Private/Tests`
- Add project-specific log categories.
- Add one minimal Unreal automation smoke test named under `Eden.Unit.Foundation`.
- Keep `EnhancedInput` enabled and reviewable without creating binary input assets.
- Harden repository validation for this safe-directory checkout and engine-root discovery.
- Keep generated Unreal and IDE files ignored.
- Verify Git LFS patterns for Unreal binary assets.
- Update durable and recovery documentation with verified facts only.
- Fix documentation tree diagrams that were copied with broken box-drawing characters.

### Out of scope
- Flight movement.
- Resource, power, thermal, oxygen, or fixed-step simulation.
- Mission gameplay or failure orchestration.
- UI, HUD, widgets, or EDEN OS integration.
- New `.uasset` or `.umap` creation or modification.
- Broad source layout refactors or placeholder classes.

## Current repository state
- Git root is `K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator` when using `git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator`.
- Plain Git commands fail with a dubious ownership warning in this checkout.
- Exactly one `.uproject` exists: `EdenSpaceSimulator.uproject`.
- `.uproject` uses `EngineAssociation` GUID `{57DE750B-48E9-7B17-2D9B-D8BBA771EEBF}` and does not commit a local engine path.
- No `UE_ENGINE_ROOT` environment variable is visible from this shell.
- No registry mapping for the engine association was visible under the checked HKCU/HKLM paths.
- Common `C:\Program Files\Epic Games\UE_5.8` and `K:\UnrealEngine\UE_5.8` probes did not find `Build.bat`.
- The primary runtime module is `EdenSpaceSimulator`.
- Targets already use `BuildSettingsVersion.V7` and `EngineIncludeOrderVersion.Unreal5_8`.
- `EdenSpaceSimulator.Build.cs` already includes `Core`, `CoreUObject`, `Engine`, `InputCore`, and `EnhancedInput`.
- `Config/DefaultInput.ini` already uses `EnhancedPlayerInput` and `EnhancedInputComponent`.
- `Content` currently contains only visible collection/developer folders and no `.uasset` or `.umap` files.
- Git LFS is installed as `git-lfs/3.3.0`.
- `.gitattributes` maps `*.uasset` and `*.umap` to LFS.
- `.gitignore` ignores standard Unreal generated directories and `*.sln`, but does not yet ignore `*.slnx` or `.vsconfig`.
- `BOOTSTRAP_MANIFEST.json` and `scripts/Initialize-Repository.ps1` reference `CODEX_BOOTSTRAP_PROMPT.md`, which was missing before this plan.
- Direct `.\scripts\Validate-Project.ps1` is blocked by local PowerShell execution policy.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1` currently fails at the Git safe-directory check.

## Architecture alignment
This plan does not introduce gameplay state. The only new runtime surface is logging categories, which are global Unreal log categories and not mutable simulation truth. The automation smoke test validates module foundation only.

Dependency direction remains aligned with `docs/ARCHITECTURE.md`: foundation code sits in `Core`, tests sit under `Private/Tests`, no UI, mission, telemetry transport, or system ownership boundary is introduced. No ADR is required because state ownership, module boundaries, engine baseline, telemetry contracts, and the C++/Blueprint boundary remain unchanged.

## Alternatives considered
1. Add only a test file and leave logging, validation, and hygiene unchanged.
   - Rejected because the acceptance criteria explicitly require repository hygiene, project-specific logging, and baseline validation.

2. Create full `Flight`, `Systems`, `Missions`, and `Telemetry` folder trees now.
   - Rejected because empty folders and placeholder classes would add structure without owned responsibility.

3. Add a separate test module.
   - Rejected for this bootstrap because the requested smoke test is minimal and does not justify a new module dependency or target boundary.

4. Require users to run `git config --global --add safe.directory ...`.
   - Rejected because validation should work in this checkout without mutating global Git configuration.

## Milestones
1. Write the active ExecPlan.
2. Update repository hygiene and validation script.
3. Add `Core` logging categories and the foundation automation smoke test.
4. Update docs with verified facts, recovery status, and encoding cleanup.
5. Run repository-only validation.
6. Attempt build/test only if an engine root is discoverable or provided.
7. Review the final diff.

## Detailed steps
1. Edit `.gitignore` to ignore `*.slnx` and `.vsconfig`.
2. Optionally extend `.gitattributes` to include common Unreal binary sidecars if not already covered.
3. Edit `scripts/Validate-Project.ps1`:
   - Use per-command `git -c safe.directory=<project-root>` for repo-local Git operations.
   - Preserve the exact Git root check.
   - Resolve Unreal Engine root from `-EngineRoot`, `UE_ENGINE_ROOT`, or the `.uproject` engine association registry mapping when available.
   - Fail with a precise pending-verification message when no engine root is available.
4. Edit `EdenSpaceSimulator.uproject` only if needed to explicitly enable `EnhancedInput`, preserving existing plugin settings.
5. Edit `Source/EdenSpaceSimulator/EdenSpaceSimulator.Build.cs` only for justified foundation settings.
6. Add `Source/EdenSpaceSimulator/Public/Core/EdenLogCategories.h`.
7. Add `Source/EdenSpaceSimulator/Private/Core/EdenLogCategories.cpp`.
8. Add `Source/EdenSpaceSimulator/Private/Tests/EdenFoundationSmokeTest.cpp`.
9. Add missing `CODEX_BOOTSTRAP_PROMPT.md` to align the manifest and initialization script.
10. Harden `scripts/Initialize-Repository.ps1` for this safe-directory checkout.
11. Fix text tree diagrams in `README.md` and `docs/ARCHITECTURE.md`.
12. Update `docs/REMEMBER.md` with durable verified facts only.
13. Update `docs/RECOVER.md` with branch, active plan, validation commands, remaining engine build/test steps, and restart procedure.

## Validation
Repository-only validation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1
```

Expected result: pass after script hardening.

Editor build when an engine root is available:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -EngineRoot $env:UE_ENGINE_ROOT
```

Expected result: `EdenSpaceSimulatorEditor Win64 Development` builds.

Automation tests when an engine root is available:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -TestFilter Eden -EngineRoot $env:UE_ENGINE_ROOT
```

Expected result: `Eden.Unit.Foundation.Smoke` is discovered and passes.

## Failure modes and rollback
- If validation still fails on Git safe-directory handling, revert only the script changes and use per-command Git evidence manually.
- If the smoke test does not compile, remove `Private/Tests/EdenFoundationSmokeTest.cpp` and re-run the build to isolate the failure.
- If explicit `EnhancedInput` plugin enablement conflicts with the project descriptor, remove only that plugin entry and record the editor step required.
- If build or tests cannot run because the Unreal engine root is unavailable, record the exact pending commands in `docs/RECOVER.md`.
- No binary assets are edited, so rollback is text-only.

## Progress log
2026-07-22: Read `AGENTS.md`, mandatory docs, `ADR-0001`, `.agent/PLANS.md`, and relevant setup docs.
2026-07-22: Confirmed exactly one `.uproject`, current module/target files, LFS availability, Enhanced Input config, and no authored assets visible under `Content`.
2026-07-22: Confirmed direct PowerShell script invocation is blocked by execution policy and bypassed invocation fails on Git safe-directory handling.
2026-07-22: Added repository hygiene updates, explicit Enhanced Input plugin enablement, C++20 module setting, project log categories, and `Eden.Unit.Foundation.Smoke`.
2026-07-22: Hardened validation and initialization scripts for per-command Git safe-directory handling.
2026-07-22: Repository-only validation passed.
2026-07-22: Build validation reached the engine-root gate and is pending local Unreal Engine 5.8 discovery or `UE_ENGINE_ROOT`.
2026-07-22: Local foundation verification completed with `-Build -RunTests -TestFilter Eden -EngineRoot $env:UE_ENGINE_ROOT`.
2026-07-22: Repository validation passed; `EdenSpaceSimulatorEditor` Win64 Development build passed; `Eden.Unit.Foundation.Smoke` passed.
2026-07-22: Unreal Engine 5.8 opened the project successfully; Enhanced Input remained enabled; no authored `.uasset` or `.umap` files were modified.

## Decision log
2026-07-22: Keep the existing primary runtime module because it matches the project name, targets, and architecture.
2026-07-22: Do not create empty `Flight`, `Systems`, `Missions`, or `Telemetry` folders until those responsibilities have implemented code.
2026-07-22: Use per-command Git safe-directory overrides inside validation instead of mutating global Git config.
2026-07-22: Mark Unreal build, automation, and editor checks pending because no engine root is discoverable from this shell.
2026-07-22: Close those pending checks after local verification evidence; do not start flight implementation until the first foundation commit and ExecPlan `0002-six-axis-flight.md` are prepared.

## Acceptance evidence
Verified complete:

- Repository validation passed with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden
```

- Win64 Development Editor build passed (`EdenSpaceSimulatorEditor`).
- `Eden.Unit.Foundation.Smoke` was discovered and passed.
- Unreal Engine 5.8 opened the project successfully.
- Enhanced Input is enabled in the project descriptor and input config.
- No authored `.uasset` or `.umap` files were modified during verification.

Earlier repository-only evidence retained:

- `EdenSpaceSimulator.uproject` parses as JSON after edits.
- `scripts/Validate-Project.ps1` and `scripts/Initialize-Repository.ps1` parse with the PowerShell AST parser.
- `git check-ignore` confirms generated directories, `.sln`, `.slnx`, and `.vsconfig` are ignored.
- `Select-String` found no `LogTemp` usage in source.

Not claimed as verified by this plan:

- First foundation Git commit.
- Exact machine-local engine installation path as a durable committed fact.
- Flight, systems, mission, UI, telemetry gameplay, or EDEN OS work.

## Handoff
Foundation bootstrap and local verification are complete. Do not begin flight implementation yet.

Next clean checkpoint:

1. Review the uncommitted bootstrap working tree and make the first foundation commit when requested.
2. Author ExecPlan `0002-six-axis-flight.md` only after that commit or an explicit go-ahead.
3. Keep flight movement, resource simulation, mission gameplay, UI, and EDEN OS integration out of scope until their own ExecPlans exist.
