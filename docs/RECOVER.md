# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** - Checkpoint G implemented and ready for acceptance; Checkpoints H-M locked |
| ExecPlan 0004 | Complete |
| ExecPlan 0005 | Complete |
| ExecPlan 0006 | **Complete** — JSON export + ShowAfterAction (2B) |
| Last successful validation | Checkpoint G validation: build PASS with explicit `EdenOsTransportTests.cpp` compile; focused `Eden.Integration.EdenOs.` PASS with 4 tests including Observe invariant; focused `Eden.Unit.EdenOs.` PASS with 38 tests; live Observe E2E PASS through real ProjectEden FastAPI/auth/service/repository/SQLite; full `Automation RunTests Eden.` PASS with 239 tests; repository validation PASS; editor verifiers PASS; final Win64 Development Editor build PASS; `git diff --check`, Source `LogTemp`, token, and added-diff scope scans PASS |
| Next task | Review/accept Checkpoint G. Do not begin Checkpoints H-M until G is committed and explicitly accepted |

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
- 0007 Unreal lane is one checkpoint at a time: A, B, C, E. Do not implement ProjectEden Checkpoint D in this repository.
- Do not begin Checkpoints H-M until Checkpoint G is committed and explicitly accepted.
- 0007 proceeds directly on `main`; use tests plus source audit as the checkpoint gate, not branch topology.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- ProjectEden Checkpoint D completed at `B:\repo\ProjectEden` commit `6442029`. Unreal corrective C was accepted at `3f69f9b`; Checkpoint F now verifies both the deterministic route replay and the real Unreal `FHttpModule` -> ProjectEden FastAPI -> auth/service/repository -> isolated SQLite path.

## Session Handoff

### Completed

- 0005 PIE + merge to `main`.
- 0006 minimal export/AAR surface.
- 0007 Checkpoint A initial sink seam was rejected at `8209fbc`; remediation was accepted and pushed as `7a42fcf`.
- 0007 Checkpoint B was accepted and pushed as `a63de4e`.
- 0007 Checkpoint C was accepted and committed as `f66cda3`.
- 0007 Checkpoint E was committed as `32c8f9a`, then accepted at `75fcd90` after the mission-level failure-isolation proof.
- 0007 Checkpoint F was initially stopped before implementation after ProjectEden D (`6442029`) revealed create/telemetry/event/complete DTO mismatches against Unreal Checkpoint C.
- The narrow Checkpoint C corrective wire-contract patch was accepted at `3f69f9b`.
- 0007 Checkpoint F session lifecycle convergence plus live cross-process E2E proof are accepted at `63768ab` and `5249a6a`.
- 0007 Checkpoint G Observe mode is implemented and ready for acceptance. Checkpoints H-M remain locked.

### Latest Checkpoint G Evidence

