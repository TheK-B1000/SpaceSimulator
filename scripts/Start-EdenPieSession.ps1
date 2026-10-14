# Human PIE session against a local, throwaway ProjectEden (ExecPlan 0010).
#
# - ProjectEden runs on 127.0.0.1 with EDEN_ADVISORY_REASONER=deterministic (no LLM, $0).
# - Its database is a temp SQLite file under $RunRoot; ProjectEden's data/eden.db is untouched.
# - A local test user is registered; the bearer JWT goes only into this process's environment
#   and the Editor launched from it. It is never printed or written to disk.
# - The Editor reads EDEN_OS_LIVE_E2E_BASE_URL / EDEN_OS_LIVE_E2E_BEARER_JWT at console-command
#   time, so it must be started from here (an Editor already open cannot see new env vars).
#
# Then in PIE (console ~): EnableEdenOs ; SetEdenOsBearerFromEnv ; StartMission
# Afterwards:  .\scripts\Get-EdenPieEvidence.ps1 -RunRoot <printed path>
param(
    [int]$Port = 8791,
    [string]$ProjectEdenRoot = "B:\repo\ProjectEden",
    [string]$EngineRoot = "K:\Program Files\Epic Games\UE_5.8",
    [switch]$NoEditor
)
$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$UProject = Join-Path $RepoRoot "EdenSpaceSimulator.uproject"
$Editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$ApiRoot = Join-Path $ProjectEdenRoot "packages\api"
$Python = Join-Path $ApiRoot ".venv\Scripts\python.exe"

if (-not $NoEditor) {
    $Open = Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" |
        Where-Object { $_.CommandLine -like "*EdenSpaceSimulator/EdenSpaceSimulator.uproject*" -or
                       $_.CommandLine -like "*EdenSpaceSimulator\EdenSpaceSimulator.uproject*" }
    if ($Open) { throw "Close the EdenSpaceSimulator Editor first: it cannot see new environment variables." }
}
if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
    throw "Port $Port is already in use."
}

$RunRoot = Join-Path $RepoRoot ("Saved\Automation\EdenPie\" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
$Db = (Join-Path $RunRoot "projecteden-pie.sqlite").Replace("\", "/")

$env:EDEN_DATABASE_URL = "sqlite:///$Db"
$env:EDEN_DATA_DIR = $RunRoot
$env:EDEN_LOGS_DIR = $RunRoot
$env:EDEN_JWT_SECRET = [guid]::NewGuid().ToString("N")
$env:EDEN_ENVIRONMENT = "development"
$env:EDEN_ALLOWED_HOSTS = "127.0.0.1,localhost"
$env:EDEN_ADVISORY_REASONER = "deterministic"
Remove-Item Env:EDEN_COMMAND_PROPOSAL_REASONER -ErrorAction SilentlyContinue

$Server = Start-Process -FilePath $Python -WorkingDirectory $ApiRoot -PassThru -WindowStyle Hidden `
    -ArgumentList @("-m", "uvicorn", "eden_api.main:app", "--host", "127.0.0.1", "--port", "$Port") `
    -RedirectStandardOutput (Join-Path $RunRoot "uvicorn.out.log") `
    -RedirectStandardError (Join-Path $RunRoot "uvicorn.err.log")

$Base = "http://127.0.0.1:$Port"
$Deadline = (Get-Date).AddSeconds(45)
do {
    Start-Sleep -Milliseconds 500
    try { Invoke-RestMethod "$Base/health" -TimeoutSec 2 | Out-Null; $Up = $true } catch { $Up = $false }
} while (-not $Up -and (Get-Date) -lt $Deadline)
if (-not $Up) {
    Stop-Process -Id $Server.Id -ErrorAction SilentlyContinue
    throw "ProjectEden did not become healthy; see $RunRoot\uvicorn.err.log"
}

$Cred = @{ email = "pie-$((Get-Date).Ticks)@example.test"; password = [guid]::NewGuid().ToString("N") + "Aa1!" } |
    ConvertTo-Json -Compress
Invoke-RestMethod -Method Post "$Base/api/auth/register" -ContentType "application/json" -Body $Cred | Out-Null
$Token = (Invoke-RestMethod -Method Post "$Base/api/auth/login" -ContentType "application/json" -Body $Cred).access_token
if ([string]::IsNullOrWhiteSpace($Token)) { throw "Login returned no token." }

$env:EDEN_OS_LIVE_E2E_BASE_URL = $Base
$env:EDEN_OS_LIVE_E2E_BEARER_JWT = $Token
@{ baseUrl = $Base; reasoner = "deterministic"; serverPid = $Server.Id; database = $Db; startedAt = (Get-Date).ToString("o") } |
    ConvertTo-Json | Out-File -Encoding utf8 (Join-Path $RunRoot "session.json")

if (-not $NoEditor) {
    Start-Process -FilePath $Editor -ArgumentList "`"$UProject`""
}

Write-Host "ProjectEden: $Base (deterministic advisories), server PID $($Server.Id)"
Write-Host "Run folder: $RunRoot"
if (-not $NoEditor) {
    Write-Host "In PIE open the console (~): EnableEdenOs, SetEdenOsBearerFromEnv, StartMission"
}
Write-Host "Evidence:   .\scripts\Get-EdenPieEvidence.ps1 -RunRoot `"$RunRoot`""
Write-Host "Stop:       Stop-Process -Id $($Server.Id)"
