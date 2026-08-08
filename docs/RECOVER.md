# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** — Checkpoint J **READY FOR ACCEPTANCE**; K–M locked |
| Accepted Checkpoint I | `89d47da` + `89761fd` (+ docs `54b38dd`) |
| Checkpoint J status | Implemented for review; not accepted |
| ExecPlan 0004–0006 | Complete |
| Last successful validation | J: Win64 Dev Editor PASS; ExternalCommand unit 24/24; `Eden.Unit.EdenOs.` 86/86; `Eden.Integration.EdenOs.` 11/11; full `Eden.` **294/294** exit 0; Validate-Project PASS |
| Next task | Review/accept Checkpoint J. **Do not begin Checkpoint K** until J is explicitly accepted |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md` and ExecPlan 0007 §20 (Checkpoint J contract).

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator/AAR widgets and Input Actions (Git LFS).
- Do not reopen 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.
- AAR remains console-driven (`ShowAfterAction`); no auto-popup.
- 0007 Unreal lane is one checkpoint at a time.
- Checkpoint I is accepted. Checkpoint J is ready for acceptance.
- **Do not begin Checkpoint K** (execution) until J is accepted.
- Validated ≠ executed. J must never call operator/resource/mission/flight mutators.
- 0007 proceeds directly on `main`.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- Advisory HTTP is gated to mission `Running`; late advisories may 409 vs SessionComplete.
- `GetEventHistory()` returns by value — keep the `TArray` alive while holding pointers into it.
- `bExternalCommandValidationEnabled` defaults false; AuthorizedControl alone does not validate.

## Session Handoff

### Completed

- Checkpoint I accepted (advisory return path).
- Checkpoint J implemented: typed validation airlock with zero execution.

### Checkpoint J evidence (summary)

- Internal proposal contract only (no ProjectEden command HTTP).
- Allowlist: three 0005 operator-mode intents; exact existing enums.
- Enable flag default false; Valid only under AuthorizedControl + enable.
- EvaluationId always required; session/eval correlation; ProposalId consume-on-Valid.
- Rate limiting deferred to K.
- Source inspection: no execution path from Valid → simulation mutation.

### Next (locked)

- **K** — authorized execution of validated commands (still locked).
