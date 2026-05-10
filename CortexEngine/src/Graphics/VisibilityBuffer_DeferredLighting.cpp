#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DescriptorHeap.h"
#include <cstring>

namespace Cortex::Graphics {
Result<void> VisibilityBufferRenderer::BuildClusteredLightLists(
    ID3D12GraphicsCommandList* cmdList,
    const DeferredLightingParams& params
) {
    if (!cmdList) {
        return Result<void>::Err("Clustered light culling requires a valid command list");
    }

    // Disabled or no local lights for this frame.
    if (!m_clusterPipeline || !m_clusterRootSignature) {
        return Result<void>::Ok();
    }
    if (m_localLightCount == 0 || params.clusterParams.z == 0u) {
        return Result<void>::Ok();
    }
    if (!m_localLightsBuffer || !m_clusterRangesBuffer || !m_clusterLightIndicesBuffer) {
        return Result<void>::Err("Clustered light culling missing buffers");
    }

    // Keep shader/cpu configuration consistent.
    if (params.screenAndCluster.z != m_clusterCountX ||
        params.screenAndCluster.w != m_clusterCountY ||
        params.clusterParams.x != m_clusterCountZ ||
        params.clusterParams.y != m_maxLightsPerCluster)
    {
        return Result<void>::Err("Clustered light params mismatch (cluster dims/max lights)");
    }

    // Transition outputs to UAV.
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    uint32_t barrierCount = 0;

    if (!m_transitionSkip.clusteredLights && m_clusterRangesState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_clusterRangesBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_clusterRangesState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_clusterRangesState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (!m_transitionSkip.clusteredLights && m_clusterLightIndicesState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_clusterLightIndicesBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_clusterLightIndicesState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_clusterLightIndicesState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(static_cast<UINT>(barrierCount), barriers);
    }

    cmdList->SetPipelineState(m_clusterPipeline.Get());
    cmdList->SetComputeRootSignature(m_clusterRootSignature.Get());

    struct ClusterConstants {
        glm::mat4 viewMatrix;
        glm::vec4 projectionParams;
        glm::uvec4 screenAndCluster;
        glm::uvec4 clusterParams;
    } constants{};

    constants.viewMatrix = params.viewMatrix;
    constants.projectionParams = params.projectionParams;
    constants.screenAndCluster = params.screenAndCluster;
    constants.clusterParams = params.clusterParams;

    cmdList->SetComputeRoot32BitConstants(0, 28, &constants, 0);
    cmdList->SetComputeRootShaderResourceView(1, m_localLightsBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(2, m_clusterRangesBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(3, m_clusterLightIndicesBuffer->GetGPUVirtualAddress());

    const uint32_t groups = (m_clusterCount + 63u) / 64u;
    cmdList->Dispatch(groups, 1, 1);

    // Transition to SRV for deferred shading.
    barrierCount = 0;
    if (!m_transitionSkip.clusteredLights && m_clusterRangesState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_clusterRangesBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_clusterRangesState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_clusterRangesState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (!m_transitionSkip.clusteredLights && m_clusterLightIndicesState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_clusterLightIndicesBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_clusterLightIndicesState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_clusterLightIndicesState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(static_cast<UINT>(barrierCount), barriers);
    }

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::EnsureBRDFLUT(ID3D12GraphicsCommandList* cmdList) {
    if (m_brdfLutReady) {
        return Result<void>::Ok();
    }

    if (!cmdList) {
        return Result<void>::Err("EnsureBRDFLUT requires a valid command list");
    }
    if (!m_brdfLut || !m_brdfLutPipeline || !m_brdfLutRootSignature) {
        return Result<void>::Err("BRDF LUT resources/pipeline not initialized");
    }
    if (!m_brdfLutSRV.IsValid() || !m_brdfLutUAV.IsValid()) {
        return Result<void>::Err("BRDF LUT SRV/UAV handles invalid");
    }

    constexpr uint32_t kBrdfLutSize = 256;
    const float rcp = 1.0f / static_cast<float>(kBrdfLutSize);

    // Transition to UAV for generation.
    if (!m_transitionSkip.brdfLut && m_brdfLutState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_brdfLut.Get();
        barrier.Transition.StateBefore = m_brdfLutState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_brdfLutState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetPipelineState(m_brdfLutPipeline.Get());
    cmdList->SetComputeRootSignature(m_brdfLutRootSignature.Get());

    uint32_t constants[4];
    constants[0] = kBrdfLutSize;
    constants[1] = kBrdfLutSize;
    static_assert(sizeof(float) == sizeof(uint32_t));
    memcpy(&constants[2], &rcp, sizeof(float));
    memcpy(&constants[3], &rcp, sizeof(float));

    cmdList->SetComputeRoot32BitConstants(0, _countof(constants), constants, 0);
    cmdList->SetComputeRootDescriptorTable(1, m_brdfLutUAV.gpu);

    const uint32_t groupCount = (kBrdfLutSize + 7u) / 8u;
    cmdList->Dispatch(groupCount, groupCount, 1);

    // Transition to SRV for sampling in deferred lighting and DXR/compute consumers.
    if (!m_transitionSkip.brdfLut) {
        constexpr D3D12_RESOURCE_STATES kSrvState =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_brdfLut.Get();
        barrier.Transition.StateBefore = m_brdfLutState;
        barrier.Transition.StateAfter = kSrvState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_brdfLutState = kSrvState;
    }

    m_brdfLutReady = true;
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::ApplyDeferredLighting(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* hdrTarget,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
    ID3D12Resource* depthBuffer,
    const DescriptorHandle& depthSRV,
    ID3D12Resource* envDiffuseResource,
    ID3D12Resource* envSpecularResource,
    DXGI_FORMAT envFormat,
    const DescriptorHandle& shadowMapSRV,
    const DeferredLightingParams& params
) {
    if (!m_deferredLightingPipeline) {
        return Result<void>::Err("Deferred lighting pipeline not initialized");
    }
    (void)hdrTarget;
    (void)depthSRV; // SRVs are written directly from the resources below.
    if (!depthBuffer) {
        return Result<void>::Err("Deferred lighting requires a valid depth buffer");
    }
    // Only shadow map SRV is required; env diffuse/specular can be empty (null SRVs created below)
    if (!shadowMapSRV.IsValid()) {
        return Result<void>::Err("Deferred lighting requires a valid shadow map SRV");
    }
    {
        auto brdfResult = EnsureBRDFLUT(cmdList);
        if (brdfResult.IsErr()) {
            return Result<void>::Err("Deferred lighting failed to ensure BRDF LUT: " + brdfResult.Error());
        }
    }
    if (params.clusterParams.z > 0u && !m_transitionSkip.clusteredLights) {
        auto clusterResult = BuildClusteredLightLists(cmdList, params);
        if (clusterResult.IsErr()) {
            return Result<void>::Err("Deferred lighting failed to build clustered light lists: " + clusterResult.Error());
        }
    }

    // Transition G-buffers to shader resource (pixel + non-pixel) so the
    // results can be sampled by post-process and DXR without additional barriers.
    constexpr D3D12_RESOURCE_STATES kSrvState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER barriers[6] = {};
    int barrierCount = 0;

    if (!m_transitionSkip.deferredLighting && m_albedoState != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferAlbedo.Get();
        barriers[barrierCount].Transition.StateBefore = m_albedoState;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_albedoState = kSrvState;
    }

    if (!m_transitionSkip.deferredLighting && m_normalRoughnessState != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferNormalRoughness.Get();
        barriers[barrierCount].Transition.StateBefore = m_normalRoughnessState;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_normalRoughnessState = kSrvState;
    }

    if (!m_transitionSkip.deferredLighting && m_emissiveMetallicState != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferEmissiveMetallic.Get();
        barriers[barrierCount].Transition.StateBefore = m_emissiveMetallicState;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_emissiveMetallicState = kSrvState;
    }

    if (!m_transitionSkip.deferredLighting && m_materialExt0State != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt0.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt0State;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt0State = kSrvState;
    }

    if (!m_transitionSkip.deferredLighting && m_materialExt1State != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt1.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt1State;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt1State = kSrvState;
    }

    if (!m_transitionSkip.deferredLighting && m_materialExt2State != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt2.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt2State;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt2State = kSrvState;
    }

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(barrierCount, barriers);
    }

    // Set render target
    cmdList->OMSetRenderTargets(1, &hdrRTV, FALSE, nullptr);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    // Set pipeline
    cmdList->SetPipelineState(m_deferredLightingPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_deferredLightingRootSignature.Get());

    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = {m_descriptorManager->GetCBV_SRV_UAV_Heap()};
    cmdList->SetDescriptorHeaps(1, heaps);

    // b0: Lighting parameters (using persistent constant buffer)
    // Create CB on first use, reuse every frame
    if (!m_deferredLightingCB) {
        D3D12_HEAP_PROPERTIES uploadHeapProps = {};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC cbDesc = {};
        cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width = (sizeof(DeferredLightingParams) + 255) & ~255; // Align to 256 bytes
        cbDesc.Height = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels = 1;
        cbDesc.SampleDesc.Count = 1;
        cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_deferredLightingCB)
        );

        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create deferred lighting constant buffer");
        }
        m_deferredLightingCB->SetName(L"DeferredLightingCB");
    }

    // Map and update constant buffer with current frame's lighting params
    void* mapped = nullptr;
    HRESULT hr = m_deferredLightingCB->Map(0, nullptr, &mapped);
    if (SUCCEEDED(hr) && mapped) {
        memcpy(mapped, &params, sizeof(DeferredLightingParams));
        m_deferredLightingCB->Unmap(0, nullptr);
    }

    // Bind persistent CB as root CBV
    cmdList->SetGraphicsRootConstantBufferView(0, m_deferredLightingCB->GetGPUVirtualAddress());

    // NOTE: DeferredLighting.hlsl expects a stable texel size and specular max mip
    // in the constant buffer; validate here so missing CPU-side wiring fails loudly.
    if (params.shadowInvSizeAndSpecMaxMip.x <= 0.0f || params.shadowInvSizeAndSpecMaxMip.y <= 0.0f) {
        return Result<void>::Err("Deferred lighting requires valid shadowInvSize (set from shadow map dimensions)");
    }

    // t0-t6: G-buffer + depth + material extension SRVs (descriptor table)
    auto& gbufferTable = m_deferredGBufferSrvTables[m_frameIndex];

    D3D12_CPU_DESCRIPTOR_HANDLE dst0 = gbufferTable[0].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dst1 = gbufferTable[1].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dst2 = gbufferTable[2].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dst3 = gbufferTable[3].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dst4 = gbufferTable[4].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dst5 = gbufferTable[5].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dst6 = gbufferTable[6].cpu;

    // Avoid CopyDescriptorsSimple from shader-visible heaps; write the descriptors directly.
    D3D12_SHADER_RESOURCE_VIEW_DESC albedoSrvDesc{};
    albedoSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    albedoSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    albedoSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    albedoSrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(m_gbufferAlbedo.Get(), &albedoSrvDesc, dst0);

    D3D12_SHADER_RESOURCE_VIEW_DESC normalSrvDesc{};
    normalSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    normalSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    normalSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    normalSrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(m_gbufferNormalRoughness.Get(), &normalSrvDesc, dst1);

    D3D12_SHADER_RESOURCE_VIEW_DESC emissiveSrvDesc{};
    emissiveSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    emissiveSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    emissiveSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    emissiveSrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(m_gbufferEmissiveMetallic.Get(), &emissiveSrvDesc, dst2);

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
    depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(depthBuffer, &depthSrvDesc, dst3);

    D3D12_SHADER_RESOURCE_VIEW_DESC ext0SrvDesc{};
    ext0SrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ext0SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    ext0SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    ext0SrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(m_gbufferMaterialExt0.Get(), &ext0SrvDesc, dst4);

    D3D12_SHADER_RESOURCE_VIEW_DESC ext1SrvDesc{};
    ext1SrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ext1SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    ext1SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    ext1SrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(m_gbufferMaterialExt1.Get(), &ext1SrvDesc, dst5);

    D3D12_SHADER_RESOURCE_VIEW_DESC ext2SrvDesc{};
    ext2SrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ext2SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    ext2SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    ext2SrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(m_gbufferMaterialExt2.Get(), &ext2SrvDesc, dst6);

    cmdList->SetGraphicsRootDescriptorTable(1, gbufferTable[0].gpu);

    // t7-t10: Environment (diffuse+specular) + shadow map + BRDF LUT SRVs (descriptor table)
    auto& envShadowTable = m_deferredEnvShadowSrvTables[m_frameIndex];
    D3D12_CPU_DESCRIPTOR_HANDLE envDst = envShadowTable[0].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE specDst = envShadowTable[1].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDst = envShadowTable[2].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE brdfDst = envShadowTable[3].cpu;

    // Create SRVs directly from resources to avoid copying from shader-visible heaps
    auto createSrvOrNull = [&](D3D12_CPU_DESCRIPTOR_HANDLE dst, ID3D12Resource* resource, DXGI_FORMAT fmt) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = fmt;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = resource ? resource->GetDesc().MipLevels : 1;
        
        m_device->GetDevice()->CreateShaderResourceView(resource, &srvDesc, dst);
    };

    createSrvOrNull(envDst, envDiffuseResource, envFormat);
    createSrvOrNull(specDst, envSpecularResource, envFormat);

    // Shadow map comes as a descriptor handle (assumed CPU readable), so copy it
    if (shadowMapSRV.IsValid()) {
        m_device->GetDevice()->CopyDescriptorsSimple(1, shadowDst, shadowMapSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    } else {
        createSrvOrNull(shadowDst, nullptr, DXGI_FORMAT_R32_FLOAT);
    }

    // BRDF LUT is owned by the VB system; write directly to avoid copying from shader-visible heaps.
    D3D12_SHADER_RESOURCE_VIEW_DESC brdfSrvDesc{};
    brdfSrvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    brdfSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    brdfSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    brdfSrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(m_brdfLut.Get(), &brdfSrvDesc, brdfDst);

    cmdList->SetGraphicsRootDescriptorTable(2, envShadowTable[0].gpu);

    // t11-t13: Clustered deferred resources (root SRVs). These may be null when localLightCount==0.
    const D3D12_GPU_VIRTUAL_ADDRESS lightsVA =
        (m_localLightCount > 0 && m_localLightsBuffer) ? m_localLightsBuffer->GetGPUVirtualAddress() : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS rangesVA =
        m_clusterRangesBuffer ? m_clusterRangesBuffer->GetGPUVirtualAddress() : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS indicesVA =
        m_clusterLightIndicesBuffer ? m_clusterLightIndicesBuffer->GetGPUVirtualAddress() : 0;

    cmdList->SetGraphicsRootShaderResourceView(3, lightsVA);
    cmdList->SetGraphicsRootShaderResourceView(4, rangesVA);
    cmdList->SetGraphicsRootShaderResourceView(5, indicesVA);

    // Draw fullscreen triangle
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

