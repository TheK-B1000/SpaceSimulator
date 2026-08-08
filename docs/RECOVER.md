# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag | `v0.3.0-emergency-mission` |
| Active ExecPlan | `docs/exec-plans/0005-operator-systems-control-and-mission-hud.md` (content wiring) |
| ExecPlan 0003 | Complete — delayed PIE folded into 0004 H session 2026-08-08 |
| ExecPlan 0004 | **Complete** — Checkpoint H manual PIE passed 2026-08-08 |
| ExecPlan 0005 | Code on `main` (`6f6f6cb`); next: Blueprint HUD + input wiring + PIE |
| ExecPlan 0006 | Code on `main` (`ec07143`); next after 0005 content PIE: AAR presentation closeout |
| Last successful validation | Win64 Development Editor build; `Automation RunTests Eden.` → **186** successes / **0** failures; human PIE 0003+0004 passed |
| Next task | Create/use feature branch for 0005 content: `WBP_EdenOperatorHud`, operator Input Actions, IMC bindings, BP controller HUD assignment, then 0005 PIE |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md`, ADR-0002, ExecPlan 0005.

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve `DA_SolarEventEmergency.uasset` and `DA_EdenOperatorControlConfig.uasset` (Git LFS).
- Do not reopen ExecPlan 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.

## Current Known Risks

- Production HUD C++ widget exists; Blueprint `WBP_EdenOperatorHud` still needs content authoring and assignment on `BP_EdenFlightPlayerController`.
- Operator Enhanced Input actions (`IA_ThermalMode`, `IA_LoadShed`, `IA_PropulsionPriority`) still need asset creation + IMC mapping.
- Telemetry `LoadMission` history-close policy is explicit `ClearHistory()` (not auto-wired to every mission state transition).

## Session Handoff

### Completed

- 0004 Checkpoint H + delayed 0003 PIE closeout recorded.
- Tag `v0.3.0-emergency-mission` created for verified resource simulation + emergency mission shell.

### Next Clean Action

0005 Blueprint/input wiring on a feature branch from `main`, then 0005 PIE.
