param(
    [int]$SmokeFrames = 190,
    [double]$MaxGpuFrameMs = 18.5,
    [double]$MinVisualAvgLuma = 35.0,
    [double]$MaxVisualAvgLuma = 215.0,
    [double]$MinVisualNonBlackRatio = 0.88,
    [double]$MaxVisualSaturatedRatio = 0.22,
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
    $runId = "liquid_gallery_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
    $env:CORTEX_LOG_DIR = $activeLogDir
}

$reportPath = Join-Path $activeLogDir "frame_report_last.json"
$shutdownReportPath = Join-Path $activeLogDir "frame_report_shutdown.json"
$visualPath = Join-Path $activeLogDir "visual_validation_rt_showcase.bmp"

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $visualPath

$oldCapture = $env:CORTEX_CAPTURE_VISUAL_VALIDATION
$oldDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER
$oldMinFrame = $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME
try {
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

    Push-Location (Split-Path -Parent $exe)
    try {
        $output = & $exe `
            "--scene" "liquid_gallery" `
            "--camera-bookmark" "hero" `
            "--environment" "warm_gallery" `
            "--graphics-preset" "release_showcase" `
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
    }
} finally {
    if ($null -eq $oldCapture) { Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue } else { $env:CORTEX_CAPTURE_VISUAL_VALIDATION = $oldCapture }
    if ($null -eq $oldDebugLayer) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $oldDebugLayer }
    if ($null -eq $oldMinFrame) { Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue } else { $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = $oldMinFrame }
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message) | Out-Null
}

if ($exitCode -ne 0) {
    Add-Failure "Liquid Gallery smoke process failed with exit code $exitCode"
}
if (-not (Test-Path $reportPath) -and (Test-Path $shutdownReportPath)) {
    $reportPath = $shutdownReportPath
}
if (-not (Test-Path $reportPath)) {
    Add-Failure "Expected frame report was not written: $reportPath"
}

if ($failures.Count -eq 0) {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $materials = $report.frame_contract.materials
    $renderables = $report.frame_contract.renderables
    $drawCounts = $report.frame_contract.draw_counts
    $water = $report.frame_contract.water

    if ([string]$report.scene -ne "liquid_gallery") {
        Add-Failure "expected liquid_gallery scene but report scene was '$($report.scene)'"
    }
    if (-not [bool]$report.camera.active -or [string]$report.camera.bookmark -ne "hero") {
        Add-Failure "Liquid Gallery did not report the hero camera bookmark"
    }
    if ($report.health_warnings.Count -ne 0) {
        Add-Failure "health_warnings is not empty: $($report.health_warnings -join ', ')"
    }
    if ($report.frame_contract.warnings.Count -ne 0) {
        Add-Failure "frame_contract warnings is not empty: $($report.frame_contract.warnings -join ', ')"
    }
    if ([double]$report.gpu_frame_ms -gt $MaxGpuFrameMs) {
        Add-Failure "gpu_frame_ms is $($report.gpu_frame_ms), budget is <= $MaxGpuFrameMs"
    }
    if ([string]$report.frame_contract.environment.active -ne "warm_gallery") {
        Add-Failure "environment is '$($report.frame_contract.environment.active)', expected warm_gallery"
    }
    if ([string]$report.frame_contract.lighting.rig_id -ne "liquid_gallery") {
        Add-Failure "lighting rig is '$($report.frame_contract.lighting.rig_id)', expected liquid_gallery"
    }

    if (-not [bool]$report.visual_validation.captured -or
        -not [bool]$report.visual_validation.image_stats.valid) {
        Add-Failure "visual validation capture is invalid"
    } else {
        $stats = $report.visual_validation.image_stats
        if ([double]$stats.avg_luma -lt $MinVisualAvgLuma -or [double]$stats.avg_luma -gt $MaxVisualAvgLuma) {
            Add-Failure "visual avg_luma=$($stats.avg_luma), expected [$MinVisualAvgLuma, $MaxVisualAvgLuma]"
        }
        if ([double]$stats.nonblack_ratio -lt $MinVisualNonBlackRatio) {
            Add-Failure "visual nonblack_ratio=$($stats.nonblack_ratio), expected >= $MinVisualNonBlackRatio"
        }
        if ([double]$stats.saturated_ratio -gt $MaxVisualSaturatedRatio) {
            Add-Failure "visual saturated_ratio=$($stats.saturated_ratio), expected <= $MaxVisualSaturatedRatio"
        }
    }
    if (-not (Test-Path $visualPath)) {
        Add-Failure "visual validation image missing: $visualPath"
    }

    if ([int]$renderables.water_depth_tested_no_write -lt 4) {
        Add-Failure "liquid water-depth renderables are $($renderables.water_depth_tested_no_write), expected >= 4"
    }
    if ([int]$drawCounts.water_draws -lt 4) {
        Add-Failure "water draw count is $($drawCounts.water_draws), expected >= 4"
    }
    if ([int]$materials.surface_water -lt 4) {
        Add-Failure "surface_water=$($materials.surface_water), expected >= 4 liquid surfaces"
    }
    if ($null -eq $water) {
        Add-Failure "water contract section is missing"
    } else {
        if ([int]$water.surface_count -lt 4) {
            Add-Failure "water.surface_count=$($water.surface_count), expected >= 4"
        }
        if ([int]$water.water_count -lt 1 -or
            [int]$water.lava_count -lt 1 -or
            [int]$water.honey_count -lt 1 -or
            [int]$water.molasses_count -lt 1) {
            Add-Failure "typed liquid counts water/lava/honey/molasses=$($water.water_count)/$($water.lava_count)/$($water.honey_count)/$($water.molasses_count), expected all >= 1"
        }
        if ([int]$water.emissive_liquid_count -lt 1 -or [double]$water.max_emissive_heat -lt 1.0) {
            Add-Failure "emissive liquid coverage missing: count=$($water.emissive_liquid_count) heat=$($water.max_emissive_heat)"
        }
        if ([double]$water.avg_absorption -le 0.30 -or [double]$water.avg_viscosity -le 0.30) {
            Add-Failure "liquid optical averages are too low: absorption=$($water.avg_absorption) viscosity=$($water.avg_viscosity)"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Liquid Gallery smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Liquid Gallery smoke passed" -ForegroundColor Green
Write-Host (" logs={0}" -f $activeLogDir)
Write-Host (" gpu_ms={0:N3}/{1:N1} luma={2:N2} liquid_counts={3}/{4}/{5}/{6} water_draws={7}" -f `
    [double]$report.gpu_frame_ms,
    $MaxGpuFrameMs,
    [double]$report.visual_validation.image_stats.avg_luma,
    [int]$report.frame_contract.water.water_count,
    [int]$report.frame_contract.water.lava_count,
    [int]$report.frame_contract.water.honey_count,
    [int]$report.frame_contract.water.molasses_count,
    [int]$report.frame_contract.draw_counts.water_draws)
exit 0
