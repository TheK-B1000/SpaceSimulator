# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** — Checkpoint K **READY FOR ACCEPTANCE**; L–M locked |
| Accepted Checkpoint I | `89d47da` + `89761fd` (+ docs `54b38dd`) |
| Accepted Checkpoint J | `22f8bb9` (+ docs `964c54c`) |
| Checkpoint K | Implemented; awaiting acceptance (see commit after `feat(eden): execute authorized validated commands`) |
| ExecPlan 0004–0006 | Complete |
| Last successful validation | K closeout: Win64 Dev Editor PASS; ExternalCommand unit 32/32; Integration ExternalCommand 25/25; `Eden.Unit.EdenOs.` 94/94; `Eden.Integration.EdenOs.` 31/31; full `Eden.` **322/322** exit 0; Validate-Project PASS |
| Next task | Accept Checkpoint K after review. **Do not begin L** until authorized. |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md` and ExecPlan 0007 (§20 J accepted; §21 K READY FOR ACCEPTANCE).

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator/AAR widgets and Input Actions (Git LFS).
- Do not reopen 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.
- AAR remains console-driven (`ShowAfterAction`); no auto-popup.
- 0007 Unreal lane is one checkpoint at a time.
- Checkpoints I and J are accepted.
- Checkpoint K is READY FOR ACCEPTANCE — do not begin L.
- Validated ≠ executed until an explicit authorized `ExecuteValidatedExternalCommand` call.
- 0007 proceeds directly on `main`.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- Advisory HTTP is gated to mission `Running`; late advisories may 409 vs SessionComplete.
- `GetEventHistory()` returns by value — keep the `TArray` alive while holding pointers into it.
- `bExternalCommandValidationEnabled` and `bExternalCommandExecutionEnabled` default false; neither auto-enables from AuthorizedControl alone.
- Optional non-blocking J coverage follow-ups: assert ValidationHistory field contents; include Duplicate in multi-gate precedence peel.

## Session Handoff

### Completed

- Checkpoint I accepted (advisory return path).
- Checkpoint J accepted (`22f8bb9`): typed validation airlock with zero execution.
- Checkpoint K implemented: authorized execution through `UEdenExternalCommandExecutor` → `UEdenOperatorControlComponent`.

### Next

- Accept K after maintainer review.
- **L** — cross-project control transport (locked until authorized).
- M remains locked.
