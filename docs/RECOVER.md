# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `codex/0007-unreal-adapter` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** - Unreal lane Checkpoint A ready for acceptance |
| ExecPlan 0004 | Complete |
| ExecPlan 0005 | Complete |
| ExecPlan 0006 | **Complete** — JSON export + ShowAfterAction (2B) |
| Last successful validation | Repository validation PASS; Win64 Development Editor build PASS; `Automation RunTests Eden.Unit.Telemetry` PASS; `Automation RunTests Eden` PASS; `git diff --check` PASS; Source `LogTemp` search clean |
| Next task | Review/accept ExecPlan 0007 Checkpoint A. Do not begin Checkpoint B until explicit approval |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md` and ExecPlan 0007.

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator/AAR widgets and Input Actions (Git LFS).
- Do not reopen 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.
- Do not polish 0005/0006 unless 0007 exposes a genuine contract defect.
- AAR remains console-driven (`ShowAfterAction`); no auto-popup.
- 0007 Unreal lane is one checkpoint at a time: A, B, C, E. Do not implement ProjectEden Checkpoint D in this repository.
- Do not begin Checkpoint B until Checkpoint A is explicitly accepted.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- ProjectEden Checkpoint D is separate and must be complete before Checkpoint F convergence.

## Session Handoff

### Completed

- 0005 PIE + merge to `main`.
- 0006 minimal export/AAR surface.
- 0007 Checkpoint A implemented for review: telemetry sink seam plus local JSON sink.

### Latest Checkpoint A Evidence

- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1` passed.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -EngineRoot "K:\Program Files\Epic Games\UE_5.8"` passed.
- `UnrealEditor-Cmd.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.Telemetry; Quit" "-TestExit=Automation Test Queue Empty"` passed with 5 tests found and exit code 0 in `Saved/Logs/Automation-0007A-Telemetry.log`.
- `UnrealEditor-Cmd.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden; Quit" "-TestExit=Automation Test Queue Empty"` passed with 196 tests found and exit code 0 in `Saved/Logs/Automation-0007A-Eden.log`.
- `git diff --check` passed.
- `Get-ChildItem -Path Source -Recurse -Include *.h,*.cpp | Select-String -Pattern "LogTemp"` returned no matches.

### Next Clean Action

Wait for Checkpoint A acceptance. After approval, implement Checkpoint B only: EDEN connection/config/auth contract.