- Added `Eden.Integration.EdenOs.ObserveModePreservesAuthoritativeMissionResult` in `Source/EdenSpaceSimulator/Private/Tests/EdenOsTransportTests.cpp`.
- Expanded the mission isolation probe to compare flight and operator telemetry snapshots along with mission outcome, mission phase, mission/objective runtime states, fuel, battery, temperature, telemetry history counts, and clock timing.
- Observe test verifies EDEN Observe registers the EDEN sink, sends lifecycle telemetry through create/telemetry/events/complete routes, reports `EEdenOsAuthorityMode::Observe`, reaches connected state, and does not call advisory or command routes.
- `scripts/Run-EdenOsLiveE2E.ps1` now accepts `-AuthorityMode Advisory|Observe` and passes `EDEN_OS_LIVE_E2E_AUTHORITY_MODE` only to the live Unreal process. Evidence now records `authorityMode`; request bodies and bearer tokens remain absent from evidence.
- Build passed via `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild`; UBT explicitly compiled `EdenOsTransportTests.cpp` and returned `Result: Succeeded`.
- Focused `Automation RunTests Eden.Integration.EdenOs.` passed with 4 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****` in `Saved/Logs/Automation-0007G-IntegrationEdenOs.log`.
- Focused `Automation RunTests Eden.Unit.EdenOs.` passed with 38 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****` in `Saved/Logs/Automation-0007G-UnitEdenOs.log`.
- Live Observe E2E passed via `.\scripts\Run-EdenOsLiveE2E.ps1 -ProjectEdenRoot 'B:\repo\ProjectEden' -EngineRoot 'K:\Program Files\Epic Games\UE_5.8' -Port 8792 -AuthorityMode Observe`.
- Live Unreal evidence `Saved/Automation/EdenOsLiveE2E/20260808-113526/UnrealLiveE2E.json` recorded `authorityMode = Observe`, session `595fd951-442b-9acf-07e3-7c9e43800f4b`, scenario `SolarEventEmergency`, mission state `Succeeded`, adapter state `Connected`, zero pending/dropped messages, create 201, telemetry 202, seven event 202 responses, and complete 200.
- Live ProjectEden evidence `Saved/Automation/EdenOsLiveE2E/20260808-113526/ProjectEdenDbEvidence.json` verified one `mission_environment` run, external session id match, `seed = null`, `ended_at` populated, DB status `completed`, one telemetry payload/state, seven events, `alertsCount = 1`, `ticks = null`, and `highestRiskSystem = null`.
- Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`.
- Full `Automation RunTests Eden.` passed with 239 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****`; the external live test skipped in the full suite because live env vars were unset.
- Editor verification passed: `VerifyFlightAssets.py`, `VerifyMissionAssets.py`, `VerifyOperatorAssets.py`, `VerifyResourceAssets.py`, and `VerifyFlightRuntimeSmoke.py`.
- Final Win64 Development Editor build passed and reported target up to date.
- `git diff --check` passed with the existing LF/CRLF warning on `scripts/Run-EdenOsLiveE2E.ps1`.
- Source `LogTemp` search returned no matches. Token scan found only existing runtime-header/test-script references and synthetic `test-token` usage. Added-diff scope scan found only authority-mode setup and negative assertions that Observe does not call advisory or command routes.
- No ProjectEden code, advisory request/response contract, HUD advisory event, external command router, retry/backoff, WebSocket, Blueprint asset, or gameplay feature was added.

### Latest Checkpoint F Evidence

