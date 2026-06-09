param(
    [string]$Scene = "rain_glass_pavilion",
    [string]$ExpectedReportScene = "",
    [string]$CameraBookmark = "roof_under_glass",
    [int]$CaptureStartFrame = 80,
    [int]$CaptureCount = 30,
    [int]$MotionFrames = 140,
    [double]$MotionLookAmplitude = 1.20,
    [double]$MotionLookCycles = 5.0,
    [double]$FixedDeltaTime = 0.016666667,
    [double]$MaxMeanAbsLumaDelta = 12.0,
    [double]$MaxChangedPixelRatio = 0.22,
    [double]$MaxLargeChangedPixelRatio = 0.08,
    [string]$LogDir = "",
    [switch]$IsolatedLogs,
    [switch]$UseMouseJitterPath,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$baseLogDir = Join-Path $root "build/bin/logs"
$activeLogDir = $baseLogDir
if (-not [string]::IsNullOrWhiteSpace($LogDir)) {
    $activeLogDir = $LogDir
} elseif ($IsolatedLogs) {
    $runId = "{0}_mouse_jiggle_{1}_{2}_{3}" -f `
        $Scene,
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
}

if ($CaptureCount -lt 2) {
    throw "CaptureCount must be at least 2 for adjacent-frame comparison."
}

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}

if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue `
    (Join-Path $activeLogDir "visual_validation_frame_*.bmp"),
    (Join-Path $activeLogDir "frame_report_last.json"),
    (Join-Path $activeLogDir "frame_report_shutdown.json"),
    (Join-Path $activeLogDir "engine_stdout.txt"),
    (Join-Path $activeLogDir "scene_mouse_jiggle_summary.json"),
    (Join-Path $activeLogDir "rain_glass_mouse_jiggle_summary.json")

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
    $largeChanged = 0
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
            if ($delta -gt 10.0) { ++$changed }
            if ($delta -gt 35.0) { ++$largeChanged }
        }
    }

    return [pscustomobject]@{
        valid = $true
        reason = ""
        mean_abs_luma_delta = $sumAbs / [double]$pixelCount
        changed_pixel_ratio = [double]$changed / [double]$pixelCount
        large_changed_pixel_ratio = [double]$largeChanged / [double]$pixelCount
    }
}

$oldEnv = @{
    CORTEX_LOG_DIR = $env:CORTEX_LOG_DIR
    CORTEX_CAPTURE_VISUAL_VALIDATION = $env:CORTEX_CAPTURE_VISUAL_VALIDATION
    CORTEX_VISUAL_VALIDATION_MIN_FRAME = $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME
    CORTEX_VISUAL_VALIDATION_SEQUENCE_COUNT = $env:CORTEX_VISUAL_VALIDATION_SEQUENCE_COUNT
    CORTEX_DISABLE_DEBUG_LAYER = $env:CORTEX_DISABLE_DEBUG_LAYER
    CORTEX_FIXED_DELTA_TIME = $env:CORTEX_FIXED_DELTA_TIME
    CORTEX_CAMERA_MOTION_FRAMES = $env:CORTEX_CAMERA_MOTION_FRAMES
    CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE = $env:CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE
    CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE = $env:CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE
    CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE = $env:CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE
    CORTEX_CAMERA_MOTION_LOOK_CYCLES = $env:CORTEX_CAMERA_MOTION_LOOK_CYCLES
    CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE = $env:CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE
    CORTEX_CAMERA_MOUSE_JITTER_FRAMES = $env:CORTEX_CAMERA_MOUSE_JITTER_FRAMES
    CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE = $env:CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE
    CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE = $env:CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE
    CORTEX_CAMERA_MOUSE_JITTER_CYCLES = $env:CORTEX_CAMERA_MOUSE_JITTER_CYCLES
}

