param(
    [int]$SmokeFrames = 110,
    [int]$MotionFrames = 80,
    [double]$MinCameraDelta = 0.35,
    [double]$MaxGpuFrameMs = 16.7,
    [double]$MinVisualAvgLuma = 45.0,
    [double]$MinVisualNonBlackRatio = 0.90,
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
    $runId = "camera_motion_stability_{0}_{1}_{2}" -f `
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
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = [string]([Math]::Min($SmokeFrames - 5, $MotionFrames))
$env:CORTEX_CAMERA_MOTION_FRAMES = [string]$MotionFrames
$env:CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE = "0.85"
$env:CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE = "0.55"
$env:CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE = "0.35"

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
    Remove-Item Env:\CORTEX_CAMERA_MOTION_FRAMES -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if ($exitCode -ne 0) {
    Add-Failure "camera motion stability process failed with exit code $exitCode"
}
if (-not (Test-Path $reportPath)) {
    if (Test-Path $shutdownReportPath) {
        $reportPath = $shutdownReportPath
    } else {
        Add-Failure "Expected frame report was not written: $reportPath"
    }
}

if ($failures.Count -eq 0) {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if ([string]$report.scene -ne "glass_water_courtyard") {
        Add-Failure "expected glass_water_courtyard scene but report scene was '$($report.scene)'"
    }
    if ($null -eq $report.camera -or -not [bool]$report.camera.active) {
        Add-Failure "frame report does not contain an active camera"
    } else {
        $start = [pscustomobject]@{ x = 0.0; y = 2.55; z = -8.8 }
        $pos = $report.camera.position
        $dx = [double]$pos.x - [double]$start.x
        $dy = [double]$pos.y - [double]$start.y
        $dz = [double]$pos.z - [double]$start.z
        $cameraDelta = [Math]::Sqrt(($dx * $dx) + ($dy * $dy) + ($dz * $dz))
        if ($cameraDelta -lt $MinCameraDelta) {
            Add-Failure "camera only moved $cameraDelta world units, expected >= $MinCameraDelta"
        }
    }

    $motion = $report.smoke_automation.camera_motion
    if ($null -eq $motion) {
        Add-Failure "smoke_automation.camera_motion report is missing"
    } else {
        if (-not [bool]$motion.enabled -or -not [bool]$motion.initialized -or -not [bool]$motion.applied) {
            Add-Failure "camera motion automation did not run: enabled=$($motion.enabled) initialized=$($motion.initialized) applied=$($motion.applied)"
        }
        if ([uint64]$motion.frames -ne [uint64]$MotionFrames) {
            Add-Failure "camera motion frames reported $($motion.frames), expected $MotionFrames"
        }
    }

    if ($report.health_warnings.Count -gt 0) {
        Add-Failure "health warnings were reported: $($report.health_warnings -join ', ')"
    }
    if ($report.frame_contract.warnings.Count -gt 0) {
        Add-Failure "frame contract warnings were reported: $($report.frame_contract.warnings -join ', ')"
    }
    if ([double]$report.gpu_frame_ms -gt $MaxGpuFrameMs) {
        Add-Failure "GPU frame time exceeded budget: $($report.gpu_frame_ms) ms > $MaxGpuFrameMs ms"
    }
    if ($null -eq $report.visual_validation -or -not [bool]$report.visual_validation.captured) {
        Add-Failure "visual validation capture missing after camera motion"
    } elseif ($null -eq $report.visual_validation.image_stats -or
              -not [bool]$report.visual_validation.image_stats.valid) {
        Add-Failure "visual validation image stats are invalid after camera motion"
    } else {
        $stats = $report.visual_validation.image_stats
        if ([double]$stats.avg_luma -lt $MinVisualAvgLuma) {
            Add-Failure "visual avg_luma=$($stats.avg_luma), expected >= $MinVisualAvgLuma"
        }
        if ([double]$stats.nonblack_ratio -lt $MinVisualNonBlackRatio) {
            Add-Failure "visual nonblack_ratio=$($stats.nonblack_ratio), expected >= $MinVisualNonBlackRatio"
        }
    }
    if (-not (Test-Path $visualPath)) {
        Add-Failure "visual validation image missing: $visualPath"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Camera motion stability smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Camera motion stability smoke passed" -ForegroundColor Green
Write-Host " logs=$activeLogDir"
