param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 220,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "rt_firefly_outlier_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}

$args = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"),
    "-SmokeFrames", [string]$SmokeFrames,
    "-CameraBookmark", "hero",
    "-MaxRTReflectionSignalOutlierRatio", "0.015",
    "-MaxRTReflectionHistorySignalOutlierRatio", "0.015",
    "-MaxVisualSaturatedRatio", "0.10",
    "-MaxVisualNearWhiteRatio", "0.12",
    "-MaxVisualCenterLuma", "220",
    "-LogDir", $LogDir,
    "-SkipSurfaceDebug"
)
if ($NoBuild) {
    $args += "-NoBuild"
}

& powershell @args
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$reportPath = Join-Path $LogDir "frame_report_last.json"
if (-not (Test-Path $reportPath)) {
    throw "RT firefly/outlier test did not write frame report: $reportPath"
}
$report = Get-Content $reportPath -Raw | ConvertFrom-Json
$rt = $report.frame_contract.ray_tracing
if (-not [bool]$rt.reflection_signal_valid -or -not [bool]$rt.reflection_history_signal_valid) {
    throw "RT firefly/outlier test requires valid raw and history reflection signal"
}
Write-Host ("RT firefly/outlier test passed: raw_outlier={0} history_outlier={1} logs={2}" -f `
    $rt.reflection_signal_outlier_ratio,
    $rt.reflection_history_signal_outlier_ratio,
    $LogDir) -ForegroundColor Green
