param(
    [switch]$NoBuild,
    [ValidateSet("High", "Release")]
    [string]$Quality = "High",
    [string]$OutputDir = "",
    [int]$Width = 1920,
    [int]$Height = 1080,
    [int]$SmokeFrames = 220
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $root "docs/media"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName

if (-not $NoBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before public capture gallery"
    }
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

Add-Type -AssemblyName System.Drawing

$preset = if ($Quality -eq "High") { "public_high" } else { "release_showcase" }
$cases = @(
    @{ id = "rt_showcase"; title = "RT Showcase"; scene = "rt_showcase"; bookmark = "hero"; environment = "studio"; frames = 260; image = "rt_showcase_hero.png" },
    @{ id = "rt_showcase_reflection_closeup"; title = "RT Reflection Closeup"; scene = "rt_showcase"; bookmark = "reflection_closeup"; environment = "studio"; frames = 180; image = "rt_showcase_reflection_closeup.png" },
    @{ id = "rt_showcase_material_overview"; title = "RT Material Overview"; scene = "rt_showcase"; bookmark = "material_overview"; environment = "warm_gallery"; frames = 180; image = "rt_showcase_material_overview.png" },
    @{ id = "material_lab"; title = "Material Lab"; scene = "material_lab"; bookmark = "hero"; environment = "cool_overcast"; frames = 180; image = "material_lab_hero.png" },
    @{ id = "material_lab_metal_closeup"; title = "Material Lab Metal Closeup"; scene = "material_lab"; bookmark = "metal_closeup"; environment = "studio"; frames = 160; image = "material_lab_metal_closeup.png" },
    @{ id = "material_lab_glass_emissive"; title = "Material Lab Glass and Emissive"; scene = "material_lab"; bookmark = "glass_emissive"; environment = "night_city"; frames = 160; image = "material_lab_glass_emissive.png" },
    @{ id = "material_lab_prop_context"; title = "Material Lab Prop Context"; scene = "material_lab"; bookmark = "prop_context"; environment = "warm_gallery"; frames = 160; image = "material_lab_prop_context.png" },
    @{ id = "glass_water_courtyard"; title = "Glass and Water Courtyard"; scene = "glass_water_courtyard"; bookmark = "hero"; environment = "sunset_courtyard"; frames = 180; image = "glass_water_courtyard_hero.png" },
    @{ id = "glass_water_courtyard_water_closeup"; title = "Water Reflection Closeup"; scene = "glass_water_courtyard"; bookmark = "water_closeup"; environment = "sunset_courtyard"; frames = 160; image = "glass_water_courtyard_water_closeup.png" },
    @{ id = "glass_water_courtyard_glass_canopy"; title = "Glass Canopy Rim Light"; scene = "glass_water_courtyard"; bookmark = "glass_canopy"; environment = "warm_gallery"; frames = 160; image = "glass_water_courtyard_glass_canopy.png" },
    @{ id = "glass_water_courtyard_pool_steps"; title = "Pool Steps and Coping"; scene = "glass_water_courtyard"; bookmark = "pool_steps"; environment = "sunset_courtyard"; frames = 160; image = "glass_water_courtyard_pool_steps.png" },
    @{ id = "effects_showcase"; title = "Effects Showcase"; scene = "effects_showcase"; bookmark = "hero"; environment = "night_city"; frames = 220; image = "effects_showcase_hero.png" },
    @{ id = "effects_showcase_particles_closeup"; title = "Particle and Bloom Closeup"; scene = "effects_showcase"; bookmark = "particles_closeup"; environment = "night_city"; frames = 180; image = "effects_showcase_particles_closeup.png" },
    @{ id = "effects_showcase_neon_materials"; title = "Neon Materials"; scene = "effects_showcase"; bookmark = "neon_materials"; environment = "night_city"; frames = 180; image = "effects_showcase_neon_materials.png" },
    @{ id = "outdoor_sunset_beach"; title = "Outdoor Sunset Beach"; scene = "outdoor_sunset_beach"; bookmark = "hero"; environment = "sunset_courtyard"; frames = 180; image = "outdoor_sunset_beach_hero.png" },
    @{ id = "outdoor_sunset_beach_waterline"; title = "Outdoor Waterline"; scene = "outdoor_sunset_beach"; bookmark = "waterline"; environment = "sunset_courtyard"; frames = 160; image = "outdoor_sunset_beach_waterline.png" },
    @{ id = "outdoor_sunset_beach_life"; title = "Beach Props and Shoreline"; scene = "outdoor_sunset_beach"; bookmark = "beach_life"; environment = "sunset_courtyard"; frames = 160; image = "outdoor_sunset_beach_life.png" },
    @{ id = "liquid_gallery"; title = "Liquid Gallery"; scene = "liquid_gallery"; bookmark = "hero"; environment = "warm_gallery"; frames = 190; image = "liquid_gallery_hero.png" },
    @{ id = "liquid_gallery_water_lava"; title = "Water and Lava"; scene = "liquid_gallery"; bookmark = "water_lava"; environment = "warm_gallery"; frames = 170; image = "liquid_gallery_water_lava.png" },
    @{ id = "liquid_gallery_viscous_pair"; title = "Honey and Molasses"; scene = "liquid_gallery"; bookmark = "viscous_pair"; environment = "warm_gallery"; frames = 170; image = "liquid_gallery_viscous_pair.png" },
    @{ id = "liquid_gallery_context"; title = "Liquid Gallery Context"; scene = "liquid_gallery"; bookmark = "liquid_context"; environment = "warm_gallery"; frames = 170; image = "liquid_gallery_context.png" },
    @{ id = "coastal_cliff_foundry"; title = "Coastal Cliff Foundry"; scene = "coastal_cliff_foundry"; bookmark = "hero"; environment = "sunset_courtyard"; frames = 170; image = "coastal_cliff_foundry_hero.png" },
    @{ id = "coastal_cliff_foundry_material_closeup"; title = "Foundry Material Closeup"; scene = "coastal_cliff_foundry"; bookmark = "material_closeup"; environment = "sunset_courtyard"; frames = 150; image = "coastal_cliff_foundry_material_closeup.png" },
    @{ id = "coastal_cliff_foundry_atmosphere"; title = "Foundry Atmosphere"; scene = "coastal_cliff_foundry"; bookmark = "atmosphere"; environment = "sunset_courtyard"; frames = 150; image = "coastal_cliff_foundry_atmosphere.png" },
    @{ id = "rain_glass_pavilion"; title = "Rain Glass Pavilion"; scene = "rain_glass_pavilion"; bookmark = "hero"; environment = "cool_overcast"; frames = 170; image = "rain_glass_pavilion_hero.png" },
    @{ id = "rain_glass_pavilion_glass_closeup"; title = "Rain Glass Closeup"; scene = "rain_glass_pavilion"; bookmark = "glass_closeup"; environment = "cool_overcast"; frames = 150; image = "rain_glass_pavilion_glass_closeup.png" },
    @{ id = "rain_glass_pavilion_puddle_chrome"; title = "Puddle and Chrome"; scene = "rain_glass_pavilion"; bookmark = "puddle_chrome"; environment = "cool_overcast"; frames = 150; image = "rain_glass_pavilion_puddle_chrome.png" },
    @{ id = "desert_relic_gallery"; title = "Desert Relic Gallery"; scene = "desert_relic_gallery"; bookmark = "hero"; environment = "sunset_courtyard"; frames = 170; image = "desert_relic_gallery_hero.png" },
    @{ id = "desert_relic_gallery_stone_metal"; title = "Desert Stone and Metal"; scene = "desert_relic_gallery"; bookmark = "stone_metal_closeup"; environment = "sunset_courtyard"; frames = 150; image = "desert_relic_gallery_stone_metal.png" },
    @{ id = "desert_relic_gallery_arch_depth"; title = "Desert Arch Depth"; scene = "desert_relic_gallery"; bookmark = "arch_depth"; environment = "sunset_courtyard"; frames = 150; image = "desert_relic_gallery_arch_depth.png" },
    @{ id = "neon_alley_material_market"; title = "Neon Alley Material Market"; scene = "neon_alley_material_market"; bookmark = "hero"; environment = "night_city"; frames = 170; image = "neon_alley_material_market_hero.png" },
    @{ id = "neon_alley_material_market_materials"; title = "Neon Market Materials"; scene = "neon_alley_material_market"; bookmark = "materials"; environment = "night_city"; frames = 150; image = "neon_alley_material_market_materials.png" },
    @{ id = "neon_alley_material_market_particles"; title = "Neon Market Particles"; scene = "neon_alley_material_market"; bookmark = "particles"; environment = "night_city"; frames = 150; image = "neon_alley_material_market_particles.png" },
    @{ id = "forest_creek_shrine"; title = "Forest Creek Shrine"; scene = "forest_creek_shrine"; bookmark = "hero"; environment = "cool_overcast"; frames = 170; image = "forest_creek_shrine_hero.png" },
    @{ id = "forest_creek_shrine_moss_water"; title = "Forest Moss and Water"; scene = "forest_creek_shrine"; bookmark = "moss_water"; environment = "cool_overcast"; frames = 150; image = "forest_creek_shrine_moss_water.png" },
    @{ id = "forest_creek_shrine_depth"; title = "Forest Shrine Depth"; scene = "forest_creek_shrine"; bookmark = "shrine_depth"; environment = "cool_overcast"; frames = 150; image = "forest_creek_shrine_depth.png" },
    @{ id = "ibl_gallery_hero"; title = "IBL Gallery Hero"; scene = "ibl_gallery"; bookmark = "hero"; environment = "warm_gallery"; frames = 160; image = "ibl_gallery_hero.png" },
    @{ id = "ibl_gallery"; title = "IBL Gallery Sweep"; scene = "ibl_gallery"; bookmark = "environment_sweep"; environment = "warm_gallery"; frames = 180; image = "ibl_gallery_sweep.png" }
)

$runId = "public_capture_gallery_{0}_{1}_{2}" -f `
    (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
    $PID,
    ([Guid]::NewGuid().ToString("N").Substring(0, 8))
$runRoot = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

function Get-ReportPath([string]$CaseLogDir) {
    foreach ($name in @("frame_report_last.json", "frame_report_shutdown.json")) {
        $candidate = Join-Path $CaseLogDir $name
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return ""
}

function Convert-BmpToPng([string]$BmpPath, [string]$PngPath) {
    $image = [System.Drawing.Image]::FromFile($BmpPath)
    try {
        $image.Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $image.Dispose()
    }
}

$entries = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[string]
$commit = (& git -C (Join-Path $root "..") rev-parse --short HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) {
    $commit = "unknown"
}

foreach ($case in $cases) {
    $caseLogDir = Join-Path $runRoot $case.id
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

    $oldLogDir = $env:CORTEX_LOG_DIR
    $oldCapture = $env:CORTEX_CAPTURE_VISUAL_VALIDATION
    $oldDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER
    $oldMinFrame = $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME
    $oldPublicClean = $env:CORTEX_PUBLIC_CAPTURE_CLEAN
    try {
        $env:CORTEX_LOG_DIR = $caseLogDir
        $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
        $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
        $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"
        $env:CORTEX_PUBLIC_CAPTURE_CLEAN = "1"

        $frames = [Math]::Max($SmokeFrames, [int]$case.frames)
        Push-Location (Split-Path -Parent $exe)
        try {
            $output = & $exe `
                "--scene" ([string]$case.scene) `
                "--camera-bookmark" ([string]$case.bookmark) `
                "--environment" ([string]$case.environment) `
                "--graphics-preset" $preset `
                "--window-width" ([string]$Width) `
                "--window-height" ([string]$Height) `
                "--mode=default" `
                "--no-llm" `
                "--no-dreamer" `
                "--no-launcher" `
                "--smoke-frames=$frames" `
                "--exit-after-visual-validation" 2>&1
            $exitCode = $LASTEXITCODE
            $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "engine_stdout.txt")
        } finally {
            Pop-Location
        }
    } finally {
        if ($null -eq $oldLogDir) { Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue } else { $env:CORTEX_LOG_DIR = $oldLogDir }
        if ($null -eq $oldCapture) { Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue } else { $env:CORTEX_CAPTURE_VISUAL_VALIDATION = $oldCapture }
        if ($null -eq $oldDebugLayer) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $oldDebugLayer }
        if ($null -eq $oldMinFrame) { Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue } else { $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = $oldMinFrame }
        if ($null -eq $oldPublicClean) { Remove-Item Env:\CORTEX_PUBLIC_CAPTURE_CLEAN -ErrorAction SilentlyContinue } else { $env:CORTEX_PUBLIC_CAPTURE_CLEAN = $oldPublicClean }
    }

    if ($exitCode -ne 0) {
        $failures.Add("$($case.id) exited with code $exitCode logs=$caseLogDir") | Out-Null
        continue
    }

    $reportPath = Get-ReportPath $caseLogDir
    if ([string]::IsNullOrWhiteSpace($reportPath)) {
        $failures.Add("$($case.id) did not write a frame report logs=$caseLogDir") | Out-Null
        continue
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $bmpPath = Join-Path $caseLogDir "visual_validation_rt_showcase.bmp"
    if (-not (Test-Path $bmpPath)) {
        $failures.Add("$($case.id) did not write visual_validation_rt_showcase.bmp logs=$caseLogDir") | Out-Null
        continue
    }

    $pngPath = Join-Path $OutputDir ([string]$case.image)
    Convert-BmpToPng $bmpPath $pngPath

    $stats = $report.visual_validation.image_stats
    if (-not [bool]$stats.valid) {
        $failures.Add("$($case.id) visual stats invalid: $($stats.reason)") | Out-Null
    }

    $rt = $report.frame_contract.ray_tracing
    $entry = [ordered]@{
        id = [string]$case.id
        title = [string]$case.title
        image = ("docs/media/{0}" -f [string]$case.image)
        scene = [string]$case.scene
        camera_bookmark = [string]$case.bookmark
        environment = [string]$case.environment
        graphics_preset = $preset
        quality = $Quality
        window_width = [int]$report.window.width
        window_height = [int]$report.window.height
        capture_width = [int]$stats.width
        capture_height = [int]$stats.height
        render_scale = [double]$report.frame_contract.graphics_preset.render_scale
        gpu_frame_ms = [double]$report.gpu_frame_ms
        avg_luma = [double]$stats.avg_luma
        center_avg_luma = [double]$stats.center_avg_luma
        nonblack_ratio = [double]$stats.nonblack_ratio
        saturated_ratio = [double]$stats.saturated_ratio
        near_white_ratio = [double]$stats.near_white_ratio
        rt_reflection_signal_avg_luma = if ($rt) { [double]$rt.reflection_signal_avg_luma } else { 0.0 }
        rt_reflection_history_avg_luma = if ($rt) { [double]$rt.reflection_history_signal_avg_luma } else { 0.0 }
        report = $reportPath
        source_bmp = $bmpPath
    }
    $entries.Add([pscustomobject]$entry) | Out-Null
}

$manifest = [ordered]@{
    schema = 1
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    commit = $commit.Trim()
    quality = $Quality
    graphics_preset = $preset
    requested_width = $Width
    requested_height = $Height
    run_log_dir = $runRoot
    entries = @($entries.ToArray())
    failures = @($failures.ToArray())
}
$manifestPath = Join-Path $OutputDir "gallery_manifest.json"
$manifestJson = ($manifest | ConvertTo-Json -Depth 8) -replace "`r`n", "`n"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson + "`n", $utf8NoBom)

if ($failures.Count -gt 0) {
    Write-Host "Public capture gallery failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "manifest=$manifestPath" -ForegroundColor Red
    exit 1
}

Write-Host "Public capture gallery passed" -ForegroundColor Green
Write-Host " manifest=$manifestPath"
Write-Host " logs=$runRoot"
Write-Host " captures=$($entries.Count) size=${Width}x${Height} preset=$preset"
