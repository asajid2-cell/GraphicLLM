param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 90,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "graphics_settings_persistence_{0}_{1}_{2}" -f `
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
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

$validPath = Join-Path $LogDir "valid_graphics_settings.json"
$corruptPath = Join-Path $LogDir "corrupt_graphics_settings.json"
@'
{
  "schema": 1,
  "quality": {
    "render_scale": 0.72,
    "taa": true,
    "fxaa": true,
    "gpu_culling": true,
    "safe_lighting_rig_on_low_vram": true
  },
  "lighting": {
    "exposure": 1.37,
    "bloom_intensity": 0.18,
    "warm": 0.42,
    "cool": -0.18,
    "sun_intensity": 3.5,
    "god_ray_intensity": 0.0
  },
  "environment": {
    "id": "studio",
    "ibl_enabled": true,
    "ibl_limit_enabled": true,
    "diffuse_intensity": 1.1,
    "specular_intensity": 0.9,
    "background_visible": true,
    "background_exposure": 1.23,
    "background_blur": 0.37
  },
  "ray_tracing": {
    "enabled": false,
    "reflections": false,
    "gi": false,
    "reflection_denoise_alpha": 0.41,
    "reflection_composition_strength": 0.62
  },
  "screen_space": {
    "ssao": true,
    "ssao_radius": 0.42,
    "ssao_bias": 0.055,
    "ssao_intensity": 0.48,
    "ssr": true,
    "ssr_max_distance": 42.0,
    "ssr_thickness": 0.24,
    "ssr_strength": 0.67,
    "pcss": true
  },
  "atmosphere": {
    "fog": false,
    "fog_density": 0.0,
    "fog_height": 4.25,
    "fog_falloff": 0.72
  },
  "water": {
    "level_y": -0.12,
    "wave_amplitude": 0.19,
    "wave_length": 11.5,
    "wave_speed": 1.7,
    "secondary_amplitude": 0.08,
    "roughness": 0.18,
    "fresnel_strength": 1.45
  },
  "particles": {
    "enabled": false,
    "density_scale": 0.43
  },
  "cinematic_post": {
    "enabled": true,
    "bloom_threshold": 1.2,
    "bloom_soft_knee": 0.4,
    "vignette": 0.31,
    "lens_dirt": 0.22
  }
}
'@ | Set-Content -Encoding UTF8 $validPath
"{ not valid json" | Set-Content -Encoding UTF8 $corruptPath

$exeWorkingDir = Split-Path -Parent $exe
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Invoke-SettingsSmoke([string]$Name, [string]$SettingsPath) {
    $caseLogDir = Join-Path $script:LogDir $Name
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null
    $env:CORTEX_LOG_DIR = $caseLogDir
    $env:CORTEX_LOAD_USER_GRAPHICS_SETTINGS = "1"
    $env:CORTEX_GRAPHICS_SETTINGS_PATH = $SettingsPath
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

    Push-Location $script:exeWorkingDir
    try {
        $output = & $script:exe `
            "--scene" "temporal_validation" `
            "--mode=default" `
            "--no-llm" `
            "--no-dreamer" `
            "--no-launcher" `
            "--smoke-frames=$script:SmokeFrames" `
            "--exit-after-visual-validation" 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "engine_stdout.txt")
    } finally {
        Pop-Location
        Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_LOAD_USER_GRAPHICS_SETTINGS -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_GRAPHICS_SETTINGS_PATH -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
    }

    return [pscustomobject]@{
        name = $Name
        exit_code = $exitCode
        log_dir = $caseLogDir
        report = Join-Path $caseLogDir "frame_report_last.json"
    }
}

