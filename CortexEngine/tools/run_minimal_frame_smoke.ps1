param(
    [int]$SmokeFrames = 20,
    [string]$LogDir = "",
    [switch]$IsolatedLogs,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$baseLogDir = Join-Path $root "build/bin/logs"
$activeLogDir = $baseLogDir
if (-not [string]::IsNullOrWhiteSpace($LogDir)) {
    $activeLogDir = $LogDir
    $env:CORTEX_LOG_DIR = $activeLogDir
} elseif ($IsolatedLogs) {
    $runId = "minimal_frame_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
    $env:CORTEX_LOG_DIR = $activeLogDir
}

$reportPath = Join-Path $activeLogDir "frame_report_last.json"
$shutdownReportPath = Join-Path $activeLogDir "frame_report_shutdown.json"
$runLogPath = Join-Path $activeLogDir "cortex_last_run.txt"
$visualPath = Join-Path $activeLogDir "visual_validation_rt_showcase.bmp"

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath, $visualPath

$env:CORTEX_FORCE_MINIMAL_FRAME = "1"
$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "2"

Push-Location (Split-Path -Parent $exe)
try {
    $output = & $exe `
        "--scene" "temporal_validation" `
        "--mode=default" `
        "--no-llm" `
        "--no-dreamer" `
        "--no-launcher" `
        "--smoke-frames=$SmokeFrames" `
        "--exit-after-visual-validation" 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -Encoding UTF8 (Join-Path $activeLogDir "engine_stdout.txt")
} finally {
    Pop-Location
    Remove-Item Env:\CORTEX_FORCE_MINIMAL_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
}

if ($exitCode -ne 0) {
    throw "CortexEngine minimal-frame process failed with exit code $exitCode"
}
if (-not (Test-Path $reportPath)) {
    if (Test-Path $shutdownReportPath) {
        $reportPath = $shutdownReportPath
    } else {
        throw "Expected frame report was not written: $reportPath"
    }
}

$report = Get-Content $reportPath -Raw | ConvertFrom-Json
$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}
function Test-LogContains([string]$Pattern) {
    if (-not (Test-Path $runLogPath)) {
        return $false
    }
    return [bool](Select-String -Path $runLogPath -Pattern $Pattern -SimpleMatch -Quiet)
}

if ([string]$report.scene -ne "temporal_validation") {
    Add-Failure "expected temporal_validation scene but report scene was '$($report.scene)'"
}
if ($report.health_warnings.Count -ne 0) {
    $allowedHealthWarnings = @(
        "frame_contract:visibility_buffer_planned_but_not_rendered",
        "visual_validation_mostly_black",
        "visual_validation_too_dark"
    )
    foreach ($warning in $report.health_warnings) {
        if ($allowedHealthWarnings -notcontains [string]$warning) {
            Add-Failure "unexpected health warning in minimal frame: $warning"
        }
    }
}
if ($report.frame_contract.warnings.Count -ne 0) {
    foreach ($warning in $report.frame_contract.warnings) {
        if ([string]$warning -ne "visibility_buffer_planned_but_not_rendered") {
            Add-Failure "unexpected frame_contract warning in minimal frame: $warning"
        }
    }
}

$executed = $report.frame_contract.executed_features
foreach ($feature in @("ray_tracing_enabled", "visibility_buffer_enabled", "gpu_culling_enabled", "taa_enabled", "ssr_enabled", "ssao_enabled", "bloom_enabled", "particles_enabled")) {
    if ($null -ne $executed -and [bool]$executed.$feature) {
        Add-Failure "minimal frame unexpectedly executed feature '$feature'"
    }
}
if ([int]$report.frame_contract.renderables.visible -le 0 -and -not (Test-LogContains "Last completed pass: MinimalFrame_Done")) {
    Add-Failure "minimal frame did not confirm MinimalFrame_Done in run log"
}
if (-not (Test-LogContains "Renderer: CORTEX_FORCE_MINIMAL_FRAME set; running ultra-minimal clear-only frame path")) {
    Add-Failure "run log does not confirm forced minimal frame mode"
}
if (-not (Test-LogContains "Backbuffer used-as-RT: true")) {
    Add-Failure "run log does not confirm backbuffer render-target use"
}

if (-not [bool]$report.visual_validation.captured) {
    Add-Failure "visual validation capture was not written"
} elseif ($null -eq $report.visual_validation.image_stats -or -not [bool]$report.visual_validation.image_stats.valid) {
    Add-Failure "visual validation image stats are invalid"
} else {
    if ([double]$report.visual_validation.image_stats.avg_luma -gt 1.0) {
        Add-Failure "minimal clear average luma is too high: $($report.visual_validation.image_stats.avg_luma)"
    }
    if ([double]$report.visual_validation.image_stats.nonblack_ratio -gt 0.01) {
        Add-Failure "minimal clear nonblack ratio is too high: $($report.visual_validation.image_stats.nonblack_ratio)"
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "Minimal frame smoke failed with $($failures.Count) issue(s)."
}

Write-Host (
    "Minimal frame smoke passed: " +
    "gpu_ms=$([Math]::Round([double]$report.gpu_frame_ms, 3)) " +
    "avg_luma=$([Math]::Round([double]$report.visual_validation.image_stats.avg_luma, 3)) " +
    "nonblack=$([Math]::Round([double]$report.visual_validation.image_stats.nonblack_ratio, 3))"
)
Write-Host " logs=$activeLogDir"
