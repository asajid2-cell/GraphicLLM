param(
    [string]$PresetPath = "",
    [switch]$RuntimeSmoke,
    [switch]$NoBuild,
    [int]$SmokeFrames = 90,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($PresetPath)) {
    $PresetPath = Join-Path $root "assets/config/graphics_presets.json"
}
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "graphics_preset_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Assert-Range([string]$Name, [double]$Value, [double]$Min, [double]$Max) {
    if ($Value -lt $Min -or $Value -gt $Max) {
        Add-Failure "$Name=$Value outside [$Min, $Max]"
    }
}

if (-not (Test-Path $PresetPath)) {
    throw "Graphics preset file not found: $PresetPath"
}

$raw = Get-Content $PresetPath -Raw
$presetDoc = $raw | ConvertFrom-Json

if ([int]$presetDoc.schema -ne 1) {
    Add-Failure "graphics preset schema must be 1"
}
if ([string]::IsNullOrWhiteSpace([string]$presetDoc.default)) {
    Add-Failure "graphics preset default id is missing"
}
if ($null -eq $presetDoc.presets -or $presetDoc.presets.Count -lt 1) {
    Add-Failure "graphics preset list is empty"
}

$ids = @{}
$defaultFound = $false
foreach ($preset in $presetDoc.presets) {
    $id = [string]$preset.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "preset id is missing"
        continue
    }
    if ($ids.ContainsKey($id)) {
        Add-Failure "duplicate preset id '$id'"
    }
    $ids[$id] = $true
    if ($id -eq [string]$presetDoc.default) {
        $defaultFound = $true
    }

    Assert-Range "$id.quality.render_scale" ([double]$preset.quality.render_scale) 0.5 1.5
    Assert-Range "$id.environment.diffuse_intensity" ([double]$preset.environment.diffuse_intensity) 0.0 4.0
    Assert-Range "$id.environment.specular_intensity" ([double]$preset.environment.specular_intensity) 0.0 4.0
    Assert-Range "$id.environment.background_exposure" ([double]$preset.environment.background_exposure) 0.0 4.0
    Assert-Range "$id.environment.background_blur" ([double]$preset.environment.background_blur) 0.0 1.0
    Assert-Range "$id.lighting.exposure" ([double]$preset.lighting.exposure) 0.01 8.0
    Assert-Range "$id.lighting.bloom_intensity" ([double]$preset.lighting.bloom_intensity) 0.0 5.0
    Assert-Range "$id.screen_space.ssao_radius" ([double]$preset.screen_space.ssao_radius) 0.05 5.0
    Assert-Range "$id.screen_space.ssao_bias" ([double]$preset.screen_space.ssao_bias) 0.0 0.2
    Assert-Range "$id.screen_space.ssao_intensity" ([double]$preset.screen_space.ssao_intensity) 0.0 3.0
    Assert-Range "$id.ray_tracing.gi_strength" ([double]$preset.ray_tracing.gi_strength) 0.0 1.0
    Assert-Range "$id.ray_tracing.gi_ray_distance" ([double]$preset.ray_tracing.gi_ray_distance) 0.5 20.0
    Assert-Range "$id.atmosphere.fog_density" ([double]$preset.atmosphere.fog_density) 0.0 0.2
    Assert-Range "$id.particles.density" ([double]$preset.particles.density) 0.0 2.0
    Assert-Range "$id.particles.bloom_contribution" ([double]$preset.particles.bloom_contribution) 0.0 2.0
    Assert-Range "$id.particles.soft_depth_fade" ([double]$preset.particles.soft_depth_fade) 0.0 1.0
    Assert-Range "$id.particles.wind_influence" ([double]$preset.particles.wind_influence) 0.0 2.0
    if ([string]::IsNullOrWhiteSpace([string]$preset.particles.effect_preset)) {
        Add-Failure "$id.particles.effect_preset is missing"
    }
    Assert-Range "$id.cinematic_post.bloom_threshold" ([double]$preset.cinematic_post.bloom_threshold) 0.1 10.0
    Assert-Range "$id.cinematic_post.bloom_soft_knee" ([double]$preset.cinematic_post.bloom_soft_knee) 0.0 1.0
    Assert-Range "$id.cinematic_post.vignette" ([double]$preset.cinematic_post.vignette) 0.0 1.0
    Assert-Range "$id.cinematic_post.motion_blur" ([double]$preset.cinematic_post.motion_blur) 0.0 1.0
    Assert-Range "$id.cinematic_post.depth_of_field" ([double]$preset.cinematic_post.depth_of_field) 0.0 1.0
    if ([string]::IsNullOrWhiteSpace([string]$preset.cinematic_post.tone_mapper_preset)) {
        Add-Failure "$id.cinematic_post.tone_mapper_preset is missing"
    }
}

