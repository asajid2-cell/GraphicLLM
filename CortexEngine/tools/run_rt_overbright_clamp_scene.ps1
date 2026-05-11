param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 220,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "rt_overbright_clamp_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$settingsPath = Join-Path $LogDir "rt_overbright_clamp_settings.json"
@'
{
  "schema": 1,
  "quality": {
    "render_scale": 0.85,
    "taa": true,
    "fxaa": true,
    "gpu_culling": true,
    "safe_lighting_rig_on_low_vram": true
  },
  "lighting": {
    "exposure": 1.12,
    "bloom_intensity": 0.12,
    "warm": 0.0,
    "cool": 0.0,
    "sun_intensity": 3.0,
    "god_ray_intensity": 0.42
  },
  "environment": {
    "id": "studio",
    "ibl_enabled": true,
    "ibl_limit_enabled": true,
    "diffuse_intensity": 1.05,
    "specular_intensity": 0.78,
    "background_visible": true,
    "background_exposure": 1.0,
    "background_blur": 0.0
  },
  "ray_tracing": {
    "enabled": true,
    "reflections": true,
    "gi": true,
    "reflection_denoise_alpha": 0.18,
    "reflection_composition_strength": 1.0,
    "reflection_roughness_threshold": 0.72,
    "reflection_history_max_blend": 0.10,
    "reflection_firefly_clamp_luma": 4.0,
    "reflection_signal_scale": 2.0
  },
  "screen_space": {
    "ssao": true,
    "ssao_radius": 0.20,
    "ssao_bias": 0.04,
    "ssao_intensity": 0.22,
    "ssr": true,
    "ssr_max_distance": 60.0,
    "ssr_thickness": 0.18,
    "ssr_strength": 0.7,
    "pcss": true
  },
  "atmosphere": {
    "fog": true,
    "fog_density": 0.012,
    "fog_height": 0.0,
    "fog_falloff": 0.55
  },
  "particles": {
    "enabled": true,
    "density_scale": 1.0
  },
  "cinematic_post": {
    "enabled": true,
    "contrast": 1.0,
    "saturation": 1.0,
    "vignette": 0.18,
    "lens_dirt": 0.08
  }
}
'@ | Set-Content -Encoding UTF8 $settingsPath

$env:CORTEX_RT_REFLECTION_OVERBRIGHT_STRESS = "1"
$env:CORTEX_LOAD_USER_GRAPHICS_SETTINGS = "1"
$env:CORTEX_GRAPHICS_SETTINGS_PATH = $settingsPath
try {
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"),
        "-SmokeFrames", [string]$SmokeFrames,
        "-CameraBookmark", "hero",
        "-MaxRTReflectionSignalOutlierRatio", "0.08",
        "-MaxRTReflectionHistorySignalOutlierRatio", "0.04",
        "-MaxVisualSaturatedRatio", "0.12",
        "-MaxVisualNearWhiteRatio", "0.14",
        "-MaxVisualCenterLuma", "230",
        "-LogDir", $LogDir,
        "-SkipSurfaceDebug"
    )
    if ($NoBuild) {
        $args += "-NoBuild"
    }

    & powershell @args
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Remove-Item Env:\CORTEX_RT_REFLECTION_OVERBRIGHT_STRESS -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_LOAD_USER_GRAPHICS_SETTINGS -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_GRAPHICS_SETTINGS_PATH -ErrorAction SilentlyContinue
}

$reportPath = Join-Path $LogDir "frame_report_last.json"
$runLogPath = Join-Path $LogDir "cortex_last_run.txt"
if (-not (Test-Path $reportPath)) {
    throw "RT overbright clamp test did not write frame report: $reportPath"
}
if (-not (Test-Path $runLogPath)) {
    throw "RT overbright clamp test did not write run log: $runLogPath"
}

$runLog = Get-Content $runLogPath -Raw
if (-not $runLog.Contains("RTShowcase overbright reflection stress enabled")) {
    throw "RT overbright clamp stress geometry was not enabled"
}

$report = Get-Content $reportPath -Raw | ConvertFrom-Json
$rt = $report.frame_contract.ray_tracing
$tuning = $rt.rt_reflection_tuning
if (-not [bool]$rt.reflection_signal_valid -or -not [bool]$rt.reflection_history_signal_valid) {
    throw "RT overbright clamp test requires valid raw and history reflection signal"
}
if ([Math]::Abs(([double]$tuning.firefly_clamp_luma) - 4.0) -gt 0.05) {
    throw "RT overbright clamp test did not apply firefly clamp tuning: $($tuning.firefly_clamp_luma)"
}
if ([Math]::Abs(([double]$tuning.signal_scale) - 2.0) -gt 0.05) {
    throw "RT overbright clamp test did not apply reflection signal scale tuning: $($tuning.signal_scale)"
}
if ([double]$rt.reflection_signal_max_luma -lt 12.0) {
    throw "RT overbright clamp test did not generate a hot raw reflection signal: max_luma=$($rt.reflection_signal_max_luma)"
}
if ([double]$report.visual_validation.image_stats.near_white_ratio -gt 0.14) {
    throw "RT overbright clamp visual near-white ratio is too high: $($report.visual_validation.image_stats.near_white_ratio)"
}
if ([double]$report.visual_validation.image_stats.saturated_ratio -gt 0.12) {
    throw "RT overbright clamp visual saturated ratio is too high: $($report.visual_validation.image_stats.saturated_ratio)"
}

Write-Host ("RT overbright clamp scene passed: raw_max_luma={0} raw_outlier={1} history_outlier={2} near_white={3} logs={4}" -f `
    $rt.reflection_signal_max_luma,
    $rt.reflection_signal_outlier_ratio,
    $rt.reflection_history_signal_outlier_ratio,
    $report.visual_validation.image_stats.near_white_ratio,
    $LogDir) -ForegroundColor Green
