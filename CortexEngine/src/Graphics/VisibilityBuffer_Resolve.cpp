#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {
Result<void> VisibilityBufferRenderer::ResolveMaterials(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE depthSRV,
    const std::vector<VBMeshDrawInfo>& meshDraws,
    const glm::mat4& viewProj,
    D3D12_GPU_VIRTUAL_ADDRESS biomeMaterialsAddress
) {
    if (meshDraws.empty()) {
        return Result<void>::Err("No meshes provided for material resolve");
    }
    (void)depthSRV; // SRV is written directly from depthBuffer below.
    if (!depthBuffer) {
        return Result<void>::Err("Material resolve requires a valid depth buffer");
    }
    if (!m_device || !m_descriptorManager) {
        return Result<void>::Err("Material resolve missing device or descriptor manager");
    }
    // Transition visibility buffer to shader resource
    if (!m_transitionSkip.materialResolve && m_visibilityState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityBuffer.Get();
        barrier.Transition.StateBefore = m_visibilityState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    // Transition G-buffers to UAV
    D3D12_RESOURCE_BARRIER barriers[6] = {};
    int barrierCount = 0;

    if (!m_transitionSkip.materialResolve && m_albedoState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferAlbedo.Get();
        barriers[barrierCount].Transition.StateBefore = m_albedoState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_albedoState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (!m_transitionSkip.materialResolve && m_normalRoughnessState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferNormalRoughness.Get();
        barriers[barrierCount].Transition.StateBefore = m_normalRoughnessState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_normalRoughnessState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (!m_transitionSkip.materialResolve && m_emissiveMetallicState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferEmissiveMetallic.Get();
        barriers[barrierCount].Transition.StateBefore = m_emissiveMetallicState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_emissiveMetallicState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (!m_transitionSkip.materialResolve && m_materialExt0State != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt0.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt0State;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt0State = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (!m_transitionSkip.materialResolve && m_materialExt1State != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt1.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt1State;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt1State = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (!m_transitionSkip.materialResolve && m_materialExt2State != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt2.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt2State;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt2State = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(barrierCount, barriers);
    }

    // Set compute pipeline
    cmdList->SetPipelineState(m_resolvePipeline.Get());
    cmdList->SetComputeRootSignature(m_resolveRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Param 0 (b0): Resolution constants + view-projection matrix + mesh index
    struct ResolutionConstants {
        uint32_t width;
        uint32_t height;
        float rcpWidth;
        float rcpHeight;
        glm::mat4 viewProj;  // 16 floats
        uint32_t materialCount;
        uint32_t meshCount;
        uint32_t instanceCount;  // For bounds checking in shader
        uint32_t pad;
    } resConsts = {};

    // Param 1 (t1): Instance buffer SRV (root descriptor)
    // Use frame-indexed buffer to match what was written by UpdateInstances()
    D3D12_GPU_VIRTUAL_ADDRESS instanceBufferAddress = m_instanceBuffer[m_frameIndex]->GetGPUVirtualAddress();
    cmdList->SetComputeRootShaderResourceView(1, instanceBufferAddress);

    // Param 5 (t5): Material constants buffer SRV (root descriptor)
    // Use frame-indexed buffer to match what was written by UpdateMaterials()
    resConsts.materialCount = m_materialCount;
    resConsts.meshCount = static_cast<uint32_t>(meshDraws.size());
    resConsts.instanceCount = m_instanceCount;  // For bounds checking in shader
    D3D12_GPU_VIRTUAL_ADDRESS materialBufferAddress = 0;
    if (m_materialBuffer[m_frameIndex]) {
        materialBufferAddress = m_materialBuffer[m_frameIndex]->GetGPUVirtualAddress();
    }
    cmdList->SetComputeRootShaderResourceView(5, materialBufferAddress);

    // Param 6 (b4): Biome materials constants (CBV - root descriptor)
    if (biomeMaterialsAddress != 0) {
        cmdList->SetComputeRootConstantBufferView(6, biomeMaterialsAddress);
    }

    auto meshTableResult = UpdateMeshTable(meshDraws);
    if (meshTableResult.IsErr()) {
        return meshTableResult;
    }
    cmdList->SetComputeRootShaderResourceView(
        4,
        (m_meshCount > 0 && m_meshTableBuffer[m_frameIndex])
            ? m_meshTableBuffer[m_frameIndex]->GetGPUVirtualAddress()
            : 0
    );

    // Param 2: Descriptor table with t0 (visibility) + t2 (depth).
    auto& visDepthTable = m_resolveVisDepthTables[m_frameIndex];
    D3D12_CPU_DESCRIPTOR_HANDLE visDst = visDepthTable[0].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE depthDst = visDepthTable[1].cpu;

    // Avoid CopyDescriptorsSimple from shader-visible heaps; write the descriptors directly.
    D3D12_SHADER_RESOURCE_VIEW_DESC visSrvDesc{};
    visSrvDesc.Format = DXGI_FORMAT_R32G32_UINT;
    visSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    visSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    visSrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(m_visibilityBuffer.Get(), &visSrvDesc, visDst);

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
    depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(depthBuffer, &depthSrvDesc, depthDst);

    cmdList->SetComputeRootDescriptorTable(2, visDepthTable[0].gpu);

    // Param 3: Descriptor table with u0-u5 (G-buffer UAVs).
    auto& gbufferUavTable = m_resolveGBufferUavTables[m_frameIndex];
    D3D12_CPU_DESCRIPTOR_HANDLE albedoUavDst = gbufferUavTable[0].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE normalUavDst = gbufferUavTable[1].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE emissiveUavDst = gbufferUavTable[2].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE ext0UavDst = gbufferUavTable[3].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE ext1UavDst = gbufferUavTable[4].cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE ext2UavDst = gbufferUavTable[5].cpu;

    D3D12_UNORDERED_ACCESS_VIEW_DESC albedoUavDesc{};
    albedoUavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    albedoUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferAlbedo.Get(), nullptr, &albedoUavDesc, albedoUavDst);

    D3D12_UNORDERED_ACCESS_VIEW_DESC normalUavDesc{};
    normalUavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    normalUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferNormalRoughness.Get(), nullptr, &normalUavDesc, normalUavDst);

    D3D12_UNORDERED_ACCESS_VIEW_DESC emissiveUavDesc{};
    emissiveUavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    emissiveUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferEmissiveMetallic.Get(), nullptr, &emissiveUavDesc, emissiveUavDst);

    D3D12_UNORDERED_ACCESS_VIEW_DESC ext0UavDesc{};
    ext0UavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ext0UavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferMaterialExt0.Get(), nullptr, &ext0UavDesc, ext0UavDst);

    D3D12_UNORDERED_ACCESS_VIEW_DESC ext1UavDesc{};
    ext1UavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ext1UavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferMaterialExt1.Get(), nullptr, &ext1UavDesc, ext1UavDst);

    D3D12_UNORDERED_ACCESS_VIEW_DESC ext2UavDesc{};
    ext2UavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ext2UavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferMaterialExt2.Get(), nullptr, &ext2UavDesc, ext2UavDst);

    cmdList->SetComputeRootDescriptorTable(3, gbufferUavTable[0].gpu);

    uint32_t dispatchX = (m_width + 7) / 8;
    uint32_t dispatchY = (m_height + 7) / 8;

    resConsts.width = m_width;
    resConsts.height = m_height;
    resConsts.rcpWidth = 1.0f / static_cast<float>(m_width);
    resConsts.rcpHeight = 1.0f / static_cast<float>(m_height);
    resConsts.viewProj = viewProj;
    cmdList->SetComputeRoot32BitConstants(0, 24, &resConsts, 0);  // 4 + 16 + 4 = 24 dwords

    // Single fullscreen dispatch; per-pixel mesh selection is driven by instance.meshIndex.
    cmdList->Dispatch(dispatchX, dispatchY, 1);

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::ComputeMotionVectors(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* velocityBuffer,
    const std::vector<VBMeshDrawInfo>& meshDraws,
    D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress
) {
    if (!cmdList || !velocityBuffer) {
        return Result<void>::Err("VB motion vectors requires a valid command list and velocity buffer");
    }
    if (!m_motionVectorsPipeline || !m_motionVectorsRootSignature) {
        return Result<void>::Err("VB motion vectors pipeline not initialized");
    }
    if (!m_instanceBuffer[m_frameIndex] || m_instanceCount == 0) {
        return Result<void>::Ok();
    }
    if (!m_visibilitySRV.IsValid()) {
        return Result<void>::Err("VB motion vectors requires valid visibility SRV");
    }
    auto meshTableResult = UpdateMeshTable(meshDraws);
    if (meshTableResult.IsErr()) {
        return meshTableResult;
    }
    if (m_meshCount == 0) {
        return Result<void>::Ok();
    }
    if (!m_meshTableBuffer[m_frameIndex] || !m_meshTableBufferMapped[m_frameIndex]) {
        return Result<void>::Err("VB motion vectors requires mesh table buffer");
    }
    if (frameConstantsAddress == 0) {
        return Result<void>::Err("VB motion vectors requires a valid FrameConstants GPU address");
    }

    cmdList->SetPipelineState(m_motionVectorsPipeline.Get());
    cmdList->SetComputeRootSignature(m_motionVectorsRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    struct MotionConstants {
        uint32_t width;
        uint32_t height;
        float rcpWidth;
        float rcpHeight;
        uint32_t meshCount;
        uint32_t pad[3];
    } mv{};
    mv.width = m_width;
    mv.height = m_height;
    mv.rcpWidth = (m_width > 0) ? (1.0f / static_cast<float>(m_width)) : 0.0f;
    mv.rcpHeight = (m_height > 0) ? (1.0f / static_cast<float>(m_height)) : 0.0f;
    mv.meshCount = m_meshCount;
    cmdList->SetComputeRoot32BitConstants(0, 8, &mv, 0);

    cmdList->SetComputeRootConstantBufferView(1, frameConstantsAddress);

    cmdList->SetComputeRootShaderResourceView(2, m_instanceBuffer[m_frameIndex]->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(3, m_meshTableBuffer[m_frameIndex]->GetGPUVirtualAddress());

    // t0: visibility SRV table. The persistent visibility SRV is already a valid single-descriptor table.
    cmdList->SetComputeRootDescriptorTable(4, m_visibilitySRV.gpu);

    // u0: velocity UAV table.
    DescriptorHandle velUav = m_motionVelocityUavTables[m_frameIndex];
    if (!velUav.IsValid()) {
        return Result<void>::Err("VB motion vectors velocity UAV table is not allocated");
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;
    m_device->GetDevice()->CreateUnorderedAccessView(velocityBuffer, nullptr, &uavDesc, velUav.cpu);

    cmdList->SetComputeRootDescriptorTable(5, velUav.gpu);

    const uint32_t dispatchX = (m_width + 7u) / 8u;
    const uint32_t dispatchY = (m_height + 7u) / 8u;
    cmdList->Dispatch(dispatchX, dispatchY, 1);

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

