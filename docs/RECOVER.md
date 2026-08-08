# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** — Checkpoint L **accepted**; M locked |
| Accepted Checkpoint I | `89d47da` + `89761fd` (+ docs `54b38dd`) |
| Accepted Checkpoint J | `22f8bb9` (+ docs `964c54c`) |
| Accepted Checkpoint K | `a50bcf1` (+ docs hash `48edc80`) |
| Accepted Checkpoint L | Unreal `87a5a97` (+ ready-docs `22102ab`); ProjectEden `d2822b2` |
| ExecPlan 0004–0006 | Complete |
| Last successful validation | L acceptance audit: live AuthorizedControl E2E `20260808-155507` (Normal→Shed; Executed=1; `/events` → PE persistence); prior Win64 Dev Editor + `Validate-Project Eden.` PASS; PE API 361 passed; alembic `c8d9e0f1a2b3` |
| Next task | **Do not begin M** until separately authorized. |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md` and ExecPlan 0007 (§20–§22 J/K/L accepted; M locked).

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator/AAR widgets and Input Actions (Git LFS).
- Do not reopen 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.
- AAR remains console-driven (`ShowAfterAction`); no auto-popup.
- 0007 Unreal lane is one checkpoint at a time.
- Checkpoints I, J, K, and L are accepted.
- **M remains locked.**
- Validated ≠ executed until an explicit authorized `ExecuteValidatedExternalCommand` call; L may dry-run validate without deferred execute.
- 0007 proceeds directly on `main`.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- Advisory HTTP is gated to mission `Running`; late advisories may 409 vs SessionComplete.
- `GetEventHistory()` returns by value — keep the `TArray` alive while holding pointers into it.
- `bExternalCommandValidationEnabled`, `bExternalCommandExecutionEnabled`, and `bExternalCommandAutomationEnabled` default false; none auto-enable from AuthorizedControl alone.
- AuthorizedControl permits advisory evaluation (required for L accept→proposal chain); Observe still never evaluates.
- Live L E2E uses deterministic ProjectEden reasoner via `EDEN_COMMAND_PROPOSAL_REASONER=test-load-shed` — not a production AI policy.
- Optional non-blocking coverage polish (J/K/L soft notes) does not block accepted checkpoints.

## Session Handoff

### Completed

- Checkpoint I accepted (advisory return path).
- Checkpoint J accepted (`22f8bb9`): typed validation airlock with zero execution.
- Checkpoint K accepted (`a50bcf1`): authorized execution through `UEdenExternalCommandExecutor` → `UEdenOperatorControlComponent`.
- Checkpoint L accepted (`87a5a97` / ProjectEden `d2822b2`): outbound `/command-proposals` → J → K; live Normal→Shed; Executed feedback via existing `/events`.

### Next

- **M remains locked** until separately authorized.
