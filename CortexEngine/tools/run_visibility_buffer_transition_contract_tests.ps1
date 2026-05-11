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

function Assert-MinCount([string]$Name, [string]$Text, [string]$Pattern, [int]$Minimum) {
    $count = [regex]::Matches($Text, $Pattern).Count
    if ($count -lt $Minimum) {
        Add-Failure "$Name expected at least $Minimum matches for pattern '$Pattern' but found $count"
    }
}

$visibilityHeader = Read-Text "src/Graphics/VisibilityBuffer.h"
$vbGraph = Read-Text "src/Graphics/Renderer_RenderGraphVisibilityBuffer.cpp"
$vbHelpers = Read-Text "src/Graphics/Renderer_RenderGraphVisibilityBufferHelpers.h"
$resolve = Read-Text "src/Graphics/VisibilityBuffer_Resolve.cpp"
$visibilityPass = Read-Text "src/Graphics/VisibilityBuffer_VisibilityPass.cpp"
$debugBlit = Read-Text "src/Graphics/VisibilityBuffer_DebugBlit.cpp"
$deferredLighting = Read-Text "src/Graphics/VisibilityBuffer_DeferredLighting.cpp"

foreach ($field in @(
    "visibilityPass",
    "materialResolve",
    "debugBlit",
    "clusteredLights",
    "brdfLut",
    "deferredLighting"
)) {
    Assert-Matches "VisibilityBuffer.h" $visibilityHeader "bool\s+$field\s*=\s*false"
    Assert-Matches "Renderer_RenderGraphVisibilityBuffer.cpp" $vbGraph "controls\.$field\s*=\s*true"
}

Assert-Matches "VisibilityBuffer.h" $visibilityHeader "struct\s+TransitionSkipControls"
Assert-Matches "VisibilityBuffer.h" $visibilityHeader "void\s+SetTransitionSkipControls"
Assert-Matches "VisibilityBuffer.h" $visibilityHeader "GetTransitionSkipControls\(\)\s+const"
Assert-Matches "VisibilityBuffer.h" $visibilityHeader "struct\s+ResourceStateSnapshot"
Assert-Matches "VisibilityBuffer.h" $visibilityHeader "GetResourceStateSnapshot\(\)\s+const"
Assert-Matches "VisibilityBuffer.h" $visibilityHeader "ApplyResourceStateSnapshot"

Assert-Matches "Renderer_RenderGraphVisibilityBufferHelpers.h" $vbHelpers "initialStates\s*=\s*visibilityBuffer\.GetResourceStateSnapshot\(\)"
foreach ($resource in @(
    "visibility",
    "albedo",
    "normalRoughness",
    "emissiveMetallic",
    "materialExt0",
    "materialExt1",
    "materialExt2",
    "brdfLut",
    "clusterRanges",
    "clusterLightIndices"
)) {
    Assert-Matches "Renderer_RenderGraphVisibilityBufferHelpers.h" $vbHelpers "initialStates\.$resource"
}

Assert-MinCount "Renderer_RenderGraphVisibilityBuffer.cpp" $vbGraph "GetResourceStateSnapshot\(\)" 8
Assert-MinCount "Renderer_RenderGraphVisibilityBuffer.cpp" $vbGraph "ApplyResourceStateSnapshot\(states\)" 7
Assert-MinCount "Renderer_RenderGraphVisibilityBuffer.cpp" $vbGraph "GetTransitionSkipControls\(\)" 7
Assert-MinCount "Renderer_RenderGraphVisibilityBuffer.cpp" $vbGraph "SetTransitionSkipControls\(controls\)" 7
Assert-MinCount "Renderer_RenderGraphVisibilityBuffer.cpp" $vbGraph "SetTransitionSkipControls\(previousControls\)" 7

foreach ($state in @(
    "finalStates\.visibility\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.visibility\)",
    "finalStates\.albedo\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.albedo\)",
    "finalStates\.normalRoughness\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.normalRoughness\)",
    "finalStates\.emissiveMetallic\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.emissiveMetallic\)",
    "finalStates\.materialExt0\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.materialExt0\)",
    "finalStates\.materialExt1\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.materialExt1\)",
    "finalStates\.materialExt2\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.materialExt2\)",
    "finalStates\.brdfLut\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.brdfLut\)",
    "finalStates\.clusterRanges\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.clusterRanges\)",
    "finalStates\.clusterLightIndices\s*=\s*m_services\.renderGraph->GetResourceState\(vbResources\.clusterLightIndices\)"
)) {
    Assert-Matches "Renderer_RenderGraphVisibilityBuffer.cpp" $vbGraph $state
}
Assert-Matches "Renderer_RenderGraphVisibilityBuffer.cpp" $vbGraph "ApplyResourceStateSnapshot\(finalStates\)"

Assert-Matches "VisibilityBuffer_VisibilityPass.cpp" $visibilityPass "!m_transitionSkip\.visibilityPass"
Assert-Matches "VisibilityBuffer_Resolve.cpp" $resolve "!m_transitionSkip\.materialResolve"
Assert-Matches "VisibilityBuffer_DebugBlit.cpp" $debugBlit "!m_transitionSkip\.debugBlit"
Assert-Matches "VisibilityBuffer_DeferredLighting.cpp" $deferredLighting "!m_transitionSkip\.clusteredLights"
Assert-Matches "VisibilityBuffer_DeferredLighting.cpp" $deferredLighting "!m_transitionSkip\.brdfLut"
Assert-Matches "VisibilityBuffer_DeferredLighting.cpp" $deferredLighting "!m_transitionSkip\.deferredLighting"

if ($failures.Count -gt 0) {
    Write-Host "Visibility buffer transition contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Visibility buffer transition contract tests passed." -ForegroundColor Green
Write-Host "  transition_skip_fields=covered"
Write-Host "  graph_owned_state_snapshots=covered"
Write-Host "  graph_final_state_writeback=covered"
