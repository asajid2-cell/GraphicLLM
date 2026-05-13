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
    if ($Text.IndexOf($Needle, [System.StringComparison]::Ordinal) -lt 0) {
        Add-Failure $Message
    }
}

$components = Read-Text "src/Scene/Components.h"
$waterShader = Read-Text "assets/shaders/Water.hlsl"
$basicShader = Read-Text "assets/shaders/Basic.hlsl"
$waterRenderer = Read-Text "src/Graphics/Renderer_WaterSurfaces.cpp"
$presets = Read-Text "src/Graphics/MaterialPresetRegistry.cpp"
$scenes = Read-Text "src/Core/Engine_Scenes.cpp"
$frameContract = Read-Text "src/Graphics/FrameContract.h"
$frameJson = Read-Text "src/Graphics/FrameContractJson.cpp"
$snapshot = Read-Text "src/Graphics/Renderer_FrameContractSnapshot.cpp"
$release = Read-Text "tools/run_release_validation.ps1"
$ledger = Read-Text "docs/MATERIAL_LIQUID_MATURITY_LEDGER.md"

foreach ($token in @(
    "bodyThickness",
    "sloshStrength",
    "meniscusStrength",
    "flowSpeed"
)) {
    Require-Contains $components $token "WaterSurfaceComponent missing maturity field '$token'"
    Require-Contains $waterRenderer "liquidProfile.$token" "Renderer_WaterSurfaces.cpp does not consume '$token'"
    Require-Contains $scenes "liquidComponent.$token" "Liquid gallery scene does not author '$token'"
}

foreach ($token in @(
    "materialData.coatParams",
    "objectData._pad0",
    "bodyThickness",
    "meniscusStrength",
    "sloshStrength",
    "flowSpeed"
)) {
    Require-Contains $waterRenderer $token "Water renderer missing packed liquid control '$token'"
}
foreach ($token in @(
    "bodyThickness",
    "meniscusStrength",
    "sloshStrength",
    "flowSpeed"
)) {
    Require-Contains $waterShader $token "Water shader missing liquid control '$token'"
}

foreach ($token in @(
    "edgeMass",
    "viscousMotion",
    "localPhase",
    "height += slosh",
    "height += edgeMass",
    "thicknessDepth",
    "alpha = saturate(alpha + bodyThickness"
)) {
    Require-Contains $waterShader $token "Water shader missing thick/moving liquid behavior '$token'"
}

foreach ($token in @(
    "glassOptics",
    "iorBend",
    "opticalNoise",
    "caustic",
    "transmittedTint",
    "fresnelEdge"
)) {
    Require-Contains $basicShader $token "Basic.hlsl missing glass optics/refraction cue '$token'"
}

foreach ($token in @(
    "specularIBL *= 1.35f",
    "metallic > 0.85f && roughness < 0.16f"
)) {
    Require-Contains $basicShader $token "Forward shader/preset missing chrome readability token '$token'"
}
Require-Contains $presets "defaultRoughness = contains(`"chrome`") ? 0.045f : 0.09f" `
    "MaterialPresetRegistry chrome default roughness was not tightened"
Require-Contains $presets "defaultSpecularFactor = contains(`"chrome`") ? 1.30f : 1.18f" `
    "MaterialPresetRegistry chrome specular factor was not strengthened"

foreach ($token in @(
    "MaterialLab_GlassRefractionStrip_Cyan",
    "MaterialLab_GlassRefractionStrip_Amber",
    "MaterialLab_GlassRefractionStrip_Red",
    "`"_Body`"",
    "LiquidGallery_GlassProbe",
    "GlassWaterCourtyard_WaterSurface"
)) {
    Require-Contains $scenes $token "Showcase scenes missing maturity demonstration '$token'"
}

foreach ($token in @(
    "avgBodyThickness",
    "avgSloshStrength",
    "avgMeniscusStrength",
    "avgFlowSpeed",
    "thickLiquidCount",
    "movingLiquidCount"
)) {
    Require-Contains $frameContract $token "FrameContract missing water maturity metric '$token'"
    Require-Contains $snapshot "contract.water.$token" "Frame contract snapshot missing '$token'"
}
foreach ($key in @(
    '"avg_body_thickness"',
    '"avg_slosh_strength"',
    '"avg_meniscus_strength"',
    '"avg_flow_speed"',
    '"thick_liquid_count"',
    '"moving_liquid_count"'
)) {
    Require-Contains $frameJson $key "FrameContractJson missing water maturity key $key"
}

Require-Contains $release "material_liquid_maturity_contract" `
    "Release validation must include material/liquid maturity contract"
Require-Contains $release "run_material_liquid_maturity_contract_tests.ps1" `
    "Release validation must invoke run_material_liquid_maturity_contract_tests.ps1"
Require-Contains $ledger "MATLIQ-008 Targeted and Full Validation" `
    "Material/liquid maturity ledger missing final validation gate"

if ($failures.Count -gt 0) {
    Write-Host "Material/liquid maturity contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Material/liquid maturity contract tests passed" -ForegroundColor Green
exit 0
