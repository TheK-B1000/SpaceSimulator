# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `feature/operator-hud-and-input` (merging to `main`) |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | `docs/exec-plans/0006-telemetry-and-after-action-review.md` |
| ExecPlan 0005 | **Complete** — Checkpoint I PIE passed 2026-08-08 |
| ExecPlan 0006 | Core on `main`; next: minimal JSON export + ShowAfterAction (2B) |
| Last successful validation | Win64 Development Editor build; `Automation RunTests Eden.` → **186** / **0**; `VerifyOperatorAssets.py` passed; human 0005 PIE passed |
| Next task | Minimal 0006: real `ExportSessionJsonV1`, `Saved/Telemetry/*.json`, `ExportTelemetry` / `ShowAfterAction`, thin AAR WBP (no auto-popup) |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md`, ADR-0002, ExecPlan 0006.

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator Input Actions, `IMC_Flight`, `WBP_EdenOperatorHud`, and `DA_EdenOperatorControlConfig` (Git LFS).
- Do not reopen ExecPlan 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.
- Do not add automatic AAR popups for 0006 (locked 2B: console/exec only).

## Current Known Risks

- Operator keys: `T` thermal cycle, `L` load-shed toggle, `P` propulsion priority toggle.
- `ExportSessionJsonV1` is still a metadata stub until 0006 closeout; 0007 must consume the finished wire schema.
- Telemetry `ClearHistory()` remains explicit (not auto-clear on every mission reset).

## Session Handoff

### Completed

- 0005 content wiring + PIE closeout.

### Next Clean Action

Implement minimal 0006 export/AAR (2B) on `main` after merge.
