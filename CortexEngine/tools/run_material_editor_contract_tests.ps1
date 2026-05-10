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
Assert-Contains $ui "IDC_SE_MATERIAL_VALIDATION" "Scene editor does not expose material validation status."
Assert-Contains $ui "RefreshMaterialValidationStatus" "Scene editor does not refresh material validation status."
Assert-Contains $ui "MaterialPresetRegistry::Resolve" "Scene editor validation does not reuse MaterialPresetRegistry."
Assert-Contains $ui "ModifyMaterialCommand" "Scene editor does not route material edits through ModifyMaterialCommand."
Assert-Contains $ui "Apply Material to Focused" "Scene editor does not expose focused material apply button."
Assert-Contains $commands "struct ModifyMaterialCommand" "ModifyMaterialCommand is missing."

Write-Host "Material editor contract tests passed"
