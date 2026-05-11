param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 75,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "particle_disabled_zero_cost_{0}_{1}_{2}" -f `
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
$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "25"

Push-Location (Split-Path -Parent $exe)
try {
    $output = & $exe `
        "--scene" "effects_showcase" `
        "--graphics-preset" "safe_startup" `
        "--mode=conservative" `
        "--no-llm" `
        "--no-dreamer" `
        "--no-launcher" `
        "--smoke-frames=$SmokeFrames" `
        "--exit-after-visual-validation" 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -Encoding UTF8 (Join-Path $LogDir "engine_stdout.txt")
} finally {
    Pop-Location
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) }

$reportPath = Join-Path $LogDir "frame_report_last.json"
if ($exitCode -ne 0) {
    Add-Failure "particle-disabled smoke exited with code $exitCode"
} elseif (-not (Test-Path $reportPath)) {
    Add-Failure "particle-disabled smoke did not write frame report: $reportPath"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $particles = $report.frame_contract.particles
    if ($null -eq $particles) {
        Add-Failure "frame_contract.particles is missing"
    } else {
        if ([bool]$particles.enabled) { Add-Failure "particles.enabled is true" }
        if ([bool]$particles.planned) { Add-Failure "particles.planned is true" }
        if ([bool]$particles.executed) { Add-Failure "particles.executed is true" }
        if ([double]$particles.density_scale -ne 0.0) { Add-Failure "particles.density_scale is $($particles.density_scale), expected 0" }
        if ([int]$particles.live_particles -ne 0) { Add-Failure "particles.live_particles is $($particles.live_particles), expected 0" }
        if ([int]$particles.submitted_instances -ne 0) { Add-Failure "particles.submitted_instances is $($particles.submitted_instances), expected 0" }
        if ([int]$particles.instance_buffer_bytes -ne 0) { Add-Failure "particles.instance_buffer_bytes is $($particles.instance_buffer_bytes), expected 0" }
    }
    if ([int]$report.frame_contract.draw_counts.particle_instances -ne 0) {
        Add-Failure "draw_counts.particle_instances is $($report.frame_contract.draw_counts.particle_instances), expected 0"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Particle disabled zero-cost test failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Particle disabled zero-cost test passed logs=$LogDir" -ForegroundColor Green
