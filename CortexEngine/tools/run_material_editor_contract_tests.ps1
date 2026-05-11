param(
    [string]$SourceRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $SourceRoot = Resolve-Path (Join-Path $scriptDir "..")
} else {
    $SourceRoot = Resolve-Path $SourceRoot
}

function Read-Text([string]$relativePath) {
    $path = Join-Path $SourceRoot $relativePath
    if (-not (Test-Path $path)) {
        throw "Missing required file: $relativePath"
    }
    return Get-Content -Raw -Path $path
}

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if ($text.IndexOf($needle, [StringComparison]::Ordinal) -lt 0) {
        throw $message
    }
}

$ui = Read-Text "src/UI/SceneEditorWindow.cpp"
$header = Read-Text "src/UI/SceneEditorWindow.h"
$commands = Read-Text "src/LLM/SceneCommands.h"
$engine = Read-Text "src/Core/Engine.cpp"
$materialModel = Read-Text "src/Graphics/MaterialModel.cpp"
$presetRegistry = Read-Text "src/Graphics/MaterialPresetRegistry.cpp"
$presetHeader = Read-Text "src/Graphics/MaterialPresetRegistry.h"
$basicShader = Read-Text "assets/shaders/Basic.hlsl"
$resolveShader = Read-Text "assets/shaders/MaterialResolve.hlsl"

Assert-Contains $header "SceneEditorWindow" "SceneEditorWindow facade is missing."
Assert-Contains $presetHeader "MaterialPresetDescriptor" "Material preset registry does not expose canonical preset descriptors."
Assert-Contains $presetHeader "CanonicalPresets" "Material preset registry does not expose canonical preset list."
Assert-Contains $presetRegistry "MaterialPresetRegistry::CanonicalPresets" "Material preset registry canonical list is missing."
Assert-Contains $presetRegistry '"glass_panel"' "Material preset registry missing glass panel preset."
Assert-Contains $presetRegistry '"brushed_gold"' "Material preset registry missing brushed gold preset."
Assert-Contains $presetRegistry '"wet_stone"' "Material preset registry missing wet stone preset."
Assert-Contains $presetRegistry '"anisotropic_car_paint"' "Material preset registry missing anisotropic car paint preset."
Assert-Contains $presetRegistry '"procedural_marble"' "Material preset registry missing procedural marble preset."
Assert-Contains $ui "PopulateMaterialPresetCombo" "Scene editor does not populate material presets from the registry."
Assert-Contains $ui "MaterialPresetIdForComboIndex" "Scene editor does not map material combo selections to registry preset ids."
Assert-Contains $ui "comboFocusedMaterial" "Scene editor does not expose focused material preset dropdown."
Assert-Contains $ui "sliderFocusedMetallic" "Scene editor does not expose focused metallic slider."
Assert-Contains $ui "sliderFocusedRoughness" "Scene editor does not expose focused roughness slider."
Assert-Contains $ui "sliderFocusedClearcoat" "Scene editor does not expose focused clearcoat slider."
Assert-Contains $ui "sliderFocusedClearcoatRoughness" "Scene editor does not expose focused clearcoat roughness slider."
Assert-Contains $ui "sliderFocusedTransmission" "Scene editor does not expose focused transmission slider."
Assert-Contains $ui "sliderFocusedEmissiveStrength" "Scene editor does not expose focused emissive strength slider."
Assert-Contains $ui "sliderFocusedSheen" "Scene editor does not expose focused sheen slider."
Assert-Contains $ui "sliderFocusedSubsurface" "Scene editor does not expose focused subsurface slider."
Assert-Contains $ui "sliderFocusedAnisotropy" "Scene editor does not expose focused anisotropy slider."
Assert-Contains $ui "sliderFocusedWetness" "Scene editor does not expose focused wetness slider."
Assert-Contains $ui "sliderFocusedEmissiveBloom" "Scene editor does not expose focused emissive bloom slider."
Assert-Contains $ui "sliderFocusedProceduralMask" "Scene editor does not expose focused procedural mask slider."
Assert-Contains $ui "IDC_SE_MATERIAL_VALIDATION" "Scene editor does not expose material validation status."
Assert-Contains $ui "RefreshMaterialValidationStatus" "Scene editor does not refresh material validation status."
Assert-Contains $ui "MaterialPresetRegistry::Resolve" "Scene editor validation does not reuse MaterialPresetRegistry."
Assert-Contains $ui "ModifyMaterialCommand" "Scene editor does not route material edits through ModifyMaterialCommand."
Assert-Contains $ui "Apply Material to Focused" "Scene editor does not expose focused material apply button."
Assert-Contains $commands "struct ModifyMaterialCommand" "ModifyMaterialCommand is missing."
Assert-Contains $commands "setClearcoat" "ModifyMaterialCommand cannot set clearcoat."
Assert-Contains $commands "setClearcoatRoughness" "ModifyMaterialCommand cannot set clearcoat roughness."
Assert-Contains $commands "setTransmission" "ModifyMaterialCommand cannot set transmission."
Assert-Contains $commands "setEmissiveStrength" "ModifyMaterialCommand cannot set emissive strength."
Assert-Contains $commands "setSheen" "ModifyMaterialCommand cannot set sheen."
Assert-Contains $commands "setSubsurface" "ModifyMaterialCommand cannot set subsurface wrap."
Assert-Contains $commands "setAnisotropy" "ModifyMaterialCommand cannot set anisotropy."
Assert-Contains $commands "setWetness" "ModifyMaterialCommand cannot set wetness."
Assert-Contains $commands "setEmissiveBloom" "ModifyMaterialCommand cannot set emissive bloom."
Assert-Contains $commands "setProceduralMask" "ModifyMaterialCommand cannot set procedural mask."
Assert-Contains $engine '"anisotropy"' "Frame report does not expose focused material anisotropy."
Assert-Contains $engine '"wetness"' "Frame report does not expose focused material wetness."
Assert-Contains $engine '"emissive_bloom"' "Frame report does not expose focused material emissive bloom."
Assert-Contains $engine '"procedural_mask"' "Frame report does not expose focused material procedural mask."
Assert-Contains $materialModel "anisotropyStrength" "Material resolver does not pack authored anisotropy."
Assert-Contains $materialModel "wetnessFactor" "Material resolver does not pack authored wetness."
Assert-Contains $materialModel "emissiveBloomFactor" "Material resolver does not pack authored emissive bloom."
Assert-Contains $materialModel "proceduralMaskStrength" "Material resolver does not pack authored procedural mask."
Assert-Contains $basicShader "emissiveBloomBoost" "Forward shader does not consume emissive bloom authoring."
Assert-Contains $basicShader "g_ExtraParams.z" "Forward shader does not consume authored anisotropy."
Assert-Contains $basicShader "g_ExtraParams.w" "Forward shader does not consume authored wetness."
Assert-Contains $basicShader "ProceduralMaterialMask" "Forward shader does not consume authored procedural mask."
Assert-Contains $resolveShader "wetnessFactor" "Visibility-buffer material resolve does not consume authored wetness."
Assert-Contains $resolveShader "mat.transmissionParams.z" "Visibility-buffer material resolve does not consume emissive bloom."
Assert-Contains $resolveShader "mat.transmissionParams.w" "Visibility-buffer material resolve does not consume procedural mask."

Write-Host "Material editor contract tests passed"