- `main` was pushed to `origin/main` after Checkpoint C corrective acceptance and before Checkpoint F began.
- Implemented `FEdenOsMissionLifecycleModel` as the production lifecycle request model. It derives create and completion DTOs from immutable `FEdenTelemetrySessionPayload` values, maps terminal mission facts to `succeeded`, `failed`, or `aborted`, and omits completion when terminal facts are unknown.
- `FEdenOsTelemetrySink` now sends the ProjectEden lifecycle route sequence through the existing adapter queue: create once per session, telemetry on new sequence, one request per new event sequence, and complete once after a terminal fact.
- `DefaultScenarioId` is now explicit EDEN OS connection configuration used by session create. Runtime adapter state remains owned by `UEdenOsAdapterSubsystem`; canonical 0006 telemetry remains the telemetry truth.
- Added `Source/EdenSpaceSimulator/Public/EdenOs/EdenOsMissionLifecycle.h`, `Source/EdenSpaceSimulator/Private/EdenOs/EdenOsMissionLifecycle.cpp`, `Source/EdenSpaceSimulator/Private/Tests/EdenOsLifecycleTests.cpp`, and `scripts/Verify-EdenOsMissionContract.ps1`.
- Updated the previous E failure-isolation test so it fails every lifecycle HTTP attempt and asserts observable multi-request failure without changing authoritative mission/resource/clock state.
- Initial F build passed via `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild`; UBT invalidated the makefile for `source file added` and explicitly compiled `EdenOsMissionLifecycle.cpp`, `EdenOsTelemetrySink.cpp`, `EdenOsConnectionConfigTests.cpp`, `EdenOsLifecycleTests.cpp`, and `EdenOsTransportTests.cpp`.
- Corrective rebuild after the E isolation assertion update explicitly compiled `EdenOsTransportTests.cpp` and passed.
- Focused automation passed via `UnrealEditor-Cmd.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.EdenOs.; Quit" "-TestExit=Automation Test Queue Empty" -Log`.
- Focused integration passed via `Automation RunTests Eden.Integration.EdenOs.` with 3 tests found: `FailingTransportDoesNotChangeSimulation`, `FailingTransportPreservesAuthoritativeMissionResult`, and `SessionLifecycleEmitsProjectEdenRouteSequence`.
- `scripts/Verify-EdenOsMissionContract.ps1` passed against `B:\repo\ProjectEden`: replayed 7 Unreal-produced lifecycle requests through ProjectEden FastAPI routes and verified persisted session, telemetry, events, and completion in isolated SQLite.
- Full `Automation RunTests Eden.` passed with 237 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****`.
- Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`.
- Editor verification passed: `VerifyFlightAssets.py`, `VerifyMissionAssets.py`, `VerifyOperatorAssets.py`, `VerifyResourceAssets.py`, and `VerifyFlightRuntimeSmoke.py`.
- Final Win64 Development Editor build passed and reported target up to date.
- `git diff --check` passed. Source `LogTemp` search returned no matches. Secret scan matches were limited to synthetic test credentials and docs evidence. Scope scan matches were limited to the intended async Unreal HTTP transport from Checkpoint E, existing advisory configuration, route-version tests, and pre-existing mission "no retry" log text.
- Corrective live E2E proof added sanitized adapter delivery records and `Eden.External.EdenOs.LiveProjectEdenMissionLifecycle`. The test is opt-in and skips when `EDEN_OS_LIVE_E2E_BASE_URL` or `EDEN_OS_LIVE_E2E_BEARER_JWT` is absent.
- `scripts/Run-EdenOsLiveE2E.ps1 -ProjectEdenRoot 'B:\repo\ProjectEden' -EngineRoot 'K:\Program Files\Epic Games\UE_5.8' -Port 8791` passed. It applied ProjectEden Alembic head through `a6b7c8d9e0f1 add_mission_environment_sessions`, started a real `uvicorn eden_api.main:app` listener at `http://127.0.0.1:8791`, registered/logged in through ProjectEden auth, injected the bearer JWT into Unreal only through process environment, ran the live Unreal automation test, and queried the isolated SQLite DB.
- Live Unreal evidence: `Saved/Automation/EdenOsLiveE2E/20260808-110140/UnrealLiveE2E.json` recorded `FEdenOsUnrealHttpTransport`, session `dfc2dd9f-42ea-4082-f2ad-8f89818e2532`, scenario `SolarEventEmergency`, mission state `Succeeded`, adapter state `Connected`, zero pending/dropped messages, create 201, telemetry 202, seven event 202 responses, and complete 200. Response bodies contained the public ProjectEden session id and final `succeeded` status; no request body or bearer token was recorded.
- Live ProjectEden evidence: uvicorn access logs showed real `/api/missions/sessions`, `/telemetry`, seven `/events`, and `/complete` requests. `ProjectEdenDbEvidence.json` verified exactly one `mission_environment` run for the Unreal external session id, `seed` null, `ended_at` populated after completion, DB status `completed`, one telemetry payload/state, seven mission events, `alertsCount = 1`, `ticks = null`, and `highestRiskSystem = null`.
- Regression validation after live proof: focused `Automation RunTests Eden.Unit.EdenOs.` PASS; focused `Automation RunTests Eden.Integration.EdenOs.` PASS; existing replay verifier PASS; full `Automation RunTests Eden.` found 238 tests and passed, with the external live test skipped because live env vars were unset; repository validation PASS; final Win64 Development Editor build PASS; `git diff --check`, Source `LogTemp`, token, and G-scope scans PASS.
- F acceptance recorded on 2026-08-08. `main` was pushed to `origin/main` from `3f69f9b` through `5249a6a` before this acceptance documentation update.

### Latest Checkpoint C Corrective Evidence

