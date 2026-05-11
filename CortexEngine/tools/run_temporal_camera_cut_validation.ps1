param(
    [int]$SmokeFrames = 96,
    [int]$CutFrame = 20,
    [string]$InitialCameraBookmark = "hero",
    [string]$CutCameraBookmark = "reflection_closeup",
    [double]$MaxGpuFrameMs = 16.7,
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
    $runId = "temporal_camera_cut_{0}_{1}_{2}" -f `
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

$exeWorkingDir = Split-Path -Parent $exe
function Invoke-CortexEngine([string[]]$Arguments) {
    Push-Location $script:exeWorkingDir
    try {
        $output = & $script:exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        $output | ForEach-Object { Write-Host $_ }
        return [int]$exitCode
    } finally {
        Pop-Location
    }
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath, $visualPath

$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "50"
$env:CORTEX_CAMERA_CUT_FRAME = [string]$CutFrame
$env:CORTEX_CAMERA_CUT_BOOKMARK = $CutCameraBookmark

try {
    $exitCode = Invoke-CortexEngine @(
        "--scene", "rt_showcase",
        "--camera-bookmark", $InitialCameraBookmark,
        "--mode=default",
        "--no-llm",
        "--no-dreamer",
        "--no-launcher",
        "--smoke-frames=$SmokeFrames",
        "--exit-after-visual-validation"
    )
} finally {
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_CUT_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_CUT_BOOKMARK -ErrorAction SilentlyContinue
}

if ($exitCode -ne 0) {
    throw "CortexEngine temporal camera cut process failed with exit code $exitCode"
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

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Get-History([object]$ReportObject, [string]$Name) {
    if ($null -eq $ReportObject.frame_contract.histories) {
        return $null
    }
    foreach ($history in $ReportObject.frame_contract.histories) {
        if ([string]$history.name -eq $Name) {
            return $history
        }
    }
    return $null
}

if ([string]$report.scene -ne "rt_showcase") {
    Add-Failure "expected rt_showcase scene but report scene was '$($report.scene)'"
}
if ($null -eq $report.camera -or -not [bool]$report.camera.active) {
    Add-Failure "frame report does not contain an active camera"
} elseif ([string]$report.camera.bookmark -ne $CutCameraBookmark) {
    Add-Failure "camera cut did not leave the active bookmark at '$CutCameraBookmark' (actual '$($report.camera.bookmark)')"
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

foreach ($historyName in @("rt_shadow_mask", "rt_reflection", "rt_gi")) {
    $history = Get-History $report $historyName
    if ($null -eq $history) {
        Add-Failure "missing temporal history '$historyName'"
        continue
    }
    if ([string]$history.last_reset_reason -ne "camera_cut") {
        Add-Failure "$historyName last_reset_reason expected camera_cut but was '$($history.last_reset_reason)'"
    }
    if ([uint64]$history.last_invalidated_frame -lt [uint64]$CutFrame) {
        Add-Failure "$historyName last_invalidated_frame $($history.last_invalidated_frame) is before cut frame $CutFrame"
    }
    if (-not [bool]$history.resource_valid) {
        Add-Failure "$historyName resource is not valid after camera cut recovery"
    }
    if (-not [bool]$history.seeded) {
        Add-Failure "$historyName was not reseeded after camera cut"
    }
}

if ($null -eq $report.visual_validation -or -not [bool]$report.visual_validation.captured) {
    Add-Failure "visual_validation capture missing after camera cut"
} elseif ($null -eq $report.visual_validation.image_stats) {
    Add-Failure "visual_validation image_stats missing after camera cut"
} else {
    if (-not [bool]$report.visual_validation.image_stats.valid) {
        Add-Failure "visual_validation image_stats reported invalid capture"
    }
    if ([double]$report.visual_validation.image_stats.avg_luma -le 0.0) {
        Add-Failure "screenshot avg luma is non-positive after camera cut"
    }
}

if (-not (Test-Path $visualPath)) {
    Add-Failure "visual validation image was not written: $visualPath"
}

if ($failures.Count -gt 0) {
    Write-Host "Temporal camera cut validation failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir"
    exit 1
}

Write-Host "Temporal camera cut validation passed." -ForegroundColor Green
$reflectionHistory = Get-History $report "rt_reflection"
Write-Host ("  frames={0} cut_frame={1} camera={2} gpu_ms={3:N3}" -f `
    $report.smoke_automation.total_frames,
    $CutFrame,
    $report.camera.bookmark,
    [double]$report.gpu_frame_ms)
Write-Host ("  rt_reflection_reset={0} invalidated_frame={1}" -f `
    $reflectionHistory.last_reset_reason,
    $reflectionHistory.last_invalidated_frame)
Write-Host "  logs=$activeLogDir"
