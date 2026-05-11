param(
    [int]$SmokeFrames = 240,
    [double]$MaxAvgLumaDelta = 0.35,
    [double]$MaxHistoryOutlierRatio = 0.025,
    [double]$MaxHistoryToRawOutlierScale = 1.35,
    [double]$MinHistoryNonZeroRatioScale = 0.35,
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "rt_reflection_history_quality_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not $NoBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
}

$smokeScript = Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"
$smokeArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript,
    "-NoBuild",
    "-IsolatedLogs",
    "-SmokeFrames", [string]$SmokeFrames,
    "-SkipSurfaceDebug",
    "-LogDir", $LogDir
)
$output = & powershell @smokeArgs 2>&1
$exitCode = $LASTEXITCODE
$outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
Set-Content -Path (Join-Path $LogDir "history_quality_stdout.txt") -Value $outputText -Encoding UTF8
if ($exitCode -ne 0) {
    Write-Host $outputText
    throw "RT showcase prerequisite failed for reflection history quality gate. logs=$LogDir"
}

$reportPath = Join-Path $LogDir "frame_report_last.json"
if (-not (Test-Path $reportPath)) {
    $reportPath = Join-Path $LogDir "frame_report_shutdown.json"
}
if (-not (Test-Path $reportPath)) {
    throw "RT reflection history quality gate could not find frame report. logs=$LogDir"
}

$report = Get-Content $reportPath -Raw | ConvertFrom-Json
$rt = $report.frame_contract.ray_tracing
$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if (-not [bool]$rt.reflection_signal_stats_captured -or -not [bool]$rt.reflection_signal_valid) {
    Add-Failure "raw RT reflection signal stats were not captured/valid"
}
if (-not [bool]$rt.reflection_history_signal_stats_captured -or -not [bool]$rt.reflection_history_signal_valid) {
    Add-Failure "history RT reflection signal stats were not captured/valid"
}
if (-not [bool]$rt.reflection_dispatch_ready) {
    Add-Failure "RT reflection dispatch was not ready: $($rt.reflection_readiness_reason)"
}

$rawAvg = [double]$rt.reflection_signal_avg_luma
$rawMax = [double]$rt.reflection_signal_max_luma
$rawNonZero = [double]$rt.reflection_signal_nonzero_ratio
$rawOutlier = [double]$rt.reflection_signal_outlier_ratio
$histAvg = [double]$rt.reflection_history_signal_avg_luma
$histMax = [double]$rt.reflection_history_signal_max_luma
$histNonZero = [double]$rt.reflection_history_signal_nonzero_ratio
$histOutlier = [double]$rt.reflection_history_signal_outlier_ratio
$histDelta = [double]$rt.reflection_history_signal_avg_luma_delta

if ($rawMax -le 0.001) {
    Add-Failure "raw RT reflection max luma is too low: $rawMax"
}
if ($histMax -le 0.001) {
    Add-Failure "history RT reflection max luma is too low: $histMax"
}
if ($histDelta -gt $MaxAvgLumaDelta) {
    Add-Failure "raw/history reflection avg-luma delta is $histDelta, max $MaxAvgLumaDelta"
}
if ($histOutlier -gt $MaxHistoryOutlierRatio) {
    Add-Failure "history reflection outlier ratio is $histOutlier, max $MaxHistoryOutlierRatio"
}
$scaledOutlierLimit = [Math]::Max(0.0025, $rawOutlier * $MaxHistoryToRawOutlierScale)
if ($histOutlier -gt $scaledOutlierLimit) {
    Add-Failure "history outlier ratio $histOutlier exceeds raw-scaled limit $scaledOutlierLimit (raw=$rawOutlier)"
}
$minHistoryCoverage = $rawNonZero * $MinHistoryNonZeroRatioScale
if ($histNonZero -lt $minHistoryCoverage) {
    Add-Failure "history nonzero ratio $histNonZero is below raw-scaled minimum $minHistoryCoverage (raw=$rawNonZero)"
}
if ($histAvg -le 0.0 -or $rawAvg -le 0.0) {
    Add-Failure "raw/history avg luma must both be positive: raw=$rawAvg history=$histAvg"
}

$denoisePass = $report.frame_contract.passes | Where-Object { [string]$_.name -eq "RTDenoise" } | Select-Object -First 1
if ($null -eq $denoisePass -or -not [bool]$denoisePass.executed) {
    Add-Failure "RTDenoise pass did not execute"
}
$historyStatsPass = $report.frame_contract.passes | Where-Object { [string]$_.name -eq "RTReflectionHistorySignalStats" } | Select-Object -First 1
if ($null -eq $historyStatsPass -or -not [bool]$historyStatsPass.executed) {
    Add-Failure "RTReflectionHistorySignalStats pass did not execute"
}

if ($failures.Count -gt 0) {
    Write-Host "RT reflection history quality tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "RT reflection history quality tests passed" -ForegroundColor Green
Write-Host (" logs={0}" -f $LogDir)
Write-Host (" raw={0:N4}/{1:N4}/{2:N4}/{3:N4} history={4:N4}/{5:N4}/{6:N4}/{7:N4} delta={8:N4}" -f `
    $rawAvg, $rawNonZero, $rawMax, $rawOutlier,
    $histAvg, $histNonZero, $histMax, $histOutlier,
    $histDelta)
exit 0