- ProjectEden D source of truth inspected: `B:\repo\ProjectEden\packages\api\eden_api\schemas\missions.py` at commit `6442029 feat(missions): add external mission session ingestion`.
- Unreal create payload now emits only `schemaVersion`, `sessionId`, `scenarioId`, and `startedAt`.
- Unreal telemetry payload now emits ProjectEden's outer fields `schemaVersion`, `sessionId`, `sequence`, and `simulationTimeSeconds`, while preserving canonical 0006 telemetry under `telemetry`.
- Unreal event payload now emits `eventId`, `eventType`, `sequence`, `simulationTimeSeconds`, and `payload` directly, without the old nested `event` wrapper.
- Unreal complete payload now emits `finalStatus` as `succeeded`, `failed`, or `aborted`, `completedAt`, and optional `finalSequence`, `ticks`, `alertsCount`, and `highestRiskSystem` only when known.
- Canonical 0006 telemetry `missionId` remains unchanged. Future convergence must provide the scenario mapping explicitly; this corrective patch does not implement Checkpoint F.
- No lifecycle orchestration, HTTP transport, advisory, command, retry/backoff, FastAPI, DB, Unreal asset, or ProjectEden code was changed.
- Build passed via `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild`; UBT explicitly compiled `EdenOsWireSerializationModel.cpp` and `EdenOsWireSerializationTests.cpp`, relinked `UnrealEditor-EdenSpaceSimulator.dll`, and returned `Result: Succeeded`.
- Focused automation passed via `UnrealEditor-Cmd.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.EdenOs.Wire.; Quit" "-TestExit=Automation Test Queue Empty" -Log`; `Saved/Logs/EdenSpaceSimulator.log` reported 13 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****`.
- Full automation passed via `Automation RunTests Eden.` with 232 tests found and `**** TEST COMPLETE. EXIT CODE: 0 ****`.
- Repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`.
- `git diff --check` passed. Source `LogTemp` scan returned no matches. added-source credential and F-scope scans returned no matches.

### Latest Checkpoint E Evidence

