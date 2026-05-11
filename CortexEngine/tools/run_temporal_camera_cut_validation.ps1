param(
    [int]$SmokeFrames = 96,
    [int]$CutFrame = 20,
    [string]$InitialCameraBookmark = "hero",
    [string]$CutCameraBookmark = "reflection_closeup",
    [double]$MaxGpuFrameMs = 16.7,
    [double]$MaxCutVsCleanMeanAbsLumaDelta = 4.0,
    [double]$MaxCutVsCleanChangedPixelRatio = 0.12,
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
$cutVisualPath = Join-Path $activeLogDir "visual_validation_rt_showcase_after_cut.bmp"
$cleanVisualPath = Join-Path $activeLogDir "visual_validation_rt_showcase_clean_destination.bmp"
$cleanReportPath = Join-Path $activeLogDir "frame_report_clean_destination.json"

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
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath, $visualPath, $cutVisualPath, $cleanVisualPath, $cleanReportPath

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

function Get-BmpInfo([string]$Path) {
    if (-not (Test-Path $Path)) {
        return [pscustomobject]@{ valid = $false; reason = "missing_file" }
    }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 54 -or $bytes[0] -ne 0x42 -or $bytes[1] -ne 0x4d) {
        return [pscustomobject]@{ valid = $false; reason = "not_bmp" }
    }

    $dataOffset = [BitConverter]::ToUInt32($bytes, 10)
    $width = [BitConverter]::ToInt32($bytes, 18)
    $heightSigned = [BitConverter]::ToInt32($bytes, 22)
    $planes = [BitConverter]::ToUInt16($bytes, 26)
    $bpp = [BitConverter]::ToUInt16($bytes, 28)
    $compression = [BitConverter]::ToUInt32($bytes, 30)
    if ($width -le 0 -or $heightSigned -eq 0 -or $planes -ne 1 -or
        ($bpp -ne 24 -and $bpp -ne 32) -or $compression -ne 0) {
        return [pscustomobject]@{ valid = $false; reason = "unsupported_bmp" }
    }

    $height = [Math]::Abs($heightSigned)
    $bytesPerPixel = [int]($bpp / 8)
    $rowStride = [int]([Math]::Floor(((($width * $bytesPerPixel) + 3) / 4.0))) * 4
    $requiredSize = [int64]$dataOffset + ([int64]$rowStride * [int64]$height)
    if ($dataOffset -ge $bytes.Length -or $requiredSize -gt $bytes.Length) {
        return [pscustomobject]@{ valid = $false; reason = "truncated_pixels" }
    }

    return [pscustomobject]@{
        valid = $true
        reason = ""
        bytes = $bytes
        data_offset = [int]$dataOffset
        width = [int]$width
        height = [int]$height
        bytes_per_pixel = $bytesPerPixel
        row_stride = $rowStride
    }
}

function Measure-BmpLumaDifference([string]$BaselinePath, [string]$CandidatePath) {
    $a = Get-BmpInfo $BaselinePath
    $b = Get-BmpInfo $CandidatePath
    if (-not [bool]$a.valid) {
        return [pscustomobject]@{ valid = $false; reason = "baseline_$($a.reason)" }
    }
    if (-not [bool]$b.valid) {
        return [pscustomobject]@{ valid = $false; reason = "candidate_$($b.reason)" }
    }
    if ($a.width -ne $b.width -or $a.height -ne $b.height -or
        $a.bytes_per_pixel -ne $b.bytes_per_pixel) {
        return [pscustomobject]@{ valid = $false; reason = "dimension_or_format_mismatch" }
    }

    $sumAbs = 0.0
    $changed = 0
    $pixelCount = [int64]$a.width * [int64]$a.height
    for ($y = 0; $y -lt $a.height; ++$y) {
        $rowA = $a.data_offset + ($y * $a.row_stride)
        $rowB = $b.data_offset + ($y * $b.row_stride)
        for ($x = 0; $x -lt $a.width; ++$x) {
            $pa = $rowA + ($x * $a.bytes_per_pixel)
            $pb = $rowB + ($x * $b.bytes_per_pixel)
            $lumaA = (0.2126 * [double]$a.bytes[$pa + 2]) + (0.7152 * [double]$a.bytes[$pa + 1]) + (0.0722 * [double]$a.bytes[$pa])
            $lumaB = (0.2126 * [double]$b.bytes[$pb + 2]) + (0.7152 * [double]$b.bytes[$pb + 1]) + (0.0722 * [double]$b.bytes[$pb])
            $delta = [Math]::Abs($lumaA - $lumaB)
            $sumAbs += $delta
            if ($delta -gt 10.0) {
                ++$changed
            }
        }
    }

    return [pscustomobject]@{
        valid = $true
        reason = ""
        mean_abs_luma_delta = $sumAbs / [double]$pixelCount
        changed_pixel_ratio = [double]$changed / [double]$pixelCount
    }
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
} else {
    Copy-Item -Force $visualPath $cutVisualPath
}

