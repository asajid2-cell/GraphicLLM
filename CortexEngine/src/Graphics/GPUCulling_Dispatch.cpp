#include "GPUCullingInternal.h"
#include "RHI/DX12Device.h"
#include "RHI/DescriptorHeap.h"
#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
void GPUCullingPipeline::SetHZBForOcclusion(
    ID3D12Resource* hzbTexture,
    uint32_t hzbWidth,
    uint32_t hzbHeight,
    uint32_t hzbMipCount,
    const glm::mat4& hzbViewMatrix,
    const glm::mat4& hzbViewProjMatrix,
    const glm::vec3& hzbCameraPosWS,
    float cameraNearPlane,
    float cameraFarPlane,
    bool enabled)
{
    m_hzbEnabled = enabled && (hzbTexture != nullptr) && (hzbMipCount > 0) && (hzbWidth > 0) && (hzbHeight > 0);
    m_hzbTexture = hzbTexture;
    m_hzbWidth = hzbWidth;
    m_hzbHeight = hzbHeight;
    m_hzbMipCount = hzbMipCount;
    m_hzbViewMatrix = hzbViewMatrix;
    m_hzbViewProjMatrix = hzbViewProjMatrix;
    m_hzbCameraPosWS = hzbCameraPosWS;
    m_hzbNearPlane = cameraNearPlane;
    m_hzbFarPlane = cameraFarPlane;

    if (!m_descriptorManager || !m_device) {
        return;
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return;
    }

    // Update a CPU-only staging SRV; DispatchCulling() copies it into the
    // current frame's persistent dispatch descriptor.
    if (!m_hzbSrvStaging.IsValid()) {
        auto srvRes = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvRes.IsOk()) {
            m_hzbSrvStaging = srvRes.Value();
        }
    }
    if (!m_hzbSrvStaging.IsValid()) {
        return;
    }

    ID3D12Resource* srvResource = m_hzbEnabled ? m_hzbTexture : m_dummyHzbTexture.Get();
    if (!srvResource) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = (m_hzbEnabled ? static_cast<UINT>(m_hzbMipCount) : 1u);
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(srvResource, &srvDesc, m_hzbSrvStaging.cpu);
}

void GPUCullingPipeline::ExtractFrustumPlanes(const glm::mat4& viewProj, FrustumPlanes& planes) {
    // Extract frustum planes from view-projection matrix
    // Left plane
    planes.planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );

    // Right plane
    planes.planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );

    // Bottom plane
    planes.planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );

    // Top plane
    planes.planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );

    // Near plane
    planes.planes[4] = glm::vec4(
        viewProj[0][2],
        viewProj[1][2],
        viewProj[2][2],
        viewProj[3][2]
    );

    // Far plane
    planes.planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes.planes[i]));
        if (len > 0.0001f) {
            planes.planes[i] /= len;
        }
    }
}

