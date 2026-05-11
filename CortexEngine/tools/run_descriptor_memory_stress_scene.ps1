param(
    [int]$SmokeFrames = 240,
    [int]$MinPersistentDescriptors = 900,
    [int]$MaxPersistentDescriptors = 1024,
    [int]$MaxStagingDescriptors = 128,
    [double]$MaxDxgiMemoryMb = 512.0,
    [double]$MaxEstimatedMemoryMb = 256.0,
    [double]$MaxRenderTargetMemoryMb = 160.0,
    [double]$MaxEstimatedWriteMb = 128.0,
    [double]$MinRTReflectionAverageLuma = 0.005,
    [double]$MinRTReflectionHistoryAverageLuma = 0.005,
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$smokeScript = Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"
$activeLogDir = if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "descriptor_memory_stress_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    Join-Path (Join-Path $root "build/bin/logs/runs") $runId
} else {
    $LogDir
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null

$smokeArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $smokeScript,
    "-SmokeFrames", [string]$SmokeFrames,
    "-TemporalRuns", "1",
    "-SkipSurfaceDebug",
    "-AllowRTCadenceSkips",
    "-RTBudgetProfile", "8gb_balanced",
    "-ExpectedRTBudgetProfile", "8gb_balanced",
    "-MaxPersistentDescriptors", [string]$MaxPersistentDescriptors,
    "-MaxStagingDescriptors", [string]$MaxStagingDescriptors,
    "-MaxDxgiMemoryMb", [string]$MaxDxgiMemoryMb,
    "-MaxEstimatedMemoryMb", [string]$MaxEstimatedMemoryMb,
    "-MaxRenderTargetMemoryMb", [string]$MaxRenderTargetMemoryMb,
    "-MaxEstimatedWriteMb", [string]$MaxEstimatedWriteMb,
    "-LogDir", $activeLogDir
)
if ($NoBuild) {
    $smokeArgs += "-NoBuild"
}

$output = & powershell @smokeArgs 2>&1
$exitCode = $LASTEXITCODE
$outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
Set-Content -Path (Join-Path $activeLogDir "rt_showcase_stress_stdout.txt") -Value $outputText -Encoding UTF8
if (-not [string]::IsNullOrWhiteSpace($outputText)) {
    $outputText -split "`r?`n" | ForEach-Object { Write-Host $_ }
}
if ($exitCode -ne 0) {
    Write-Host "Descriptor/memory stress scene failed in RT showcase smoke." -ForegroundColor Red
    Write-Host "logs=$activeLogDir"
    exit $exitCode
}

$reportPath = Join-Path $activeLogDir "frame_report_last.json"
if (-not (Test-Path $reportPath)) {
    throw "Expected frame report was not written: $reportPath"
}

$report = Get-Content $reportPath -Raw | ConvertFrom-Json
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

$persistentUsed = [int]$report.descriptors.persistent_used
$stagingUsed = [int]$report.descriptors.staging_used
$bindlessAllocated = [int]$report.descriptors.bindless_allocated
$transientStart = [int]$report.descriptors.transient_start
$transientEnd = [int]$report.descriptors.transient_end
$transientBudget = $transientEnd - $transientStart
$transientDelta = [int]$report.frame_contract.pass_budget_summary.transient_descriptor_delta_total
$estimatedWriteMb = [double]$report.frame_contract.pass_budget_summary.estimated_write_mb_total
$estimatedMemoryMb = [double]$report.memory_mb.total_estimated
$renderTargetMemoryMb = [double]$report.memory_mb.render_targets
$dxgiMemoryMb = [double]$report.dxgi_memory_mb.current_usage
$rtAvgLuma = [double]$report.frame_contract.ray_tracing.reflection_signal_avg_luma
$rtHistoryAvgLuma = [double]$report.frame_contract.ray_tracing.reflection_history_signal_avg_luma

