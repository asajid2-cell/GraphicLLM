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
$debugState = Read-Text "src/Graphics/RendererDebugLineState.h"
$debugImpl = Read-Text "src/Graphics/Renderer_DebugLines.cpp"
$debugPassHeader = Read-Text "src/Graphics/Passes/DebugLinePass.h"
$debugPassImpl = Read-Text "src/Graphics/Passes/DebugLinePass.cpp"

Assert-Matches "Renderer.h" $rendererHeader "#include\s+`"Graphics/RendererDebugLineState\.h`""
Assert-Matches "Renderer.h" $rendererHeader "DebugLineRenderState\s+m_debugLineState"
Assert-Matches "Renderer.h" $rendererHeader "RendererDebugViewState\s+m_debugViewState"
Assert-Matches "Renderer.h" $rendererHeader "AddDebugLine"
Assert-Matches "Renderer.h" $rendererHeader "ClearDebugLines"
Assert-Matches "Renderer.h" $rendererHeader "RenderDebugLines"

Assert-Matches "RendererDebugLineState.h" $debugState "struct\s+DebugLineVertex"
Assert-Matches "RendererDebugLineState.h" $debugState "struct\s+DebugLineRenderState"
Assert-Matches "RendererDebugLineState.h" $debugState "std::vector<DebugLineVertex>\s+lines"
Assert-Matches "RendererDebugLineState.h" $debugState "ComPtr<ID3D12Resource>\s+vertexBuffer"
Assert-Matches "RendererDebugLineState.h" $debugState "EnsureVertexBuffer"
Assert-Matches "RendererDebugLineState.h" $debugState "UploadVertices"
Assert-Matches "RendererDebugLineState.h" $debugState "VertexBufferView"
Assert-Matches "RendererDebugLineState.h" $debugState "ResetFrame"
Assert-Matches "RendererDebugLineState.h" $debugState "ResetResources"

Assert-Matches "Renderer_DebugLines.cpp" $debugImpl "Renderer::AddDebugLine"
Assert-Matches "Renderer_DebugLines.cpp" $debugImpl "Renderer::ClearDebugLines"
Assert-Matches "Renderer_DebugLines.cpp" $debugImpl "Renderer::RenderDebugLines"
Assert-Matches "Renderer_DebugLines.cpp" $debugImpl "m_debugLineState\.lines"
Assert-Matches "Renderer_DebugLines.cpp" $debugImpl "debugLineDraws"
Assert-Matches "Renderer_DebugLines.cpp" $debugImpl "debugLineVertices"
Assert-Matches "Renderer_DebugLines.cpp" $debugImpl "m_debugLineState\.lines\.clear\(\)"
Assert-Matches "Renderer_DebugLines.cpp" $debugImpl "DebugLinePass::Draw"

Assert-Matches "DebugLinePass.h" $debugPassHeader "namespace\s+Cortex::Graphics::DebugLinePass"
Assert-Matches "DebugLinePass.h" $debugPassHeader "struct\s+DrawContext"
Assert-Matches "DebugLinePass.cpp" $debugPassImpl "SetPipelineState"
Assert-Matches "DebugLinePass.cpp" $debugPassImpl "IASetPrimitiveTopology"
Assert-Matches "DebugLinePass.cpp" $debugPassImpl "DrawInstanced"

foreach ($forbidden in @("CreateCommittedResource", "vertexBuffer->Map", "vertexBuffer->Unmap", "SetPipelineState", "IASetPrimitiveTopology", "DrawInstanced")) {
    if ($debugImpl.IndexOf($forbidden, [StringComparison]::Ordinal) -ge 0) {
        Add-Failure "Renderer_DebugLines.cpp still owns debug-line resource or draw work: $forbidden"
    }
}

if ($rendererHeader -match "ComPtr<ID3D12Resource>\s+m_debug") {
    Add-Failure "Renderer.h declares a loose debug ComPtr instead of using RendererDebugLineState"
}

if ($failures.Count -gt 0) {
    Write-Host "Debug primitive contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Debug primitive contract tests passed." -ForegroundColor Green
Write-Host "  debug_line_state=owned"
Write-Host "  renderer_surface=methods_only"
Write-Host "  draw_contract=reported"
