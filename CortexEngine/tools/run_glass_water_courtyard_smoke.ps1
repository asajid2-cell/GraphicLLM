param(
    [int]$SmokeFrames = 180,
    [double]$MaxGpuFrameMs = 16.7,
    [double]$MinVisualAvgLuma = 55.0,
    [double]$MaxVisualAvgLuma = 190.0,
    [double]$MinVisualCenterLuma = 45.0,
    [double]$MaxVisualCenterLuma = 210.0,
    [double]$MinVisualNonBlackRatio = 0.92,
    [double]$MaxVisualSaturatedRatio = 0.16,
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
    $runId = "glass_water_courtyard_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
    $env:CORTEX_LOG_DIR = $activeLogDir
}

$reportPath = Join-Path $activeLogDir "frame_report_last.json"
$visualPath = Join-Path $activeLogDir "visual_validation_rt_showcase.bmp"
$runLogPath = Join-Path $activeLogDir "cortex_last_run.txt"

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $visualPath, $runLogPath

$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

Push-Location (Split-Path -Parent $exe)
try {
    $output = & $exe `
        "--scene" "glass_water_courtyard" `
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

function Get-FrameContractPass([object]$reportObject, [string]$name) {
    if ($null -eq $reportObject.frame_contract.passes) {
        return $null
    }
    foreach ($pass in $reportObject.frame_contract.passes) {
        if ([string]$pass.name -eq $name) {
            return $pass
        }
    }
    return $null
}

if ($exitCode -ne 0) {
    Add-Failure "Glass Water Courtyard smoke process failed with exit code $exitCode"
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

    if ([string]$report.scene -ne "glass_water_courtyard") {
        Add-Failure "expected glass_water_courtyard scene but report scene was '$($report.scene)'"
    }
    if (-not [bool]$report.camera.active -or [string]$report.camera.bookmark -ne "hero") {
        Add-Failure "Glass Water Courtyard did not report the hero camera bookmark"
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
        if ([double]$stats.center_avg_luma -lt $MinVisualCenterLuma -or [double]$stats.center_avg_luma -gt $MaxVisualCenterLuma) {
            Add-Failure "visual center_avg_luma=$($stats.center_avg_luma), expected [$MinVisualCenterLuma, $MaxVisualCenterLuma]"
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
    if ($null -eq $water) {
        Add-Failure "water contract section is missing"
    } else {
        if ([double]$water.wave_amplitude -le 0.0 -or [double]$water.wave_amplitude -gt 2.0) {
            Add-Failure "water wave amplitude is $($water.wave_amplitude), expected (0, 2]"
        }
        if ([double]$water.wave_length -lt 0.1 -or [double]$water.wave_length -gt 100.0) {
            Add-Failure "water wavelength is $($water.wave_length), expected [0.1, 100]"
        }
        if ([double]$water.wave_speed -le 0.0 -or [double]$water.wave_speed -gt 20.0) {
            Add-Failure "water speed is $($water.wave_speed), expected (0, 20]"
        }
        if ([double]$water.secondary_amplitude -lt 0.0 -or [double]$water.secondary_amplitude -gt 2.0) {
            Add-Failure "water secondary amplitude is $($water.secondary_amplitude), expected [0, 2]"
        }
    }
    $waterPass = Get-FrameContractPass $report "Water"
    if ($null -eq $waterPass) {
        Add-Failure "Water pass record is missing"
    } elseif (-not [bool]$waterPass.executed) {
        Add-Failure "Water pass record was not executed"
    }

    if ([int]$materials.sampled -lt 10) {
        Add-Failure "sampled material count is $($materials.sampled), expected >= 10"
    }
    if ([int]$materials.validation_warnings -ne 0 -or [int]$materials.validation_errors -ne 0) {
        Add-Failure "material validation issues: warnings=$($materials.validation_warnings) errors=$($materials.validation_errors)"
    }
    if ([int]$materials.surface_water -lt 1 -or
        [int]$materials.surface_glass -lt 1 -or
        [int]$materials.surface_mirror -lt 1) {
        Add-Failure "water/glass/mirror surface coverage is incomplete"
    }
    if ([int]$materials.advanced_transmission -lt 1 -or
        [int]$materials.reflection_water -lt 1 -or
        [int]$materials.reflection_transmissive -lt 1) {
        Add-Failure "water/glass reflection and transmission coverage is incomplete"
    }
    if ([int]$materials.blend_transmission -ne 0 -or [int]$materials.metallic_transmission -ne 0) {
        Add-Failure "invalid transmission material combination detected"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Glass Water Courtyard smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Glass Water Courtyard smoke passed" -ForegroundColor Green
Write-Host " logs=$activeLogDir"
