param(
    [string]$ScenePath = "",
    [string]$ShowcasePath = "",
    [string]$ReleaseValidationPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ScenePath)) {
    $ScenePath = Join-Path $root "src/Core/Engine_Scenes.cpp"
}
if ([string]::IsNullOrWhiteSpace($ShowcasePath)) {
    $ShowcasePath = Join-Path $root "assets/config/showcase_scenes.json"
}
if ([string]::IsNullOrWhiteSpace($ReleaseValidationPath)) {
    $ReleaseValidationPath = Join-Path $root "tools/run_release_validation.ps1"
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Read-Text([string]$Path) {
    if (-not (Test-Path $Path)) {
        throw "Required file missing: $Path"
    }
    return Get-Content $Path -Raw
}

function Get-Scene([object]$Doc, [string]$SceneId) {
    foreach ($scene in $Doc.scenes) {
        if ([string]$scene.id -eq $SceneId) {
            return $scene
        }
    }
    Add-Failure "showcase_scenes.json missing scene '$SceneId'"
    return $null
}

function Get-Bookmark([object]$Scene, [string]$BookmarkId) {
    if ($null -eq $Scene) { return $null }
    foreach ($bookmark in $Scene.camera_bookmarks) {
        if ([string]$bookmark.id -eq $BookmarkId) {
            return $bookmark
        }
    }
    Add-Failure "$($Scene.id) missing bookmark '$BookmarkId'"
    return $null
}

function Assert-Contains([string]$Name, [string]$Text, [string]$Token) {
    if ($Text.IndexOf($Token, [StringComparison]::Ordinal) -lt 0) {
        Add-Failure "$Name missing token '$Token'"
    }
}

function Assert-NotContains([string]$Name, [string]$Text, [string]$Token) {
    if ($Text.IndexOf($Token, [StringComparison]::Ordinal) -ge 0) {
        Add-Failure "$Name still contains forbidden token '$Token'"
    }
}

$sceneSource = Read-Text $ScenePath
$releaseSource = Read-Text $ReleaseValidationPath
$captureGallerySource = Read-Text (Join-Path $root "tools/run_public_capture_gallery.ps1")
$showcase = Get-Content $ShowcasePath -Raw | ConvertFrom-Json

foreach ($token in @(
    "OutdoorBeach_ShoreFoam_",
    "OutdoorBeach_SandMound_",
    "OutdoorBeach_RockCluster_",
    "OutdoorBeach_Driftwood_",
    "OutdoorBeach_Coconut_",
    "OutdoorBeach_WetSandPatch_",
    "OutdoorBeach_PalmTrunkBand_",
    "OutdoorBeach_PalmCrownCore_",
    "OutdoorBeach_SkyBackdrop",
    "OutdoorBeach_OceanHorizon",
    "for (int i = 0; i < 10; ++i)"
)) {
    Assert-Contains "Outdoor beach polish" $sceneSource $token
}
Assert-NotContains "Outdoor beach polish" $sceneSource "OutdoorBeach_SunsetGlowPanel"
Assert-NotContains "Outdoor beach polish" $sceneSource "OutdoorBeach_Dune_"

foreach ($token in @(
    "GlassWaterCourtyard_PoolCoping_North",
    "GlassWaterCourtyard_PoolCoping_South",
    "GlassWaterCourtyard_PoolCorner_NW",
    "GlassWaterCourtyard_CourtyardSkirt_Front",
    "GlassWaterCourtyard_PoolStep_ShallowA",
    "GlassWaterCourtyard_WaterlineTile_North",
    "GlassWaterCourtyard_CanopyFrame_North",
    "GlassWaterCourtyard_CanopyFrame_CenterB",
    "GlassWaterCourtyard_ColumnBase_",
    "GlassWaterCourtyard_ColumnCap_",
    "GlassWaterCourtyard_BackWall_LowerTrim",
    "GlassWaterCourtyard_GlassScreen_Left"
)) {
    Assert-Contains "Glass courtyard polish" $sceneSource $token
}

foreach ($token in @(
    "MaterialLab_BackdropBaseRail",
    "MaterialLab_RightPropPlatform",
    "MaterialLab_CenterFloorRunway",
    "MaterialLab_ScannedWoodenTable"
)) {
    Assert-Contains "Material lab polish" $sceneSource $token
}

foreach ($token in @(
    "LiquidGallery_BackWall_BaseTrim",
    "LiquidGallery_IntegratedCountertop",
    "LiquidGallery_FrontApron",
    "_CornerPost_",
    "LiquidGallery_CoolReflectionPanel",
    "LiquidGallery_CenterDrainGrate"
)) {
    Assert-Contains "Liquid gallery polish" $sceneSource $token
}

foreach ($token in @(
    "RTGallery_DragonReflectionPanel_Warm",
    "RTGallery_DragonReflectionPanel_Cool"
)) {
    Assert-Contains "RT showcase polish" $sceneSource $token
}

$beach = Get-Scene $showcase "outdoor_sunset_beach"
$beachHero = Get-Bookmark $beach "hero"
$beachWaterline = Get-Bookmark $beach "waterline"
$beachLife = Get-Bookmark $beach "beach_life"
if ($beachHero -and ([double]$beachHero.fov -gt 52.0)) {
    Add-Failure "outdoor_sunset_beach.hero fov should be tightened after polish"
}
if ($beachWaterline -and ([double]$beachWaterline.position[1] -gt 1.5)) {
    Add-Failure "outdoor_sunset_beach.waterline should use a lower waterline camera"
}
if ($beachLife -and ([double]$beachLife.fov -gt 40.0)) {
    Add-Failure "outdoor_sunset_beach.beach_life should use a detail framing"
}

$courtyard = Get-Scene $showcase "glass_water_courtyard"
$courtyardHero = Get-Bookmark $courtyard "hero"
$courtyardCanopy = Get-Bookmark $courtyard "glass_canopy"
$courtyardPoolSteps = Get-Bookmark $courtyard "pool_steps"
if ($courtyardHero -and ([double]$courtyardHero.fov -gt 52.0)) {
    Add-Failure "glass_water_courtyard.hero fov should be tightened after polish"
}
if ($courtyardCanopy -and ([double]$courtyardCanopy.target[1] -lt 2.0)) {
    Add-Failure "glass_water_courtyard.glass_canopy should target the canopy/frame area"
}
if ($courtyardPoolSteps -and ([double]$courtyardPoolSteps.fov -gt 40.0)) {
    Add-Failure "glass_water_courtyard.pool_steps should use a tight coping/step framing"
}

$materialLab = Get-Scene $showcase "material_lab"
$propContext = Get-Bookmark $materialLab "prop_context"
if ($propContext -and ([double]$propContext.fov -gt 42.0)) {
    Add-Failure "material_lab.prop_context should use a focused naturalistic prop framing"
}

$liquid = Get-Scene $showcase "liquid_gallery"
$liquidContext = Get-Bookmark $liquid "liquid_context"
if ($liquidContext -and ([double]$liquidContext.fov -gt 48.0)) {
    Add-Failure "liquid_gallery.liquid_context should avoid the old wide flat-panel framing"
}

$rt = Get-Scene $showcase "rt_showcase"
$reflectionCloseup = Get-Bookmark $rt "reflection_closeup"
if ($reflectionCloseup) {
    if ([double]$reflectionCloseup.position[0] -lt -15.0) {
        Add-Failure "rt_showcase.reflection_closeup still uses the old far-left wall framing"
    }
    if ([double]$reflectionCloseup.fov -gt 44.0) {
        Add-Failure "rt_showcase.reflection_closeup should be a tighter closeup"
    }
}

Assert-Contains "release validation" $releaseSource "scene_polish_contract"
Assert-Contains "release validation" $releaseSource "run_scene_polish_contract_tests.ps1"
Assert-Contains "public capture gallery established filter" $captureGallerySource "[switch]`$EstablishedOnly"
Assert-Contains "public capture gallery established filter" $captureGallerySource "-AssetLedOnly and -EstablishedOnly are mutually exclusive."
Assert-Contains "public capture gallery established filter" $captureGallerySource '$assetLedScenes -notcontains $_.scene'

Assert-Contains "rt showcase dragon display" $sceneSource "RTGallery_MetalDragon"
Assert-Contains "rt showcase dragon upright rotation" $sceneSource "glm::radians(90.0f), glm::radians(180.0f)"
Assert-Contains "rt showcase dragon plinth scale" $sceneSource "dt.scale = glm::vec3(0.16f)"
Assert-Contains "rt showcase dragon plinth seating" $sceneSource "dt.position = glm::vec3(galleryX, 0.82f, 1.2f)"

if ($failures.Count -gt 0) {
    Write-Host "Scene polish contract failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Scene polish contract passed." -ForegroundColor Green
Write-Host "  beach=shoreline/palms/props"
Write-Host "  courtyard=coping/canopy/architecture"
Write-Host "  rt_showcase=dragon/framing/reflection_closeup"
