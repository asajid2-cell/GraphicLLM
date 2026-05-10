param(
    [string]$CatalogPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($CatalogPath)) {
    $CatalogPath = Join-Path $root "assets/config/advanced_graphics_catalog.json"
}
$showcasePath = Join-Path $root "assets/config/showcase_scenes.json"

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if (-not (Test-Path $CatalogPath)) {
    throw "Advanced graphics catalog not found: $CatalogPath"
}
if (-not (Test-Path $showcasePath)) {
    throw "Showcase scene config not found: $showcasePath"
}

$catalog = Get-Content $CatalogPath -Raw | ConvertFrom-Json
$showcase = Get-Content $showcasePath -Raw | ConvertFrom-Json

if ([int]$catalog.schema -ne 1) {
    Add-Failure "advanced graphics catalog schema must be 1"
}
if ($null -eq $catalog.systems -or $catalog.systems.Count -lt 1) {
    Add-Failure "advanced graphics catalog systems list is empty"
}

$sceneIds = @{}
foreach ($scene in $showcase.scenes) {
    $sceneIds[[string]$scene.id] = $scene
}

$requiredSystems = @("advanced_materials", "lighting_rigs", "particles", "cinematic_post")
$systems = @{}
foreach ($system in $catalog.systems) {
    $id = [string]$system.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "advanced graphics system id is missing"
        continue
    }
    if ($systems.ContainsKey($id)) {
        Add-Failure "duplicate advanced graphics system id '$id'"
    }
    $systems[$id] = $system

    if ($null -eq $system.existing_entry_points -or $system.existing_entry_points.Count -lt 1) {
        Add-Failure "$id existing_entry_points is empty"
    } else {
        foreach ($entry in $system.existing_entry_points) {
            $relative = [string]$entry
            if ([string]::IsNullOrWhiteSpace($relative)) {
                Add-Failure "$id has an empty existing_entry_points entry"
                continue
            }
            $path = Join-Path $root $relative
            if (-not (Test-Path $path)) {
                Add-Failure "$id entry point does not exist: $relative"
            }
        }
    }

    if ($null -eq $system.planned_features -or $system.planned_features.Count -lt 1) {
        Add-Failure "$id planned_features is empty"
    }

    $validationScene = [string]$system.first_validation_scene
    if ([string]::IsNullOrWhiteSpace($validationScene)) {
        Add-Failure "$id first_validation_scene is missing"
    } elseif (-not $sceneIds.ContainsKey($validationScene)) {
        Add-Failure "$id first_validation_scene '$validationScene' is not in showcase_scenes.json"
    }
}

foreach ($required in $requiredSystems) {
    if (-not $systems.ContainsKey($required)) {
        Add-Failure "advanced graphics catalog missing required system '$required'"
    }
}

if ($systems.ContainsKey("advanced_materials") -and
    [string]$systems["advanced_materials"].first_validation_scene -ne "material_lab") {
    Add-Failure "advanced_materials must validate first in material_lab"
}
if ($systems.ContainsKey("lighting_rigs") -and
    -not $sceneIds.ContainsKey([string]$systems["lighting_rigs"].first_validation_scene)) {
    Add-Failure "lighting_rigs first_validation_scene is not a known showcase scene"
}
foreach ($effectsSystem in @("particles", "cinematic_post")) {
    if ($systems.ContainsKey($effectsSystem) -and
        [string]$systems[$effectsSystem].first_validation_scene -ne "effects_showcase") {
        Add-Failure "$effectsSystem must validate first in effects_showcase"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Advanced graphics catalog tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Advanced graphics catalog tests passed: systems=$($catalog.systems.Count)" -ForegroundColor Green
