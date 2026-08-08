# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** - Checkpoint I implemented and ready for acceptance; Checkpoints J-M locked |
| ExecPlan 0004 | Complete |
| ExecPlan 0005 | Complete |
| ExecPlan 0006 | **Complete** — JSON export + ShowAfterAction (2B) |
| Last successful validation | Checkpoint I: Win64 Development Editor build PASS; `Eden.Unit.EdenOs.` PASS; `Eden.Integration.EdenOs.` PASS (6 tests including advisory HUD/telemetry issue); full `Automation RunTests Eden.` PASS exit 0; live Advisory E2E PASS via `Run-EdenOsLiveE2E.ps1 -AuthorityMode Advisory` (evidence `Saved/Automation/EdenOsLiveE2E/20260808-135543/`) with successful `/advisories` 201 and persisted `MissionAdvisory` |
| Next task | Review/accept Checkpoint I. Do not begin Checkpoints J-M until I is explicitly accepted |

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
- 0007 Unreal lane is one checkpoint at a time.
- Do not begin Checkpoints J-M until Checkpoint I is committed and explicitly accepted.
- 0007 proceeds directly on `main`; use tests plus source audit as the checkpoint gate, not branch topology.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- Advisory HTTP is gated to mission `Running` and flushes lifecycle create before `/advisories` so ProjectEden session existence is guaranteed.
- Late in-flight advisories that lose the race to `SessionComplete` may 409; adapter keeps Connected for that case and cancels queued advisories on successful complete.

## Session Handoff

### Completed

- 0007 Checkpoint I (advisory wire + response + `EdenAdvisoryIssued` + read-only HUD) implemented for review.
- ProjectEden H.1 advisory route is live (`POST .../advisories`, plural) and was exercised by Unreal live E2E.

### Checkpoint I evidence (summary)

- §5.9 amended to H.1 facts + three timestamps.
- Wire: five locked trigger strings, advisory request/response DTOs, Json module for response parse.
- Adapter dispatches advisories asynchronously with evaluationId correlation and stale-callback HUD safety.
- `RecordObservationEvent` allows only `EdenAdvisoryIssued` into telemetry from the adapter.
- HUD shows recommendation/rationale/id/issued time only when adapter holds a validated advisory.
- Live Advisory E2E: create 201, advisories 201, telemetry/events/complete succeeded; DB persisted MissionAdvisory.
