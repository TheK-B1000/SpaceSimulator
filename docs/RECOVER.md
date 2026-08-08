# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `feature/operator-hud-and-input` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | `docs/exec-plans/0005-operator-systems-control-and-mission-hud.md` |
| ExecPlan 0005 | Content wiring automated green (F/G); **Checkpoint I PIE remaining** |
| ExecPlan 0006 | Core on `main`; AAR/presentation after 0005 PIE |
| Last successful validation | Win64 Development Editor build; `Automation RunTests Eden.` → **186** / **0**; `VerifyOperatorAssets.py` passed |
| Next task | **0005 PIE** — Solar Crisis operator actions + HUD trade-offs, then docs Complete |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md`, ADR-0002, ExecPlan 0005.

Verify content wiring:

```powershell
& "K:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" `
  -Unattended -NoSplash -NullRHI -NoP4 `
  "-ExecutePythonScript=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\scripts\Editor\VerifyOperatorAssets.py"
```

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator Input Actions, `IMC_Flight`, `WBP_EdenOperatorHud`, and `DA_EdenOperatorControlConfig` (Git LFS).
- Do not reopen ExecPlan 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.

## Current Known Risks

- Operator keys: `T` thermal cycle, `L` load-shed toggle, `P` propulsion priority toggle.
- Production HUD formats snapshots in C++ (`UEdenOperatorHudWidget`); Blueprint may extend via `OnHudSnapshotUpdated` without owning simulation state.
- Telemetry `ClearHistory()` remains explicit (not auto-clear on every mission reset).

## Session Handoff

### Completed

- Created `IA_ThermalMode`, `IA_LoadShed`, `IA_PropulsionPriority`.
- Bound them on `IMC_Flight`; assigned on `BP_EdenFlightPlayerController` with `WBP_EdenOperatorHud`.
- Added `VerifyOperatorAssets.py` + create script; updated `VerifyFlightAssets.py` for additional IMC mappings.
- Build + 186 Eden automation tests green.

### Next Clean Action

Human 0005 PIE gate on `L_FlightSandbox` during Solar Crisis.
