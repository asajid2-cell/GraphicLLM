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

function Assert-Contains([string]$Name, [string]$Text, [string]$Needle) {
    if ($Text.IndexOf($Needle, [StringComparison]::Ordinal) -lt 0) {
        Add-Failure "$Name missing required declaration marker: $Needle"
    }
}

function Assert-NotContains([string]$Name, [string]$Text, [string]$Needle) {
    if ($Text.IndexOf($Needle, [StringComparison]::Ordinal) -ge 0) {
        Add-Failure "$Name still contains renderer-side graph declaration marker: $Needle"
    }
}

$expectedGraphPassFiles = @(
    "BloomGraphPass.cpp",
    "DepthPrepass.cpp",
    "HZBPass.cpp",
    "MotionVectorPass.cpp",
    "PostProcessGraphPass.cpp",
    "RenderGraphValidationPass.cpp",
    "ShadowPass.cpp",
    "SSAOPass.cpp",
    "SSRPass.cpp",
    "TAAPass.cpp",
    "VisibilityBufferGraphPass.cpp"
)

$passesDir = Join-Path $root "src/Graphics/Passes"
$actualGraphPassFiles = @(
    Get-ChildItem $passesDir -Filter "*.cpp" -File |
        Where-Object {
            $text = Get-Content $_.FullName -Raw
            $text.IndexOf("graph.AddPass", [StringComparison]::Ordinal) -ge 0
        } |
        ForEach-Object { $_.Name } |
        Sort-Object
)

$missingGraphPassFiles = @($expectedGraphPassFiles | Where-Object { $actualGraphPassFiles -notcontains $_ })
if ($missingGraphPassFiles.Count -gt 0) {
    Add-Failure "Expected graph pass files no longer declare graph passes:`n$($missingGraphPassFiles -join [Environment]::NewLine)"
}

$unexpectedGraphPassFiles = @($actualGraphPassFiles | Where-Object { $expectedGraphPassFiles -notcontains $_ })
if ($unexpectedGraphPassFiles.Count -gt 0) {
    Add-Failure "New graph pass files must be added to this declaration contract:`n$($unexpectedGraphPassFiles -join [Environment]::NewLine)"
}

foreach ($rendererGraphFile in Get-ChildItem (Join-Path $root "src/Graphics") -Filter "Renderer_RenderGraph*.cpp" -File) {
    $text = Get-Content $rendererGraphFile.FullName -Raw
    foreach ($forbidden in @("graph.AddPass", "RGPassBuilder", "builder.", "CreateTransient(")) {
        Assert-NotContains $rendererGraphFile.Name $text $forbidden
    }
}

$bloomHeader = Read-Text "src/Graphics/Passes/BloomGraphPass.h"
$bloomGraph = Read-Text "src/Graphics/Passes/BloomGraphPass.cpp"
$rendererBloom = Read-Text "src/Graphics/Renderer_RenderGraphBloom.cpp"
Assert-Contains "BloomGraphPass.h" $bloomHeader "std::span<ID3D12Resource* const> bloomATemplates"
Assert-Contains "BloomGraphPass.h" $bloomHeader "std::span<ID3D12Resource* const> bloomBTemplates"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "void DeclareTransients(RGPassBuilder& builder, const StandaloneBloomContext& context)"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "builder.CreateTransient("
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "builder.Read(context.hdr, RGResourceUsage::ShaderResource)"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "builder.Write(context.bloomA[0], RGResourceUsage::RenderTarget)"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "builder.Read(context.bloomB[combinedLevel], RGResourceUsage::CopySrc)"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "builder.Write(context.bloomA[combinedLevel], RGResourceUsage::CopyDst)"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "BloomPass::RenderFullscreen(context.fullscreen"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "BloomPass::RenderComposite(context.fullscreen"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "BloomPass::CopyCompositeToCombined({"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "MarkHdrShaderResource(context)"
Assert-Contains "BloomGraphPass.cpp" $bloomGraph "MarkBloomRan(context)"
Assert-NotContains "BloomGraphPass.h" $bloomHeader "markHdrShaderResource"
Assert-NotContains "BloomGraphPass.h" $bloomHeader "markBloomRan"
Assert-NotContains "Renderer_RenderGraphBloom.cpp" $rendererBloom "declareBloomTransients"
Assert-NotContains "Renderer_RenderGraphBloom.cpp" $rendererBloom "builder.CreateTransient"
Assert-NotContains "Renderer_RenderGraphBloom.cpp" $rendererBloom "RenderBloomDownsample"
Assert-NotContains "Renderer_RenderGraphBloom.cpp" $rendererBloom "RenderBloomBlur"
Assert-NotContains "Renderer_RenderGraphBloom.cpp" $rendererBloom "RenderBloomComposite"
Assert-NotContains "Renderer_RenderGraphBloom.cpp" $rendererBloom "CopyBloomCompositeToCombined"
Assert-NotContains "Renderer_RenderGraphBloom.cpp" $rendererBloom "PrepareBloomPassState"

