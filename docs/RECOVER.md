# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** — Checkpoint I **accepted**; Checkpoint J authorized next; K–M locked |
| Accepted Checkpoint I | `89d47da` + `89761fd` |
| ExecPlan 0004 | Complete |
| ExecPlan 0005 | Complete |
| ExecPlan 0006 | **Complete** — JSON export + ShowAfterAction (2B) |
| Last successful validation | Checkpoint I closeout: Win64 Development Editor build PASS; `Eden.Integration.EdenOs.` 6/6; full `Automation RunTests Eden.` **265/265**; live Advisory E2E PASS (`Saved/Automation/EdenOsLiveE2E/20260808-140801/`) with return-path evidence in `UnrealLiveE2E.json` |
| Next task | Await Checkpoint J brief, then implement the authorized-command boundary (built and tested, disabled by default). Do **not** begin Checkpoint K |

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
- Checkpoint I is accepted. Do not begin Checkpoint K until J is accepted.
- Do not begin Checkpoint J implementation until the maintainer sends the J brief.
- 0007 proceeds directly on `main`; use tests plus source audit as the checkpoint gate, not branch topology.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- Advisory HTTP is gated to mission `Running` and flushes lifecycle create before `/advisories` so ProjectEden session existence is guaranteed.
- Late in-flight advisories that lose the race to `SessionComplete` may 409; adapter keeps Connected for that case and cancels queued advisories on successful complete.
- `GetEventHistory()` returns by value — live/automation code that retains pointers into the returned array must keep the `TArray` alive for the duration of use (fixed in `89761fd`).

## Session Handoff

### Completed

- 0007 Checkpoint I accepted: advisory wire + response + exactly-once `EdenAdvisoryIssued` + read-only HUD, with live return-path proof.
- ProjectEden H.1 advisory route is live (`POST .../advisories`, plural) and exercised by Unreal live E2E.

### Checkpoint I accepted evidence (summary)

- Commits: `89d47da` (implementation), `89761fd` (live return-path proof).
- Live evidence dir: `Saved/Automation/EdenOsLiveE2E/20260808-140801/`.
- `advisoryIssuedCount == 1`; presentation matches ProjectEden response; clocks `0.7 / 0.4 / 0.4`.
- Full Eden suite 265/265; no J/K leakage in the I evidence patch.

### Next (authorized, not started)

- **J** — validated command boundary (`UEdenExternalCommandRouter` / allowlist / rate limits): built and tested, **disabled by default**. EDEN still must not touch the ship.
- **K** — locked until J is accepted (Observe / Advisory / AuthorizedControl execution gating).
