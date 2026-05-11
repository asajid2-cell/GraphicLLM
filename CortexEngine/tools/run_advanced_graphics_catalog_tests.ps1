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

    if ($null -ne $system.planned_features) {
        Add-Failure "$id still uses planned_features; release catalog must use validated_features plus future_extensions"
    }

    if ([string]$system.release_status -ne "foundation_validated") {
        Add-Failure "$id release_status is '$($system.release_status)', expected foundation_validated"
    }
    if ($null -eq $system.validated_features -or $system.validated_features.Count -lt 1) {
        Add-Failure "$id validated_features is empty"
    } else {
        foreach ($feature in $system.validated_features) {
            if ([string]::IsNullOrWhiteSpace([string]$feature)) {
                Add-Failure "$id has an empty validated_features entry"
            }
        }
    }
    if ($null -eq $system.validation_contracts -or $system.validation_contracts.Count -lt 1) {
        Add-Failure "$id validation_contracts is empty"
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
if ($systems.ContainsKey("advanced_materials")) {
    $features = @{}
    foreach ($feature in $systems["advanced_materials"].validated_features) {
        $features[[string]$feature] = $true
    }
    foreach ($requiredFeature in @("clearcoat", "anisotropy_response", "wet_surface_response", "sheen", "subsurface_wrap", "emissive_bloom")) {
        if (-not $features.ContainsKey($requiredFeature)) {
            Add-Failure "advanced_materials missing validated feature '$requiredFeature'"
        }
    }
}
if ($systems.ContainsKey("lighting_rigs") -and
    -not $sceneIds.ContainsKey([string]$systems["lighting_rigs"].first_validation_scene)) {
    Add-Failure "lighting_rigs first_validation_scene is not a known showcase scene"
}
if ($systems.ContainsKey("lighting_rigs")) {
    $features = @{}
    foreach ($feature in $systems["lighting_rigs"].validated_features) {
        $features[[string]$feature] = $true
    }
    foreach ($requiredRig in @("studio_three_point", "material_lab_review", "sunset_rim", "night_emissive")) {
        if (-not $features.ContainsKey($requiredRig)) {
            Add-Failure "lighting_rigs missing validated rig '$requiredRig'"
        }
    }
}
foreach ($effectsSystem in @("particles", "cinematic_post")) {
    if ($systems.ContainsKey($effectsSystem) -and
        [string]$systems[$effectsSystem].first_validation_scene -ne "effects_showcase") {
        Add-Failure "$effectsSystem must validate first in effects_showcase"
    }
}
if ($systems.ContainsKey("particles")) {
    $features = @{}
    foreach ($feature in $systems["particles"].validated_features) {
        $features[[string]$feature] = $true
    }
    foreach ($requiredParticleFeature in @(
        "ecs_billboard_emitters",
        "fire",
        "smoke",
        "dust_motes",
        "sparks",
        "embers",
        "mist",
        "rain",
        "snow",
        "density_scale",
        "frame_contract_stats",
        "budgeted_zero_cost_disabled_path",
        "procedural_billboard_fallback")) {
        if (-not $features.ContainsKey($requiredParticleFeature)) {
            Add-Failure "particles missing validated feature '$requiredParticleFeature'"
        }
    }
    if ($null -eq $systems["particles"].fallback_policy) {
        Add-Failure "particles fallback_policy is missing"
    } else {
        if ([string]$systems["particles"].fallback_policy.texture -ne "procedural_billboard") {
            Add-Failure "particles fallback texture must be procedural_billboard"
        }
        if ([bool]$systems["particles"].fallback_policy.startup_downloads) {
            Add-Failure "particles fallback policy must not use startup downloads"
        }
    }

    $particleLibraryPath = Join-Path $root "src/Scene/ParticleEffectLibrary.cpp"
    $particleLibrary = if (Test-Path $particleLibraryPath) {
        Get-Content $particleLibraryPath -Raw
    } else {
        ""
    }
    foreach ($effectId in @("fire", "smoke", "dust", "sparks", "embers", "mist", "rain", "snow")) {
        if ($particleLibrary.IndexOf("`"$effectId`"", [StringComparison]::Ordinal) -lt 0) {
            Add-Failure "particle effect library missing effect id '$effectId'"
        }
    }
    if ($particleLibrary.IndexOf("procedural_billboard", [StringComparison]::Ordinal) -lt 0) {
        Add-Failure "particle effect library missing procedural billboard fallback"
    }
}
if ($systems.ContainsKey("cinematic_post")) {
    $features = @{}
    foreach ($feature in $systems["cinematic_post"].validated_features) {
        $features[[string]$feature] = $true
    }
    foreach ($requiredPostFeature in @("bloom_threshold", "soft_knee", "vignette", "lens_dirt", "frame_contract_stats")) {
        if (-not $features.ContainsKey($requiredPostFeature)) {
            Add-Failure "cinematic_post missing validated feature '$requiredPostFeature'"
        }
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
