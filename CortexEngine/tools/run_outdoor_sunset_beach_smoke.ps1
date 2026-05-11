param(
    [int]$SmokeFrames = 180,
    [double]$MaxGpuFrameMs = 16.7,
    [double]$MinVisualAvgLuma = 45.0,
    [double]$MaxVisualAvgLuma = 205.0,
    [double]$MinVisualNonBlackRatio = 0.90,
    [double]$MaxVisualSaturatedRatio = 0.18,
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
    $runId = "outdoor_sunset_beach_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
    $env:CORTEX_LOG_DIR = $activeLogDir
}

$reportPath = Join-Path $activeLogDir "frame_report_last.json"
$shutdownReportPath = Join-Path $activeLogDir "frame_report_shutdown.json"
$visualPath = Join-Path $activeLogDir "visual_validation_rt_showcase.bmp"
$runLogPath = Join-Path $activeLogDir "cortex_last_run.txt"

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $visualPath, $runLogPath

$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

Push-Location (Split-Path -Parent $exe)
try {
    $output = & $exe `
        "--scene" "outdoor_sunset_beach" `
        "--camera-bookmark" "hero" `
        "--environment" "sunset_courtyard" `
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
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if ($exitCode -ne 0) {
    Add-Failure "Outdoor Sunset Beach smoke process failed with exit code $exitCode"
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
    $rt = $report.frame_contract.ray_tracing
    $atmosphere = $report.frame_contract.atmosphere

    if ([string]$report.scene -ne "outdoor_sunset_beach") {
        Add-Failure "expected outdoor_sunset_beach scene but report scene was '$($report.scene)'"
    }
    if (-not [bool]$report.camera.active -or [string]$report.camera.bookmark -ne "hero") {
        Add-Failure "Outdoor Sunset Beach did not report the hero camera bookmark"
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
    if ([string]$report.frame_contract.environment.active -ne "sunset_courtyard") {
        Add-Failure "environment is '$($report.frame_contract.environment.active)', expected sunset_courtyard"
    }
    if ([string]$report.frame_contract.lighting.rig_id -ne "sunset_rim") {
        Add-Failure "lighting rig is '$($report.frame_contract.lighting.rig_id)', expected sunset_rim"
    }
    if (-not [bool]$report.frame_contract.executed_features.fog_enabled) {
        Add-Failure "fog is not enabled"
    }
    if ($null -eq $atmosphere) {
        Add-Failure "atmosphere contract section is missing"
    } else {
        if (-not [bool]$atmosphere.enabled -or -not [bool]$atmosphere.fog_enabled) {
            Add-Failure "atmosphere fog is not active"
        }
        if (-not [bool]$atmosphere.height_fog_enabled -or -not [bool]$atmosphere.depth_aware_fog) {
            Add-Failure "atmosphere height/depth-aware fog is not active"
        }
        if (-not [bool]$atmosphere.environment_matched_fog -or
            [string]$atmosphere.fog_color_source -ne "ambient_sun_environment") {
            Add-Failure "atmosphere fog is not environment-matched"
        }
        if (-not [bool]$atmosphere.volumetric_shafts_enabled -or
            -not [bool]$atmosphere.depth_aware_shafts -or
            [string]$atmosphere.shaft_occlusion_source -ne "scene_depth_radial_samples") {
            Add-Failure "depth-aware volumetric shaft contract is not active"
        }
        if ([double]$atmosphere.fog_density -le 0.0 -or [double]$atmosphere.god_ray_intensity -le 0.0) {
            Add-Failure "atmosphere density/god-ray intensity is not positive"
        }
    }
    if (-not [bool]$rt.reflection_dispatch_ready) {
        Add-Failure "RT reflection dispatch is not ready: $($rt.reflection_dispatch_reason)"
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

    if ([int]$renderables.water_depth_tested_no_write -lt 1) {
        Add-Failure "water depth-tested renderables are missing"
    }
    if ([int]$drawCounts.water_draws -lt 1) {
        Add-Failure "water draw count is $($drawCounts.water_draws), expected >= 1"
    }
    if ($null -eq $water -or [double]$water.wave_amplitude -le 0.0) {
        Add-Failure "water contract is missing or inactive"
    }
    if ([int]$materials.surface_water -lt 1) {
        Add-Failure "surface_water=$($materials.surface_water), expected >= 1"
    }
    if ([int]$materials.surface_wood -lt 2) {
        Add-Failure "surface_wood=$($materials.surface_wood), expected palm/wood coverage"
    }
    if ([int]$materials.surface_emissive -lt 1) {
        Add-Failure "surface_emissive=$($materials.surface_emissive), expected sunset glow coverage"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Outdoor Sunset Beach smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Outdoor Sunset Beach smoke passed" -ForegroundColor Green
Write-Host (" logs={0}" -f $activeLogDir)
Write-Host (" gpu_ms={0:N3}/{1:N1} luma={2:N2} water_draws={3} surfaces water/wood/emissive={4}/{5}/{6}" -f `
    [double]$report.gpu_frame_ms,
    $MaxGpuFrameMs,
    [double]$report.visual_validation.image_stats.avg_luma,
    [int]$report.frame_contract.draw_counts.water_draws,
    [int]$report.frame_contract.materials.surface_water,
    [int]$report.frame_contract.materials.surface_wood,
    [int]$report.frame_contract.materials.surface_emissive)
exit 0