$depth = Read-Text "src/Graphics/Passes/DepthPrepass.cpp"
$depthHeader = Read-Text "src/Graphics/Passes/DepthPrepass.h"
$rendererDepthShadow = Read-Text "src/Graphics/Renderer_RenderGraphDepthShadow.cpp"
Assert-Contains "DepthPrepass.cpp" $depth "graph.AddPass("
Assert-Contains "DepthPrepass.cpp" $depth "builder.Write(context.depth, RGResourceUsage::DepthStencilWrite)"
Assert-Contains "DepthPrepass.cpp" $depth "Draw(context.draw)"
Assert-Contains "DepthPrepass.cpp" $depth "DepthPrepassTargetPass::BindAndClear(context.target)"
Assert-NotContains "DepthPrepass.h" $depthHeader "std::function<bool()> execute"
Assert-NotContains "Renderer_RenderGraphDepthShadow.cpp" $rendererDepthShadow "RenderDepthPrepass(registry)"

$hzb = Read-Text "src/Graphics/Passes/HZBPass.cpp"
Assert-Contains "HZBPass.cpp" $hzb "builder.Read(depthHandle, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead)"
Assert-Contains "HZBPass.cpp" $hzb "builder.Write(hzbHandle, RGResourceUsage::UnorderedAccess, 0)"
Assert-Contains "HZBPass.cpp" $hzb "builder.Read(hzbHandle, RGResourceUsage::ShaderResource, inMip)"
Assert-Contains "HZBPass.cpp" $hzb "builder.Write(hzbHandle, RGResourceUsage::UnorderedAccess, outMip)"

$motion = Read-Text "src/Graphics/Passes/MotionVectorPass.cpp"
$motionHeader = Read-Text "src/Graphics/Passes/MotionVectorPass.h"
$rendererMotion = Read-Text "src/Graphics/Renderer_RenderGraphMotionVectors.cpp"
Assert-Contains "MotionVectorPass.cpp" $motion "builder.Read(context.visibility, RGResourceUsage::ShaderResource)"
Assert-Contains "MotionVectorPass.cpp" $motion "builder.Write(context.velocity, RGResourceUsage::UnorderedAccess)"
Assert-Contains "MotionVectorPass.cpp" $motion "builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead)"
Assert-Contains "MotionVectorPass.cpp" $motion "MotionVectorTargetPass::TransitionCameraTargets(context.cameraTarget)"
Assert-Contains "MotionVectorPass.cpp" $motion "Draw(context.cameraDraw)"
Assert-Contains "MotionVectorPass.cpp" $motion "ComputeVisibilityBufferMotion(context.visibilityMotion)"
Assert-NotContains "MotionVectorPass.h" $motionHeader "drawCameraMotion"
Assert-NotContains "MotionVectorPass.h" $motionHeader "std::function<bool()> computeVisibilityBufferMotion"
Assert-NotContains "Renderer_RenderGraphMotionVectors.cpp" $rendererMotion "RenderMotionVectors();"
Assert-NotContains "Renderer_RenderGraphMotionVectors.cpp" $rendererMotion "ComputeMotionVectors("

