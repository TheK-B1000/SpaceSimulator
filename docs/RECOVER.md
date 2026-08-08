# RECOVER

This file is the operational handoff for interrupted work and fresh Codex sessions. Update it at every meaningful checkpoint.

## Last verified checkpoint

| Field | Value |
|---|---|
| Date | 2026-08-08 |
| Branch | `main` |
| Milestone tag (main) | `v0.3.0-emergency-mission` |
| Active ExecPlan | **0007 EDEN OS adapter** - Checkpoint E accepted; Checkpoint F locked pending ProjectEden D and explicit authorization |
| ExecPlan 0004 | Complete |
| ExecPlan 0005 | Complete |
| ExecPlan 0006 | **Complete** — JSON export + ShowAfterAction (2B) |
| Last successful validation | Repository validation PASS; Win64 Development Editor build PASS with explicit corrective `EdenOsTransportTests.cpp` compile and final up-to-date build; `Automation RunTests Eden.Integration.EdenOs.` PASS with 2 tests; `Automation RunTests Eden.Unit.EdenOs.Transport.` PASS with 10 tests; `Automation RunTests Eden.` PASS with 230 tests; `git diff --check` PASS; Source `LogTemp`, secret, scope, hardcoded URL, and blocking HTTP scans clean or limited to expected contract/async transport matches |
| Next task | Wait for ProjectEden Checkpoint D completion and explicit Checkpoint F authorization. Do not begin F yet |

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
- Do not begin Checkpoint F until Checkpoint E is explicitly accepted and ProjectEden Checkpoint D is complete.
- 0007 proceeds directly on `main`; use tests plus source audit as the checkpoint gate, not branch topology.

## Current Known Risks

- Operator keys: `T` / `L` / `P`.
- Telemetry export path: `Saved/Telemetry/` (runtime output; not tracked).
- `ClearHistory()` remains explicit.
- ProjectEden Checkpoint D is separate and must be complete before Checkpoint F convergence.

## Session Handoff

### Completed

- 0005 PIE + merge to `main`.
- 0006 minimal export/AAR surface.
- 0007 Checkpoint A initial sink seam was rejected at `8209fbc`; remediation was accepted and pushed as `7a42fcf`.
- 0007 Checkpoint B was accepted and pushed as `a63de4e`.
- 0007 Checkpoint C was accepted and committed as `f66cda3`.
- 0007 Checkpoint E was committed as `32c8f9a`, then accepted at `75fcd90` after the mission-level failure-isolation proof.

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

Wait for ProjectEden Checkpoint D completion and explicit Checkpoint F authorization. Do not begin Checkpoint F until both repositories are ready against the locked contract.