- Implemented `FEdenOsTelemetrySink`, `IEdenOsHttpTransport`, `FEdenOsUnrealHttpTransport`, immutable queued request descriptors, URL/request models, and adapter-owned bounded queue/pump state.
- `FEdenOsTelemetrySink` uses the accepted `IEdenTelemetrySink` seam. `UEdenTelemetrySubsystem` was not special-cased and remains unaware of HTTP/FastAPI details.
- `UEdenOsAdapterSubsystem` owns runtime connection state, bearer-token presence, pending count, dropped count, sanitized last error summary, bounded outbound queue, one-in-flight FIFO pump, and callback lifetime.
- Queue depth uses `MaxQueueDepth` as the maximum outstanding count including the in-flight request. Overflow drops the newest message, increments `DroppedMessageCount`, and returns a failed sink result with an observable summary.
- Production HTTP uses Unreal `FHttpModule` / `IHttpRequest` asynchronously with `Content-Type: application/json` and `Accept: application/json`. HTTP success is 2xx only. Non-2xx, network failure, and request-start failure are failures.
- Runtime JWT is sent only as an `Authorization: Bearer <token>` header when present. It is not serialized into payload JSON, snapshots, logs, or error summaries.
- URL construction is centralized through `FEdenOsUrlModel` using validated `BaseUrl` plus Checkpoint C route constants. No localhost or hardcoded endpoint implementation was added outside constants/tests.
- Timeout mapping: `RequestTimeoutSeconds` maps to `IHttpRequest::SetTimeout`; `ConnectionTimeoutSeconds` remains validated config but is not separately enforceable by Unreal's request API in this checkpoint.
- Callback lifetime: callbacks capture `TWeakObjectPtr<UEdenOsAdapterSubsystem>` and are ignored after `Deinitialize()`. Deinitialize unregisters the EDEN sink, clears queue/in-flight state, and resets transport ownership.
- No per-frame Tick or fixed-step pump was added. Enqueue starts the pump when idle; completion starts the next queued request.
- UBT source-discovery evidence: `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild` explicitly compiled the new E source/test files and returned `Result: Succeeded`.
- `UnrealEditor.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.EdenOs.Transport.; Quit" "-TestExit=Automation Test Queue Empty" -Log` passed with 10 tests found and exit code 0 in `Saved/Logs/EdenSpaceSimulator.log`.
- `UnrealEditor.exe ... "-ExecCmds=Automation RunTests Eden.Integration.EdenOs.; Quit" ...` passed with 1 test found and exit code 0.
- `UnrealEditor.exe ... "-ExecCmds=Automation RunTests Eden.Integration.Telemetry.; Quit" ...` passed with 1 test found and exit code 0.
- `UnrealEditor.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.; Quit" "-TestExit=Automation Test Queue Empty" -Log` passed with 229 tests found and exit code 0 in `Saved/Logs/EdenSpaceSimulator.log`.
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1` passed.
- Final `Build.bat EdenSpaceSimulatorEditor Win64 Development ... -NoMutex -FromMsBuild` passed and reported target up to date.
- `git diff --check` passed with a line-ending warning for `EdenSpaceSimulator.Build.cs` only.
- `Get-ChildItem -Path Source -Recurse -Include *.h,*.cpp | Select-String -Pattern "LogTemp"` returned no matches.
- Scope scans found no advisory, TriggerReasons, external command router, retry/backoff, WebSocket, socket, localhost, `127.0.0.1`, or implementation `/api/v1/` usage; matches were limited to pre-existing mission "no retry" log text and route-constant tests.
- Blocking/synchronous HTTP scan found no wait/sleep pattern around HTTP; matches were unrelated mission `Inf` validation test names.
- Secret scan found no realistic committed JWT value; matches were limited to synthetic `test-token` literals in EdenOs tests and existing docs evidence.

### Latest Checkpoint E Corrective Evidence

- `Eden.Integration.EdenOs.FailingTransportPreservesAuthoritativeMissionResult` was added after review found `FailingTransportDoesNotChangeSimulation` was resource-only and too weak for Checkpoint E.
- The corrective test runs the same deterministic world-backed mission twice: EDEN disabled, then EDEN enabled with `FEdenOsTelemetrySink` registered and fake HTTP transport returning `offline` failure for outbound delivery.
- Compared authoritative fields: mission state, mission phase, mission elapsed time, mission event runtime states, mission objective runtime states, fuel kilograms/fraction/state, battery kWh/fraction/state, thermal temperature/state, telemetry event/snapshot counts, simulation-clock elapsed time, and dropped clock steps.
- Excluded fields are adapter bookkeeping only: EDEN connection state, pending/dropped counts, last transport error, and fake transport attempt records.
- The failing run proves EDEN was actually active: one EDEN sink registered, one telemetry delivery attempted, one fake HTTP request sent, adapter connection state `Disconnected`, and `LastErrorSummary` contains `offline`.
- UBT initially trusted stale test intermediates after the test edit. Only `EdenOsTransportTests.cpp` intermediate object/response/dependency files under `Intermediate/Build/.../EdenSpaceSimulator/` were removed. The next build invalidated the makefile for `EdenOsTransportTests.cpp.obj.rsp deleted`, explicitly compiled `EdenOsTransportTests.cpp`, relinked `UnrealEditor-EdenSpaceSimulator.dll`, and returned `Result: Succeeded`.
- `UnrealEditor.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Integration.EdenOs.; Quit" "-TestExit=Automation Test Queue Empty" -Log` passed with 2 tests found, including `FailingTransportPreservesAuthoritativeMissionResult`, and exit code 0 in `Saved/Logs/EdenSpaceSimulator.log`.
- `UnrealEditor.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.Unit.EdenOs.Transport.; Quit" "-TestExit=Automation Test Queue Empty" -Log` passed with 10 tests found and exit code 0.
- `UnrealEditor.exe ... -DDC-ForceMemoryCache "-ExecCmds=Automation RunTests Eden.; Quit" "-TestExit=Automation Test Queue Empty" -Log` passed with 230 tests found and exit code 0.
- Final repository validation passed via `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1`.
- Final Win64 Development Editor build passed via `Build.bat EdenSpaceSimulatorEditor Win64 Development "-Project=K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" -NoMutex -FromMsBuild` and reported target up to date after the corrective compile.
- `git diff --check` passed.
- Source `LogTemp` scan returned no matches. Secret scan returned no matches. Scope scan found no advisory, external command, retry/backoff implementation, hardcoded localhost, or blocking HTTP wait; matches were limited to the existing authority-mode contract/tests, pre-existing mission "no retry" log text, the route-version negative assertion, and intended async Unreal HTTP calls.

### Next Clean Action

Begin Checkpoint G Observe mode. Do not begin Checkpoints H-M until G is implemented, validated, committed, and explicitly accepted.
