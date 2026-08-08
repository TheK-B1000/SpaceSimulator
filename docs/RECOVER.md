# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `plan/emergency-mission-shell` |
| Active ExecPlan | `docs/exec-plans/0004-emergency-scenario-mission-shell.md` |
| Checkpoint A–E | Accepted architecture baseline through D `326057b`; E dispatch retained and corrected under remediation |
| Checkpoint F | Remediated and automation-green (objective/outcome integration) |
| Checkpoint G | Remediated with real `Content/Eden/Data/Missions/DA_SolarEventEmergency.uasset`; VerifyMissionAssets validates the actual asset |
| Checkpoint H | Code present and automated-revalidated against corrected F/G; **manual PIE remains the final human gate** |
| ExecPlan 0003 | Still blocked on delayed hands-on PIE |
| Last successful validation | Repo validate; Win64 Development Editor build; `Automation RunTests Eden.` exit 0 (178 project `Eden.*` tests); VerifyFlightAssets / VerifyResourceAssets / VerifyMissionAssets passed; no `LogTemp` / no mission `TObjectIterator` |
| Next task | Manual PIE closeout for 0004 Checkpoint H (and delayed 0003 PIE). Do not mark 0004 Complete until PIE evidence is recorded. |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md` and mandatory docs, then ExecPlan 0004.

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve `DA_SolarEventEmergency.uasset` (Git LFS).
- Do not revert historical premature G/H commits merely because claims were early; fix forward.
- Parallel 0005 planning may exist as an untracked/local ExecPlan; do not mix it into 0004 remediation commits unless requested.

## Current Known Risks

- Manual PIE for 0004 H and delayed 0003 resource PIE are still outstanding.
- Historical commits `3f2dfd5` / `9662bea` / `4a12241` claimed G/H prematurely; current tree is corrected by remediation.
- Fail-only objectives (`KeepTemperatureBelow`, `MaintainFuelAbove`) remain Active when held; EvaluateOutcome treats them as satisfied for success once required complete-seeking objectives Complete.
- Missing resource targets no longer invent depleted fuel/power readings during objective evaluation.
- `SetPowerGeneration` remains unsupported and is rejected by definition validation.
- Platform INVALID SDK noise and expected negative-path automation warnings remain.

## Session Handoff

### Completed in remediation

- F: EvaluateObjectives implemented with ChargeFraction/FuelFraction; AdvanceSimulation order corrected; typed PhaseParameter; LoadMission previous-state broadcast; cached weak targets preserved; production Succeeded/Failed paths wired.
- G: Real `DA_SolarEventEmergency` created (MissionId `SolarCrisis`); factory helper removed; verifier loads actual `.uasset`; Solar Event integration tests green.
- H automated: Start/Restart/Abort load the Data Asset (soft path); ShowDebug EdenMission code remains read-only; asset verifiers pass.

### Remaining Work

- Hands-on PIE checklist for ShowDebug EdenMission / StartMission / RestartMission / AbortMission.
- Mark ExecPlan 0004 Complete only after PIE evidence.
- Create `v0.3.0-emergency-mission` only after Complete.

### Next Clean Action

Perform manual PIE closeout. Do not start unrelated milestone implementation as part of 0004.