if (-not $defaultFound) {
    Add-Failure "default preset '$($presetDoc.default)' is not present"
}

$roundTripPath = Join-Path ([System.IO.Path]::GetTempPath()) ("cortex_graphics_presets_roundtrip_{0}.json" -f ([Guid]::NewGuid().ToString("N")))
try {
    $presetDoc | ConvertTo-Json -Depth 16 | Set-Content -Encoding UTF8 $roundTripPath
    $roundTrip = Get-Content $roundTripPath -Raw | ConvertFrom-Json
    if ([int]$roundTrip.schema -ne [int]$presetDoc.schema) {
        Add-Failure "round-trip schema changed"
    }
    if ([string]$roundTrip.default -ne [string]$presetDoc.default) {
        Add-Failure "round-trip default changed"
    }
    if ($roundTrip.presets.Count -ne $presetDoc.presets.Count) {
        Add-Failure "round-trip preset count changed"
    }
} finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $roundTripPath
}

if ($RuntimeSmoke -and $failures.Count -eq 0) {
    $exe = Join-Path $root "build/bin/CortexEngine.exe"
    if (-not $NoBuild) {
        cmake --build (Join-Path $root "build") --config Release --target CortexEngine
    }
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild."
    } else {
        New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
        $env:CORTEX_LOG_DIR = $LogDir
        $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
        $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
        $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

        Push-Location (Split-Path -Parent $exe)
        try {
            $output = & $exe `
                "--scene" "temporal_validation" `
                "--graphics-preset" "release_showcase" `
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
            Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
            Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
            Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
        }

        $reportPath = Join-Path $LogDir "frame_report_last.json"
        if ($exitCode -ne 0) {
            Add-Failure "runtime graphics preset smoke failed with exit code $exitCode. logs=$LogDir"
        } elseif (-not (Test-Path $reportPath)) {
            Add-Failure "runtime graphics preset smoke did not write frame report: $reportPath"
        } else {
            $report = Get-Content $reportPath -Raw | ConvertFrom-Json
            $contractPreset = [string]$report.frame_contract.graphics_preset.id
            $dirty = [bool]$report.frame_contract.graphics_preset.dirty_from_ui
            $renderScale = [double]$report.frame_contract.graphics_preset.render_scale
            $exposure = [double]$report.frame_contract.lighting.exposure
            $backgroundExposure = [double]$report.frame_contract.environment.background_exposure
            $backgroundBlur = [double]$report.frame_contract.environment.background_blur
            if ($contractPreset -ne "release_showcase") {
                Add-Failure "runtime contract graphics preset was '$contractPreset', expected 'release_showcase'"
            }
            if ($dirty) {
                Add-Failure "runtime contract reported release_showcase as dirty_from_ui"
            }
            if ([Math]::Abs($renderScale - 0.85) -gt 0.04) {
                Add-Failure "runtime contract render_scale was $renderScale, expected about 0.85"
            }
            if ([Math]::Abs($exposure - 1.12) -gt 0.03) {
                Add-Failure "runtime contract exposure was $exposure, expected about 1.12"
            }
            if ([Math]::Abs($backgroundExposure - 1.0) -gt 0.03) {
                Add-Failure "runtime contract background exposure was $backgroundExposure, expected about 1.0"
            }
            if ([Math]::Abs($backgroundBlur - 0.0) -gt 0.03) {
                Add-Failure "runtime contract background blur was $backgroundBlur, expected about 0.0"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Graphics preset tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Graphics preset tests passed: presets=$($presetDoc.presets.Count) default=$($presetDoc.default)" -ForegroundColor Green
if ($RuntimeSmoke) {
    Write-Host "logs=$LogDir"
}
