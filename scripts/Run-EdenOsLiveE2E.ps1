param(
    [string]$ProjectEdenRoot = "B:\repo\ProjectEden",
    [string]$EngineRoot = $env:UE_ENGINE_ROOT,
    [string]$PythonExe = "",
    [int]$Port = 8791,
    [string]$RunId = "",
    [ValidateSet("Advisory", "Observe")]
    [string]$AuthorityMode = "Advisory"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$UProject = Join-Path $RepoRoot "EdenSpaceSimulator.uproject"
if (!(Test-Path $UProject)) {
    throw "Unreal project not found: $UProject"
}

if ([string]::IsNullOrWhiteSpace($RunId)) {
    $RunId = Get-Date -Format "yyyyMMdd-HHmmss"
}

$RunRoot = Join-Path $RepoRoot ("Saved\Automation\EdenOsLiveE2E\{0}" -f $RunId)
$ProjectEdenDataDir = Join-Path $RunRoot "ProjectEdenData"
$ProjectEdenLogsDir = Join-Path $RunRoot "ProjectEdenLogs"
New-Item -ItemType Directory -Force -Path $RunRoot, $ProjectEdenDataDir, $ProjectEdenLogsDir | Out-Null

if (!(Test-Path $ProjectEdenRoot)) {
    throw "ProjectEden root not found: $ProjectEdenRoot"
}

$ProjectEdenApiRoot = Join-Path $ProjectEdenRoot "packages\api"
if (!(Test-Path $ProjectEdenApiRoot)) {
    throw "ProjectEden API package not found: $ProjectEdenApiRoot"
}

if ([string]::IsNullOrWhiteSpace($PythonExe)) {
    $VenvPython = Join-Path $ProjectEdenApiRoot ".venv\Scripts\python.exe"
    if (Test-Path $VenvPython) {
        $PythonExe = $VenvPython
    } else {
        $PythonExe = "python"
    }
}

if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    throw "EngineRoot was not provided and UE_ENGINE_ROOT is not set."
}

$UnrealEditorCmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (!(Test-Path $UnrealEditorCmd)) {
    throw "UnrealEditor-Cmd.exe not found: $UnrealEditorCmd"
}

