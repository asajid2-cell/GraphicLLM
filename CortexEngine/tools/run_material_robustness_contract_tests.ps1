param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Read-Text([string]$RelativePath) {
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path $path)) {
        throw "Required file missing: $RelativePath"
    }
    return Get-Content -Path $path -Raw
}

function Require-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if ($Text.IndexOf($Needle, [System.StringComparison]::Ordinal) -lt 0) {
        Add-Failure $Message
    }
}

$registryHeader = Read-Text "src/Graphics/MaterialPresetRegistry.h"
$registry = Read-Text "src/Graphics/MaterialPresetRegistry.cpp"
$model = Read-Text "src/Graphics/MaterialModel.cpp"
$basic = Read-Text "assets/shaders/Basic.hlsl"
$resolve = Read-Text "assets/shaders/MaterialResolve.hlsl"
$deferred = Read-Text "assets/shaders/DeferredLighting.hlsl"
$release = Read-Text "tools/run_release_validation.ps1"
$ledger = Read-Text "docs/MATERIALS_GRAPHICS_ROBUSTNESS_LEDGER.md"

Require-Contains $registryHeader "Canonicalize" `
    "MaterialPresetRegistry does not expose Canonicalize for UI/config-safe preset names"
Require-Contains $registry "MaterialPresetRegistry::Canonicalize" `
    "MaterialPresetRegistry.cpp missing Canonicalize implementation"
Require-Contains $registry "std::isalnum" `
    "Material preset normalization must canonicalize punctuation and spacing"
Require-Contains $registry "lastWasSeparator" `
    "Material preset normalization must collapse repeated separators"
Require-Contains $registry "Normalize(preset.displayName)" `
    "Material preset canonicalization must accept public display names"
Require-Contains $registry "const std::string presetLower = Canonicalize(presetName);" `
    "Material preset Resolve must use canonicalized preset names"
Require-Contains $registry '{"car_paint", "anisotropic_car_paint"}' `
    "Material preset aliases must map car_paint to anisotropic_car_paint"
Require-Contains $registry '{"gold", "brushed_gold"}' `
    "Material preset aliases must map gold to brushed_gold"
Require-Contains $registry '{"marble", "procedural_marble"}' `
    "Material preset aliases must map marble to procedural_marble"

$presetIds = New-Object System.Collections.Generic.HashSet[string]
$publicPresetCount = 0
$advancedPresetCount = 0
$presetPattern = '\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*(true|false),\s*(true|false)\}'
foreach ($match in [regex]::Matches($registry, $presetPattern)) {
    $id = [string]$match.Groups[1].Value
    $advanced = [string]$match.Groups[4].Value
    $public = [string]$match.Groups[5].Value
    if (-not $presetIds.Add($id)) {
        Add-Failure "Duplicate material preset id '$id'"
    }
    if ($id -notmatch '^[a-z0-9_]+$') {
        Add-Failure "Material preset id '$id' is not normalized"
    }
    if ($public -eq "true") {
        ++$publicPresetCount
    }
    if ($advanced -eq "true") {
        ++$advancedPresetCount
    }
}
if ($presetIds.Count -lt 20) {
    Add-Failure "Expected at least 20 canonical material presets, found $($presetIds.Count)"
}
if ($publicPresetCount -ne $presetIds.Count) {
    Add-Failure "Every canonical material preset should currently be public-authoring-safe"
}
if ($advancedPresetCount -lt 10) {
    Add-Failure "Advanced material preset coverage is too low: $advancedPresetCount"
}

foreach ($id in @(
    "glass_panel",
    "mirror",
    "water",
    "neon_tube",
    "brushed_gold",
    "wet_stone",
    "anisotropic_car_paint",
    "procedural_marble"
)) {
    if (-not $presetIds.Contains($id)) {
        Add-Failure "Missing required public material preset '$id'"
    }
}

Require-Contains $model "if (preset.transmissive && model.transmissionFactor > 0.0f)" `
    "Transmissive preset defaults must clear metallic conflicts"
Require-Contains $model "glm::clamp(preset.defaultTransmission, 0.0f, 1.0f)" `
    "Preset transmission defaults must be clamped"
Require-Contains $model "glm::clamp(preset.defaultSpecularFactor, 0.0f, 2.0f)" `
    "Preset specular defaults must be clamped"
Require-Contains $model "Metallic transmission is physically ambiguous" `
    "Material validation must report metallic transmission"
Require-Contains $model "Very low roughness with a normal map can shimmer" `
    "Material validation must report low-roughness normal shimmer risk"

Require-Contains $basic "ProceduralMaterialMask" `
    "Forward shader must consume procedural material mask"
Require-Contains $basic "emissiveBloomBoost" `
    "Forward shader must consume emissive bloom authoring"
Require-Contains $basic "g_ExtraParams.z" `
    "Forward shader must consume anisotropy from material constants"
Require-Contains $basic "g_ExtraParams.w" `
    "Forward shader must consume wetness from material constants"
Require-Contains $resolve "wetnessFactor" `
    "Visibility-buffer material resolve must consume wetness"
Require-Contains $resolve "mat.transmissionParams.z" `
    "Visibility-buffer material resolve must consume emissive bloom"
Require-Contains $resolve "mat.transmissionParams.w" `
    "Visibility-buffer material resolve must consume procedural mask"
Require-Contains $deferred "float3 F0 = lerp(dielectricF0, albedoColor, metallic);" `
    "Deferred lighting must preserve conductor F0 from authored albedo/metallic"

Require-Contains $release "material_robustness_contract" `
    "Release validation must include material robustness contract step"
Require-Contains $release "run_material_robustness_contract_tests.ps1" `
    "Release validation must invoke run_material_robustness_contract_tests.ps1"

foreach ($id in @("MR-01", "MR-02", "MR-03", "GR-03", "GR-07")) {
    Require-Contains $ledger $id "Materials/graphics robustness ledger missing item $id"
}

if ($failures.Count -gt 0) {
    Write-Host "Material robustness contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Material robustness contract tests passed"
