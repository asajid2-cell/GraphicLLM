param(
    [string]$SceneId = "",
    [switch]$RuntimeSmoke,
    [int]$SmokeFrames = 45
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$seedRoot = Join-Path $root "assets/scenes/hand_authored"
$showcasePath = Join-Path $root "assets/config/showcase_scenes.json"
$engineHeaderPath = Join-Path $root "src/Core/Engine.h"
$enginePath = Join-Path $root "src/Core/Engine.cpp"
$engineCameraPath = Join-Path $root "src/Core/Engine_Camera.cpp"
$engineScenesPath = Join-Path $root "src/Core/Engine_Scenes.cpp"
$runtimeLayoutContractsPath = Join-Path $root "assets/scenes/hand_authored/runtime_layout_contracts.json"
$worldPalettePath = Join-Path $root "assets/config/asset_led_world_palettes.json"
$requiredScenes = @(
    "coastal_cliff_foundry",
    "rain_glass_pavilion",
    "desert_relic_gallery",
    "neon_alley_material_market",
    "forest_creek_shrine"
)
$builderByScene = @{
    "coastal_cliff_foundry" = "BuildCoastalCliffFoundryScene"
    "rain_glass_pavilion" = "BuildRainGlassPavilionScene"
    "desert_relic_gallery" = "BuildDesertRelicGalleryScene"
    "neon_alley_material_market" = "BuildNeonAlleyMaterialMarketScene"
    "forest_creek_shrine" = "BuildForestCreekShrineScene"
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) | Out-Null }
function Assert-Contains([string]$Name, [string]$Text, [string]$Token) {
    if ($Text.IndexOf($Token, [StringComparison]::Ordinal) -lt 0) {
        Add-Failure "$Name missing token '$Token'"
    }
}

$engineHeader = Get-Content $engineHeaderPath -Raw
$engineSource = Get-Content $enginePath -Raw
$engineCamera = Get-Content $engineCameraPath -Raw
$engineScenes = Get-Content $engineScenesPath -Raw
$showcase = Get-Content $showcasePath -Raw | ConvertFrom-Json
$showcaseById = @{}
foreach ($scene in @($showcase.scenes)) {
    $showcaseById[[string]$scene.id] = $scene
}
$layoutById = @{}
if (Test-Path $runtimeLayoutContractsPath) {
    $layoutContracts = Get-Content $runtimeLayoutContractsPath -Raw | ConvertFrom-Json
    foreach ($contract in @($layoutContracts.scenes)) {
        $layoutById[[string]$contract.id] = $contract
    }
} else {
    Add-Failure "runtime layout contracts missing: $runtimeLayoutContractsPath"
}
$paletteByScene = @{}
if (Test-Path $worldPalettePath) {
    $worldPalettes = Get-Content $worldPalettePath -Raw | ConvertFrom-Json
    foreach ($palette in @($worldPalettes.palettes)) {
        $paletteByScene[[string]$palette.scene] = $palette
    }
} else {
    Add-Failure "world palette manifest missing: $worldPalettePath"
}

foreach ($scene in $requiredScenes) {
    if (-not [string]::IsNullOrWhiteSpace($SceneId) -and $scene -ne $SceneId) { continue }
    $sceneDir = Join-Path $seedRoot $scene
    if (-not (Test-Path (Join-Path $sceneDir "scene_seed.json"))) {
        Add-Failure "$scene missing scene_seed.json"
    }
    if (-not (Test-Path (Join-Path $sceneDir "art_bible.md"))) {
        Add-Failure "$scene missing art_bible.md"
    }
    if (-not $showcaseById.ContainsKey($scene)) {
        Add-Failure "$scene missing showcase_scenes.json entry"
    } else {
        $showcaseScene = $showcaseById[$scene]
        if (@($showcaseScene.required_bookmarks).Count -lt 3) {
            Add-Failure "$scene should expose hero and at least two detail bookmarks"
        }
        if (@($showcaseScene.required_features).Count -lt 4) {
            Add-Failure "$scene should declare at least four renderer/material features"
        }
    }
    $builder = $builderByScene[$scene]
    Assert-Contains "Engine.h" $engineHeader $builder
    Assert-Contains "Engine.cpp" $engineSource $scene
    Assert-Contains "Engine_Camera.cpp" $engineCamera $scene
    Assert-Contains "Engine_Scenes.cpp" $engineScenes "void Engine::$builder()"
}