Result<void> GPUCullingPipeline::DispatchCulling(
    ID3D12GraphicsCommandList* cmdList,
    const glm::mat4& viewProj,
    const glm::vec3& cameraPos)
{
    if (m_totalInstances == 0) {
        m_visibleCount = 0;
        return Result<void>::Ok();
    }
    if (!m_visibleCommandBuffer[m_frameIndex] || !m_allCommandBuffer[m_frameIndex] || !m_commandCountBuffer[m_frameIndex]) {
        return Result<void>::Err("GPU culling buffers are not initialized");
    }

    // Update constants
    CullConstants constants;
    constants.viewProj = viewProj;
    constants.cameraPos[0] = cameraPos.x;
    constants.cameraPos[1] = cameraPos.y;
    constants.cameraPos[2] = cameraPos.z;
    constants.instanceCount = m_totalInstances;

    // HZB occlusion culling using simplified depth comparison.
    // Changed from complex world-space near-point to simpler view-space centerZ - radius.
    const uint32_t hzbEnabled =
        (m_hzbEnabled && m_hzbTexture && (m_hzbMipCount > 0) && (m_hzbWidth > 0) && (m_hzbHeight > 0)) ? 1u : 0u;

    // Streak threshold: require N consecutive occluded frames before culling.
    // Higher values reduce popping/flickering but delay culling slightly.
    // At 60fps, 8 frames = 133ms delay before occlusion kicks in.
    constexpr uint32_t kOcclusionStreakThreshold = 8u;
    constants.occlusionParams0 = glm::uvec4(
        m_forceVisible ? 1u : 0u,
        hzbEnabled,
        m_hzbMipCount,
        kOcclusionStreakThreshold);

    constants.occlusionParams1 =
        glm::uvec4(m_hzbWidth, m_hzbHeight, m_maxInstances, m_debugEnabled ? 1u : 0u);

    const float invW = (m_hzbWidth > 0) ? (1.0f / static_cast<float>(m_hzbWidth)) : 0.0f;
    const float invH = (m_hzbHeight > 0) ? (1.0f / static_cast<float>(m_hzbHeight)) : 0.0f;

    // Projection scale terms (P00, P11) used for screen-radius estimation.
    // Derive the projection matrix from the captured view + view-projection.
    const glm::mat4 proj = m_hzbViewProjMatrix * glm::inverse(m_hzbViewMatrix);
    constants.occlusionParams2 = glm::vec4(invW, invH, proj[0][0], proj[1][1]);

    // View-space depth epsilon (meters/units). This is intentionally larger than
    // the old NDC-depth epsilon because HZB now stores view-space Z.
    // Increased to 5cm to be more conservative and reduce false occlusion.
    constexpr float kHzbEpsilon = 0.05f;
    const float cameraMotionWS =
        glm::length(glm::vec3(constants.cameraPos[0], constants.cameraPos[1], constants.cameraPos[2]) - m_hzbCameraPosWS);
    constants.occlusionParams3 = glm::vec4(m_hzbNearPlane, m_hzbFarPlane, kHzbEpsilon, cameraMotionWS);
    constants.hzbViewMatrix = m_hzbViewMatrix;
    constants.hzbViewProjMatrix = m_hzbViewProjMatrix;
    constants.hzbCameraPos = glm::vec4(m_hzbCameraPosWS, 0.0f);

    FrustumPlanes frustum;
    ExtractFrustumPlanes(viewProj, frustum);
    for (int i = 0; i < 6; i++) {
        constants.frustumPlanes[i] = frustum.planes[i];
    }

    // Map and upload constants
    void* mappedCB = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_cullConstantBuffer->Map(0, &readRange, &mappedCB);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map culling constant buffer");
    }
    memcpy(mappedCB, &constants, sizeof(CullConstants));
    m_cullConstantBuffer->Unmap(0, nullptr);

    // Ensure UAV resources are in the correct state before clearing/dispatch.
    if (m_allCommandState[m_frameIndex] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_allCommandBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_allCommandState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_allCommandState[m_frameIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    if (m_visibleCommandState[m_frameIndex] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibleCommandBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_visibleCommandState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibleCommandState[m_frameIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_commandCountState[m_frameIndex] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_commandCountBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_commandCountState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_commandCountState[m_frameIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_visibilityMaskBuffer[m_frameIndex] && m_visibilityMaskState[m_frameIndex] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityMaskBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_visibilityMaskState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityMaskState[m_frameIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Keep the debug UAV in a valid state for dispatch even when debug writes are disabled.
    if (m_debugBuffer && m_debugState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_debugBuffer.Get();
        barrier.Transition.StateBefore = m_debugState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_debugState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_instanceState[m_frameIndex] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_instanceBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_instanceState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_instanceState[m_frameIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    if (m_allCommandState[m_frameIndex] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_allCommandBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_allCommandState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_allCommandState[m_frameIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    // Ensure descriptor heap is bound (ClearUAV + HZB SRV table use it).
    if (m_descriptorManager) {
        ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
        cmdList->SetDescriptorHeaps(1, heaps);
    }

    // Clear the command count buffer to 0 using ClearUnorderedAccessViewUint (frame-indexed)
    const UINT clearValues[4] = { 0, 0, 0, 0 };
    cmdList->ClearUnorderedAccessViewUint(
        m_counterUAV[m_frameIndex].gpu,
        m_counterUAVStaging[m_frameIndex].cpu,
        m_commandCountBuffer[m_frameIndex].Get(),
        clearValues,
        0,
        nullptr
    );

    // One-time init: clear both occlusion history buffers so streaks start at 0.
    if (!m_historyInitialized && m_occlusionHistoryA && m_occlusionHistoryB &&
        m_historyAUAV.IsValid() && m_historyBUAV.IsValid()) {
        cmdList->ClearUnorderedAccessViewUint(
            m_historyAUAV.gpu, m_historyAUAVStaging.cpu, m_occlusionHistoryA.Get(),
            clearValues, 0, nullptr);
        cmdList->ClearUnorderedAccessViewUint(
            m_historyBUAV.gpu, m_historyBUAVStaging.cpu, m_occlusionHistoryB.Get(),
            clearValues, 0, nullptr);
        m_historyInitialized = true;
    }

    // Clear debug counters/sample (optional).
    if (m_debugEnabled && m_debugBuffer && m_debugUAV.IsValid()) {
        cmdList->ClearUnorderedAccessViewUint(
            m_debugUAV.gpu,
            m_debugUAVStaging.cpu,
            m_debugBuffer.Get(),
            clearValues,
            0,
            nullptr);
    }

    // UAV barriers to ensure clears complete before compute dispatch.
    D3D12_RESOURCE_BARRIER clearBarriers[4] = {};
    uint32_t barrierCount = 0;
    clearBarriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarriers[barrierCount].UAV.pResource = m_commandCountBuffer[m_frameIndex].Get();
    ++barrierCount;
    if (m_historyInitialized) {
        clearBarriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        clearBarriers[barrierCount].UAV.pResource = m_occlusionHistoryA.Get();
        ++barrierCount;
        clearBarriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        clearBarriers[barrierCount].UAV.pResource = m_occlusionHistoryB.Get();
        ++barrierCount;
    }
    if (m_debugEnabled && m_debugBuffer) {
        clearBarriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        clearBarriers[barrierCount].UAV.pResource = m_debugBuffer.Get();
        ++barrierCount;
    }
    cmdList->ResourceBarrier(barrierCount, clearBarriers);

    // Select occlusion history buffers for this dispatch.
    ID3D12Resource* historyIn = m_historyPingPong ? m_occlusionHistoryB.Get() : m_occlusionHistoryA.Get();
    ID3D12Resource* historyOut = m_historyPingPong ? m_occlusionHistoryA.Get() : m_occlusionHistoryB.Get();
    D3D12_RESOURCE_STATES* historyInState = m_historyPingPong ? &m_historyBState : &m_historyAState;
    D3D12_RESOURCE_STATES* historyOutState = m_historyPingPong ? &m_historyAState : &m_historyBState;

    // Transition history buffers to the required states.
    if (historyIn && *historyInState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = historyIn;
        barrier.Transition.StateBefore = *historyInState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        *historyInState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    if (historyOut && *historyOutState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = historyOut;
        barrier.Transition.StateBefore = *historyOutState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        *historyOutState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Set pipeline and root signature
    cmdList->SetComputeRootSignature(m_rootSignature.Get());
    cmdList->SetPipelineState(m_cullPipeline.Get());

    // Bind resources (use frame-indexed buffers to avoid race conditions)
    cmdList->SetComputeRootConstantBufferView(0, m_cullConstantBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(1, m_instanceBuffer[m_frameIndex]->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(2, m_allCommandBuffer[m_frameIndex]->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(3, historyIn ? historyIn->GetGPUVirtualAddress() : 0);
    cmdList->SetComputeRootUnorderedAccessView(4, m_visibleCommandBuffer[m_frameIndex]->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(5, m_commandCountBuffer[m_frameIndex]->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(6, historyOut ? historyOut->GetGPUVirtualAddress() : 0);
    cmdList->SetComputeRootUnorderedAccessView(7, m_debugBuffer ? m_debugBuffer->GetGPUVirtualAddress() : 0);
    cmdList->SetComputeRootUnorderedAccessView(8, m_visibilityMaskBuffer[m_frameIndex] ? m_visibilityMaskBuffer[m_frameIndex]->GetGPUVirtualAddress() : 0);

    // Bind HZB SRV through the current frame's persistent descriptor slot so
    // dispatch does not consume the transient descriptor ring.
    DescriptorHandle hzbSrvForDispatch = m_hzbSrv; // fallback dummy (always valid)
    DescriptorHandle& frameHzbSrv = m_hzbSrvDispatch[m_frameIndex];
    if (m_descriptorManager && m_hzbSrvStaging.IsValid() && frameHzbSrv.IsValid()) {
        hzbSrvForDispatch = frameHzbSrv;
        m_device->GetDevice()->CopyDescriptorsSimple(
            1,
            hzbSrvForDispatch.cpu,
            m_hzbSrvStaging.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    cmdList->SetComputeRootDescriptorTable(9, hzbSrvForDispatch.gpu);

    // Dispatch compute shader (64 threads per group)
    uint32_t numGroups = (m_totalInstances + 63) / 64;
    cmdList->Dispatch(numGroups, 1, 1);

    // Barrier to ensure compute writes are visible (frame-indexed)
    D3D12_RESOURCE_BARRIER uavBarriers[5] = {};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[0].UAV.pResource = m_visibleCommandBuffer[m_frameIndex].Get();
    uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[1].UAV.pResource = m_commandCountBuffer[m_frameIndex].Get();
    uavBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[2].UAV.pResource = historyOut;
    uint32_t uavBarrierCount = historyOut ? 3u : 2u;
    if (m_visibilityMaskBuffer[m_frameIndex]) {
        uavBarriers[uavBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarriers[uavBarrierCount].UAV.pResource = m_visibilityMaskBuffer[m_frameIndex].Get();
        ++uavBarrierCount;
    }
    if (m_debugEnabled && m_debugBuffer) {
        uavBarriers[uavBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarriers[uavBarrierCount].UAV.pResource = m_debugBuffer.Get();
        ++uavBarrierCount;
    }
    cmdList->ResourceBarrier(uavBarrierCount, uavBarriers);

    // Swap occlusion history buffers for next frame.
    m_historyPingPong = !m_historyPingPong;

    if (m_commandReadbackRequested && m_commandReadbackCount > 0) {
        const uint32_t readbackCount = std::min(m_commandReadbackCount, m_maxInstances);
        const size_t readbackBytes = static_cast<size_t>(readbackCount) * sizeof(IndirectCommand);
        if (!m_visibleCommandReadback ||
            m_visibleCommandReadback->GetDesc().Width < readbackBytes) {
            D3D12_HEAP_PROPERTIES readbackHeap = {};
            readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

            D3D12_RESOURCE_DESC readbackDesc = {};
            readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            readbackDesc.Width = readbackBytes;
            readbackDesc.Height = 1;
            readbackDesc.DepthOrArraySize = 1;
            readbackDesc.MipLevels = 1;
            readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
            readbackDesc.SampleDesc.Count = 1;
            readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            m_visibleCommandReadback.Reset();
            HRESULT rbHr = m_device->GetDevice()->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_visibleCommandReadback)
            );
            if (FAILED(rbHr)) {
                spdlog::warn("GPU culling: failed to create command readback buffer");
            }
        }

        if (m_visibleCommandReadback) {
            if (m_visibleCommandState[m_frameIndex] != D3D12_RESOURCE_STATE_COPY_SOURCE) {
                D3D12_RESOURCE_BARRIER toCopy = {};
                toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toCopy.Transition.pResource = m_visibleCommandBuffer[m_frameIndex].Get();
                toCopy.Transition.StateBefore = m_visibleCommandState[m_frameIndex];
                toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cmdList->ResourceBarrier(1, &toCopy);
                m_visibleCommandState[m_frameIndex] = D3D12_RESOURCE_STATE_COPY_SOURCE;
            }

            cmdList->CopyBufferRegion(
                m_visibleCommandReadback.Get(),
                0,
                m_visibleCommandBuffer[m_frameIndex].Get(),
                0,
                readbackBytes);

            m_commandReadbackPending = true;
            m_commandReadbackRequested = false;
            m_commandReadbackCount = readbackCount;
        } else {
            m_commandReadbackRequested = false;
        }
    }

    // Copy command count to readback for CPU stats (frame-indexed).
    if (m_commandCountReadback) {
        D3D12_RESOURCE_BARRIER toCopy = {};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = m_commandCountBuffer[m_frameIndex].Get();
        toCopy.Transition.StateBefore = m_commandCountState[m_frameIndex];
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &toCopy);
        m_commandCountState[m_frameIndex] = D3D12_RESOURCE_STATE_COPY_SOURCE;

        cmdList->CopyResource(m_commandCountReadback.Get(), m_commandCountBuffer[m_frameIndex].Get());
    }

    // Copy debug counters/sample to readback (optional).
    if (m_debugEnabled && m_debugReadback && m_debugBuffer) {
        D3D12_RESOURCE_BARRIER toCopy = {};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = m_debugBuffer.Get();
        toCopy.Transition.StateBefore = m_debugState;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &toCopy);
        m_debugState = D3D12_RESOURCE_STATE_COPY_SOURCE;

        cmdList->CopyResource(m_debugReadback.Get(), m_debugBuffer.Get());
        m_debugReadbackPending = true;
    }

    // Transition buffers for ExecuteIndirect (frame-indexed).
    D3D12_RESOURCE_BARRIER postBarriers[3] = {};
    UINT postCount = 0;

    if (m_commandCountState[m_frameIndex] != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
        postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postBarriers[postCount].Transition.pResource = m_commandCountBuffer[m_frameIndex].Get();
        postBarriers[postCount].Transition.StateBefore = m_commandCountState[m_frameIndex];
        postBarriers[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;
        m_commandCountState[m_frameIndex] = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    if (m_visibleCommandState[m_frameIndex] != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
        postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postBarriers[postCount].Transition.pResource = m_visibleCommandBuffer[m_frameIndex].Get();
        postBarriers[postCount].Transition.StateBefore = m_visibleCommandState[m_frameIndex];
        postBarriers[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;
        m_visibleCommandState[m_frameIndex] = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    constexpr D3D12_RESOURCE_STATES kVisibilityMaskSrvState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (m_visibilityMaskBuffer[m_frameIndex] && m_visibilityMaskState[m_frameIndex] != kVisibilityMaskSrvState) {
        postBarriers[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postBarriers[postCount].Transition.pResource = m_visibilityMaskBuffer[m_frameIndex].Get();
        postBarriers[postCount].Transition.StateBefore = m_visibilityMaskState[m_frameIndex];
        postBarriers[postCount].Transition.StateAfter = kVisibilityMaskSrvState;
        postBarriers[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;
        m_visibilityMaskState[m_frameIndex] = kVisibilityMaskSrvState;
    }

    if (postCount > 0) {
        cmdList->ResourceBarrier(postCount, postBarriers);
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

