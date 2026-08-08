# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** — Checkpoint J **accepted**; Checkpoint K authorized pending contract lock; L–M locked |
| Accepted Checkpoint I | `89d47da` + `89761fd` (+ docs `54b38dd`) |
| Accepted Checkpoint J | `22f8bb9` |
| ExecPlan 0004–0006 | Complete |
| Last successful validation | J closeout: Win64 Dev Editor PASS; ExternalCommand unit 24/24; `Eden.Unit.EdenOs.` 86/86; `Eden.Integration.EdenOs.` 11/11; full `Eden.` **294/294** exit 0; Validate-Project PASS |
| Next task | Lock Checkpoint K contract (execution bridge), then implement. **Do not begin K code until the K brief is locked** |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md` and ExecPlan 0007 (§20 J accepted; await §21 K contract).

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator/AAR widgets and Input Actions (Git LFS).
- Do not reopen 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.
- AAR remains console-driven (`ShowAfterAction`); no auto-popup.
- 0007 Unreal lane is one checkpoint at a time.
- Checkpoints I and J are accepted.
- **Do not begin Checkpoint K implementation** until the maintainer locks the K execution contract and sends the brief.
- Validated ≠ executed until K builds an explicit authorized bridge.
- 0007 proceeds directly on `main`.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- Advisory HTTP is gated to mission `Running`; late advisories may 409 vs SessionComplete.
- `GetEventHistory()` returns by value — keep the `TArray` alive while holding pointers into it.
- `bExternalCommandValidationEnabled` defaults false; AuthorizedControl alone does not validate or execute.
- Optional non-blocking J coverage follow-ups: assert ValidationHistory field contents; include Duplicate in multi-gate precedence peel.

## Session Handoff

### Completed

- Checkpoint I accepted (advisory return path).
- Checkpoint J accepted (`22f8bb9`): typed validation airlock with zero execution.

### Next (authorized, contract-first)

- **K** — first controlled bridge from validated command → authoritative mutation via `UEdenOperatorControlComponent`.
- Must lock before code: executable allowlist, authorization, rate/cooldown, stale protection, exactly-once execution, operator-control convergence, execution telemetry, failure behavior, human-confirmation policy.
- L–M remain locked.