try {
    $env:CORTEX_LOG_DIR = $activeLogDir
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = [string]$CaptureStartFrame
    $env:CORTEX_VISUAL_VALIDATION_SEQUENCE_COUNT = [string]$CaptureCount
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_FIXED_DELTA_TIME = [string]$FixedDeltaTime
    if ($UseMouseJitterPath) {
        Remove-Item Env:\CORTEX_CAMERA_MOTION_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_LOOK_CYCLES -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE -ErrorAction SilentlyContinue
        $env:CORTEX_CAMERA_MOUSE_JITTER_FRAMES = [string]$MotionFrames
        $env:CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE = [string]$MotionLookAmplitude
        $env:CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE = "0.0"
        $env:CORTEX_CAMERA_MOUSE_JITTER_CYCLES = [string]$MotionLookCycles
    } else {
        Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_CYCLES -ErrorAction SilentlyContinue
        $env:CORTEX_CAMERA_MOTION_FRAMES = [string]$MotionFrames
        $env:CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE = "0.0"
        $env:CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE = "0.0"
        $env:CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE = [string]$MotionLookAmplitude
        $env:CORTEX_CAMERA_MOTION_LOOK_CYCLES = [string]$MotionLookCycles
        $env:CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE = "0.0"
    }

    $smokeFrames = [Math]::Max($CaptureStartFrame + $CaptureCount + 8, $MotionFrames + 8)
    $arguments = @(
        "--scene", $Scene,
        "--camera-bookmark", $CameraBookmark,
        "--mode=default",
        "--no-llm",
        "--no-dreamer",
        "--no-launcher",
        "--smoke-frames=$smokeFrames"
    )

    Push-Location (Split-Path -Parent $exe)
    try {
        $output = & $exe @arguments 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -Encoding UTF8 (Join-Path $activeLogDir "engine_stdout.txt")
    } finally {
        Pop-Location
    }
} finally {
    foreach ($key in $oldEnv.Keys) {
        if ($null -eq $oldEnv[$key]) {
            Remove-Item "Env:\$key" -ErrorAction SilentlyContinue
        } else {
            Set-Item "Env:\$key" $oldEnv[$key]
        }
    }
}

$failures = New-Object System.Collections.Generic.List[string]
if ($exitCode -ne 0) {
    $failures.Add("engine process failed with exit code $exitCode") | Out-Null
}

$captures = Get-ChildItem -Path $activeLogDir -Filter "visual_validation_frame_*.bmp" |
    Sort-Object Name
if ($captures.Count -ne $CaptureCount) {
    $failures.Add("expected $CaptureCount continuous captures, found $($captures.Count)") | Out-Null
}

$comparisons = New-Object System.Collections.Generic.List[object]
for ($i = 0; $i -lt ($captures.Count - 1); ++$i) {
    $diff = Measure-BmpLumaDifference $captures[$i].FullName $captures[$i + 1].FullName
    if (-not [bool]$diff.valid) {
        $failures.Add("$($captures[$i].Name)->$($captures[$i + 1].Name) diff failed: $($diff.reason)") | Out-Null
        continue
    }
    if ([double]$diff.mean_abs_luma_delta -gt $MaxMeanAbsLumaDelta) {
        $failures.Add("$($captures[$i].Name)->$($captures[$i + 1].Name) mean_abs_luma_delta=$($diff.mean_abs_luma_delta) exceeds $MaxMeanAbsLumaDelta") | Out-Null
    }
    if ([double]$diff.changed_pixel_ratio -gt $MaxChangedPixelRatio) {
        $failures.Add("$($captures[$i].Name)->$($captures[$i + 1].Name) changed_pixel_ratio=$($diff.changed_pixel_ratio) exceeds $MaxChangedPixelRatio") | Out-Null
    }
    if ([double]$diff.large_changed_pixel_ratio -gt $MaxLargeChangedPixelRatio) {
        $failures.Add("$($captures[$i].Name)->$($captures[$i + 1].Name) large_changed_pixel_ratio=$($diff.large_changed_pixel_ratio) exceeds $MaxLargeChangedPixelRatio") | Out-Null
    }
    $comparisons.Add([pscustomobject]@{
        from = $captures[$i].Name
        to = $captures[$i + 1].Name
        mean_abs_luma_delta = [double]$diff.mean_abs_luma_delta
        changed_pixel_ratio = [double]$diff.changed_pixel_ratio
        large_changed_pixel_ratio = [double]$diff.large_changed_pixel_ratio
    }) | Out-Null
}