if ($persistentUsed -lt $MinPersistentDescriptors) {
    Add-Failure "persistent descriptors used is $persistentUsed, expected descriptor-stress pressure >= $MinPersistentDescriptors"
}
if ($persistentUsed -gt $MaxPersistentDescriptors) {
    Add-Failure "persistent descriptors used is $persistentUsed, budget is <= $MaxPersistentDescriptors"
}
if ($stagingUsed -gt $MaxStagingDescriptors) {
    Add-Failure "staging descriptors used is $stagingUsed, budget is <= $MaxStagingDescriptors"
}
if ($bindlessAllocated -le 0) {
    Add-Failure "bindless allocation count is not positive"
}
if ($transientBudget -le 0) {
    Add-Failure "transient descriptor segment is empty: start=$transientStart end=$transientEnd"
}
if ($transientDelta -ne 0) {
    Add-Failure "transient descriptor delta is $transientDelta, expected 0 at frame end"
}
if ($dxgiMemoryMb -gt $MaxDxgiMemoryMb) {
    Add-Failure "DXGI memory is $dxgiMemoryMb MB, budget is <= $MaxDxgiMemoryMb MB"
}
if ($estimatedMemoryMb -gt $MaxEstimatedMemoryMb) {
    Add-Failure "estimated renderer memory is $estimatedMemoryMb MB, budget is <= $MaxEstimatedMemoryMb MB"
}
if ($renderTargetMemoryMb -gt $MaxRenderTargetMemoryMb) {
    Add-Failure "render target memory is $renderTargetMemoryMb MB, budget is <= $MaxRenderTargetMemoryMb MB"
}
if ($estimatedWriteMb -gt $MaxEstimatedWriteMb) {
    Add-Failure "estimated pass write bandwidth is $estimatedWriteMb MB/frame, budget is <= $MaxEstimatedWriteMb MB/frame"
}
if ($rtAvgLuma -lt $MinRTReflectionAverageLuma) {
    Add-Failure "raw RT reflection average luma is $rtAvgLuma, expected >= $MinRTReflectionAverageLuma"
}
if ($rtHistoryAvgLuma -lt $MinRTReflectionHistoryAverageLuma) {
    Add-Failure "history RT reflection average luma is $rtHistoryAvgLuma, expected >= $MinRTReflectionHistoryAverageLuma"
}

if ($null -ne $report.frame_contract.health.last_warning_code -and
    -not [string]::IsNullOrWhiteSpace([string]$report.frame_contract.health.last_warning_code)) {
    Add-Failure "renderer health warning present: $($report.frame_contract.health.last_warning_code)"
}

$summary = [pscustomobject]@{
    scene = "rt_showcase_descriptor_memory_stress"
    report = $reportPath
    persistent_descriptors = $persistentUsed
    persistent_descriptor_budget = $MaxPersistentDescriptors
    staging_descriptors = $stagingUsed
    staging_descriptor_budget = $MaxStagingDescriptors
    bindless_allocated = $bindlessAllocated
    transient_descriptor_budget = $transientBudget
    transient_descriptor_delta = $transientDelta
    dxgi_memory_mb = $dxgiMemoryMb
    estimated_memory_mb = $estimatedMemoryMb
    render_target_memory_mb = $renderTargetMemoryMb
    estimated_write_mb = $estimatedWriteMb
    rt_reflection_avg_luma = $rtAvgLuma
    rt_reflection_history_avg_luma = $rtHistoryAvgLuma
}
$summaryPath = Join-Path $activeLogDir "descriptor_memory_stress_summary.json"
$summary | ConvertTo-Json -Depth 4 | Set-Content -Path $summaryPath -Encoding UTF8

if ($failures.Count -gt 0) {
    Write-Host "Descriptor/memory stress scene failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir"
    exit 1
}

Write-Host "Descriptor/memory stress scene passed." -ForegroundColor Green
Write-Host ("  persistent_descriptors={0}/{1} staging={2}/{3} transient_budget={4} transient_delta={5}" -f `
    $persistentUsed, $MaxPersistentDescriptors, $stagingUsed, $MaxStagingDescriptors, $transientBudget, $transientDelta)
Write-Host ("  dxgi_mb={0:N2}/{1:N0} estimated_mb={2:N2}/{3:N0} write_mb={4:N2}/{5:N0}" -f `
    $dxgiMemoryMb, $MaxDxgiMemoryMb, $estimatedMemoryMb, $MaxEstimatedMemoryMb, $estimatedWriteMb, $MaxEstimatedWriteMb)
Write-Host ("  rt_signal_avg={0:N4} rt_history_avg={1:N4}" -f $rtAvgLuma, $rtHistoryAvgLuma)
Write-Host "  logs=$activeLogDir"
