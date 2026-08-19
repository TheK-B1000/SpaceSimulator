# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** — Checkpoint M **AUTOMATION READY; HUMAN PIE PENDING** |
| Accepted Checkpoint I | `89d47da` + `89761fd` (+ docs `54b38dd`) |
| Accepted Checkpoint J | `22f8bb9` (+ docs `964c54c`) |
| Accepted Checkpoint K | `a50bcf1` (+ docs hash `48edc80`) |
| Accepted Checkpoint L | Unreal `87a5a97` (+ ready-docs `22102ab` / acceptance docs `5069988`); ProjectEden `d2822b2` |
| ExecPlan 0004–0006 | Complete |
| Last successful validation | M automation: Win64 Dev Editor PASS; `Eden.` **340/340** exit 0; Unit EdenOs 99/99; Integration EdenOs 44/44; PE pytest 361 passed; alembic `c8d9e0f1a2b3` lifecycle PASS; live Observe/Advisory/AuthorizedControl E2E PASS (`Saved/Automation/EdenOsLiveE2E/20260808-M-*`). **2026-08-18 compile recovery:** both workspace and `EdenSpaceSimulator 5.8` Editor Win64 Development builds succeeded on VS 2022 `14.44.35207` with `-MaxParallelActions=1 -NoHotReloadFromIDE`. |
| Next task | **Human PIE gate** (§23.8). Do not accept M / do not close 0007 until PIE PASS. Restart the Editor so it loads the rebuilt `UnrealEditor-EdenSpaceSimulator.dll`. |

## Recovery protocol

```powershell
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator status --short --untracked-files=all
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator branch --show-current
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator log -8 --oneline
git -c safe.directory=K:/UnrealProjects/SpaceSimulator/EdenSpaceSimulator diff --stat
```

Then read `AGENTS.md` and ExecPlan 0007 (§23 M; HUMAN PIE PENDING).

## Safe Restart Rules

- Do not discard local changes without inspecting them.
- Do not delete `Content`, `Config`, or `Source`.
- Preserve operator/AAR widgets and Input Actions (Git LFS).
- Do not reopen 0004 Checkpoint F.
- Do not rewrite `v0.3.0-emergency-mission` history.
- AAR remains console-driven (`ShowAfterAction`); no auto-popup.
- 0007 Unreal lane is one checkpoint at a time.
- Checkpoints I, J, K, and L are accepted.
- Checkpoint M is verification/closeout only — **no new architecture** unless a real defect appears.
- Codex must **not** self-certify the human PIE gate.
- Do not mark ExecPlan 0007 complete until M is accepted after human PIE PASS.
- 0007 proceeds directly on `main`.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- Advisory HTTP is gated to mission `Running`; late advisories may 409 vs SessionComplete.
- `GetEventHistory()` returns by value — keep the `TArray` alive while holding pointers into it.
- `bExternalCommandValidationEnabled`, `bExternalCommandExecutionEnabled`, and `bExternalCommandAutomationEnabled` default false; none auto-enable from AuthorizedControl alone.
- AuthorizedControl permits advisory evaluation (required for L accept→proposal chain); Observe still never evaluates.
- Live AuthorizedControl E2E / PIE AC path uses deterministic ProjectEden reasoner via `EDEN_COMMAND_PROPOSAL_REASONER=test-load-shed` — not a production AI policy.
- Temporary PIE Game-config overrides for AC gates must be reverted and must not be committed.
- Uncommitted compile pin (needed on this machine): `Source/*.Target.cs` + `Config/DefaultEngine.ini` force VS 2022 `14.44.35207`. Do not drop that pin; VS 2026 `14.50` ICE/C1001s.
- Editor-open compiles on this host OOM if UBT runs several unity files at once. Keep local `Saved/UnrealBuildTool/BuildConfiguration.xml` at `MaxParallelActions=1`. `-NoUBA` only disables UBA detours in UE 5.8; the local UBA executor still runs.
- This machine has two project copies: Cursor workspace `EdenSpaceSimulator` and the Editor's `EdenSpaceSimulator 5.8` (space in path). Keep Target.cs / DefaultEngine.ini in sync if both are used.

## Session Handoff

### Completed

- Checkpoint I–L accepted.
- Checkpoint M **automation** complete: mode matrix + live FastAPI + full Unreal/PE regression + source/security audit. Evidence: `Saved/Automation/EdenOsLiveE2E/20260808-M-Closeout/`.
- 2026-08-18: Editor compile failure was MSVC 14.50 ICE + UBA OOM, not a C++ defect. Pinned 14.44 and rebuilt both project copies successfully.

### Next (human)

- Perform §23.8 PIE checklist; report PASS/FAIL.
- Only after PASS: record M READY FOR FINAL ACCEPTANCE / closeout docs commit.