$post = Read-Text "src/Graphics/Passes/PostProcessGraphPass.cpp"
$postHeader = Read-Text "src/Graphics/Passes/PostProcessGraphPass.h"
$rendererEndFrame = Read-Text "src/Graphics/Renderer_RenderGraphEndFrame.cpp"
Assert-Contains "PostProcessGraphPass.cpp" $post "void Declare(RGPassBuilder& builder, const ResourceHandles& resources)"
Assert-Contains "PostProcessGraphPass.cpp" $post "builder.Read(resources.hdr, RGResourceUsage::ShaderResource)"
Assert-Contains "PostProcessGraphPass.cpp" $post "builder.Write(resources.backBuffer, RGResourceUsage::RenderTarget)"
Assert-Contains "PostProcessGraphPass.cpp" $post "PostProcessPass::UpdateDescriptorTable"
Assert-Contains "PostProcessGraphPass.cpp" $post "PostProcessPass::Draw"
Assert-NotContains "PostProcessGraphPass.h" $postHeader "renderWithBloomOverride"
Assert-NotContains "PostProcessGraphPass.h" $postHeader "renderDefault"
Assert-NotContains "Renderer_RenderGraphEndFrame.cpp" $rendererEndFrame "RenderPostProcess();"

$validation = Read-Text "src/Graphics/Passes/RenderGraphValidationPass.cpp"
$validationHeader = Read-Text "src/Graphics/Passes/RenderGraphValidationPass.h"
$rendererDiagnostics = Read-Text "src/Graphics/Renderer_RenderGraphDiagnostics.cpp"
Assert-Contains "RenderGraphValidationPass.cpp" $validation "RenderGraphValidationPass"
Assert-Contains "RenderGraphValidationPass.cpp" $validation "builder.Write(*transientA, RGResourceUsage::RenderTarget)"
Assert-Contains "RenderGraphValidationPass.cpp" $validation "builder.Write(*transientB, RGResourceUsage::RenderTarget)"
Assert-Contains "RenderGraphValidationPass.cpp" $validation "CreateTransientValidationViews(DescriptorHeapManager* descriptorManager)"
Assert-Contains "RenderGraphValidationPass.cpp" $validation "ClearTransientTarget(commandList, graph, *transientA"
Assert-Contains "RenderGraphValidationPass.cpp" $validation "ClearTransientTarget(commandList, graph, *transientB"
Assert-NotContains "RenderGraphValidationPass.h" $validationHeader "StageCallback"
Assert-NotContains "Renderer_RenderGraphDiagnostics.cpp" $rendererDiagnostics "DescriptorTable::EnsureColorTargetViewHandles"
Assert-NotContains "Renderer_RenderGraphDiagnostics.cpp" $rendererDiagnostics "DescriptorTable::WriteTexture2DRTVAndSRV"
Assert-NotContains "Renderer_RenderGraphDiagnostics.cpp" $rendererDiagnostics "ClearRenderTargetView"

$shadow = Read-Text "src/Graphics/Passes/ShadowPass.cpp"
$shadowHeader = Read-Text "src/Graphics/Passes/ShadowPass.h"
Assert-Contains "ShadowPass.cpp" $shadow "builder.Write(context.shadowMap, RGResourceUsage::DepthStencilWrite)"
Assert-Contains "ShadowPass.cpp" $shadow "builder.Read(context.shadowMap, RGResourceUsage::ShaderResource)"
Assert-Contains "ShadowPass.cpp" $shadow "Draw(context.draw)"
Assert-Contains "ShadowPass.cpp" $shadow "ShadowTargetPass::TransitionToDepthWrite(context.target)"
Assert-Contains "ShadowPass.cpp" $shadow "ShadowTargetPass::BindAndClearSlice(sliceContext)"
Assert-NotContains "ShadowPass.h" $shadowHeader "std::function<bool()> execute"
Assert-NotContains "Renderer_RenderGraphDepthShadow.cpp" $rendererDepthShadow "RenderShadowPass(registry)"

$ssao = Read-Text "src/Graphics/Passes/SSAOPass.cpp"
$ssaoHeader = Read-Text "src/Graphics/Passes/SSAOPass.h"
$rendererSSAO = Read-Text "src/Graphics/Renderer_RenderGraphSSAO.cpp"
Assert-Contains "SSAOPass.cpp" $ssao "builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead)"
Assert-Contains "SSAOPass.cpp" $ssao "builder.Write(context.ssao"
Assert-Contains "SSAOPass.cpp" $ssao "builder.Read(context.ssao, RGResourceUsage::ShaderResource)"
Assert-Contains "SSAOPass.cpp" $ssao "DispatchCompute(context.compute)"
Assert-Contains "SSAOPass.cpp" $ssao "DrawGraphics(context.graphics)"
Assert-NotContains "SSAOPass.h" $ssaoHeader "std::function<bool()> execute"
Assert-NotContains "Renderer_RenderGraphSSAO.cpp" $rendererSSAO "RenderSSAO();"
Assert-NotContains "Renderer_RenderGraphSSAO.cpp" $rendererSSAO "RenderSSAOAsync();"

