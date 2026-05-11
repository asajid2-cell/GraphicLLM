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

Assert-Contains $header "SceneEditorWindow" "SceneEditorWindow facade is missing."
Assert-Contains $ui "kMaterialPresetLabels" "Scene editor does not expose material preset labels."
Assert-Contains $ui "comboFocusedMaterial" "Scene editor does not expose focused material preset dropdown."
Assert-Contains $ui "sliderFocusedMetallic" "Scene editor does not expose focused metallic slider."
Assert-Contains $ui "sliderFocusedRoughness" "Scene editor does not expose focused roughness slider."
Assert-Contains $ui "sliderFocusedClearcoat" "Scene editor does not expose focused clearcoat slider."
Assert-Contains $ui "sliderFocusedClearcoatRoughness" "Scene editor does not expose focused clearcoat roughness slider."
Assert-Contains $ui "sliderFocusedTransmission" "Scene editor does not expose focused transmission slider."
Assert-Contains $ui "sliderFocusedEmissiveStrength" "Scene editor does not expose focused emissive strength slider."
Assert-Contains $ui "sliderFocusedSheen" "Scene editor does not expose focused sheen slider."
Assert-Contains $ui "sliderFocusedSubsurface" "Scene editor does not expose focused subsurface slider."
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

Write-Host "Material editor contract tests passed"
