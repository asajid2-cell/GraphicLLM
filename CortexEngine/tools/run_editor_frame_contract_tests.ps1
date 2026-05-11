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
        Add-Failure "Missing required file: $RelativePath"
        return ""
    }
    return Get-Content $path -Raw
}

function Assert-Matches([string]$Name, [string]$Text, [string]$Pattern) {
    if ($Text -notmatch $Pattern) {
        Add-Failure "$Name does not match required pattern: $Pattern"
    }
}

$rendererHeader = Read-Text "src/Graphics/Renderer.h"
$editorHooks = Read-Text "src/Graphics/Renderer_EditorHooks.cpp"
$editorMode = Read-Text "src/Core/EngineEditorMode.cpp"

foreach ($method in @(
    "BeginFrameForEditor",
    "EndFrameForEditor",
    "PrepareMainPassForEditor",
    "UpdateFrameConstantsForEditor",
    "RenderSkyboxForEditor",
    "RenderShadowPassForEditor",
    "RenderSceneForEditor",
    "RenderSSAOForEditor",
    "RenderBloomForEditor",
    "RenderPostProcessForEditor",
    "RenderDebugLinesForEditor",
    "RenderTAAForEditor",
    "RenderSSRForEditor",
    "PrewarmMaterialDescriptorsForEditor"
)) {
    Assert-Matches "Renderer.h" $rendererHeader "void\s+$method"
    Assert-Matches "Renderer_EditorHooks.cpp" $editorHooks "Renderer::$method"
    Assert-Matches "EngineEditorMode.cpp" $editorMode $method
}

foreach ($delegate in @(
    "BeginFrame\(\)",
    "EndFrame\(\)",
    "PrepareMainPass\(\)",
    "UpdateFrameConstants\(deltaTime, registry\)",
    "RenderSkybox\(\)",
    "RenderShadowPass\(registry\)",
    "RenderScene\(registry\)",
    "RenderSSAO\(\)",
    "RenderBloom\(\)",
    "RenderPostProcess\(\)",
    "RenderDebugLines\(\)",
    "RenderTAA\(\)",
    "RenderSSR\(\)",
    "PrewarmMaterialDescriptors\(registry\)"
)) {
    Assert-Matches "Renderer_EditorHooks.cpp" $editorHooks $delegate
}

Assert-Matches "EngineEditorMode.cpp" $editorMode "(?s)BeginFrameForEditor\(\).*UpdateFrameConstantsForEditor\(deltaTime, m_registry\).*PrewarmMaterialDescriptorsForEditor\(m_registry\).*PrepareMainPassForEditor\(\).*RenderPostProcessForEditor\(\).*RenderDebugLinesForEditor\(\).*EndFrameForEditor\(\)"

if ($failures.Count -gt 0) {
    Write-Host "Editor frame contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Editor frame contract tests passed." -ForegroundColor Green
Write-Host "  renderer_editor_hooks=covered"
Write-Host "  editor_frame_sequence=covered"
