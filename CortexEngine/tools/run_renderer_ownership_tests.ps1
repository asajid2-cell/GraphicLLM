param(
    [string]$OwnershipPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OwnershipPath)) {
    $OwnershipPath = Join-Path $root "assets/config/renderer_ownership_targets.json"
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if (-not (Test-Path $OwnershipPath)) {
    throw "Renderer ownership target file not found: $OwnershipPath"
}

$doc = Get-Content $OwnershipPath -Raw | ConvertFrom-Json
$rendererHeaderPath = Join-Path $root "src/Graphics/Renderer.h"
if (-not (Test-Path $rendererHeaderPath)) {
    throw "Renderer.h not found: $rendererHeaderPath"
}
$rendererHeader = Get-Content $rendererHeaderPath -Raw
$rendererRtStatePath = Join-Path $root "src/Graphics/RendererRTState.h"
if (-not (Test-Path $rendererRtStatePath)) {
    throw "RendererRTState.h not found: $rendererRtStatePath"
}
$rendererRtState = Get-Content $rendererRtStatePath -Raw
$rtHistoryCopyPassPath = Join-Path $root "src/Graphics/Passes/RTHistoryCopyPass.cpp"
$frameEndPath = Join-Path $root "src/Graphics/Renderer_FrameEnd.cpp"
$endFrameShaderResourcePassPath = Join-Path $root "src/Graphics/Passes/EndFrameShaderResourcePass.cpp"
$backBufferPresentPassPath = Join-Path $root "src/Graphics/Passes/BackBufferPresentPass.cpp"
$particleStatePath = Join-Path $root "src/Graphics/RendererParticleState.h"
if (-not (Test-Path $particleStatePath)) {
    throw "RendererParticleState.h not found: $particleStatePath"
}
$particleState = Get-Content $particleStatePath -Raw
$particleRendererPath = Join-Path $root "src/Graphics/Renderer_Particles.cpp"
if (-not (Test-Path $particleRendererPath)) {
    throw "Renderer_Particles.cpp not found: $particleRendererPath"
}
$particleRenderer = Get-Content $particleRendererPath -Raw
$particlePassPath = Join-Path $root "src/Graphics/Passes/ParticleBillboardPass.cpp"
if (-not (Test-Path $particlePassPath)) {
    throw "ParticleBillboardPass.cpp not found: $particlePassPath"
}
$particlePass = Get-Content $particlePassPath -Raw
$debugLineStatePath = Join-Path $root "src/Graphics/RendererDebugLineState.h"
$debugLineRendererPath = Join-Path $root "src/Graphics/Renderer_DebugLines.cpp"
$debugLinePassPath = Join-Path $root "src/Graphics/Passes/DebugLinePass.cpp"
$debugLineState = if (Test-Path $debugLineStatePath) { Get-Content $debugLineStatePath -Raw } else { "" }
$debugLineRenderer = if (Test-Path $debugLineRendererPath) { Get-Content $debugLineRendererPath -Raw } else { "" }
$debugLinePass = if (Test-Path $debugLinePassPath) { Get-Content $debugLinePassPath -Raw } else { "" }
$postStatePath = Join-Path $root "src/Graphics/RendererPostProcessState.h"
if (-not (Test-Path $postStatePath)) {
    throw "RendererPostProcessState.h not found: $postStatePath"
}
$postState = Get-Content $postStatePath -Raw
$environmentStatePath = Join-Path $root "src/Graphics/RendererEnvironmentState.h"
if (-not (Test-Path $environmentStatePath)) {
    throw "RendererEnvironmentState.h not found: $environmentStatePath"
}
$environmentState = Get-Content $environmentStatePath -Raw
$bloomStatePath = Join-Path $root "src/Graphics/RendererBloomState.h"
$bloomRendererPath = Join-Path $root "src/Graphics/Renderer_Bloom.cpp"
$bloomPassPath = Join-Path $root "src/Graphics/Passes/BloomPass.cpp"
$temporalScreenPath = Join-Path $root "src/Graphics/RendererTemporalScreenState.h"
$taaCopyPassPath = Join-Path $root "src/Graphics/Passes/TAACopyPass.cpp"
$taaExecutionPath = Join-Path $root "src/Graphics/Renderer_TAAExecution.cpp"
$motionVectorTargetPassPath = Join-Path $root "src/Graphics/Passes/MotionVectorTargetPass.cpp"
$motionVectorRendererPath = Join-Path $root "src/Graphics/Renderer_MotionVectors.cpp"
$ssaoStatePath = Join-Path $root "src/Graphics/RendererSSAOState.h"
$ssrStatePath = Join-Path $root "src/Graphics/RendererSSRState.h"
$ssaoRendererPath = Join-Path $root "src/Graphics/Renderer_SSAO.cpp"
$ssaoPassPath = Join-Path $root "src/Graphics/Passes/SSAOPass.cpp"
$ssrRendererPath = Join-Path $root "src/Graphics/Renderer_SSRPass.cpp"
$ssrPassPath = Join-Path $root "src/Graphics/Passes/SSRPass.cpp"
$hzbStatePath = Join-Path $root "src/Graphics/RendererHZBState.h"
$hzbRendererPath = Join-Path $root "src/Graphics/Renderer_HZB.cpp"
$hzbPassPath = Join-Path $root "src/Graphics/Passes/HZBPass.cpp"
$shadowStatePath = Join-Path $root "src/Graphics/RendererShadowState.h"
$shadowResourcesPath = Join-Path $root "src/Graphics/Renderer_ShadowResources.cpp"
$shadowPassPath = Join-Path $root "src/Graphics/Renderer_ShadowPass.cpp"
$shadowTargetPassPath = Join-Path $root "src/Graphics/Passes/ShadowTargetPass.cpp"
$depthStatePath = Join-Path $root "src/Graphics/RendererDepthState.h"
$depthTargetPath = Join-Path $root "src/Graphics/Renderer_DepthTarget.cpp"
$depthPassesPath = Join-Path $root "src/Graphics/Renderer_DepthPasses.cpp"
$depthPrepassTargetPassPath = Join-Path $root "src/Graphics/Passes/DepthPrepassTargetPass.cpp"
$depthWriteTransitionPassPath = Join-Path $root "src/Graphics/Passes/DepthWriteTransitionPass.cpp"
$framePhasesMainPath = Join-Path $root "src/Graphics/Renderer_FramePhases_Main.cpp"
$mainTargetStatePath = Join-Path $root "src/Graphics/RendererMainTargetState.h"
$hdrTargetsPath = Join-Path $root "src/Graphics/Renderer_HDRTargets.cpp"
$mainPassSetupPath = Join-Path $root "src/Graphics/Renderer_MainPassSetup.cpp"
$mainPassTargetPassPath = Join-Path $root "src/Graphics/Passes/MainPassTargetPass.cpp"
$postProcessPath = Join-Path $root "src/Graphics/Renderer_PostProcess.cpp"
$postProcessTargetPassPath = Join-Path $root "src/Graphics/Passes/PostProcessTargetPass.cpp"
$rtReflectionDebugClearPassPath = Join-Path $root "src/Graphics/Passes/RTReflectionDebugClearPass.cpp"
$forwardTargetBindingPath = Join-Path $root "src/Graphics/Passes/ForwardTargetBindingPass.cpp"
$meshDrawPassPath = Join-Path $root "src/Graphics/Passes/MeshDrawPass.cpp"
$forwardPassPath = Join-Path $root "src/Graphics/Renderer_ForwardPass.cpp"
$depthPassPath = Join-Path $root "src/Graphics/Renderer_DepthPasses.cpp"
$shadowDrawPassPath = Join-Path $root "src/Graphics/Renderer_ShadowPass.cpp"
$waterSurfacesPath = Join-Path $root "src/Graphics/Renderer_WaterSurfaces.cpp"
$transparentGeometryPath = Join-Path $root "src/Graphics/Renderer_TransparentGeometry.cpp"
$overlayGeometryPath = Join-Path $root "src/Graphics/Renderer_OverlayGeometry.cpp"
$voxelPassPath = Join-Path $root "src/Graphics/Passes/VoxelPass.cpp"
$voxelRendererPath = Join-Path $root "src/Graphics/Renderer_Voxel.cpp"
$minimalFramePassPath = Join-Path $root "src/Graphics/Passes/MinimalFramePass.cpp"
$framePlanningPath = Join-Path $root "src/Graphics/Renderer_FramePlanning.cpp"

if ([int]$doc.schema -ne 1) {
    Add-Failure "renderer ownership schema must be 1"
}
if ($null -eq $doc.targets -or $doc.targets.Count -lt 1) {
    Add-Failure "renderer ownership target list is empty"
}

$ids = @{}
foreach ($target in $doc.targets) {
    $id = [string]$target.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "ownership target id is missing"
        continue
    }
    if ($ids.ContainsKey($id)) {
        Add-Failure "duplicate ownership target id '$id'"
    }
    $ids[$id] = $true

    if ([string]::IsNullOrWhiteSpace([string]$target.owner)) {
        Add-Failure "$id owner is missing"
    }
    $status = [string]$target.status
    if ([string]::IsNullOrWhiteSpace($status)) {
        Add-Failure "$id status is missing"
    } elseif ($status -notmatch "validated|release") {
        Add-Failure "$id status '$status' is not release validated"
    }
    if ($null -ne $target.current_issue) {
        Add-Failure "$id still declares current_issue; release ownership metadata must use release_boundary plus future_extension"
    }
    if ($null -ne $target.next_step) {
        Add-Failure "$id still declares next_step; release ownership metadata must separate validated scope from future extension"
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.release_boundary)) {
        Add-Failure "$id release_boundary is missing"
    }
    if ($null -eq $target.validated_contracts -or $target.validated_contracts.Count -lt 1) {
        Add-Failure "$id validated_contracts is empty"
    } else {
        foreach ($contract in $target.validated_contracts) {
            if ([string]::IsNullOrWhiteSpace([string]$contract)) {
                Add-Failure "$id has an empty validated_contracts entry"
            }
        }
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.future_extension)) {
        Add-Failure "$id future_extension is missing"
    }
    if ($null -eq $target.target_files -or $target.target_files.Count -lt 1) {
        Add-Failure "$id target_files is empty"
    } else {
        foreach ($file in $target.target_files) {
            $fileName = [string]$file
            $path = Join-Path $root ("src/Graphics/{0}" -f $fileName)
            if (-not (Test-Path $path)) {
                Add-Failure "$id target file does not exist under src/Graphics: $fileName"
            }
        }
    }

    if ($null -eq $target.renderer_state_members -or $target.renderer_state_members.Count -lt 1) {
        Add-Failure "$id renderer_state_members is empty"
    } else {
        foreach ($member in $target.renderer_state_members) {
            $memberName = [string]$member
            if ($rendererHeader -notmatch "\b$([regex]::Escape($memberName))\b") {
                Add-Failure "$id renderer state member not found in Renderer.h: $memberName"
            }
        }
    }

    if ($id -eq "rt_reflection_stats") {
        foreach ($required in @("struct TargetResources", "struct DescriptorTableBundle", "rawResources", "historyResources", "descriptors")) {
            if ($rendererRtState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "rt_reflection_stats missing bundled ownership marker in RendererRTState.h: $required"
            }
        }
        foreach ($oldField in @("rawStatsBuffer", "historyStatsBuffer", "rawReadback", "historyReadback", "descriptorSrvTables", "descriptorUavTables")) {
            if ($rendererRtState -match "\b$([regex]::Escape($oldField))\b") {
                Add-Failure "rt_reflection_stats still exposes loose state field in RendererRTState.h: $oldField"
            }
        }
    }

    if ($id -eq "rt_history_copy") {
        if (Test-Path $rtHistoryCopyPassPath) {
            $rtHistoryCopyPass = Get-Content $rtHistoryCopyPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::RTHistoryCopyPass", "CopyContext", "CopyToHistoryAndReturnToShaderResource", "ResourceBarrier", "CopyResource", "D3D12_RESOURCE_STATE_COPY_SOURCE", "D3D12_RESOURCE_STATE_COPY_DEST", "D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE")) {
                if ($rtHistoryCopyPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "rt_history_copy missing RTHistoryCopyPass marker: $required"
                }
            }
        } else {
            Add-Failure "rt_history_copy missing RTHistoryCopyPass.cpp"
        }
        if (Test-Path $frameEndPath) {
            $frameEnd = Get-Content $frameEndPath -Raw
            $routeCount = ([regex]::Matches($frameEnd, [regex]::Escape("RTHistoryCopyPass::CopyToHistoryAndReturnToShaderResource"))).Count
            if ($routeCount -lt 3) {
                Add-Failure "rt_history_copy expected three routed history-copy calls in Renderer_FrameEnd.cpp, found $routeCount"
            }
            foreach ($removedLocal in @("giBarrierCount", "reflBarrierCount")) {
                if ($frameEnd.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "rt_history_copy still has duplicated RT history barrier counter in Renderer_FrameEnd.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "rt_history_copy missing Renderer_FrameEnd.cpp"
        }
    }

    if ($id -eq "end_frame_shader_resource_transitions") {
        if (Test-Path $endFrameShaderResourcePassPath) {
            $endFrameShaderResourcePass = Get-Content $endFrameShaderResourcePassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::EndFrameShaderResourcePass", "TransitionTarget", "TransitionContext", "TransitionToPixelShaderResources", "ResourceBarrier", "D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE")) {
                if ($endFrameShaderResourcePass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "end_frame_shader_resource_transitions missing EndFrameShaderResourcePass marker: $required"
                }
            }
        } else {
            Add-Failure "end_frame_shader_resource_transitions missing EndFrameShaderResourcePass.cpp"
        }
        if (Test-Path $frameEndPath) {
            $frameEnd = Get-Content $frameEndPath -Raw
            if ($frameEnd.IndexOf("EndFrameShaderResourcePass::TransitionToPixelShaderResources", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "end_frame_shader_resource_transitions missing routed transition call in Renderer_FrameEnd.cpp"
            }
            foreach ($removedLocal in @("ppBarriers", "ppCount")) {
                if ($frameEnd.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "end_frame_shader_resource_transitions still has hand-built post-process barrier batch in Renderer_FrameEnd.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "end_frame_shader_resource_transitions missing Renderer_FrameEnd.cpp"
        }
    }

    if ($id -eq "back_buffer_present_transition") {
        if (Test-Path $backBufferPresentPassPath) {
            $backBufferPresentPass = Get-Content $backBufferPresentPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::BackBufferPresentPass", "VisualCaptureResult", "PresentContext", "TransitionBackBufferForPresent", "CreateCommittedResource", "CopyTextureRegion", "ResourceBarrier", "D3D12_RESOURCE_STATE_COPY_SOURCE", "D3D12_RESOURCE_STATE_PRESENT")) {
                if ($backBufferPresentPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "back_buffer_present_transition missing BackBufferPresentPass marker: $required"
                }
            }
        } else {
            Add-Failure "back_buffer_present_transition missing BackBufferPresentPass.cpp"
        }
        if (Test-Path $frameEndPath) {
            $frameEnd = Get-Content $frameEndPath -Raw
            if ($frameEnd.IndexOf("BackBufferPresentPass::TransitionBackBufferForPresent", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "back_buffer_present_transition missing routed present transition call in Renderer_FrameEnd.cpp"
            }
            foreach ($removedLocal in @("CreateCommittedResource", "CopyTextureRegion", "D3D12_RESOURCE_STATE_COPY_SOURCE", "D3D12_RESOURCE_STATE_PRESENT")) {
                if ($frameEnd.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "back_buffer_present_transition still has direct backbuffer present/capture mechanics in Renderer_FrameEnd.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "back_buffer_present_transition missing Renderer_FrameEnd.cpp"
        }
    }

    if ($id -eq "particle_resources") {
        foreach ($required in @("struct ParticleRenderControls", "struct ParticleRenderResources", "struct ParticleFrameStats", "struct ParticleRenderState", "ParticleRenderControls controls", "ParticleRenderResources resources", "ParticleFrameStats frame", "densityScale", "frameLiveParticles", "frameSubmittedInstances", "frameDensityScale", "InstanceBufferBytes", "EnsureInstanceBuffer", "UploadInstances", "EnsureQuadVertexBuffer", "InstanceBufferView", "QuadVertexBufferView")) {
            if ($particleState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "particle_resources missing public billboard state marker in RendererParticleState.h: $required"
            }
        }
        foreach ($oldFlatAccess in @("m_particleState.densityScale", "m_particleState.instanceBuffer", "m_particleState.frameLiveParticles", "m_particleState.effectPreset")) {
            if ($particleRenderer.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                Add-Failure "particle_resources still uses flat Renderer particle state access in Renderer_Particles.cpp: $oldFlatAccess"
            }
        }
        foreach ($rendererOwnedResourceCall in @("CreateCommittedResource", "resources.instanceBuffer->Map", "resources.instanceBuffer->Unmap", "resources.quadVertexBuffer->Map", "resources.quadVertexBuffer->Unmap")) {
            if ($particleRenderer.IndexOf($rendererOwnedResourceCall, [StringComparison]::Ordinal) -ge 0) {
                Add-Failure "particle_resources still allocates/maps particle-owned GPU buffers in Renderer_Particles.cpp: $rendererOwnedResourceCall"
            }
        }
        foreach ($rendererOwnedDrawCall in @("ResourceBarrier", "OMSetRenderTargets", "SetPipelineState", "DrawInstanced")) {
            if ($particleRenderer.IndexOf($rendererOwnedDrawCall, [StringComparison]::Ordinal) -ge 0) {
                Add-Failure "particle_resources still performs particle draw submission directly in Renderer_Particles.cpp: $rendererOwnedDrawCall"
            }
        }
        foreach ($passMarker in @("namespace Cortex::Graphics::ParticleBillboardPass", "TargetBindings", "DrawContext", "ResourceBarrier", "OMSetRenderTargets", "DrawInstanced")) {
            if ($particlePass.IndexOf($passMarker, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "particle_resources missing ParticleBillboardPass marker: $passMarker"
            }
        }
        if ($particleRenderer.IndexOf("View<Scene::ParticleEmitterComponent, Scene::TransformComponent>", [StringComparison]::Ordinal) -lt 0) {
            Add-Failure "particle_resources public renderer path is not the ECS billboard particle path"
        }
    }

    if ($id -eq "debug_line_resources") {
        foreach ($required in @("struct DebugLineRenderState", "EnsureVertexBuffer", "UploadVertices", "VertexBufferView")) {
            if ($debugLineState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "debug_line_resources missing DebugLineRenderState marker: $required"
            }
        }
        foreach ($required in @("namespace Cortex::Graphics::DebugLinePass", "DrawContext", "SetPipelineState", "IASetPrimitiveTopology", "DrawInstanced")) {
            if ($debugLinePass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "debug_line_resources missing DebugLinePass marker: $required"
            }
        }
        foreach ($rendererOwnedDebugCall in @("CreateCommittedResource", "vertexBuffer->Map", "vertexBuffer->Unmap", "SetPipelineState", "IASetPrimitiveTopology", "DrawInstanced")) {
            if ($debugLineRenderer.IndexOf($rendererOwnedDebugCall, [StringComparison]::Ordinal) -ge 0) {
                Add-Failure "debug_line_resources still owns debug-line GPU resource/draw work in Renderer_DebugLines.cpp: $rendererOwnedDebugCall"
            }
        }
        if ($debugLineRenderer.IndexOf("DebugLinePass::Draw", [StringComparison]::Ordinal) -lt 0) {
            Add-Failure "debug_line_resources renderer does not route through DebugLinePass::Draw"
        }
    }

    if ($id -eq "postprocess_resources") {
        if (-not (Test-Path $bloomStatePath)) {
            Add-Failure "postprocess_resources missing RendererBloomState.h"
        } else {
            $bloomState = Get-Content $bloomStatePath -Raw
            foreach ($required in @("struct BloomPassControls", "struct BloomPyramidResources", "struct BloomDescriptorTables", "BloomPassControls controls", "BloomPyramidResources<BloomLevels> resources", "BloomDescriptorTables<BloomDescriptorSlots> descriptors")) {
                if ($bloomState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "postprocess_resources missing bloom ownership marker in RendererBloomState.h: $required"
                }
            }
            foreach ($oldField in @("float intensity", "float threshold", "float softKnee", "float maxContribution", "srvTableValid = false;")) {
                if ($bloomState -match "struct BloomPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "postprocess_resources still exposes loose bloom state field in BloomPassState: $oldField"
                }
            }
        }
        if (Test-Path $bloomRendererPath) {
            $bloomRenderer = Get-Content $bloomRendererPath -Raw
            if ($bloomRenderer.IndexOf("FullscreenPass::DrawTriangle", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "postprocess_resources bloom renderer does not route fullscreen draws through FullscreenPass::DrawTriangle"
            }
            if ($bloomRenderer.IndexOf("DrawInstanced", [StringComparison]::Ordinal) -ge 0) {
                Add-Failure "postprocess_resources still submits fullscreen bloom draws directly in Renderer_Bloom.cpp"
            }
        } else {
            Add-Failure "postprocess_resources missing Renderer_Bloom.cpp"
        }
        if (-not (Test-Path $temporalScreenPath)) {
            Add-Failure "postprocess_resources missing RendererTemporalScreenState.h"
        }
        foreach ($required in @("cinematicEnabled", "EffectiveVignette", "EffectiveLensDirt", "EncodedLensDirtByte")) {
            if ($postState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "postprocess_resources missing post-state ownership marker in RendererPostProcessState.h: $required"
            }
        }
    }

    if ($id -eq "postprocess_target_transitions") {
        if (Test-Path $postProcessTargetPassPath) {
            $postProcessTargetPass = Get-Content $postProcessTargetPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::PostProcessTargetPass", "ResourceStateRef", "PrepareContext", "PrepareInputsAndBackBuffer", "ResourceBarrier", "D3D12_RESOURCE_STATE_RENDER_TARGET", "D3D12_RESOURCE_STATE_PRESENT")) {
                if ($postProcessTargetPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "postprocess_target_transitions missing PostProcessTargetPass marker: $required"
                }
            }
        } else {
            Add-Failure "postprocess_target_transitions missing PostProcessTargetPass.cpp"
        }
        if (Test-Path $postProcessPath) {
            $postProcess = Get-Content $postProcessPath -Raw
            if ($postProcess.IndexOf("PostProcessTargetPass::PrepareInputsAndBackBuffer", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "postprocess_target_transitions missing routed post-process target transition call in Renderer_PostProcess.cpp"
            }
            foreach ($removedLocal in @("barriers[", "barrierCount", "D3D12_RESOURCE_STATE_PRESENT")) {
                if ($postProcess.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "postprocess_target_transitions still has regular post-process target barrier mechanics in Renderer_PostProcess.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "postprocess_target_transitions missing Renderer_PostProcess.cpp"
        }
    }

    if ($id -eq "bloom_target_binding") {
        if (Test-Path $bloomPassPath) {
            $bloomPass = Get-Content $bloomPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::BloomPass", "TargetContext", "BindAndClearTarget", "SetFullscreenViewport", "OMSetRenderTargets", "ClearRenderTargetView")) {
                if ($bloomPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "bloom_target_binding missing BloomPass marker: $required"
                }
            }
        } else {
            Add-Failure "bloom_target_binding missing BloomPass.cpp"
        }
        if (Test-Path $bloomRendererPath) {
            $bloomRenderer = Get-Content $bloomRendererPath -Raw
            if ($bloomRenderer.IndexOf("BloomPass::BindAndClearTarget", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "bloom_target_binding missing routed target binding call in Renderer_Bloom.cpp"
            }
            foreach ($removedLocal in @("OMSetRenderTargets", "ClearRenderTargetView", "SetFullscreenViewport")) {
                if ($bloomRenderer.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "bloom_target_binding still has direct bloom target bind/clear mechanics in Renderer_Bloom.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "bloom_target_binding missing Renderer_Bloom.cpp"
        }
    }

    if ($id -eq "bloom_stage_transitions") {
        if (Test-Path $bloomPassPath) {
            $bloomPass = Get-Content $bloomPassPath -Raw
            foreach ($required in @("ResourceStateRef", "StageTransitionContext", "CompositeTransitionContext", "CopyContext", "PrepareSourceToRenderTarget", "TransitionToShaderResource", "PrepareCompositeTargets", "CopyCompositeToCombined", "ResourceBarrier", "CopyResource")) {
                if ($bloomPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "bloom_stage_transitions missing BloomPass marker: $required"
                }
            }
        } else {
            Add-Failure "bloom_stage_transitions missing BloomPass.cpp"
        }
        if (Test-Path $bloomRendererPath) {
            $bloomRenderer = Get-Content $bloomRendererPath -Raw
            foreach ($route in @("BloomPass::PrepareSourceToRenderTarget", "BloomPass::TransitionToShaderResource", "BloomPass::PrepareCompositeTargets", "BloomPass::CopyCompositeToCombined")) {
                if ($bloomRenderer.IndexOf($route, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "bloom_stage_transitions missing routed bloom stage transition call in Renderer_Bloom.cpp: $route"
                }
            }
            foreach ($removedLocal in @("D3D12_RESOURCE_BARRIER", "ResourceBarrier", "CopyResource")) {
                if ($bloomRenderer.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "bloom_stage_transitions still has direct bloom transition/copy mechanics in Renderer_Bloom.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "bloom_stage_transitions missing Renderer_Bloom.cpp"
        }
    }

    if ($id -eq "rt_reflection_debug_clear") {
        if (Test-Path $rtReflectionDebugClearPassPath) {
            $rtReflectionDebugClearPass = Get-Content $rtReflectionDebugClearPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::RTReflectionDebugClearPass", "ClearContext", "ClearForDebugView", "CreateUnorderedAccessView", "ClearUnorderedAccessViewFloat", "ResourceBarrier", "D3D12_RESOURCE_STATE_UNORDERED_ACCESS", "D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE")) {
                if ($rtReflectionDebugClearPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "rt_reflection_debug_clear missing RTReflectionDebugClearPass marker: $required"
                }
            }
        } else {
            Add-Failure "rt_reflection_debug_clear missing RTReflectionDebugClearPass.cpp"
        }
        if (Test-Path $postProcessPath) {
            $postProcess = Get-Content $postProcessPath -Raw
            if ($postProcess.IndexOf("RTReflectionDebugClearPass::ClearForDebugView", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "rt_reflection_debug_clear missing routed debug clear call in Renderer_PostProcess.cpp"
            }
            foreach ($removedLocal in @("D3D12_RESOURCE_BARRIER", "CreateUnorderedAccessView", "ClearUnorderedAccessViewFloat", "ResourceBarrier")) {
                if ($postProcess.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "rt_reflection_debug_clear still has direct RT reflection debug clear mechanics in Renderer_PostProcess.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "rt_reflection_debug_clear missing Renderer_PostProcess.cpp"
        }
    }

    if ($id -eq "taa_copy_transitions") {
        if (Test-Path $taaCopyPassPath) {
            $taaCopyPass = Get-Content $taaCopyPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::TAACopyPass", "ResourceStateRef", "HistoryCopyContext", "IntermediateCopyContext", "CopyHdrToHistory", "CopyIntermediateToHdr", "ResourceBarrier", "CopyResource", "D3D12_RESOURCE_STATE_COPY_SOURCE", "D3D12_RESOURCE_STATE_COPY_DEST")) {
                if ($taaCopyPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "taa_copy_transitions missing TAACopyPass marker: $required"
                }
            }
        } else {
            Add-Failure "taa_copy_transitions missing TAACopyPass.cpp"
        }
        if (Test-Path $taaExecutionPath) {
            $taaExecution = Get-Content $taaExecutionPath -Raw
            foreach ($route in @("TAACopyPass::CopyHdrToHistory", "TAACopyPass::CopyIntermediateToHdr")) {
                if ($taaExecution.IndexOf($route, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "taa_copy_transitions missing routed TAA copy call in Renderer_TAAExecution.cpp: $route"
                }
            }
            foreach ($removedLocal in @("initBarriers", "copyBarriers", "postTaa", "finalBarriers", "CopyResource")) {
                if ($taaExecution.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "taa_copy_transitions still has duplicated TAA copy/barrier mechanics in Renderer_TAAExecution.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "taa_copy_transitions missing Renderer_TAAExecution.cpp"
        }
    }

    if ($id -eq "taa_resolve_transitions") {
        if (Test-Path $taaCopyPassPath) {
            $taaCopyPass = Get-Content $taaCopyPassPath -Raw
            foreach ($required in @("ResolveInputsContext", "PrepareResolveInputs", "TransitionToShaderResource", "D3D12_RESOURCE_STATE_RENDER_TARGET", "D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE", "ResourceBarrier")) {
                if ($taaCopyPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "taa_resolve_transitions missing TAACopyPass marker: $required"
                }
            }
        } else {
            Add-Failure "taa_resolve_transitions missing TAACopyPass.cpp"
        }
        if (Test-Path $taaExecutionPath) {
            $taaExecution = Get-Content $taaExecutionPath -Raw
            foreach ($route in @("TAACopyPass::PrepareResolveInputs", "TAACopyPass::TransitionToShaderResource")) {
                if ($taaExecution.IndexOf($route, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "taa_resolve_transitions missing routed TAA resolve transition call in Renderer_TAAExecution.cpp: $route"
                }
            }
            foreach ($removedLocal in @("D3D12_RESOURCE_BARRIER", "ResourceBarrier")) {
                if ($taaExecution.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "taa_resolve_transitions still has direct TAA resolve transition mechanics in Renderer_TAAExecution.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "taa_resolve_transitions missing Renderer_TAAExecution.cpp"
        }
    }

    if ($id -eq "motion_vector_target_transitions") {
        if (Test-Path $motionVectorTargetPassPath) {
            $motionVectorTargetPass = Get-Content $motionVectorTargetPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::MotionVectorTargetPass", "ResourceStateRef", "VelocityUAVContext", "CameraTargetContext", "TransitionVelocityToUnorderedAccess", "TransitionCameraTargets", "ResourceBarrier", "D3D12_RESOURCE_STATE_UNORDERED_ACCESS", "D3D12_RESOURCE_STATE_RENDER_TARGET")) {
                if ($motionVectorTargetPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "motion_vector_target_transitions missing MotionVectorTargetPass marker: $required"
                }
            }
        } else {
            Add-Failure "motion_vector_target_transitions missing MotionVectorTargetPass.cpp"
        }
        if (Test-Path $motionVectorRendererPath) {
            $motionVectorRenderer = Get-Content $motionVectorRendererPath -Raw
            foreach ($route in @("MotionVectorTargetPass::TransitionVelocityToUnorderedAccess", "MotionVectorTargetPass::TransitionCameraTargets")) {
                if ($motionVectorRenderer.IndexOf($route, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "motion_vector_target_transitions missing routed transition call in Renderer_MotionVectors.cpp: $route"
                }
            }
            foreach ($removedLocal in @("D3D12_RESOURCE_BARRIER", "ResourceBarrier", "barrierCount")) {
                if ($motionVectorRenderer.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "motion_vector_target_transitions still has direct velocity/depth barrier mechanics in Renderer_MotionVectors.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "motion_vector_target_transitions missing Renderer_MotionVectors.cpp"
        }
    }

    if ($id -eq "environment_resources") {
        foreach ($required in @("ActiveEnvironment", "ActiveEnvironmentName", "UsingFallbackEnvironment", "ResidentCount", "PendingCount")) {
            if ($environmentState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "environment_resources missing state ownership marker in RendererEnvironmentState.h: $required"
            }
        }
    }

    if ($id -eq "screen_space_resources") {
        if (-not (Test-Path $ssaoStatePath)) {
            Add-Failure "screen_space_resources missing RendererSSAOState.h"
        } else {
            $ssaoState = Get-Content $ssaoStatePath -Raw
            foreach ($required in @("struct SSAOControls", "struct SSAOResources", "struct SSAODescriptorTables", "SSAOControls controls", "SSAOResources resources", "SSAODescriptorTables descriptors")) {
                if ($ssaoState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "screen_space_resources missing SSAO ownership marker in RendererSSAOState.h: $required"
                }
            }
            foreach ($oldField in @("bool enabled", "float radius", "float bias", "float intensity", "descriptorTablesValid = false;")) {
                if ($ssaoState -match "struct SSAOPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "screen_space_resources still exposes loose SSAO state field in SSAOPassState: $oldField"
                }
            }
        }
        if (-not (Test-Path $ssrStatePath)) {
            Add-Failure "screen_space_resources missing RendererSSRState.h"
        } else {
            $ssrState = Get-Content $ssrStatePath -Raw
            foreach ($required in @("struct SSRControls", "struct SSRResources", "struct SSRDescriptorTables", "struct SSRFrameState", "SSRControls controls", "SSRResources resources", "SSRDescriptorTables descriptors", "SSRFrameState frame")) {
                if ($ssrState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "screen_space_resources missing SSR ownership marker in RendererSSRState.h: $required"
                }
            }
            foreach ($oldField in @("bool enabled", "bool activeThisFrame", "float maxDistance", "float thickness", "float strength", "srvTableValid = false;")) {
                if ($ssrState -match "struct SSRPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "screen_space_resources still exposes loose SSR state field in SSRPassState: $oldField"
                }
            }
        }
        if (Test-Path $ssaoRendererPath) {
            $ssaoRenderer = Get-Content $ssaoRendererPath -Raw
            foreach ($oldFlatAccess in @("m_ssaoResources.enabled", "m_ssaoResources.texture", "m_ssaoResources.resourceState", "m_ssaoResources.srvTables")) {
                if ($ssaoRenderer.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "screen_space_resources still uses flat SSAO state access in Renderer_SSAO.cpp: $oldFlatAccess"
                }
            }
        }
        if (Test-Path $ssrRendererPath) {
            $ssrRenderer = Get-Content $ssrRendererPath -Raw
            foreach ($oldFlatAccess in @("m_ssrResources.enabled", "m_ssrResources.color", "m_ssrResources.resourceState", "m_ssrResources.srvTables")) {
                if ($ssrRenderer.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "screen_space_resources still uses flat SSR state access in Renderer_SSRPass.cpp: $oldFlatAccess"
                }
            }
        }
    }

    if ($id -eq "ssr_target_transitions") {
        if (Test-Path $ssrPassPath) {
            $ssrPass = Get-Content $ssrPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::SSRPass", "ResourceStateRef", "PrepareContext", "PrepareTargets", "desiredState", "ResourceBarrier")) {
                if ($ssrPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "ssr_target_transitions missing SSRPass marker: $required"
                }
            }
        } else {
            Add-Failure "ssr_target_transitions missing SSRPass.cpp"
        }
        if (Test-Path $ssrRendererPath) {
            $ssrRenderer = Get-Content $ssrRendererPath -Raw
            if ($ssrRenderer.IndexOf("SSRPass::PrepareTargets", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "ssr_target_transitions missing routed target preparation call in Renderer_SSRPass.cpp"
            }
            foreach ($removedLocal in @("D3D12_RESOURCE_BARRIER", "ResourceBarrier", "barrierCount")) {
                if ($ssrRenderer.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "ssr_target_transitions still has direct SSR target transition mechanics in Renderer_SSRPass.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "ssr_target_transitions missing Renderer_SSRPass.cpp"
        }
    }

    if ($id -eq "ssao_target_transitions") {
        if (Test-Path $ssaoPassPath) {
            $ssaoPass = Get-Content $ssaoPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::SSAOPass", "ResourceStateRef", "PrepareContext", "PrepareGraphicsTargets", "PrepareComputeTargets", "FinishComputeTarget", "ResourceBarrier", "D3D12_RESOURCE_STATE_RENDER_TARGET", "D3D12_RESOURCE_STATE_UNORDERED_ACCESS", "D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE")) {
                if ($ssaoPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "ssao_target_transitions missing SSAOPass marker: $required"
                }
            }
        } else {
            Add-Failure "ssao_target_transitions missing SSAOPass.cpp"
        }
        if (Test-Path $ssaoRendererPath) {
            $ssaoRenderer = Get-Content $ssaoRendererPath -Raw
            foreach ($route in @("SSAOPass::PrepareGraphicsTargets", "SSAOPass::PrepareComputeTargets", "SSAOPass::FinishComputeTarget")) {
                if ($ssaoRenderer.IndexOf($route, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "ssao_target_transitions missing routed target transition call in Renderer_SSAO.cpp: $route"
                }
            }
            foreach ($removedLocal in @("D3D12_RESOURCE_BARRIER", "ResourceBarrier")) {
                if ($ssaoRenderer.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "ssao_target_transitions still has direct SSAO target transition mechanics in Renderer_SSAO.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "ssao_target_transitions missing Renderer_SSAO.cpp"
        }
    }

    if ($id -eq "hzb_resources") {
        if (-not (Test-Path $hzbStatePath)) {
            Add-Failure "hzb_resources missing RendererHZBState.h"
        } else {
            $hzbState = Get-Content $hzbStatePath -Raw
            foreach ($required in @("struct HZBResources", "struct HZBDescriptorTables", "struct HZBDebugControls", "struct HZBCaptureState", "HZBResources resources", "HZBDescriptorTables descriptors", "HZBDebugControls debug", "HZBCaptureState capture")) {
                if ($hzbState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "hzb_resources missing ownership marker in RendererHZBState.h: $required"
                }
            }
            foreach ($oldField in @("ComPtr<ID3D12Resource> texture", "DescriptorHandle fullSRV", "dispatchTablesValid = false;", "uint32_t debugMip", "bool captureValid")) {
                if ($hzbState -match "struct HZBPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "hzb_resources still exposes loose HZB state field in HZBPassState: $oldField"
                }
            }
        }
        if (Test-Path $hzbRendererPath) {
            $hzbRenderer = Get-Content $hzbRendererPath -Raw
            foreach ($oldFlatAccess in @("m_hzbResources.texture", "m_hzbResources.fullSRV", "m_hzbResources.dispatchTablesValid", "m_hzbResources.captureValid", "m_hzbResources.debugMip")) {
                if ($hzbRenderer.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "hzb_resources still uses flat HZB state access in Renderer_HZB.cpp: $oldFlatAccess"
                }
            }
        }
    }

    if ($id -eq "hzb_compute_build") {
        if (Test-Path $hzbPassPath) {
            $hzbPass = Get-Content $hzbPassPath -Raw
            foreach ($required in @("BuildContext", "BuildFromDepth", "ResourceStateRef", "TransitionResource", "TransitionHZBMipToShaderResource", "SetComputeRootSignature", "SetComputeRootDescriptorTable", "SetPipelineState", "Dispatch", "ResourceBarrier")) {
                if ($hzbPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "hzb_compute_build missing HZBPass marker: $required"
                }
            }
        } else {
            Add-Failure "hzb_compute_build missing HZBPass.cpp"
        }
        if (Test-Path $hzbRendererPath) {
            $hzbRenderer = Get-Content $hzbRendererPath -Raw
            if ($hzbRenderer.IndexOf("HZBPass::BuildFromDepth", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "hzb_compute_build missing routed HZB build call in Renderer_HZB.cpp"
            }
            foreach ($removedLocal in @("D3D12_RESOURCE_BARRIER", "ResourceBarrier", "SetComputeRootSignature", "SetComputeRootDescriptorTable", "Dispatch(")) {
                if ($hzbRenderer.IndexOf($removedLocal, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "hzb_compute_build still has direct HZB build mechanics in Renderer_HZB.cpp: $removedLocal"
                }
            }
        } else {
            Add-Failure "hzb_compute_build missing Renderer_HZB.cpp"
        }
    }

    if ($id -eq "shadow_resources") {
        if (-not (Test-Path $shadowStatePath)) {
            Add-Failure "shadow_resources missing RendererShadowState.h"
        } else {
            $shadowState = Get-Content $shadowStatePath -Raw
            foreach ($required in @("struct ShadowMapResources", "struct ShadowMapRasterState", "struct ShadowMapControls", "struct ShadowMapPassState", "ShadowMapResources<ShadowArraySize> resources", "ShadowMapRasterState raster", "ShadowMapControls<ShadowCascadeCount> controls")) {
                if ($shadowState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "shadow_resources missing ownership marker in RendererShadowState.h: $required"
                }
            }
            foreach ($oldField in @("ComPtr<ID3D12Resource> map", "DescriptorHandle srv", "D3D12_VIEWPORT viewport", "D3D12_RECT scissor", "bool enabled", "float mapSize", "float bias")) {
                if ($shadowState -match "struct ShadowMapPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "shadow_resources still exposes loose shadow state field in ShadowMapPassState: $oldField"
                }
            }
        }
        foreach ($pathInfo in @(
            [pscustomobject]@{ Path = $shadowResourcesPath; Label = "Renderer_ShadowResources.cpp" },
            [pscustomobject]@{ Path = $shadowPassPath; Label = "Renderer_ShadowPass.cpp" }
        )) {
            if (Test-Path $pathInfo.Path) {
                $shadowSource = Get-Content $pathInfo.Path -Raw
                foreach ($oldFlatAccess in @("m_shadowResources.map", "m_shadowResources.dsvs", "m_shadowResources.srv", "m_shadowResources.resourceState", "m_shadowResources.initializedForEditor", "m_shadowResources.viewport", "m_shadowResources.scissor", "m_shadowResources.enabled", "m_shadowResources.mapSize", "m_shadowResources.bias")) {
                    if ($shadowSource.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                        Add-Failure "shadow_resources still uses flat shadow state access in $($pathInfo.Label): $oldFlatAccess"
                    }
                }
            }
        }
        if (Test-Path $shadowTargetPassPath) {
            $shadowTargetPass = Get-Content $shadowTargetPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::ShadowTargetPass", "TransitionToDepthWrite", "BindAndClearSlice", "TransitionToShaderResource", "ResourceBarrier", "OMSetRenderTargets", "ClearDepthStencilView", "RSSetViewports", "RSSetScissorRects")) {
                if ($shadowTargetPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "shadow_resources missing ShadowTargetPass marker: $required"
                }
            }
        } else {
            Add-Failure "shadow_resources missing ShadowTargetPass.cpp"
        }
        if (Test-Path $shadowPassPath) {
            $shadowPassSource = Get-Content $shadowPassPath -Raw
            foreach ($directCall in @("ResourceBarrier", "OMSetRenderTargets", "ClearDepthStencilView", "RSSetViewports", "RSSetScissorRects")) {
                if ($shadowPassSource.IndexOf($directCall, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "shadow_resources still performs shadow target setup directly in Renderer_ShadowPass.cpp: $directCall"
                }
            }
            foreach ($requiredRoute in @("ShadowTargetPass::TransitionToDepthWrite", "ShadowTargetPass::BindAndClearSlice", "ShadowTargetPass::TransitionToShaderResource")) {
                if ($shadowPassSource.IndexOf($requiredRoute, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "shadow_resources missing routed shadow target call in Renderer_ShadowPass.cpp: $requiredRoute"
                }
            }
        }
    }

    if ($id -eq "depth_resources") {
        if (-not (Test-Path $depthStatePath)) {
            Add-Failure "depth_resources missing RendererDepthState.h"
        } else {
            $depthState = Get-Content $depthStatePath -Raw
            foreach ($required in @("struct DepthTargetResources", "struct DepthTargetDescriptors", "struct DepthTargetState", "DepthTargetResources resources", "DepthTargetDescriptors descriptors")) {
                if ($depthState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "depth_resources missing ownership marker in RendererDepthState.h: $required"
                }
            }
            foreach ($oldField in @("ComPtr<ID3D12Resource> buffer", "DescriptorHandle dsv", "DescriptorHandle readOnlyDsv", "DescriptorHandle srv", "D3D12_RESOURCE_STATES resourceState")) {
                if ($depthState -match "struct DepthTargetState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "depth_resources still exposes loose depth state field in DepthTargetState: $oldField"
                }
            }
        }
        foreach ($pathInfo in @(
            [pscustomobject]@{ Path = $depthTargetPath; Label = "Renderer_DepthTarget.cpp" },
            [pscustomobject]@{ Path = $depthPassesPath; Label = "Renderer_DepthPasses.cpp" }
        )) {
            if (Test-Path $pathInfo.Path) {
                $depthSource = Get-Content $pathInfo.Path -Raw
                foreach ($oldFlatAccess in @("m_depthResources.buffer", "m_depthResources.dsv", "m_depthResources.readOnlyDsv", "m_depthResources.srv", "m_depthResources.resourceState")) {
                    if ($depthSource.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                        Add-Failure "depth_resources still uses flat depth state access in $($pathInfo.Label): $oldFlatAccess"
                    }
                }
            }
        }
    }

    if ($id -eq "depth_prepass_target_submission") {
        if (Test-Path $depthPrepassTargetPassPath) {
            $depthPrepassTargetPass = Get-Content $depthPrepassTargetPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::DepthPrepassTargetPass", "BindAndClear", "ResourceBarrier", "OMSetRenderTargets", "ClearDepthStencilView", "RSSetViewports", "RSSetScissorRects")) {
                if ($depthPrepassTargetPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "depth_prepass_target_submission missing DepthPrepassTargetPass marker: $required"
                }
            }
        } else {
            Add-Failure "depth_prepass_target_submission missing DepthPrepassTargetPass.cpp"
        }
        if (Test-Path $depthPassesPath) {
            $depthPassSource = Get-Content $depthPassesPath -Raw
            foreach ($directCall in @("ResourceBarrier", "OMSetRenderTargets", "ClearDepthStencilView", "RSSetViewports", "RSSetScissorRects")) {
                if ($depthPassSource.IndexOf($directCall, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "depth_prepass_target_submission still performs target setup directly in Renderer_DepthPasses.cpp: $directCall"
                }
            }
            if ($depthPassSource.IndexOf("DepthPrepassTargetPass::BindAndClear", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "depth_prepass_target_submission missing DepthPrepassTargetPass::BindAndClear in Renderer_DepthPasses.cpp"
            }
        } else {
            Add-Failure "depth_prepass_target_submission missing Renderer_DepthPasses.cpp"
        }
    }

    if ($id -eq "depth_write_transition") {
        if (Test-Path $depthWriteTransitionPassPath) {
            $depthWriteTransitionPass = Get-Content $depthWriteTransitionPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::DepthWriteTransitionPass", "TransitionContext", "TransitionToDepthWrite", "ResourceBarrier", "D3D12_RESOURCE_STATE_DEPTH_WRITE")) {
                if ($depthWriteTransitionPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "depth_write_transition missing DepthWriteTransitionPass marker: $required"
                }
            }
        } else {
            Add-Failure "depth_write_transition missing DepthWriteTransitionPass.cpp"
        }
        if (Test-Path $framePhasesMainPath) {
            $framePhasesMain = Get-Content $framePhasesMainPath -Raw
            if ($framePhasesMain.IndexOf("DepthWriteTransitionPass::TransitionToDepthWrite", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "depth_write_transition missing routed depth-write transition call in Renderer_FramePhases_Main.cpp"
            }
            foreach ($directCall in @("D3D12_RESOURCE_BARRIER", "ResourceBarrier")) {
                if ($framePhasesMain.IndexOf($directCall, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "depth_write_transition still performs direct depth transition in Renderer_FramePhases_Main.cpp: $directCall"
                }
            }
        } else {
            Add-Failure "depth_write_transition missing Renderer_FramePhases_Main.cpp"
        }
    }

    if ($id -eq "main_target_resources") {
        if (-not (Test-Path $mainTargetStatePath)) {
            Add-Failure "main_target_resources missing RendererMainTargetState.h"
        } else {
            $mainTargetState = Get-Content $mainTargetStatePath -Raw
            foreach ($required in @("struct HDRRenderTargetResources", "struct HDRRenderTargetDescriptors", "struct GBufferNormalRoughnessResources", "struct GBufferNormalRoughnessDescriptors", "struct HDRRenderTargetState", "struct GBufferNormalRoughnessTargetState", "HDRRenderTargetState hdr", "GBufferNormalRoughnessTargetState normalRoughness")) {
                if ($mainTargetState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "main_target_resources missing ownership marker in RendererMainTargetState.h: $required"
                }
            }
            foreach ($oldField in @("ComPtr<ID3D12Resource> hdrColor", "DescriptorHandle hdrRTV", "DescriptorHandle hdrSRV", "D3D12_RESOURCE_STATES hdrState", "ComPtr<ID3D12Resource> gbufferNormalRoughness", "DescriptorHandle gbufferNormalRoughnessRTV", "DescriptorHandle gbufferNormalRoughnessSRV", "D3D12_RESOURCE_STATES gbufferNormalRoughnessState")) {
                if ($mainTargetState -match "struct MainRenderTargetState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "main_target_resources still exposes loose main-target state field in MainRenderTargetState: $oldField"
                }
            }
        }
        foreach ($pathInfo in @(
            [pscustomobject]@{ Path = $hdrTargetsPath; Label = "Renderer_HDRTargets.cpp" },
            [pscustomobject]@{ Path = $postProcessPath; Label = "Renderer_PostProcess.cpp" }
        )) {
            if (Test-Path $pathInfo.Path) {
                $targetSource = Get-Content $pathInfo.Path -Raw
                foreach ($oldFlatAccess in @("m_mainTargets.hdrColor", "m_mainTargets.hdrRTV", "m_mainTargets.hdrSRV", "m_mainTargets.hdrState", "m_mainTargets.gbufferNormalRoughness", "m_mainTargets.gbufferNormalRoughnessRTV", "m_mainTargets.gbufferNormalRoughnessSRV", "m_mainTargets.gbufferNormalRoughnessState")) {
                    if ($targetSource.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                        Add-Failure "main_target_resources still uses flat main-target access in $($pathInfo.Label): $oldFlatAccess"
                    }
                }
            }
        }
        if (Test-Path $mainPassTargetPassPath) {
            $mainPassTargetPass = Get-Content $mainPassTargetPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::MainPassTargetPass", "PrepareContext", "Prepare", "ResourceBarrier", "OMSetRenderTargets", "ClearRenderTargetView", "ClearDepthStencilView", "RSSetViewports", "RSSetScissorRects", "SetGraphicsRootSignature", "SetPipelineState", "SetDescriptorHeaps", "IASetPrimitiveTopology")) {
                if ($mainPassTargetPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "main_target_resources missing MainPassTargetPass marker: $required"
                }
            }
        } else {
            Add-Failure "main_target_resources missing MainPassTargetPass.cpp"
        }
        if (Test-Path $mainPassSetupPath) {
            $mainPassSetup = Get-Content $mainPassSetupPath -Raw
            foreach ($directCall in @("ResourceBarrier", "OMSetRenderTargets", "ClearRenderTargetView", "ClearDepthStencilView", "RSSetViewports", "RSSetScissorRects", "SetPipelineState", "SetDescriptorHeaps", "IASetPrimitiveTopology")) {
                if ($mainPassSetup.IndexOf($directCall, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "main_target_resources still performs main-pass target setup directly in Renderer_MainPassSetup.cpp: $directCall"
                }
            }
            if ($mainPassSetup.IndexOf("MainPassTargetPass::Prepare", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "main_target_resources missing MainPassTargetPass::Prepare in Renderer_MainPassSetup.cpp"
            }
        } else {
            Add-Failure "main_target_resources missing Renderer_MainPassSetup.cpp"
        }
        if (Test-Path $forwardTargetBindingPath) {
            $forwardTargetBinding = Get-Content $forwardTargetBindingPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::ForwardTargetBindingPass", "BindHdrAndDepthReadOnly", "ResourceBarrier", "OMSetRenderTargets")) {
                if ($forwardTargetBinding.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "main_target_resources missing forward target binding marker: $required"
                }
            }
        } else {
            Add-Failure "main_target_resources missing ForwardTargetBindingPass.cpp"
        }
        foreach ($pathInfo in @(
            [pscustomobject]@{ Path = $waterSurfacesPath; Label = "Renderer_WaterSurfaces.cpp" },
            [pscustomobject]@{ Path = $transparentGeometryPath; Label = "Renderer_TransparentGeometry.cpp" },
            [pscustomobject]@{ Path = $overlayGeometryPath; Label = "Renderer_OverlayGeometry.cpp" }
        )) {
            if (Test-Path $pathInfo.Path) {
                $forwardSource = Get-Content $pathInfo.Path -Raw
                foreach ($directTargetBindingCall in @("ResourceBarrier", "OMSetRenderTargets", "RSSetViewports", "RSSetScissorRects")) {
                    if ($forwardSource.IndexOf($directTargetBindingCall, [StringComparison]::Ordinal) -ge 0) {
                        Add-Failure "main_target_resources still binds HDR/depth targets directly in $($pathInfo.Label): $directTargetBindingCall"
                    }
                }
                if ($forwardSource.IndexOf("ForwardTargetBindingPass::BindHdrAndDepthReadOnly", [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "main_target_resources missing shared HDR/depth binding in $($pathInfo.Label)"
                }
                if ($forwardSource.IndexOf("FullscreenPass::SetViewportAndScissor", [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "main_target_resources missing shared viewport/scissor setup in $($pathInfo.Label)"
                }
            }
        }
    }

    if ($id -eq "mesh_draw_submission") {
        if (Test-Path $meshDrawPassPath) {
            $meshDrawPass = Get-Content $meshDrawPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::MeshDrawPass", "DrawIndexedMesh", "IASetVertexBuffers", "IASetIndexBuffer", "DrawIndexedInstanced")) {
                if ($meshDrawPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "mesh_draw_submission missing MeshDrawPass marker: $required"
                }
            }
        } else {
            Add-Failure "mesh_draw_submission missing MeshDrawPass.cpp"
        }
        foreach ($pathInfo in @(
            [pscustomobject]@{ Path = $forwardPassPath; Label = "Renderer_ForwardPass.cpp" },
            [pscustomobject]@{ Path = $transparentGeometryPath; Label = "Renderer_TransparentGeometry.cpp" },
            [pscustomobject]@{ Path = $overlayGeometryPath; Label = "Renderer_OverlayGeometry.cpp" },
            [pscustomobject]@{ Path = $waterSurfacesPath; Label = "Renderer_WaterSurfaces.cpp" },
            [pscustomobject]@{ Path = $depthPassPath; Label = "Renderer_DepthPasses.cpp" },
            [pscustomobject]@{ Path = $shadowDrawPassPath; Label = "Renderer_ShadowPass.cpp" }
        )) {
            if (Test-Path $pathInfo.Path) {
                $meshSource = Get-Content $pathInfo.Path -Raw
                foreach ($directDrawCall in @("IASetVertexBuffers", "IASetIndexBuffer", "DrawIndexedInstanced")) {
                    if ($meshSource.IndexOf($directDrawCall, [StringComparison]::Ordinal) -ge 0) {
                        Add-Failure "mesh_draw_submission still performs indexed mesh draw submission directly in $($pathInfo.Label): $directDrawCall"
                    }
                }
                if ($meshSource.IndexOf("MeshDrawPass::DrawIndexedMesh", [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "mesh_draw_submission missing MeshDrawPass::DrawIndexedMesh in $($pathInfo.Label)"
                }
            }
        }
    }

    if ($id -eq "voxel_pass_submission") {
        if (Test-Path $voxelPassPath) {
            $voxelPass = Get-Content $voxelPassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::VoxelPass", "DrawContext", "ResourceBarrier", "OMSetRenderTargets", "FullscreenPass::DrawTriangle")) {
                if ($voxelPass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "voxel_pass_submission missing VoxelPass marker: $required"
                }
            }
        } else {
            Add-Failure "voxel_pass_submission missing VoxelPass.cpp"
        }
        if (Test-Path $voxelRendererPath) {
            $voxelRenderer = Get-Content $voxelRendererPath -Raw
            foreach ($directCall in @("ResourceBarrier", "OMSetRenderTargets", "DrawInstanced", "RSSetViewports", "RSSetScissorRects")) {
                if ($voxelRenderer.IndexOf($directCall, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "voxel_pass_submission still performs pass submission directly in Renderer_Voxel.cpp: $directCall"
                }
            }
            if ($voxelRenderer.IndexOf("VoxelPass::Draw", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "voxel_pass_submission missing VoxelPass::Draw in Renderer_Voxel.cpp"
            }
        } else {
            Add-Failure "voxel_pass_submission missing Renderer_Voxel.cpp"
        }
    }

    if ($id -eq "minimal_frame_submission") {
        if (Test-Path $minimalFramePassPath) {
            $minimalFramePass = Get-Content $minimalFramePassPath -Raw
            foreach ($required in @("namespace Cortex::Graphics::MinimalFramePass", "ClearContext", "ClearBackBuffer", "ResourceBarrier", "OMSetRenderTargets", "ClearRenderTargetView")) {
                if ($minimalFramePass.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "minimal_frame_submission missing MinimalFramePass marker: $required"
                }
            }
        } else {
            Add-Failure "minimal_frame_submission missing MinimalFramePass.cpp"
        }
        if (Test-Path $framePlanningPath) {
            $framePlanning = Get-Content $framePlanningPath -Raw
            foreach ($directCall in @("ResourceBarrier", "OMSetRenderTargets", "ClearRenderTargetView")) {
                if ($framePlanning.IndexOf($directCall, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "minimal_frame_submission still performs minimal-frame target setup directly in Renderer_FramePlanning.cpp: $directCall"
                }
            }
            if ($framePlanning.IndexOf("MinimalFramePass::ClearBackBuffer", [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "minimal_frame_submission missing MinimalFramePass::ClearBackBuffer in Renderer_FramePlanning.cpp"
            }
        } else {
            Add-Failure "minimal_frame_submission missing Renderer_FramePlanning.cpp"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Renderer ownership tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Renderer ownership tests passed: targets=$($doc.targets.Count)" -ForegroundColor Green