$reportPath = Join-Path $activeLogDir "frame_report_last.json"
if (-not (Test-Path $reportPath)) {
    $reportPath = Join-Path $activeLogDir "frame_report_shutdown.json"
}
$report = $null
if (Test-Path $reportPath) {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $expectedScene = if ([string]::IsNullOrWhiteSpace($ExpectedReportScene)) { $Scene } else { $ExpectedReportScene }
    if ([string]$report.scene -ne $expectedScene) {
        $failures.Add("expected report scene $expectedScene, got '$($report.scene)'") | Out-Null
    }
    if ($report.health_warnings.Count -gt 0) {
        $failures.Add("health warnings: $($report.health_warnings -join ', ')") | Out-Null
    }
    if ($report.frame_contract.warnings.Count -gt 0) {
        $failures.Add("frame contract warnings: $($report.frame_contract.warnings -join ', ')") | Out-Null
    }
    if ([int64]$report.frame_contract.materials.descriptor_tables_missing_after_prepare -ne 0) {
        $failures.Add("material descriptor tables missing after prepare: $($report.frame_contract.materials.descriptor_tables_missing_after_prepare)") | Out-Null
    }
    if ([int64]$report.frame_contract.materials.descriptor_refresh_failures -ne 0) {
        $failures.Add("material descriptor refresh failures: $($report.frame_contract.materials.descriptor_refresh_failures)") | Out-Null
    }
    if ([int64]$report.frame_contract.materials.validation_issues.blend_transmission -ne 0) {
        $failures.Add("blend+transmission materials still admitted: $($report.frame_contract.materials.validation_issues.blend_transmission)") | Out-Null
    }
} else {
    $failures.Add("engine did not write a frame report") | Out-Null
}

$maxMean = 0.0
$maxChanged = 0.0
$maxLarge = 0.0
foreach ($comparison in $comparisons) {
    $maxMean = [Math]::Max($maxMean, [double]$comparison.mean_abs_luma_delta)
    $maxChanged = [Math]::Max($maxChanged, [double]$comparison.changed_pixel_ratio)
    $maxLarge = [Math]::Max($maxLarge, [double]$comparison.large_changed_pixel_ratio)
}

$summary = [pscustomobject][ordered]@{
    schema = "cortex.scene_mouse_jiggle_stability.v1"
    scene = $Scene
    expected_report_scene = $ExpectedReportScene
    camera_bookmark = $CameraBookmark
    capture_start_frame = $CaptureStartFrame
    capture_count = $CaptureCount
    motion_frames = $MotionFrames
    motion_look_amplitude = $MotionLookAmplitude
    motion_look_cycles = $MotionLookCycles
    mouse_jitter_path = [bool]$UseMouseJitterPath
    fixed_delta_time = $FixedDeltaTime
    capture_count_written = $captures.Count
    max_mean_abs_luma_delta = $maxMean
    max_changed_pixel_ratio = $maxChanged
    max_large_changed_pixel_ratio = $maxLarge
    comparisons = @($comparisons.ToArray())
    failures = @($failures.ToArray())
    logs_dir = $activeLogDir
}
$summaryPath = Join-Path $activeLogDir "scene_mouse_jiggle_summary.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $summaryPath

if ($failures.Count -gt 0) {
    Write-Host "Scene mouse-jiggle stability failed:"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    Write-Host "summary=$summaryPath"
    Write-Host "logs=$activeLogDir"
    exit 1
}

Write-Host "Scene mouse-jiggle stability passed"
Write-Host (" captures={0} max_mean={1:N4} max_changed={2:N4} max_large={3:N4}" -f `
    $captures.Count, $maxMean, $maxChanged, $maxLarge)
Write-Host "summary=$summaryPath"
Write-Host "logs=$activeLogDir"