if ($failures.Count -eq 0) {
    $args = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "run_scene_seed_contract_tests.ps1"))
    if (-not [string]::IsNullOrWhiteSpace($SceneId)) { $args += @("-SceneId", $SceneId) }
    & powershell @args
    if ($LASTEXITCODE -ne 0) { Add-Failure "scene seed contract failed" }

    $args = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "run_scene_composition_stability_tests.ps1"))
    if (-not [string]::IsNullOrWhiteSpace($SceneId)) { $args += @("-SceneId", $SceneId) }
    & powershell @args
    if ($LASTEXITCODE -ne 0) { Add-Failure "scene composition stability contract failed" }

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run_world_shader_contract_tests.ps1")
    if ($LASTEXITCODE -ne 0) { Add-Failure "world shader contract failed" }

    if ($RuntimeSmoke) {
        $exe = Join-Path $root "build/bin/CortexEngine.exe"
        if (-not (Test-Path $exe)) {
            Add-Failure "runtime smoke requested but executable is missing: $exe"
        } else {
            $runRoot = Join-Path $root ("build/bin/logs/runs/asset_led_scene_contract_{0}_{1}" -f (Get-Date -Format "yyyyMMdd_HHmmss_fff"), $PID)
            New-Item -ItemType Directory -Force $runRoot | Out-Null
            foreach ($scene in $requiredScenes) {
                if (-not [string]::IsNullOrWhiteSpace($SceneId) -and $scene -ne $SceneId) { continue }
                $sceneLog = Join-Path $runRoot $scene
                New-Item -ItemType Directory -Force $sceneLog | Out-Null
                $env:CORTEX_LOG_DIR = $sceneLog
                $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
                Push-Location (Split-Path -Parent $exe)
                try {
                    $output = & $exe `
                        "--scene" $scene `
                        "--camera-bookmark" "hero" `
                        "--mode=default" `
                        "--no-llm" `
                        "--no-dreamer" `
                        "--no-launcher" `
                        "--smoke-frames=$SmokeFrames" 2>&1
                    $exitCode = $LASTEXITCODE
                    $output | Set-Content -Encoding UTF8 (Join-Path $sceneLog "engine_stdout.txt")
                } finally {
                    Pop-Location
                    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
                    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
                }
                if ($exitCode -ne 0) {
                    Add-Failure "$scene runtime smoke failed with exit code $exitCode logs=$sceneLog"
                    continue
                }
                $report = Join-Path $sceneLog "frame_report_last.json"
                if (-not (Test-Path $report)) { $report = Join-Path $sceneLog "frame_report_shutdown.json" }
                if (-not (Test-Path $report)) {
                    Add-Failure "$scene runtime smoke did not write a frame report logs=$sceneLog"
                    continue
                }
                $json = Get-Content $report -Raw | ConvertFrom-Json
                if ([string]$json.scene -ne $scene) {
                    Add-Failure "$scene runtime report scene was '$($json.scene)'"
                }
                if ($paletteByScene.ContainsKey($scene)) {
                    $expectedPalette = [string]$paletteByScene[$scene].id
                    $expectedScript = [string]$paletteByScene[$scene].lighting_script
                    $actualPalette = [string]$json.frame_contract.lighting.world_shader_palette_id
                    $actualScript = [string]$json.frame_contract.lighting.lighting_script_id
                    if ($actualPalette -ne $expectedPalette) {
                        Add-Failure "$scene runtime world palette '$actualPalette', expected '$expectedPalette'"
                    }
                    if ($actualScript -ne $expectedScript) {
                        Add-Failure "$scene runtime lighting script '$actualScript', expected '$expectedScript'"
                    }
                }
                if ($layoutById.ContainsKey($scene)) {
                    $minRenderables = [int]$layoutById[$scene].min_runtime_renderables
                    $actualRenderables = [int]$json.frame_contract.renderables.total
                    if ($actualRenderables -lt $minRenderables) {
                        Add-Failure "$scene runtime renderable count $actualRenderables is below layout contract minimum $minRenderables"
                    }
                }
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Asset-led scene contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) { Write-Host " - $failure" -ForegroundColor Red }
    exit 1
}

if ([string]::IsNullOrWhiteSpace($SceneId)) {
    Write-Host "Asset-led scene contract tests passed: scenes=$($requiredScenes.Count)" -ForegroundColor Green
} else {
    Write-Host "Asset-led scene contract tests passed: scene=$SceneId" -ForegroundColor Green
}
