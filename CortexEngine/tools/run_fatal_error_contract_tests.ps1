param(
    [switch]$NoBuild,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "fatal_error_contract_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild."
}

$env:CORTEX_LOG_DIR = $LogDir
$env:CORTEX_FORCE_FATAL_ERROR = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"

$exeWorkingDir = Split-Path -Parent $exe
Push-Location $exeWorkingDir
try {
    $output = & $exe "--no-launcher" "--no-llm" "--no-dreamer" 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -Encoding UTF8 (Join-Path $LogDir "engine_stdout.txt")
} finally {
    Pop-Location
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_FORCE_FATAL_ERROR -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if ($exitCode -eq 0) {
    Add-Failure "Forced fatal run exited successfully; expected nonzero exit code."
}

$summaryPath = Join-Path $LogDir "last_renderer_failure.json"
if (-not (Test-Path $summaryPath)) {
    Add-Failure "Fatal run did not write last_renderer_failure.json. logs=$LogDir"
} else {
    $summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
    if ([string]$summary.schema -ne "cortex.renderer_failure.v1") {
        Add-Failure "Unexpected failure summary schema '$($summary.schema)'"
    }
    if ([string]$summary.failure.kind -ne "exception") {
        Add-Failure "Unexpected failure kind '$($summary.failure.kind)'"
    }
    if ([string]::IsNullOrWhiteSpace([string]$summary.failure.message) -or
        [string]$summary.failure.message -notmatch "Forced fatal error") {
        Add-Failure "Failure summary message does not describe the forced fatal error."
    }
    if ([int]$summary.failure.exit_code -eq 0) {
        Add-Failure "Failure summary exit_code should be nonzero."
    }
    if ([string]::IsNullOrWhiteSpace([string]$summary.log_file)) {
        Add-Failure "Failure summary missing log_file."
    }
    if ($null -eq $summary.recent_log_lines -or $summary.recent_log_lines.Count -lt 1) {
        Add-Failure "Failure summary missing recent log lines."
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Fatal error contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Fatal error contract tests passed: logs=$LogDir" -ForegroundColor Green
