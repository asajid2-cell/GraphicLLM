param(
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$baseLogDir = Join-Path $root "build/bin/logs"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "phase2_validation_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

Write-Host "Phase 2 validation delegates to the current release gate." -ForegroundColor Cyan
Write-Host "This keeps the named phase2.md entrypoint while preserving the broader Phase 3-era gates." -ForegroundColor Cyan

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run_release_validation.ps1") `
    -LogDir $LogDir
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "Phase 2 validation passed." -ForegroundColor Green
    Write-Host "  logs=$LogDir"
} else {
    Write-Host "Phase 2 validation failed." -ForegroundColor Red
    Write-Host "  logs=$LogDir"
}

exit $exitCode
