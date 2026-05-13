param(
    [string]$SceneId = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$seedRoot = Join-Path $root "assets/scenes/hand_authored"
$requiredScenes = @(
    "coastal_cliff_foundry",
    "rain_glass_pavilion",
    "desert_relic_gallery",
    "neon_alley_material_market",
    "forest_creek_shrine"
)

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) | Out-Null }

foreach ($scene in $requiredScenes) {
    if (-not [string]::IsNullOrWhiteSpace($SceneId) -and $scene -ne $SceneId) { continue }
    $sceneDir = Join-Path $seedRoot $scene
    if (-not (Test-Path (Join-Path $sceneDir "scene_seed.json"))) {
        Add-Failure "$scene missing scene_seed.json"
    }
    if (-not (Test-Path (Join-Path $sceneDir "art_bible.md"))) {
        Add-Failure "$scene missing art_bible.md"
    }
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
