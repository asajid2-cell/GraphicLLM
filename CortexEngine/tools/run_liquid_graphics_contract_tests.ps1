param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message) | Out-Null
}

function Read-Text([string]$RelativePath) {
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path $path)) {
        Add-Failure "Missing file: $RelativePath"
        return ""
    }
    return Get-Content $path -Raw
}

function Require-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if ($Text -notmatch [regex]::Escape($Needle)) {
        Add-Failure $Message
    }
}

$components = Read-Text "src/Scene/Components.h"
$shader = Read-Text "assets/shaders/Water.hlsl"
$renderer = Read-Text "src/Graphics/Renderer_WaterSurfaces.cpp"
$presets = Read-Text "src/Graphics/MaterialPresetRegistry.cpp"
$surface = Read-Text "src/Graphics/SurfaceClassification.h"
$frameContract = Read-Text "src/Graphics/FrameContract.h"
$frameJson = Read-Text "src/Graphics/FrameContractJson.cpp"
$snapshot = Read-Text "src/Graphics/Renderer_FrameContractSnapshot.cpp"
$scenes = Read-Text "src/Core/Engine_Scenes.cpp"
$engine = Read-Text "src/Core/Engine.cpp"
$camera = Read-Text "src/Core/Engine_Camera.cpp"
$showcase = Read-Text "assets/config/showcase_scenes.json"

foreach ($token in @("LiquidType", "Lava", "Honey", "Molasses", "absorption", "foamStrength", "viscosity", "emissiveHeat", "shallowTint", "deepTint")) {
    Require-Contains $components $token "WaterSurfaceComponent missing liquid schema token '$token'"
}

foreach ($preset in @('"lava"', '"honey"', '"molasses"')) {
    Require-Contains $presets $preset "MaterialPresetRegistry missing liquid preset $preset"
}
Require-Contains $surface "SurfacePresetIsLiquid" "Surface classification does not centralize liquid preset handling"
Require-Contains $surface "SurfacePresetContains(material, `"lava`")" "Lava is not classified as liquid/water surface"
Require-Contains $surface "SurfacePresetContains(renderable, `"molasses`")" "Molasses renderables are not classified as liquid/water surface"

foreach ($needle in @("liquidProfile.absorption", "liquidProfile.foamStrength", "liquidProfile.viscosity", "liquidProfile.emissiveHeat", "materialData.fractalParams0", "materialData.fractalParams1", "static_cast<float>(liquidProfile.liquidType)")) {
    Require-Contains $renderer $needle "Renderer_WaterSurfaces.cpp does not pack '$needle' into material constants"
}

foreach ($needle in @("uint liquidType", "edgeFoam", "FBM", "emissiveHeat", "liquidType == 1u", "liquidType == 2u", "liquidType == 3u")) {
    Require-Contains $shader $needle "Water.hlsl missing liquid shader feature '$needle'"
}

foreach ($needle in @("waterCount", "lavaCount", "honeyCount", "molassesCount", "avgAbsorption", "avgViscosity", "maxEmissiveHeat")) {
    Require-Contains $frameContract $needle "FrameContract water metrics missing '$needle'"
}
foreach ($jsonKey in @('"water_count"', '"lava_count"', '"honey_count"', '"molasses_count"', '"avg_absorption"', '"avg_viscosity"', '"max_emissive_heat"')) {
    Require-Contains $frameJson $jsonKey "FrameContractJson missing water metric key $jsonKey"
}
foreach ($needle in @("contract.water.lavaCount", "contract.water.honeyCount", "contract.water.molassesCount", "contract.water.avgAbsorption")) {
    Require-Contains $snapshot $needle "Frame contract snapshot does not fill '$needle'"
}

foreach ($needle in @("BuildLiquidGalleryScene", '{"Water"', '{"Lava"', '{"Honey"', '{"Molasses"', '"_Surface"', "ApplyLiquidGallerySceneControls")) {
    Require-Contains $scenes $needle "Liquid gallery scene missing '$needle'"
}
Require-Contains $engine "sceneLower == `"liquid_gallery`"" "Engine startup parser does not accept liquid_gallery"
Require-Contains $engine "ScenePreset::LiquidGallery" "Engine does not route LiquidGallery preset"
Require-Contains $camera 'sceneId = "liquid_gallery"' "Camera bookmarks are not wired for liquid_gallery"
Require-Contains $showcase '"id": "liquid_gallery"' "showcase_scenes.json missing liquid_gallery scene"
Require-Contains $showcase '"water_lava"' "showcase_scenes.json missing water_lava liquid bookmark"
Require-Contains $showcase '"viscous_pair"' "showcase_scenes.json missing viscous_pair liquid bookmark"

if ($failures.Count -gt 0) {
    Write-Host "Liquid graphics contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Liquid graphics contract tests passed" -ForegroundColor Green
exit 0