$ssr = Read-Text "src/Graphics/Passes/SSRPass.cpp"
$ssrHeader = Read-Text "src/Graphics/Passes/SSRPass.h"
$rendererSSR = Read-Text "src/Graphics/Renderer_RenderGraphSSR.cpp"
Assert-Contains "SSRPass.cpp" $ssr "builder.Read(context.hdr, RGResourceUsage::ShaderResource)"
Assert-Contains "SSRPass.cpp" $ssr "builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead)"
Assert-Contains "SSRPass.cpp" $ssr "builder.Read(context.normalRoughness, RGResourceUsage::ShaderResource)"
Assert-Contains "SSRPass.cpp" $ssr "builder.Write(context.ssr, RGResourceUsage::RenderTarget)"
Assert-Contains "SSRPass.cpp" $ssr "PrepareTargets(context.prepare)"
Assert-Contains "SSRPass.cpp" $ssr "Draw(context.draw)"
Assert-NotContains "SSRPass.h" $ssrHeader "std::function<bool()> execute"
Assert-NotContains "Renderer_RenderGraphSSR.cpp" $rendererSSR "RenderSSR();"

$taa = Read-Text "src/Graphics/Passes/TAAPass.cpp"
$taaHeader = Read-Text "src/Graphics/Passes/TAAPass.h"
$rendererTAA = Read-Text "src/Graphics/Renderer_RenderGraphTAA.cpp"
Assert-Contains "TAAPass.cpp" $taa "builder.Read(context.hdr, RGResourceUsage::ShaderResource)"
Assert-Contains "TAAPass.cpp" $taa "builder.Read(context.history, RGResourceUsage::ShaderResource)"
Assert-Contains "TAAPass.cpp" $taa "builder.Write(context.intermediate, RGResourceUsage::RenderTarget)"
Assert-Contains "TAAPass.cpp" $taa "builder.Write(context.history, RGResourceUsage::CopyDst)"
Assert-Contains "TAAPass.cpp" $taa "TAACopyPass::CopyHdrToHistory(context.seedHistory)"
Assert-Contains "TAAPass.cpp" $taa "TAACopyPass::PrepareResolveInputs(context.resolveInputs)"
Assert-Contains "TAAPass.cpp" $taa "UpdateResolveDescriptorTable(context.resolveDescriptors)"
Assert-Contains "TAAPass.cpp" $taa "TAACopyPass::CopyIntermediateToHdr(context.copyToHDR)"
Assert-NotContains "TAAPass.h" $taaHeader "std::function<bool()> seedHistory"
Assert-NotContains "TAAPass.h" $taaHeader "std::function<bool()> resolve"
Assert-NotContains "Renderer_RenderGraphTAA.cpp" $rendererTAA "SeedTAAHistory(true)"
Assert-NotContains "Renderer_RenderGraphTAA.cpp" $rendererTAA "ResolveTAAIntermediate(true)"
Assert-NotContains "Renderer_RenderGraphTAA.cpp" $rendererTAA "CopyTAAIntermediateToHDR(true)"
Assert-NotContains "Renderer_RenderGraphTAA.cpp" $rendererTAA "CopyHDRToTAAHistory(true)"

