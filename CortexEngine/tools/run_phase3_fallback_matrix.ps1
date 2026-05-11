param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 60,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "phase3_fallback_matrix_{0}_{1}_{2}" -f `
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

$failures = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]
$exeWorkingDir = Split-Path -Parent $exe

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Invoke-FallbackCase([string]$Name, [string[]]$Arguments, [hashtable]$Environment = @{}) {
    $caseLogDir = Join-Path $script:LogDir $Name
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

    $oldValues = @{}
    foreach ($key in $Environment.Keys) {
        $oldValues[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, [string]$Environment[$key], "Process")
    }
    $env:CORTEX_LOG_DIR = $caseLogDir
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "25"

    Push-Location $script:exeWorkingDir
    try {
        $output = & $script:exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "engine_stdout.txt")
    } finally {
        Pop-Location
        Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
        foreach ($key in $Environment.Keys) {
            if ($null -eq $oldValues[$key]) {
                [Environment]::SetEnvironmentVariable($key, $null, "Process")
            } else {
                [Environment]::SetEnvironmentVariable($key, [string]$oldValues[$key], "Process")
            }
        }
    }

    $reportPath = Join-Path $caseLogDir "frame_report_last.json"
    $row = [ordered]@{
        name = $Name
        exit_code = $exitCode
        report = $reportPath
        startup_safe_mode = $null
        graphics_preset = ""
        environment_active = ""
        environment_requested = ""
        environment_fallback = $null
        environment_fallback_reason = ""
        rt_enabled = $null
        particles_enabled = $null
        passed = $false
    }

    if ($exitCode -ne 0) {
        Add-Failure "$Name exited with code $exitCode. logs=$caseLogDir"
        $rows.Add([pscustomobject]$row)
        return $null
    }
    if (-not (Test-Path $reportPath)) {
        Add-Failure "$Name did not write frame report: $reportPath"
        $rows.Add([pscustomobject]$row)
        return $null
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $fc = $report.frame_contract
    $row.startup_safe_mode = [bool]$fc.startup.safe_mode
    $row.graphics_preset = [string]$fc.graphics_preset.id
    $row.environment_active = [string]$fc.environment.active
    $row.environment_requested = [string]$fc.environment.requested
    $row.environment_fallback = [bool]$fc.environment.fallback
    $row.environment_fallback_reason = [string]$fc.environment.fallback_reason
    $row.rt_enabled = [bool]$fc.features.ray_tracing_enabled
    $row.particles_enabled = [bool]$fc.features.particles_enabled
    $row.passed = $true
    $rows.Add([pscustomobject]$row)
    return $report
}

$common = @("--mode=default", "--no-llm", "--no-dreamer", "--no-launcher", "--smoke-frames=$SmokeFrames", "--exit-after-visual-validation")

$safe = Invoke-FallbackCase "safe_startup_no_rt" (
    @("--scene", "rt_showcase", "--graphics-preset", "safe_startup", "--environment", "studio") + $common
) @{ "CORTEX_FORCE_SAFE_MODE" = "1" }
if ($null -ne $safe) {
    $fc = $safe.frame_contract
    if (-not [bool]$fc.startup.preflight_passed) { Add-Failure "safe_startup_no_rt preflight did not pass" }
    if (-not [bool]$fc.startup.safe_mode) { Add-Failure "safe_startup_no_rt did not report startup.safe_mode=true" }
    if ([string]$fc.graphics_preset.id -ne "safe_startup") { Add-Failure "safe_startup_no_rt preset was '$($fc.graphics_preset.id)'" }
    if ([bool]$fc.features.ray_tracing_enabled) { Add-Failure "safe_startup_no_rt still had ray tracing enabled" }
    if ([bool]$fc.features.particles_enabled) { Add-Failure "safe_startup_no_rt still had particles enabled" }
    if (-not [bool]$fc.environment.loaded) { Add-Failure "safe_startup_no_rt did not report a loaded environment" }
}

$missing = Invoke-FallbackCase "missing_selected_environment" (
    @("--scene", "rt_showcase", "--graphics-preset", "safe_startup", "--environment", "definitely_missing_environment") + $common
) @{}
if ($null -ne $missing) {
    $fc = $missing.frame_contract
    if ([string]$fc.environment.active -eq "definitely_missing_environment") {
        Add-Failure "missing_selected_environment activated the nonexistent environment"
    }
    if ([string]$fc.environment.requested -ne "definitely_missing_environment") {
        Add-Failure "missing_selected_environment did not preserve requested environment in frame contract"
    }
    if (-not [bool]$fc.environment.fallback) {
        Add-Failure "missing_selected_environment did not report environment.fallback=true"
    }
    if ([string]$fc.environment.fallback_reason -ne "requested_environment_not_found") {
        Add-Failure "missing_selected_environment fallback reason was '$($fc.environment.fallback_reason)'"
    }
}

$rtOff = Invoke-FallbackCase "explicit_no_rt_profile" (
    @("--scene", "temporal_validation", "--graphics-preset", "safe_startup", "--environment", "studio") + $common
) @{}
if ($null -ne $rtOff) {
    $fc = $rtOff.frame_contract
    if ([bool]$fc.features.ray_tracing_enabled) { Add-Failure "explicit_no_rt_profile reported ray tracing enabled" }
    if ([bool]$fc.features.rt_reflections_enabled) { Add-Failure "explicit_no_rt_profile reported RT reflections enabled" }
    if ([bool]$fc.ray_tracing.scheduler_enabled) { Add-Failure "explicit_no_rt_profile still enabled the RT scheduler" }
    if ([string]$fc.ray_tracing.scheduler_disabled_reason -ne "not_requested") {
        Add-Failure "explicit_no_rt_profile scheduler disabled reason was '$($fc.ray_tracing.scheduler_disabled_reason)'"
    }
}

$rows | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $LogDir "phase3_fallback_matrix_summary.json")

if ($failures.Count -gt 0) {
    Write-Host "Phase 3 fallback matrix failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Phase 3 fallback matrix passed: cases=$($rows.Count) logs=$LogDir" -ForegroundColor Green
exit 0
