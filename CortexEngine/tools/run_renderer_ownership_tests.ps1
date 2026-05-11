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
$temporalScreenPath = Join-Path $root "src/Graphics/RendererTemporalScreenState.h"
$ssaoStatePath = Join-Path $root "src/Graphics/RendererSSAOState.h"
$ssrStatePath = Join-Path $root "src/Graphics/RendererSSRState.h"
$ssaoRendererPath = Join-Path $root "src/Graphics/Renderer_SSAO.cpp"
$ssrRendererPath = Join-Path $root "src/Graphics/Renderer_SSRPass.cpp"
$hzbStatePath = Join-Path $root "src/Graphics/RendererHZBState.h"
$hzbRendererPath = Join-Path $root "src/Graphics/Renderer_HZB.cpp"
$shadowStatePath = Join-Path $root "src/Graphics/RendererShadowState.h"
$shadowResourcesPath = Join-Path $root "src/Graphics/Renderer_ShadowResources.cpp"
$shadowPassPath = Join-Path $root "src/Graphics/Renderer_ShadowPass.cpp"
$depthStatePath = Join-Path $root "src/Graphics/RendererDepthState.h"
$depthTargetPath = Join-Path $root "src/Graphics/Renderer_DepthTarget.cpp"
$depthPassesPath = Join-Path $root "src/Graphics/Renderer_DepthPasses.cpp"
$mainTargetStatePath = Join-Path $root "src/Graphics/RendererMainTargetState.h"
$hdrTargetsPath = Join-Path $root "src/Graphics/Renderer_HDRTargets.cpp"
$postProcessPath = Join-Path $root "src/Graphics/Renderer_PostProcess.cpp"
$forwardTargetBindingPath = Join-Path $root "src/Graphics/Passes/ForwardTargetBindingPass.cpp"
$meshDrawPassPath = Join-Path $root "src/Graphics/Passes/MeshDrawPass.cpp"
$forwardPassPath = Join-Path $root "src/Graphics/Renderer_ForwardPass.cpp"
$depthPassPath = Join-Path $root "src/Graphics/Renderer_DepthPasses.cpp"
$shadowDrawPassPath = Join-Path $root "src/Graphics/Renderer_ShadowPass.cpp"
$waterSurfacesPath = Join-Path $root "src/Graphics/Renderer_WaterSurfaces.cpp"
$transparentGeometryPath = Join-Path $root "src/Graphics/Renderer_TransparentGeometry.cpp"
$overlayGeometryPath = Join-Path $root "src/Graphics/Renderer_OverlayGeometry.cpp"

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
        if (-not (Test-Path $temporalScreenPath)) {
            Add-Failure "postprocess_resources missing RendererTemporalScreenState.h"
        }
        foreach ($required in @("cinematicEnabled", "EffectiveVignette", "EffectiveLensDirt", "EncodedLensDirtByte")) {
            if ($postState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "postprocess_resources missing post-state ownership marker in RendererPostProcessState.h: $required"
            }
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
                foreach ($directTargetBindingCall in @("ResourceBarrier", "OMSetRenderTargets")) {
                    if ($forwardSource.IndexOf($directTargetBindingCall, [StringComparison]::Ordinal) -ge 0) {
                        Add-Failure "main_target_resources still binds HDR/depth targets directly in $($pathInfo.Label): $directTargetBindingCall"
                    }
                }
                if ($forwardSource.IndexOf("ForwardTargetBindingPass::BindHdrAndDepthReadOnly", [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "main_target_resources missing shared HDR/depth binding in $($pathInfo.Label)"
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
}

if ($failures.Count -gt 0) {
    Write-Host "Renderer ownership tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Renderer ownership tests passed: targets=$($doc.targets.Count)" -ForegroundColor Green
