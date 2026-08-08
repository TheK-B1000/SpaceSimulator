# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** — Checkpoint K **accepted**; Checkpoint L authorized next; M locked |
| Accepted Checkpoint I | `89d47da` + `89761fd` (+ docs `54b38dd`) |
| Accepted Checkpoint J | `22f8bb9` (+ docs `964c54c`) |
| Accepted Checkpoint K | `a50bcf1` (+ docs hash `48edc80`) |
| ExecPlan 0004–0006 | Complete |
| Last successful validation | K closeout: Win64 Dev Editor PASS; ExternalCommand unit 32/32; Integration ExternalCommand 25/25; `Eden.Unit.EdenOs.` 94/94; `Eden.Integration.EdenOs.` 31/31; full `Eden.` **322/322** exit 0; Validate-Project PASS |
| Next task | Lock Checkpoint L contract (cross-project control transport into J/K), then implement. **Do not begin L code until the L brief is locked** |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md` and ExecPlan 0007 (§20–§21 J/K accepted; await L contract).

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator/AAR widgets and Input Actions (Git LFS).
- Do not reopen 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.
- AAR remains console-driven (`ShowAfterAction`); no auto-popup.
- 0007 Unreal lane is one checkpoint at a time.
- Checkpoints I, J, and K are accepted.
- **Do not begin Checkpoint L implementation** until the maintainer locks the L transport contract and sends the brief.
- Validated ≠ executed until an explicit authorized `ExecuteValidatedExternalCommand` call; external command wire remains L.
- 0007 proceeds directly on `main`.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- Advisory HTTP is gated to mission `Running`; late advisories may 409 vs SessionComplete.
- `GetEventHistory()` returns by value — keep the `TArray` alive while holding pointers into it.
- `bExternalCommandValidationEnabled` and `bExternalCommandExecutionEnabled` default false; neither auto-enables from AuthorizedControl alone.
- Optional non-blocking J/K coverage follow-ups: ValidationHistory field asserts; Duplicate in multi-gate peel; K multi-gate precedence peel; dedicated flight negative test name; numeric Executed `SimulationTimeSeconds` assert.

## Session Handoff

### Completed

- Checkpoint I accepted (advisory return path).
- Checkpoint J accepted (`22f8bb9`): typed validation airlock with zero execution.
- Checkpoint K accepted (`a50bcf1`): authorized execution through `UEdenExternalCommandExecutor` → `UEdenOperatorControlComponent`.

### Next (authorized, contract-first)

- **L** — cross-project control transport: carefully guarded path for ProjectEden to hand a typed proposal across the network into the existing J/K machinery.
- Must lock before code: wire schema, auth, correlation to session/evaluation, rate/safety at the transport edge, and proof that L cannot bypass J validation or K execution gates.
- M remains locked.