$cutVsCleanDiff = $null
if ($failures.Count -eq 0) {
    Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath, $visualPath

    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "50"

    try {
        $cleanExitCode = Invoke-CortexEngine @(
            "--scene", "rt_showcase",
            "--camera-bookmark", $CutCameraBookmark,
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
    }

    if ($cleanExitCode -ne 0) {
        Add-Failure "clean destination camera run failed with exit code $cleanExitCode"
    } elseif (-not (Test-Path $reportPath)) {
        Add-Failure "clean destination frame report was not written"
    } elseif (-not (Test-Path $visualPath)) {
        Add-Failure "clean destination visual capture was not written"
    } else {
        Copy-Item -Force $reportPath $cleanReportPath
        Copy-Item -Force $visualPath $cleanVisualPath
        $cleanReport = Get-Content $reportPath -Raw | ConvertFrom-Json
        if ($null -eq $cleanReport.camera -or -not [bool]$cleanReport.camera.active -or
            [string]$cleanReport.camera.bookmark -ne $CutCameraBookmark) {
            Add-Failure "clean destination run did not capture bookmark '$CutCameraBookmark'"
        }
        if ($cleanReport.frame_contract.warnings.Count -gt 0) {
            Add-Failure "clean destination frame contract warnings were reported: $($cleanReport.frame_contract.warnings -join ', ')"
        }

        $cutVsCleanDiff = Measure-BmpLumaDifference $cleanVisualPath $cutVisualPath
        if (-not [bool]$cutVsCleanDiff.valid) {
            Add-Failure "cut-vs-clean visual diff failed: $($cutVsCleanDiff.reason)"
        } else {
            if ([double]$cutVsCleanDiff.mean_abs_luma_delta -gt $MaxCutVsCleanMeanAbsLumaDelta) {
                Add-Failure "cut-vs-clean mean luma delta $($cutVsCleanDiff.mean_abs_luma_delta) > $MaxCutVsCleanMeanAbsLumaDelta"
            }
            if ([double]$cutVsCleanDiff.changed_pixel_ratio -gt $MaxCutVsCleanChangedPixelRatio) {
                Add-Failure "cut-vs-clean changed-pixel ratio $($cutVsCleanDiff.changed_pixel_ratio) > $MaxCutVsCleanChangedPixelRatio"
            }
        }
    }
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
if ($null -ne $cutVsCleanDiff -and [bool]$cutVsCleanDiff.valid) {
    Write-Host ("  cut_vs_clean mean={0:N3}/{1:N1} changed={2:N3}/{3:N2}" -f `
        [double]$cutVsCleanDiff.mean_abs_luma_delta,
        $MaxCutVsCleanMeanAbsLumaDelta,
        [double]$cutVsCleanDiff.changed_pixel_ratio,
        $MaxCutVsCleanChangedPixelRatio)
}
Write-Host "  logs=$activeLogDir"
