# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-07-22 |
| Branch | `main` |
| Commit | No commits yet. `git rev-parse --short HEAD` reports `Needed a single revision`. |
| Working tree | Initial bootstrap files are untracked. Generated Unreal and IDE outputs are ignored. |
| Active ExecPlan | `docs/exec-plans/0001-project-foundation.md` (verified complete) |
| Last successful build | `EdenSpaceSimulatorEditor` Win64 Development passed via `Validate-Project.ps1 -Build -RunTests`. |
| Last successful tests | `Eden.Unit.Foundation.Smoke` passed with `-TestFilter Eden`. |
| Editor verification | Unreal Engine 5.8 opened the project successfully. Enhanced Input is enabled. No authored `.uasset` or `.umap` files were modified during verification. |
| Next task | Review the uncommitted bootstrap tree and make the first foundation commit when requested. Do not begin flight implementation yet. |

## Recovery protocol

Run from the Git root:

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -5 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff
```

Then:

1. Read `AGENTS.md`.
2. Read all mandatory documents in its preflight order.
3. Read the active ExecPlan, if one is recorded.
4. Inspect recent commits and the current diff.
5. Run the repository-only validation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1
```

6. When an engine path is available, build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -EngineRoot $env:UE_ENGINE_ROOT
```

7. Run tests when appropriate:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -TestFilter Eden -EngineRoot $env:UE_ENGINE_ROOT
```

8. Compare actual state with this file. Correct stale recovery information before continuing.

## Safe restart rules

- Do not discard local changes without inspecting them.
- Do not run broad asset-fix, redirector, clean, or regeneration operations before a checkpoint.
- Do not delete `Content`, `Config`, or `Source`.
- Generated directories may be removed only when the editor is closed and the task requires a clean rebuild.
- Preserve logs or screenshots that provide the only evidence of a failure.
- If the build cannot be reproduced, mark the checkpoint unverified and investigate before adding features.

## Current known risks

- The repository has no commits yet, so `git diff` has no tracked baseline for untracked bootstrap files.
- Git prints a warning that it cannot access `C:\Users\K-B/.config/git/ignore`; this is outside the repository and did not block validation.
- Exact machine-local Unreal Engine installation path remains machine-specific and must not be committed.
- Git LFS patterns are present, but historical LFS migration is not applicable until binary assets exist or prior binary history is inspected.
- Flight implementation has not started; do not begin it until the first foundation commit and ExecPlan `0002-six-axis-flight.md` are ready.

## Session handoff template

Replace this section at the end of substantial work.

### Completed

- Mandatory docs and `ADR-0001` were read.
- Git root and exactly one `.uproject` were confirmed with a per-command safe-directory override.
- `.gitignore` now ignores generated `.slnx` and `.vsconfig` files.
- `.gitattributes` includes LFS patterns for `.uasset`, `.umap`, and common Unreal binary sidecars.
- `EnhancedInput` is explicitly enabled in `EdenSpaceSimulator.uproject`.
- Project-specific log categories were added under `Source/EdenSpaceSimulator/Public/Core` and `Private/Core`.
- Minimal automation smoke test `Eden.Unit.Foundation.Smoke` was added under `Private/Tests`.
- Validation and initialization scripts were hardened for this safe-directory checkout.
- README and architecture source-layout diagrams were normalized to ASCII.
- Missing `CODEX_BOOTSTRAP_PROMPT.md` was added to match `BOOTSTRAP_MANIFEST.json` and `scripts/Initialize-Repository.ps1`.
- Local foundation verification completed: repository validation, Win64 Development Editor build, smoke test, and editor open all passed.
- No authored `.uasset` or `.umap` files were modified during verification.
- Active ExecPlan `0001-project-foundation.md` marked verified complete.

### Files changed

- `.gitignore`
- `.gitattributes`
- `CODEX_BOOTSTRAP_PROMPT.md`
- `EdenSpaceSimulator.uproject`
- `README.md`
- `Source/EdenSpaceSimulator/EdenSpaceSimulator.Build.cs`
- `Source/EdenSpaceSimulator/Public/Core/EdenLogCategories.h`
- `Source/EdenSpaceSimulator/Private/Core/EdenLogCategories.cpp`
- `Source/EdenSpaceSimulator/Private/Tests/EdenFoundationSmokeTest.cpp`
- `docs/ARCHITECTURE.md`
- `docs/REMEMBER.md`
- `docs/RECOVER.md`
- `docs/exec-plans/0001-project-foundation.md`
- `scripts/Initialize-Repository.ps1`
- `scripts/Validate-Project.ps1`

### Validation

```text
Command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT -TestFilter Eden
Result: Passed.
Evidence:
- Repository validation passed
- Win64 Development Editor build passed
- Eden.Unit.Foundation.Smoke passed
- Unreal Engine 5.8 opened the project successfully
- Enhanced Input is enabled
- No authored uasset or umap files were modified during verification

Earlier repository-only evidence:
Command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1
Result: Passed.

Command: PowerShell AST parse for scripts\Validate-Project.ps1 and scripts\Initialize-Repository.ps1
Result: Passed.
```

### Decisions

- Keep the existing `EdenSpaceSimulator` runtime module.
- Add no gameplay systems during bootstrap.
- Use per-command Git `safe.directory` handling instead of changing global Git config.
- Treat foundation local verification as complete; do not start flight work until the first foundation commit and a new ExecPlan exist.

### Remaining work

- Review the uncommitted bootstrap working tree.
- Make the first foundation commit when requested.
- Author ExecPlan `0002-six-axis-flight.md` only after that commit or an explicit go-ahead.
- Do not begin flight implementation, resource simulation, mission gameplay, UI, or EDEN OS integration yet.

### Risks or blockers

- The working tree is initial and uncommitted, so review must treat all project files as candidate bootstrap content.
- Exact engine path stays local via `UE_ENGINE_ROOT` or editor association and must not be committed.

### Next clean action

- Make the first foundation commit when requested, then prepare ExecPlan `0002-six-axis-flight.md` before any flight code.