$DbPath = Join-Path $RunRoot "projecteden-live-e2e.sqlite"
$DbUrlPath = $DbPath.Replace("\", "/")
$BaseUrl = "http://127.0.0.1:$Port"
$ServerOut = Join-Path $RunRoot "projecteden-uvicorn.stdout.log"
$ServerErr = Join-Path $RunRoot "projecteden-uvicorn.stderr.log"
$UnrealLog = Join-Path $RunRoot "unreal-live-e2e.log"
$DbEvidencePath = Join-Path $RunRoot "ProjectEdenDbEvidence.json"
$DbVerifierPath = Join-Path $RunRoot "verify_projecteden_live_db.py"

$PreviousEnv = @{
    EDEN_DATABASE_URL = $env:EDEN_DATABASE_URL
    EDEN_DATA_DIR = $env:EDEN_DATA_DIR
    EDEN_LOGS_DIR = $env:EDEN_LOGS_DIR
    EDEN_JWT_SECRET = $env:EDEN_JWT_SECRET
    EDEN_ENVIRONMENT = $env:EDEN_ENVIRONMENT
    EDEN_ALLOWED_HOSTS = $env:EDEN_ALLOWED_HOSTS
    EDEN_OS_LIVE_E2E_BASE_URL = $env:EDEN_OS_LIVE_E2E_BASE_URL
    EDEN_OS_LIVE_E2E_BEARER_JWT = $env:EDEN_OS_LIVE_E2E_BEARER_JWT
    EDEN_OS_LIVE_E2E_EVIDENCE_DIR = $env:EDEN_OS_LIVE_E2E_EVIDENCE_DIR
    EDEN_OS_LIVE_E2E_AUTHORITY_MODE = $env:EDEN_OS_LIVE_E2E_AUTHORITY_MODE
}

$ServerProcess = $null
try {
    $env:EDEN_DATABASE_URL = "sqlite:///$DbUrlPath"
    $env:EDEN_DATA_DIR = $ProjectEdenDataDir
    $env:EDEN_LOGS_DIR = $ProjectEdenLogsDir
    $env:EDEN_JWT_SECRET = [System.Guid]::NewGuid().ToString("N")
    $env:EDEN_ENVIRONMENT = "development"
    $env:EDEN_ALLOWED_HOSTS = "127.0.0.1,localhost"

    Push-Location $ProjectEdenApiRoot
    try {
        & $PythonExe -m alembic -c alembic.ini upgrade head
        if ($LASTEXITCODE -ne 0) {
            throw "ProjectEden Alembic migration failed with exit code $LASTEXITCODE"
        }

        $ServerProcess = Start-Process `
            -FilePath $PythonExe `
            -ArgumentList @("-m", "uvicorn", "eden_api.main:app", "--host", "127.0.0.1", "--port", "$Port", "--log-level", "info") `
            -WorkingDirectory $ProjectEdenApiRoot `
            -RedirectStandardOutput $ServerOut `
            -RedirectStandardError $ServerErr `
            -PassThru `
            -WindowStyle Hidden
    }
    finally {
        Pop-Location
    }

    $Deadline = (Get-Date).AddSeconds(30)
    do {
        try {
            Invoke-RestMethod -Method Get -Uri "$BaseUrl/health" -TimeoutSec 2 | Out-Null
            $Healthy = $true
        }
        catch {
            $Healthy = $false
            Start-Sleep -Milliseconds 500
        }
    } while (!$Healthy -and (Get-Date) -lt $Deadline)

    if (!$Healthy) {
        throw "ProjectEden server did not become healthy at $BaseUrl"
    }

    $Email = "unreal-live-e2e-$RunId@example.test"
    $Password = "LiveE2E-password-123!"
    $RegisterBody = @{ email = $Email; password = $Password } | ConvertTo-Json -Compress
    Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/auth/register" -ContentType "application/json" -Body $RegisterBody | Out-Null

    $LoginBody = @{ email = $Email; password = $Password } | ConvertTo-Json -Compress
    $Login = Invoke-RestMethod -Method Post -Uri "$BaseUrl/api/auth/login" -ContentType "application/json" -Body $LoginBody
    $RuntimeToken = $Login.access_token
    if ([string]::IsNullOrWhiteSpace($RuntimeToken)) {
        throw "ProjectEden login did not return an access token."
    }

    $env:EDEN_OS_LIVE_E2E_BASE_URL = $BaseUrl
    $env:EDEN_OS_LIVE_E2E_BEARER_JWT = $RuntimeToken
    $env:EDEN_OS_LIVE_E2E_EVIDENCE_DIR = $RunRoot
    $env:EDEN_OS_LIVE_E2E_AUTHORITY_MODE = $AuthorityMode

    & $UnrealEditorCmd `
        $UProject `
        -Unattended `
        -NullRHI `
        -NoSplash `
        -DDC-ForceMemoryCache `
        "-ExecCmds=Automation RunTests Eden.External.EdenOs.LiveProjectEdenMissionLifecycle; Quit" `
        "-TestExit=Automation Test Queue Empty" `
        -Log `
        -AbsLog="$UnrealLog"
    if ($LASTEXITCODE -ne 0) {
        throw "Unreal live E2E automation failed with exit code $LASTEXITCODE"
    }

    $UnrealEvidence = Join-Path $RunRoot "UnrealLiveE2E.json"
    if (!(Test-Path $UnrealEvidence)) {
        throw "Unreal live E2E evidence file was not written: $UnrealEvidence"
    }

    $DbVerifier = @'
import json
import sys
from pathlib import Path

from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from eden_api.database.models import (
    MissionEnvironmentEvent,
    MissionTelemetryPayload,
    SimulationRun,
    TelemetryState,
)


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    if len(sys.argv) != 4:
        fail("usage: verifier.py <db-path> <unreal-evidence-json> <output-json>")

    db_path = Path(sys.argv[1])
    unreal_evidence_path = Path(sys.argv[2])
    output_path = Path(sys.argv[3])
    evidence = json.loads(unreal_evidence_path.read_text(encoding="utf-8"))
    session_id = evidence["sessionId"]
    deliveries = evidence["deliveries"]
    expected_event_count = sum(1 for item in deliveries if item["messageType"] == "Event")

    engine = create_engine(f"sqlite:///{db_path.as_posix()}")
    Session = sessionmaker(bind=engine)
    db = Session()
    try:
        runs = (
            db.query(SimulationRun)
            .filter(
                SimulationRun.origin == "mission_environment",
                SimulationRun.external_session_id == session_id,
            )
            .all()
        )
        if len(runs) != 1:
            fail(f"expected exactly one mission_environment session for {session_id}, got {len(runs)}")
        run = runs[0]
        if run.seed is not None:
            fail(f"expected seed NULL for mission_environment session, got {run.seed}")
        if run.ended_at is None:
            fail("expected ended_at populated after completion")
        if run.run_status.name != "completed":
            fail(f"expected completed DB status for succeeded mission, got {run.run_status.name}")
        if run.scenario.name != "SolarEventEmergency":
            fail(f"expected SolarEventEmergency scenario, got {run.scenario.name}")
        if run.alerts_count is None:
            fail("expected alerts_count to be an actual known completion value")

        telemetry_payloads = db.query(MissionTelemetryPayload).filter_by(session_id=session_id).all()
        telemetry_states = (
            db.query(TelemetryState)
            .filter(TelemetryState.simulation_run_id == run.id)
            .all()
        )
        events = db.query(MissionEnvironmentEvent).filter_by(session_id=session_id).all()
        if len(telemetry_payloads) != 1:
            fail(f"expected one mission telemetry payload, got {len(telemetry_payloads)}")
        if len(telemetry_states) != 1:
            fail(f"expected one telemetry state row for run, got {len(telemetry_states)}")
        if telemetry_payloads[0].telemetry_state.simulation_run_id != run.id:
            fail("telemetry payload does not belong to the persisted run")
        if len(events) != expected_event_count:
            fail(f"expected {expected_event_count} mission events, got {len(events)}")
        if any(event.simulation_run_id != run.id for event in events):
            fail("at least one event does not belong to the persisted run")

        summary = {
            "sessionId": session_id,
            "origin": run.origin,
            "externalSessionIdMatches": run.external_session_id == session_id,
            "scenarioId": run.scenario.name,
            "seedIsNull": run.seed is None,
            "endedAtPopulated": run.ended_at is not None,
            "databaseRunStatus": run.run_status.name,
            "unrealMissionState": evidence["missionState"],
            "terminalResultMatches": evidence["missionState"] == "Succeeded" and run.run_status.name == "completed",
            "telemetryPayloadCount": len(telemetry_payloads),
            "telemetryStateCount": len(telemetry_states),
            "eventCount": len(events),
            "alertsCount": run.alerts_count,
            "ticks": run.ticks,
            "highestRiskSystem": run.highest_risk_system,
        }
        output_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    finally:
        db.close()
        engine.dispose()

    print(f"PASS: verified live ProjectEden DB persistence for session {session_id}")


if __name__ == "__main__":
    main()
'@

    Set-Content -Path $DbVerifierPath -Value $DbVerifier -Encoding UTF8
    Push-Location $ProjectEdenApiRoot
    try {
        & $PythonExe $DbVerifierPath $DbPath $UnrealEvidence $DbEvidencePath
        if ($LASTEXITCODE -ne 0) {
            throw "ProjectEden live DB verifier failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }

    Write-Host "PASS: live Unreal -> ProjectEden E2E verified."
    Write-Host "Evidence directory: $RunRoot"
    Write-Host "FastAPI base URL: $BaseUrl"
    Write-Host "Authority mode: $AuthorityMode"
    Write-Host "Runtime JWT: <runtime-token>"
    Write-Host "Database: $DbPath"
}
finally {
    if ($ServerProcess -and !$ServerProcess.HasExited) {
        Stop-Process -Id $ServerProcess.Id -Force
        $ServerProcess.WaitForExit(5000)
    }

    foreach ($Key in $PreviousEnv.Keys) {
        if ($null -eq $PreviousEnv[$Key]) {
            Remove-Item -Path "Env:$Key" -ErrorAction SilentlyContinue
        } else {
            Set-Item -Path "Env:$Key" -Value $PreviousEnv[$Key]
        }
    }
}
