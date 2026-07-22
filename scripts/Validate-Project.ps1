[CmdletBinding()]
param(
    [switch]$Build,
    [switch]$RunTests,
    [string]$EngineRoot = $env:UE_ENGINE_ROOT,
    [string]$TestFilter = "Eden"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Fail {
    param([string]$Message)
    Write-Error $Message
    exit 1
}

function ConvertTo-GitSafePath {
    param([string]$Path)

    return $Path.Replace("\", "/")
}

function Get-RegistryValue {
    param(
        [string]$KeyPath,
        [string]$ValueName
    )

    $RegistryItem = Get-ItemProperty -LiteralPath $KeyPath -ErrorAction SilentlyContinue
    if (-not $RegistryItem) {
        return $null
    }

    $Property = $RegistryItem.PSObject.Properties |
        Where-Object { $_.Name -eq $ValueName } |
        Select-Object -First 1

    if (-not $Property) {
        return $null
    }

    return $Property.Value
}

function Resolve-RegisteredEngineRoot {
    param([string]$EngineAssociation)

    if ([string]::IsNullOrWhiteSpace($EngineAssociation)) {
        return $null
    }

    $Candidates = @()
    $BuildAssociation = Get-RegistryValue `
        -KeyPath "HKCU:\Software\Epic Games\Unreal Engine\Builds" `
        -ValueName $EngineAssociation

    if (-not [string]::IsNullOrWhiteSpace($BuildAssociation)) {
        $Candidates += $BuildAssociation
    }

    $LauncherKeys = @(
        "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$EngineAssociation",
        "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$EngineAssociation"
    )

    foreach ($KeyPath in $LauncherKeys) {
        foreach ($ValueName in @("InstalledDirectory", "InstallLocation", "RootDir")) {
            $Candidate = Get-RegistryValue -KeyPath $KeyPath -ValueName $ValueName
            if (-not [string]::IsNullOrWhiteSpace($Candidate)) {
                $Candidates += $Candidate
            }
        }
    }

    foreach ($Candidate in $Candidates) {
        $ResolvedCandidate = Resolve-Path $Candidate -ErrorAction SilentlyContinue
        if ($ResolvedCandidate) {
            return $ResolvedCandidate
        }
    }

    return $null
}

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptRoot "..")

Write-Step "Validating repository root"
$UProjectFiles = @(Get-ChildItem -Path $ProjectRoot -Filter "*.uproject" -File)

if ($UProjectFiles.Count -ne 1) {
    Fail "Expected exactly one .uproject in '$ProjectRoot' but found $($UProjectFiles.Count)."
}

$UProject = $UProjectFiles[0]
$ProjectName = [System.IO.Path]::GetFileNameWithoutExtension($UProject.Name)
$EditorTarget = "${ProjectName}Editor"

$RequiredPaths = @(
    "AGENTS.md",
    "README.md",
    ".gitignore",
    ".gitattributes",
    ".editorconfig",
    "Config",
    "Content",
    "Source",
    "docs\IMPRINT.md",
    "docs\REMEMBER.md",
    "docs\PROJECT_SPEC.md",
    "docs\ARCHITECTURE.md",
    "docs\SDLC.md",
    "docs\REVIEW.md",
    "docs\RECOVER.md",
    ".agent\PLANS.md"
)

$MissingPaths = @()
foreach ($RelativePath in $RequiredPaths) {
    $Path = Join-Path $ProjectRoot $RelativePath
    if (-not (Test-Path $Path)) {
        $MissingPaths += $RelativePath
    }
}

if ($MissingPaths.Count -gt 0) {
    Fail ("Missing required paths:`n - " + ($MissingPaths -join "`n - "))
}

Write-Host "Project: $ProjectName"
Write-Host "UProject: $($UProject.FullName)"

Write-Step "Parsing .uproject JSON"
try {
    $ProjectDescriptor = Get-Content -Raw -Path $UProject.FullName | ConvertFrom-Json
}
catch {
    Fail "The .uproject file is not valid JSON: $($_.Exception.Message)"
}

$EngineAssociation = $null
$EngineAssociationProperty = $ProjectDescriptor.PSObject.Properties["EngineAssociation"]
if ($EngineAssociationProperty) {
    $EngineAssociation = [string]$EngineAssociationProperty.Value
}

Write-Step "Checking Git repository"
$GitCommand = Get-Command git -ErrorAction SilentlyContinue
if (-not $GitCommand) {
    Fail "Git is not available on PATH."
}

$GitSafeDirectory = ConvertTo-GitSafePath $ProjectRoot.Path
$GitProjectArgs = @("-c", "safe.directory=$GitSafeDirectory", "-C", $ProjectRoot.Path)

$GitRoot = (& git @GitProjectArgs rev-parse --show-toplevel 2>$null)
if (-not $GitRoot) {
    Fail "The project is not inside a Git repository."
}

$ResolvedGitRoot = (Resolve-Path $GitRoot).Path
if ($ResolvedGitRoot -ne $ProjectRoot.Path) {
    Fail "Git root '$ResolvedGitRoot' does not match project root '$($ProjectRoot.Path)'."
}

Write-Host "Git root: $ResolvedGitRoot"

Write-Step "Checking Git LFS"
$LfsVersion = (& git @GitProjectArgs lfs version 2>$null)
if (-not $LfsVersion) {
    Fail "Git LFS is not installed or not available."
}

$Attributes = Get-Content -Raw -Path (Join-Path $ProjectRoot ".gitattributes")
foreach ($Pattern in @("*.uasset", "*.umap")) {
    if ($Attributes -notmatch [regex]::Escape($Pattern)) {
        Fail "Missing Git LFS rule for $Pattern in .gitattributes."
    }
}

Write-Host $LfsVersion

Write-Step "Checking for accidentally tracked generated directories"
$TrackedFiles = @(& git @GitProjectArgs ls-files)
$ForbiddenPrefixes = @(
    "Binaries/",
    "DerivedDataCache/",
    "Intermediate/",
    "Saved/",
    ".vs/"
)

$ForbiddenTracked = @()
foreach ($File in $TrackedFiles) {
    foreach ($Prefix in $ForbiddenPrefixes) {
        if ($File.Replace("\", "/").StartsWith($Prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $ForbiddenTracked += $File
            break
        }
    }
}

if ($ForbiddenTracked.Count -gt 0) {
    Fail ("Generated files are tracked:`n - " + ($ForbiddenTracked -join "`n - "))
}

Write-Step "Repository validation passed"

if ($Build -or $RunTests) {
    if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
        $ResolvedEngineRoot = Resolve-RegisteredEngineRoot $EngineAssociation
    }
    else {
        $ResolvedEngineRoot = Resolve-Path $EngineRoot -ErrorAction SilentlyContinue
    }

    if (-not $ResolvedEngineRoot) {
        Fail "EngineRoot is required for build or tests. Pass -EngineRoot, set UE_ENGINE_ROOT, or register the .uproject EngineAssociation '$EngineAssociation' with UnrealVersionSelector."
    }

    Write-Host "Engine root: $($ResolvedEngineRoot.Path)"

    $BuildScript = Join-Path $ResolvedEngineRoot "Engine\Build\BatchFiles\Build.bat"
    $EditorCmd = Join-Path $ResolvedEngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

    if (-not (Test-Path $BuildScript)) {
        Fail "Unreal build script not found at '$BuildScript'."
    }

    Write-Step "Building $EditorTarget Win64 Development"
    & $BuildScript $EditorTarget Win64 Development "-Project=$($UProject.FullName)" -WaitMutex -FromMsBuild
    if ($LASTEXITCODE -ne 0) {
        Fail "Unreal build failed with exit code $LASTEXITCODE."
    }

    Write-Host "Build passed." -ForegroundColor Green

    if ($RunTests) {
        if (-not (Test-Path $EditorCmd)) {
            Fail "UnrealEditor-Cmd.exe not found at '$EditorCmd'."
        }

        Write-Step "Running Unreal automation tests matching '$TestFilter'"
        $ExecCommands = "Automation RunTests $TestFilter; Quit"
        & $EditorCmd $UProject.FullName `
            -Unattended `
            -NoSplash `
            -NullRHI `
            -NoP4 `
            "-ExecCmds=$ExecCommands" `
            "-TestExit=Automation Test Queue Empty" `
            -Log

        if ($LASTEXITCODE -ne 0) {
            Fail "Automation tests failed with exit code $LASTEXITCODE."
        }

        Write-Host "Automation tests passed." -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "All requested validation completed successfully." -ForegroundColor Green
