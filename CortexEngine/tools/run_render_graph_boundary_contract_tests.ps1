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

function Assert-NotMatches([string]$Name, [string]$Text, [string]$Pattern) {
    if ($Text -match $Pattern) {
        Add-Failure "$Name still matches removed pattern: $Pattern"
    }
}

$matrix = Read-Text "tools/run_render_graph_transient_matrix.ps1"
$diagnostics = Read-Text "src/Graphics/Renderer_RenderGraphDiagnostics.cpp"
$vbGraph = Read-Text "src/Graphics/Passes/VisibilityBufferGraphPass.cpp"
$vbBoundary = Read-Text "src/Graphics/Renderer_RenderGraphVisibilityBuffer.cpp"
$bloomBoundary = Read-Text "src/Graphics/Renderer_RenderGraphBloom.cpp"

foreach ($envName in @(
    "CORTEX_RG_TRANSIENT_VALIDATE",
    "CORTEX_RG_HEAP_DUMP",
    "CORTEX_RG_DISABLE_ALIASING",
    "CORTEX_DISABLE_BLOOM_TRANSIENTS"
)) {
    Assert-Matches "run_render_graph_transient_matrix.ps1" $matrix $envName
}

foreach ($caseName in @(
    "aliasing_on_bloom_transients_on",
    "aliasing_off_bloom_transients_on",
    "aliasing_on_bloom_transients_off"
)) {
    Assert-Matches "run_render_graph_transient_matrix.ps1" $matrix $caseName
}

Assert-Matches "run_render_graph_transient_matrix.ps1" $matrix "fallback_executions\s+-ne\s+0"
Assert-Matches "run_render_graph_transient_matrix.ps1" $matrix "transient_validation_ran"
Assert-Matches "Renderer_RenderGraphDiagnostics.cpp" $diagnostics "CORTEX_RG_TRANSIENT_VALIDATE"
Assert-Matches "Renderer_RenderGraphDiagnostics.cpp" $diagnostics "RenderGraphValidationPass::AddTransientValidation"
Assert-Matches "Renderer_RenderGraphDiagnostics.cpp" $diagnostics "fallbackExecutions"

foreach ($passName in @(
    "VBClear",
    "VBVisibility",
    "VBMaterialResolve",
    "VBDebugBlit",
    "VBDeferredLighting"
)) {
    Assert-Matches "VisibilityBufferGraphPass.cpp" $vbGraph $passName
}

Assert-Matches "VisibilityBufferGraphPass.cpp" $vbGraph "AddStagedPath"
Assert-Matches "Renderer_RenderGraphVisibilityBuffer.cpp" $vbBoundary "AccumulateRenderGraphExecutionStats"
Assert-Matches "Renderer_RenderGraphVisibilityBuffer.cpp" $vbBoundary "visibility_buffer_graph_resources_missing"
Assert-Matches "Renderer_RenderGraphBloom.cpp" $bloomBoundary "graph path did not execute"
Assert-NotMatches "Renderer_RenderGraphBloom.cpp" $bloomBoundary "RenderBloom\("
Assert-NotMatches "Renderer_RenderGraphBloom.cpp" $bloomBoundary "falling back to legacy path"

foreach ($removedPattern in @(
    "AddLegacyPath",
    "LegacyPathContext",
    "VisibilityBufferPath",
    "visibility_buffer_legacy_graph_contract"
)) {
    Assert-NotMatches "VisibilityBufferGraphPass.cpp" $vbGraph $removedPattern
    Assert-NotMatches "Renderer_RenderGraphVisibilityBuffer.cpp" $vbBoundary $removedPattern
}

if ($failures.Count -gt 0) {
    Write-Host "Render graph boundary contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Render graph boundary contract tests passed." -ForegroundColor Green
Write-Host "  transient_matrix_env=covered"
Write-Host "  transient_validation_module=covered"
Write-Host "  vb_staged_boundary=covered"
Write-Host "  vb_legacy_boundary_removed=covered"
