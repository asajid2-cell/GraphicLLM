param(
    [int]$SmokeFrames = 90,
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$baseLogDir = Join-Path $root "build/bin/logs"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "llm_renderer_command_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$reportPath = Join-Path $LogDir "frame_report_last.json"
$shutdownReportPath = Join-Path $LogDir "frame_report_shutdown.json"
$runLogPath = Join-Path $LogDir "cortex_last_run.txt"
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before LLM renderer command smoke"
    }
}

if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

$commandJson = '{"commands":[{"type":"modify_renderer","exposure":1.35,"shadows":false,"fog_enabled":true,"fog_density":0.031,"lighting_rig":"studio_three_point","water_wave_amplitude":0.07}]}'

$previousLogDir = $env:CORTEX_LOG_DIR
$previousMock = $env:CORTEX_LLM_MOCK
$previousArchitectJson = $env:CORTEX_ARCHITECT_COMMAND_JSON
$previousDisableDreamer = $env:CORTEX_DISABLE_DREAMER
$previousDisableDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER

$env:CORTEX_LOG_DIR = $LogDir
$env:CORTEX_LLM_MOCK = "1"
$env:CORTEX_ARCHITECT_COMMAND_JSON = $commandJson
$env:CORTEX_DISABLE_DREAMER = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"

$exeWorkingDir = Split-Path -Parent $exe
try {
    Push-Location $exeWorkingDir
    try {
        $output = & $exe "--scene" "temporal_validation" "--llm-mock" "--no-dreamer" "--no-launcher" "--smoke-frames=$SmokeFrames" 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
} finally {
    if ($null -eq $previousLogDir) { Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue } else { $env:CORTEX_LOG_DIR = $previousLogDir }
    if ($null -eq $previousMock) { Remove-Item Env:\CORTEX_LLM_MOCK -ErrorAction SilentlyContinue } else { $env:CORTEX_LLM_MOCK = $previousMock }
    if ($null -eq $previousArchitectJson) { Remove-Item Env:\CORTEX_ARCHITECT_COMMAND_JSON -ErrorAction SilentlyContinue } else { $env:CORTEX_ARCHITECT_COMMAND_JSON = $previousArchitectJson }
    if ($null -eq $previousDisableDreamer) { Remove-Item Env:\CORTEX_DISABLE_DREAMER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DREAMER = $previousDisableDreamer }
    if ($null -eq $previousDisableDebugLayer) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $previousDisableDebugLayer }
}

$outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
$outputText | Set-Content -Path (Join-Path $LogDir "llm_renderer_command_stdout.txt") -Encoding UTF8
if (-not [string]::IsNullOrWhiteSpace($outputText)) {
    $outputText -split "`r?`n" | ForEach-Object { Write-Host $_ }
}

if ($exitCode -ne 0) {
    throw "CortexEngine LLM renderer command smoke failed with exit code $exitCode"
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

if ($outputText -notmatch "\[Architect\] Queued 1 startup Architect command") {
    Add-Failure "startup Architect command was not queued"
}
if ($outputText -notmatch "\[Architect\] renderer: .*exposure=1\.35") {
    Add-Failure "renderer command status did not report exposure application"
}
if ($outputText -notmatch "\[Architect\] renderer: .*shadows=off") {
    Add-Failure "renderer command status did not report shadows=off"
}
if ($outputText -notmatch "\[Architect\] renderer: .*lighting_rig=studio_three_point") {
    Add-Failure "renderer command status did not report lighting rig application"
}
if ([string]$report.scene -ne "temporal_validation") {
    Add-Failure "expected temporal_validation scene but report scene was '$($report.scene)'"
}
if ([bool]$report.renderer.shadows_enabled) {
    Add-Failure "renderer.shadows_enabled remained true after LLM command"
}
if ([bool]$report.frame_contract.features.shadows_enabled) {
    Add-Failure "frame_contract.features.shadows_enabled remained true after LLM command"
}
if ([Math]::Abs([double]$report.frame_contract.lighting.exposure - 1.35) -gt 0.02) {
    Add-Failure "frame_contract.lighting.exposure was $($report.frame_contract.lighting.exposure), expected 1.35"
}
if (-not [bool]$report.frame_contract.features.fog_enabled) {
    Add-Failure "frame_contract.features.fog_enabled was not true after LLM command"
}
if ([Math]::Abs([double]$report.frame_contract.lighting.fog_density - 0.031) -gt 0.005) {
    Add-Failure "frame_contract.lighting.fog_density was $($report.frame_contract.lighting.fog_density), expected 0.031"
}
if ([string]$report.frame_contract.lighting.rig_id -ne "studio_three_point") {
    Add-Failure "lighting rig id was '$($report.frame_contract.lighting.rig_id)', expected studio_three_point"
}
if ([string]$report.frame_contract.lighting.rig_source -ne "renderer_rig") {
    Add-Failure "lighting rig source was '$($report.frame_contract.lighting.rig_source)', expected renderer_rig"
}

if ($failures.Count -gt 0) {
    Write-Host "LLM renderer command smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "LLM renderer command smoke passed." -ForegroundColor Green
Write-Host ("  exposure={0:N2} shadows={1} fog_density={2:N3} rig={3}/{4}" -f `
    [double]$report.frame_contract.lighting.exposure,
    [bool]$report.frame_contract.features.shadows_enabled,
    [double]$report.frame_contract.lighting.fog_density,
    [string]$report.frame_contract.lighting.rig_id,
    [string]$report.frame_contract.lighting.rig_source)
Write-Host "  logs=$LogDir"
exit 0