$temporalMask = Read-Text "src/Graphics/TemporalRejectionMask.cpp"
$temporalMaskHeader = Read-Text "src/Graphics/TemporalRejectionMask.h"
$rendererTemporalMask = Read-Text "src/Graphics/Renderer_RenderGraphTemporalMask.cpp"
Assert-Contains "TemporalRejectionMask.h" $temporalMaskHeader "DispatchExecutionContext dispatch"
Assert-Contains "TemporalRejectionMask.cpp" $temporalMask "bool TemporalRejectionMask::ExecuteDispatch(const DispatchExecutionContext& context)"
Assert-Contains "TemporalRejectionMask.cpp" $temporalMask "ExecuteDispatch(dispatch)"
Assert-Contains "Renderer_TemporalMaskPass.cpp" (Read-Text "src/Graphics/Renderer_TemporalMaskPass.cpp") "TemporalRejectionMask::ExecuteDispatch({"
Assert-NotContains "TemporalRejectionMask.h" $temporalMaskHeader "std::function<bool()> dispatch"
Assert-NotContains "Renderer_RenderGraphTemporalMask.cpp" $rendererTemporalMask "BuildTemporalRejectionMask("

$vb = Read-Text "src/Graphics/Passes/VisibilityBufferGraphPass.cpp"
$vbHeader = Read-Text "src/Graphics/Passes/VisibilityBufferGraphPass.h"
$rendererVB = Read-Text "src/Graphics/Renderer_RenderGraphVisibilityBuffer.cpp"
foreach ($marker in @(
    "builder.Write(resources.visibility, RGResourceUsage::UnorderedAccess)",
    "builder.Write(resources.visibility, RGResourceUsage::RenderTarget)",
    "builder.Write(resources.depth, RGResourceUsage::DepthStencilWrite)",
    "builder.Read(resources.visibility, RGResourceUsage::ShaderResource)",
    "builder.Write(resources.albedo, RGResourceUsage::UnorderedAccess)",
    "builder.Write(resources.normalRoughness, RGResourceUsage::UnorderedAccess)",
    "builder.Write(resources.hdr, RGResourceUsage::RenderTarget)"
)) {
    Assert-Contains "VisibilityBufferGraphPass.cpp" $vb $marker
}
Assert-Contains "VisibilityBufferGraphPass.cpp" $vb "Clear(context.clear)"
Assert-Contains "VisibilityBufferGraphPass.cpp" $vb "RasterizeVisibility(context.visibility)"
Assert-Contains "VisibilityBufferGraphPass.cpp" $vb "ResolveMaterials(context.materialResolve)"
Assert-Contains "VisibilityBufferGraphPass.cpp" $vb "DebugBlit(context.debugBlit)"
Assert-Contains "VisibilityBufferGraphPass.cpp" $vb "GenerateBRDFLUT(context.brdfLut)"
Assert-Contains "VisibilityBufferGraphPass.cpp" $vb "BuildClusteredLights(context.clusteredLights)"
Assert-Contains "VisibilityBufferGraphPass.cpp" $vb "ApplyDeferredLighting(context.deferredLighting)"
Assert-NotContains "VisibilityBufferGraphPass.h" $vbHeader "std::function<void()> clear"
Assert-NotContains "VisibilityBufferGraphPass.h" $vbHeader "std::function<void()> visibility"
Assert-NotContains "VisibilityBufferGraphPass.h" $vbHeader "std::function<void()> materialResolve"
Assert-NotContains "VisibilityBufferGraphPass.h" $vbHeader "std::function<void()> debugBlit"
Assert-NotContains "VisibilityBufferGraphPass.h" $vbHeader "std::function<void()> brdfLut"
Assert-NotContains "VisibilityBufferGraphPass.h" $vbHeader "std::function<void()> clusteredLights"
Assert-NotContains "VisibilityBufferGraphPass.h" $vbHeader "std::function<void()> deferredLighting"
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "ClearVisibilityBuffer("
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "RasterizeVisibilityBuffer("
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "RenderVisibilityBufferMaterialResolveStage("
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "DebugBlitVisibilityToHDR("
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "DebugBlitDepthToHDR("
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "DebugBlitGBufferToHDR("
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "EnsureBRDFLUT("
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "BuildClusteredLightLists("
Assert-NotContains "Renderer_RenderGraphVisibilityBuffer.cpp" $rendererVB "ApplyVisibilityBufferDeferredLighting("

if ($failures.Count -gt 0) {
    Write-Host "Render graph declaration contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Render graph declaration contract tests passed." -ForegroundColor Green
Write-Host "  graph_pass_files=$($actualGraphPassFiles.Count)"
Write-Host "  renderer_graph_wrappers=declaration_free"
Write-Host "  bloom_transients=pass_owned"