$validRun = Invoke-SettingsSmoke "valid_settings" $validPath
if ($validRun.exit_code -ne 0) {
    Add-Failure "valid settings run failed with exit code $($validRun.exit_code). log=$($validRun.log_dir)"
} elseif (-not (Test-Path $validRun.report)) {
    Add-Failure "valid settings run did not write frame report: $($validRun.report)"
} else {
    $report = Get-Content $validRun.report -Raw | ConvertFrom-Json
    $exposure = [double]$report.frame_contract.lighting.exposure
    $renderScale = [double]$report.frame_contract.graphics_preset.render_scale
    if ([Math]::Abs($exposure - 1.37) -gt 0.02) {
        Add-Failure "valid settings exposure was $exposure, expected 1.37"
    }
    if ([Math]::Abs($renderScale - 0.72) -gt 0.02) {
        Add-Failure "valid settings render scale was $renderScale, expected 0.72"
    }
    if ([bool]$report.frame_contract.ray_tracing.enabled) {
        Add-Failure "valid settings did not disable ray tracing"
    }
    $rtTuning = $report.frame_contract.ray_tracing.rt_reflection_tuning
    if ($null -eq $rtTuning) {
        Add-Failure "valid settings frame contract did not include rt_reflection_tuning"
    } else {
        if ([Math]::Abs(([double]$rtTuning.denoise_alpha) - 0.41) -gt 0.03) {
            Add-Failure "valid settings RT reflection denoise alpha was $($rtTuning.denoise_alpha), expected 0.41"
        }
        if ([Math]::Abs(([double]$rtTuning.composition_strength) - 0.62) -gt 0.03) {
            Add-Failure "valid settings RT reflection composition strength was $($rtTuning.composition_strength), expected 0.62"
        }
    }
    $backgroundExposure = [double]$report.frame_contract.environment.background_exposure
    $backgroundBlur = [double]$report.frame_contract.environment.background_blur
    if ([Math]::Abs($backgroundExposure - 1.23) -gt 0.03) {
        Add-Failure "valid settings background exposure was $backgroundExposure, expected 1.23"
    }
    if ([Math]::Abs($backgroundBlur - 0.37) -gt 0.03) {
        Add-Failure "valid settings background blur was $backgroundBlur, expected 0.37"
    }
    $ssaoRadius = [double]$report.frame_contract.lighting.ssao_radius
    $ssaoBias = [double]$report.frame_contract.lighting.ssao_bias
    $ssaoIntensity = [double]$report.frame_contract.lighting.ssao_intensity
    $screenSpace = $report.frame_contract.screen_space
    if ($null -eq $screenSpace) {
        Add-Failure "valid settings frame contract did not include screen_space"
    } else {
        if ([Math]::Abs(([double]$screenSpace.ssr_max_distance) - 42.0) -gt 0.5) {
            Add-Failure "valid settings SSR max distance was $($screenSpace.ssr_max_distance), expected 42.0"
        }
        if ([Math]::Abs(([double]$screenSpace.ssr_thickness) - 0.24) -gt 0.02) {
            Add-Failure "valid settings SSR thickness was $($screenSpace.ssr_thickness), expected 0.24"
        }
        if ([Math]::Abs(([double]$screenSpace.ssr_strength) - 0.67) -gt 0.03) {
            Add-Failure "valid settings SSR strength was $($screenSpace.ssr_strength), expected 0.67"
        }
    }
    if ([Math]::Abs($ssaoRadius - 0.42) -gt 0.04) {
        Add-Failure "valid settings SSAO radius was $ssaoRadius, expected 0.42"
    }
    if ([Math]::Abs($ssaoBias - 0.055) -gt 0.01) {
        Add-Failure "valid settings SSAO bias was $ssaoBias, expected 0.055"
    }
    if ([Math]::Abs($ssaoIntensity - 0.48) -gt 0.04) {
        Add-Failure "valid settings SSAO intensity was $ssaoIntensity, expected 0.48"
    }
    $fogHeight = [double]$report.frame_contract.lighting.fog_height
    $fogFalloff = [double]$report.frame_contract.lighting.fog_falloff
    if ([Math]::Abs($fogHeight - 4.25) -gt 0.08) {
        Add-Failure "valid settings fog height was $fogHeight, expected 4.25"
    }
    if ([Math]::Abs($fogFalloff - 0.72) -gt 0.04) {
        Add-Failure "valid settings fog falloff was $fogFalloff, expected 0.72"
    }
    $water = $report.frame_contract.water
    if ($null -eq $water) {
        Add-Failure "valid settings frame contract did not include water"
    } else {
        if ([Math]::Abs(([double]$water.level_y) + 0.12) -gt 0.03) {
            Add-Failure "valid settings water level was $($water.level_y), expected -0.12"
        }
        if ([Math]::Abs(([double]$water.wave_amplitude) - 0.19) -gt 0.03) {
            Add-Failure "valid settings water wave amplitude was $($water.wave_amplitude), expected 0.19"
        }
        if ([Math]::Abs(([double]$water.wave_length) - 11.5) -gt 0.08) {
            Add-Failure "valid settings water wavelength was $($water.wave_length), expected 11.5"
        }
        if ([Math]::Abs(([double]$water.wave_speed) - 1.7) -gt 0.04) {
            Add-Failure "valid settings water speed was $($water.wave_speed), expected 1.7"
        }
        if ([Math]::Abs(([double]$water.secondary_amplitude) - 0.08) -gt 0.03) {
            Add-Failure "valid settings water secondary amplitude was $($water.secondary_amplitude), expected 0.08"
        }
        if ([Math]::Abs(([double]$water.roughness) - 0.18) -gt 0.03) {
            Add-Failure "valid settings water roughness was $($water.roughness), expected 0.18"
        }
        if ([Math]::Abs(([double]$water.fresnel_strength) - 1.45) -gt 0.04) {
            Add-Failure "valid settings water Fresnel strength was $($water.fresnel_strength), expected 1.45"
        }
    }
    $vignette = [double]$report.frame_contract.cinematic_post.vignette
    $lensDirt = [double]$report.frame_contract.cinematic_post.lens_dirt
    $warm = [double]$report.frame_contract.cinematic_post.warm
    $cool = [double]$report.frame_contract.cinematic_post.cool
    if (-not [bool]$report.frame_contract.cinematic_post.enabled) {
        Add-Failure "valid settings cinematic post was not enabled"
    }
    if ([Math]::Abs($warm - 0.42) -gt 0.03) {
        Add-Failure "valid settings warm color grade was $warm, expected 0.42"
    }
    if ([Math]::Abs($cool + 0.18) -gt 0.03) {
        Add-Failure "valid settings cool color grade was $cool, expected -0.18"
    }
    if ([Math]::Abs($vignette - 0.31) -gt 0.02) {
        Add-Failure "valid settings vignette was $vignette, expected 0.31"
    }
    if ([Math]::Abs($lensDirt - 0.22) -gt 0.02) {
        Add-Failure "valid settings lens dirt was $lensDirt, expected 0.22"
    }
    $particleDensity = [double]$report.frame_contract.particles.density_scale
    if ([Math]::Abs($particleDensity - 0.43) -gt 0.02) {
        Add-Failure "valid settings particle density was $particleDensity, expected 0.43"
    }
}

$corruptRun = Invoke-SettingsSmoke "corrupt_settings" $corruptPath
if ($corruptRun.exit_code -ne 0) {
    Add-Failure "corrupt settings run failed with exit code $($corruptRun.exit_code). log=$($corruptRun.log_dir)"
} elseif (-not (Test-Path $corruptRun.report)) {
    Add-Failure "corrupt settings run did not write frame report: $($corruptRun.report)"
}

if ($failures.Count -gt 0) {
    Write-Host "Graphics settings persistence tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Graphics settings persistence tests passed: logs=$LogDir" -ForegroundColor Green
