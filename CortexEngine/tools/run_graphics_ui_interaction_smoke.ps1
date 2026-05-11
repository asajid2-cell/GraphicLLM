param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 75,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "graphics_ui_interaction_{0}_{1}_{2}" -f `
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

$settingsPath = Join-Path $LogDir "simulated_graphics_ui_settings.json"
@'
{
  "schema": 1,
  "quality": {
    "preset": "runtime_ui_smoke",
    "dirty_from_ui": true,
    "render_scale": 0.74,
    "taa": true,
    "fxaa": true,
    "gpu_culling": true
  },
  "environment": {
    "id": "studio",
    "ibl_enabled": true,
    "diffuse_intensity": 0.66,
    "specular_intensity": 0.44,
    "background_visible": true,
    "background_exposure": 0.72,
    "background_blur": 0.36
  },
  "lighting": {
    "exposure": 1.31,
    "bloom_intensity": 0.19,
    "warm": 0.36,
    "cool": -0.14,
    "sun_intensity": 4.4,
    "god_ray_intensity": 0.17,
    "area_light_size_scale": 1.42
  },
  "screen_space": {
    "ssao": true,
    "ssao_radius": 0.37,
    "ssao_bias": 0.04,
    "ssao_intensity": 0.31,
    "ssr": false
  },
  "ray_tracing": {
    "enabled": false,
    "reflections": false,
    "gi": false,
    "reflection_denoise_alpha": 0.36,
    "reflection_composition_strength": 0.58
  },
  "atmosphere": {
    "fog": true,
    "fog_density": 0.018,
    "fog_height": 3.5,
    "fog_falloff": 0.55
  },
  "water": {
    "level_y": -0.08,
    "wave_amplitude": 0.16,
    "wave_length": 9.25,
    "wave_speed": 1.35,
    "secondary_amplitude": 0.06
  },
  "particles": {
    "enabled": true,
    "density_scale": 0.43
  },
  "cinematic_post": {
    "enabled": true,
    "bloom_threshold": 1.4,
    "bloom_soft_knee": 0.33,
    "vignette": 0.27,
    "lens_dirt": 0.21
  }
}
'@ | Set-Content -Encoding UTF8 $settingsPath

$env:CORTEX_LOG_DIR = $LogDir
$env:CORTEX_LOAD_USER_GRAPHICS_SETTINGS = "1"
$env:CORTEX_GRAPHICS_SETTINGS_PATH = $settingsPath
$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "25"

Push-Location (Split-Path -Parent $exe)
try {
    $output = & $exe `
        "--scene" "effects_showcase" `
        "--mode=default" `
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
    Remove-Item Env:\CORTEX_LOAD_USER_GRAPHICS_SETTINGS -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_GRAPHICS_SETTINGS_PATH -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) }
function Assert-Near([string]$Name, [double]$Actual, [double]$Expected, [double]$Tolerance) {
    if ([Math]::Abs($Actual - $Expected) -gt $Tolerance) {
        Add-Failure "$Name=$Actual expected $Expected +/- $Tolerance"
    }
}

$reportPath = Join-Path $LogDir "frame_report_last.json"
if ($exitCode -ne 0) {
    Add-Failure "graphics UI interaction smoke exited with code $exitCode"
} elseif (-not (Test-Path $reportPath)) {
    Add-Failure "graphics UI interaction smoke did not write frame report: $reportPath"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $fc = $report.frame_contract
    if ([string]$fc.graphics_preset.id -ne "runtime_ui_smoke") { Add-Failure "graphics preset id was '$($fc.graphics_preset.id)'" }
    if (-not [bool]$fc.graphics_preset.dirty_from_ui) { Add-Failure "dirty_from_ui was false" }
    Assert-Near "render_scale" ([double]$fc.graphics_preset.render_scale) 0.74 0.03
    Assert-Near "exposure" ([double]$fc.lighting.exposure) 1.31 0.03
    Assert-Near "bloom_intensity" ([double]$fc.lighting.bloom_intensity) 0.19 0.03
    Assert-Near "background_exposure" ([double]$fc.environment.background_exposure) 0.72 0.03
    Assert-Near "background_blur" ([double]$fc.environment.background_blur) 0.36 0.03
    Assert-Near "fog_height" ([double]$fc.lighting.fog_height) 3.5 0.08
    Assert-Near "fog_falloff" ([double]$fc.lighting.fog_falloff) 0.55 0.04
    Assert-Near "particle_density" ([double]$fc.particles.density_scale) 0.43 0.03
    if ($null -eq $fc.water) {
        Add-Failure "water contract section was missing"
    } else {
        Assert-Near "water_level" ([double]$fc.water.level_y) -0.08 0.03
        Assert-Near "water_wave_amplitude" ([double]$fc.water.wave_amplitude) 0.16 0.03
        Assert-Near "water_wave_length" ([double]$fc.water.wave_length) 9.25 0.08
        Assert-Near "water_wave_speed" ([double]$fc.water.wave_speed) 1.35 0.04
        Assert-Near "water_secondary_amplitude" ([double]$fc.water.secondary_amplitude) 0.06 0.03
    }
    Assert-Near "warm_color_grade" ([double]$fc.cinematic_post.warm) 0.36 0.03
    Assert-Near "cool_color_grade" ([double]$fc.cinematic_post.cool) -0.14 0.03
    Assert-Near "vignette" ([double]$fc.cinematic_post.vignette) 0.27 0.03
    Assert-Near "lens_dirt" ([double]$fc.cinematic_post.lens_dirt) 0.21 0.03
    if ($null -eq $fc.ray_tracing.rt_reflection_tuning) {
        Add-Failure "rt_reflection_tuning was missing"
    } else {
        Assert-Near "rt_reflection_denoise_alpha" ([double]$fc.ray_tracing.rt_reflection_tuning.denoise_alpha) 0.36 0.03
        Assert-Near "rt_reflection_composition_strength" ([double]$fc.ray_tracing.rt_reflection_tuning.composition_strength) 0.58 0.03
    }
    if ([bool]$fc.features.ray_tracing_enabled) { Add-Failure "ray tracing remained enabled despite settings" }
    if ([bool]$fc.features.ssr_enabled) { Add-Failure "SSR remained enabled despite settings" }
    if (-not [bool]$fc.features.ssao_enabled) { Add-Failure "SSAO was not enabled despite settings" }
}

if ($failures.Count -gt 0) {
    Write-Host "Graphics UI interaction smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Graphics UI interaction smoke passed logs=$LogDir" -ForegroundColor Green
