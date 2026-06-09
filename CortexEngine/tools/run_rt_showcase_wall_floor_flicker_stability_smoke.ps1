param(
    [int]$CaptureStartFrame = 70,
    [int]$CaptureCount = 48,
    [int]$MotionFrames = 180,
    [double]$MotionLookAmplitude = 0.025,
    [double]$MotionLookCycles = 12.0,
    [double]$FixedDeltaTime = 0.008333333,
    [double]$MaxMeanAbsLumaDelta = 4.0,
    [double]$MaxChangedPixelRatio = 0.07,
    [double]$MaxLargeChangedPixelRatio = 0.04,
    [string]$CameraBookmark = "reported_wall_floor_flicker",
    [string]$LogDir = "",
    [switch]$IsolatedLogs,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$baseLogDir = Join-Path $root "build/bin/logs"
$activeLogDir = $baseLogDir
if (-not [string]::IsNullOrWhiteSpace($LogDir)) {
    $activeLogDir = $LogDir
} elseif ($IsolatedLogs) {
    $runId = "rt_showcase_wall_floor_flicker_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null

$debugStatePath = Join-Path $root "build/bin/debug_menu_state.json"
$debugStateBackup = Join-Path $root "build/bin/debug_menu_state.json.rt_showcase_wall_floor_bak"
if (Test-Path $debugStateBackup) {
    Remove-Item -Force $debugStateBackup
}

$childArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "run_rain_glass_pavilion_mouse_jiggle_stability.ps1"),
    "-Scene", "rt_showcase",
    "-ExpectedReportScene", "rt_showcase",
    "-CameraBookmark", $CameraBookmark,
    "-CaptureStartFrame", [string]$CaptureStartFrame,
    "-CaptureCount", [string]$CaptureCount,
    "-MotionFrames", [string]$MotionFrames,
    "-MotionLookAmplitude", [string]$MotionLookAmplitude,
    "-MotionLookCycles", [string]$MotionLookCycles,
    "-FixedDeltaTime", [string]$FixedDeltaTime,
    "-MaxMeanAbsLumaDelta", [string]$MaxMeanAbsLumaDelta,
    "-MaxChangedPixelRatio", [string]$MaxChangedPixelRatio,
    "-MaxLargeChangedPixelRatio", [string]$MaxLargeChangedPixelRatio,
    "-UseMouseJitterPath",
    "-LogDir", $activeLogDir
)
if ($NoBuild) {
    $childArgs += "-NoBuild"
}

try {
    if (Test-Path $debugStatePath) {
        Move-Item -Force $debugStatePath $debugStateBackup
    }

    & powershell.exe @childArgs
    $childExit = $LASTEXITCODE
} finally {
    if (Test-Path $debugStateBackup) {
        Move-Item -Force $debugStateBackup $debugStatePath
    }
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if ($childExit -ne 0) {
    Add-Failure "rt_showcase wall/floor child smoke failed with exit code $childExit"
}

$summaryPath = Join-Path $activeLogDir "scene_mouse_jiggle_summary.json"
$reportPath = Join-Path $activeLogDir "frame_report_last.json"
if (-not (Test-Path $summaryPath)) {
    Add-Failure "expected scene mouse-jiggle summary was not written: $summaryPath"
}
if (-not (Test-Path $reportPath)) {
    $reportPath = Join-Path $activeLogDir "frame_report_shutdown.json"
}
if (-not (Test-Path $reportPath)) {
    Add-Failure "expected frame report was not written for RT Showcase wall/floor flicker gate"
}

if (Test-Path $summaryPath) {
    $summary = Get-Content $summaryPath -Raw | ConvertFrom-Json
    if ([string]$summary.scene -ne "rt_showcase") {
        Add-Failure "expected scene 'rt_showcase' but summary scene was '$($summary.scene)'"
    }
    if ([string]$summary.camera_bookmark -ne $CameraBookmark) {
        Add-Failure "expected camera bookmark '$CameraBookmark' but summary camera_bookmark was '$($summary.camera_bookmark)'"
    }
}

if (Test-Path $reportPath) {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if ([string]$report.camera.bookmark -ne $CameraBookmark) {
        Add-Failure "frame report was not aimed at $CameraBookmark; bookmark was '$($report.camera.bookmark)'"
    }
    if (-not [bool]$report.renderer.ibl_enabled) {
        Add-Failure "RT Showcase wall/floor gate must reproduce the old office/studio IBL path; IBL was disabled"
    }
    if (-not [bool]$report.frame_contract.environment.image_based_lighting_textures_bound) {
        Add-Failure "RT Showcase wall/floor gate expected bound old office/studio IBL textures"
    }
    if ([double]$report.frame_contract.lighting.ibl_diffuse_intensity -le 0.0 -or
        [double]$report.frame_contract.lighting.ibl_specular_intensity -le 0.0) {
        Add-Failure "RT Showcase wall/floor gate expected positive IBL intensity"
    }
    if (-not [bool]$report.renderer.taa_enabled) {
        Add-Failure "RT Showcase wall/floor gate should run with TAA enabled so the release path is covered"
    }
    if (-not [bool]$report.renderer.shadows_enabled) {
        Add-Failure "RT Showcase wall/floor gate must keep shadows enabled; disabling shadows masks the reported flicker"
    }
    if (-not [bool]$report.frame_contract.environment.background_visible) {
        Add-Failure "RT Showcase wall/floor gate must keep the old office/studio IBL visibly present; hiding it masks the reported issue"
    }
    if ([bool]$report.governors.perf_scale_reduced) {
        Add-Failure "RT Showcase wall/floor gate must not let the perf governor resize render targets during mouse-look"
    }
    if ([Math]::Abs([double]$report.renderer.render_scale - 0.85) -gt 0.002) {
        Add-Failure "RT Showcase wall/floor gate expected stable render_scale 0.85, got $($report.renderer.render_scale)"
    }
    if ([bool]$report.frame_contract.cinematic_post.motion_blur_enabled -or
        [double]$report.frame_contract.cinematic_post.motion_blur -ne 0.0) {
        Add-Failure "RT Showcase wall/floor gate expects interactive motion blur disabled"
    }
    if ([bool]$report.frame_contract.cinematic_post.depth_of_field_enabled -or
        [double]$report.frame_contract.cinematic_post.depth_of_field -ne 0.0) {
        Add-Failure "RT Showcase wall/floor gate expects interactive depth of field disabled"
    }
    foreach ($history in $report.frame_contract.histories) {
        if ([string]$history.last_reset_reason -eq "resource_recreated") {
            Add-Failure "RT Showcase wall/floor gate found temporal history '$($history.name)' reset by resource_recreated"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "RT Showcase wall/floor flicker stability smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "summary=$summaryPath" -ForegroundColor Red
    Write-Host "logs=$activeLogDir" -ForegroundColor Red
    exit 1
}

Write-Host "RT Showcase wall/floor flicker stability smoke passed" -ForegroundColor Green
Write-Host " summary=$summaryPath"
Write-Host " logs=$activeLogDir"
