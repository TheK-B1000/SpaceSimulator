[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptRoot "..")
$GitSafeDirectory = $ProjectRoot.Path.Replace("\", "/")
$GitProjectArgs = @("-c", "safe.directory=$GitSafeDirectory")

Push-Location $ProjectRoot
try {
    if (-not (Test-Path ".git")) {
        throw "No .git directory found at the project root: $ProjectRoot"
    }

    git @GitProjectArgs lfs install
    if ($LASTEXITCODE -ne 0) {
        throw "git lfs install failed."
    }

    git @GitProjectArgs add .gitattributes .gitignore .editorconfig AGENTS.md README.md CONTRIBUTING.md CODEX_BOOTSTRAP_PROMPT.md docs scripts .github .agent
    if ($LASTEXITCODE -ne 0) {
        throw "git add failed."
    }

    Write-Host ""
    Write-Host "Repository foundation staged. Review with:" -ForegroundColor Green
    Write-Host "  git status"
    Write-Host "  git diff --cached"
    Write-Host ""
    Write-Host "Commit only after review, for example:"
    Write-Host '  git commit -m "chore(repo): add professional project foundation"'
}
finally {
    Pop-Location
}
