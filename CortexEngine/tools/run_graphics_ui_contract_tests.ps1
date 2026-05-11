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

$cmake = Read-Text "CMakeLists.txt"
$engine = Read-Text "src/Core/Engine.cpp"
$input = Read-Text "src/Core/Engine_Input.cpp"
$ui = Read-Text "src/UI/GraphicsSettingsWindow.cpp"
$header = Read-Text "src/UI/GraphicsSettingsWindow.h"

Assert-Contains $cmake "src/UI/GraphicsSettingsWindow.cpp" "GraphicsSettingsWindow.cpp is not compiled by CMake."
Assert-Contains $cmake "src/UI/GraphicsSettingsWindow.h" "GraphicsSettingsWindow.h is not listed by CMake."

Assert-Contains $engine "UI/GraphicsSettingsWindow.h" "Engine.cpp does not include GraphicsSettingsWindow."
Assert-Contains $engine "GraphicsSettingsWindow::Initialize" "GraphicsSettingsWindow is not initialized."
Assert-Contains $engine "GraphicsSettingsWindow::Shutdown" "GraphicsSettingsWindow is not shut down."

Assert-Contains $input "UI/GraphicsSettingsWindow.h" "Engine_Input.cpp does not include GraphicsSettingsWindow."
Assert-Contains $input "GraphicsSettingsWindow::Toggle" "F8 path does not toggle GraphicsSettingsWindow."
Assert-Contains $input "GraphicsSettingsWindow::IsVisible" "ESC path does not check GraphicsSettingsWindow visibility."
Assert-Contains $input "GraphicsSettingsWindow::SetVisible(false)" "ESC path does not hide GraphicsSettingsWindow."

Assert-Contains $header "class GraphicsSettingsWindow" "GraphicsSettingsWindow public facade is missing."
Assert-Contains $ui "Graphics::RendererTuningState" "GraphicsSettingsWindow does not own RendererTuningState."
Assert-Contains $ui "Graphics::CaptureRendererTuningState" "GraphicsSettingsWindow does not capture renderer tuning state."
Assert-Contains $ui "Graphics::ApplyRendererTuningState" "GraphicsSettingsWindow does not apply renderer tuning state."
Assert-Contains $ui "BuildHealthState" "GraphicsSettingsWindow does not surface renderer health state."
Assert-Contains $ui "IDC_GFX_RT_SCHEDULER" "GraphicsSettingsWindow does not expose RT scheduler explanation panel."
Assert-Contains $ui "reflectionReadinessReason" "GraphicsSettingsWindow does not show RT reflection readiness reason."
Assert-Contains $ui "schedulerDisabledReason" "GraphicsSettingsWindow does not show RT scheduler skipped reason."
Assert-Contains $ui "IDC_GFX_RT_REFL_DENOISE" "GraphicsSettingsWindow does not expose RT reflection denoise tuning."
Assert-Contains $ui "IDC_GFX_RT_REFL_STRENGTH" "GraphicsSettingsWindow does not expose RT reflection composition tuning."
Assert-Contains $ui "reflectionDenoiseAlpha" "GraphicsSettingsWindow does not sync RT reflection denoise state."
Assert-Contains $ui "reflectionCompositionStrength" "GraphicsSettingsWindow does not sync RT reflection composition state."
Assert-Contains $ui "IDC_GFX_WARM" "GraphicsSettingsWindow does not expose warm color-grade tuning."
Assert-Contains $ui "IDC_GFX_COOL" "GraphicsSettingsWindow does not expose cool color-grade tuning."
Assert-Contains $ui "lighting.warm" "GraphicsSettingsWindow does not sync warm color-grade state."
Assert-Contains $ui "lighting.cool" "GraphicsSettingsWindow does not sync cool color-grade state."
Assert-Contains $ui "ApplyEnvironmentResidencyLoadControl" "GraphicsSettingsWindow does not expose environment residency controls."
Assert-Contains $ui "RendererLightingRigControl.h" "GraphicsSettingsWindow does not include lighting rig controls."
Assert-Contains $ui "IDC_GFX_RIG_STUDIO" "GraphicsSettingsWindow does not expose studio lighting rig control."
Assert-Contains $ui "IDC_GFX_RIG_WAREHOUSE" "GraphicsSettingsWindow does not expose top-down lighting rig control."
Assert-Contains $ui "IDC_GFX_RIG_SIDE" "GraphicsSettingsWindow does not expose side lighting rig control."
Assert-Contains $ui "IDC_GFX_RIG_LANTERNS" "GraphicsSettingsWindow does not expose lantern lighting rig control."
Assert-Contains $ui "ApplyLightingRigControl" "GraphicsSettingsWindow does not route lighting rig controls through the renderer applier."
Assert-Contains $ui "GetRegistry()" "GraphicsSettingsWindow does not pass the live scene registry to lighting rig controls."
Assert-Contains $ui "ApplyHeroVisualBaselineControls" "GraphicsSettingsWindow does not expose showcase baseline controls."
Assert-Contains $ui "SaveRendererTuningStateFile" "GraphicsSettingsWindow does not expose settings save."
Assert-Contains $ui "LoadRendererTuningStateFile" "GraphicsSettingsWindow does not expose settings load."
Assert-Contains $ui "IDC_GFX_VIGNETTE" "GraphicsSettingsWindow does not expose vignette control."
Assert-Contains $ui "IDC_GFX_LENS_DIRT" "GraphicsSettingsWindow does not expose lens dirt control."
Assert-Contains $ui "IDC_GFX_CINEMATIC_POST" "GraphicsSettingsWindow does not expose cinematic post enable control."
Assert-Contains $ui "IDC_GFX_BACKGROUND_VISIBLE" "GraphicsSettingsWindow does not expose background visibility control."
Assert-Contains $ui "IDC_GFX_BACKGROUND_EXPOSURE" "GraphicsSettingsWindow does not expose background exposure control."
Assert-Contains $ui "IDC_GFX_BACKGROUND_BLUR" "GraphicsSettingsWindow does not expose background blur control."
Assert-Contains $ui "IDC_GFX_PARTICLE_DENSITY" "GraphicsSettingsWindow does not expose particle density control."
Assert-Contains $ui "IDC_GFX_BOOKMARK_HERO" "GraphicsSettingsWindow does not expose hero camera bookmark control."
Assert-Contains $ui "IDC_GFX_BOOKMARK_REFLECTION" "GraphicsSettingsWindow does not expose reflection camera bookmark control."
Assert-Contains $ui "IDC_GFX_BOOKMARK_MATERIALS" "GraphicsSettingsWindow does not expose material overview camera bookmark control."
Assert-Contains $ui "ApplyShowcaseCameraBookmark" "GraphicsSettingsWindow does not route camera bookmark controls to Engine."
Assert-Contains $engine "CORTEX_GRAPHICS_SETTINGS_PATH" "Engine does not support user graphics settings path override."

Write-Host "Graphics UI contract tests passed"
