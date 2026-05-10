#include "VisibilityBuffer.h"
#include "RHI/DescriptorHeap.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
Result<void> VisibilityBufferRenderer::ClearVisibilityBuffer(ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList || !m_visibilityBuffer || !m_visibilityUAV.IsValid() || !m_visibilityUAVStaging.IsValid()) {
        return Result<void>::Err("Visibility buffer clear prerequisites missing");
    }

    // Ensure descriptor heap is bound for UAV clear.
    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Clear visibility buffer to 0xFFFFFFFF (background marker) via UAV to avoid
    // undefined float->uint conversions on integer RT formats.
    if (!m_transitionSkip.visibilityPass && m_visibilityState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityBuffer.Get();
        barrier.Transition.StateBefore = m_visibilityState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    const UINT clearValues[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    cmdList->ClearUnorderedAccessViewUint(
        m_visibilityUAV.gpu,
        m_visibilityUAVStaging.cpu,
        m_visibilityBuffer.Get(),
        clearValues,
        0,
        nullptr
    );

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::RasterizeVisibilityBuffer(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDSV,
    const glm::mat4& viewProj,
    const std::vector<VBMeshDrawInfo>& meshDraws,
    D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress
) {
    (void)depthBuffer;

    if (meshDraws.empty() || m_instanceCount == 0) {
        return Result<void>::Ok(); // Nothing to draw
    }

    if (!cmdList || !m_visibilityBuffer) {
        return Result<void>::Err("Visibility raster prerequisites missing");
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Transition visibility buffer to render target for the visibility pass.
    if (!m_transitionSkip.visibilityPass && m_visibilityState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityBuffer.Get();
        barrier.Transition.StateBefore = m_visibilityState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    // Set render target
    cmdList->OMSetRenderTargets(1, &m_visibilityRTV.cpu, FALSE, &depthDSV);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    // Set pipeline state and root signature (opaque, single-sided)
    cmdList->SetPipelineState(m_visibilityPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_visibilityRootSignature.Get());

    // Set instance buffer (root descriptor t0) - only needs to be set once
    // Use frame-indexed buffer to match what was written by UpdateInstances()
    D3D12_GPU_VIRTUAL_ADDRESS instanceBufferAddress = m_instanceBuffer[m_frameIndex]->GetGPUVirtualAddress();
    cmdList->SetGraphicsRootShaderResourceView(1, instanceBufferAddress);

    // Optional per-instance culling mask (root descriptor t2). Root SRVs must
    // always bind a valid GPU VA; use a small dummy buffer when the caller
    // doesn't provide a cull mask.
    const bool hasCullMask = (cullMaskAddress != 0);
    if (!hasCullMask) {
        cullMaskAddress = GetDummyCullMaskAddress();
    }
    cmdList->SetGraphicsRootShaderResourceView(2, cullMaskAddress);

    // Set primitive topology
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto drawMeshRange = [&](uint32_t meshIdx,
                             const VBMeshDrawInfo& drawInfo,
                             uint32_t instanceCount,
                             uint32_t startInstance) {
        if (!drawInfo.vertexBuffer || !drawInfo.indexBuffer || drawInfo.indexCount == 0 || instanceCount == 0) {
            return;
        }

        // Set per-mesh constants (view-projection matrix + current mesh index + material count)
        struct {
            glm::mat4 viewProj;
            uint32_t meshIndex;
            uint32_t baseInstance;
            uint32_t materialCount;
            uint32_t cullMaskCount;
        } perMeshData;
        perMeshData.viewProj = viewProj;
        perMeshData.meshIndex = meshIdx;
        perMeshData.baseInstance = startInstance;
        perMeshData.materialCount = m_materialCount;
        perMeshData.cullMaskCount = hasCullMask ? m_instanceCount : 0u;
        cmdList->SetGraphicsRoot32BitConstants(0, 20, &perMeshData, 0);

        // Set vertex buffer for this mesh
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = drawInfo.vertexBuffer->GetGPUVirtualAddress();
        const uint32_t stride = (drawInfo.vertexStrideBytes > 0) ? drawInfo.vertexStrideBytes : static_cast<uint32_t>(sizeof(Vertex));
        const uint64_t vbBytesAvail = drawInfo.vertexBuffer->GetDesc().Width;

        uint64_t vbBytes = vbBytesAvail;
        if (drawInfo.vertexCount > 0) {
            const uint64_t vbBytesNeeded = static_cast<uint64_t>(drawInfo.vertexCount) * static_cast<uint64_t>(stride);
            vbBytes = std::min(vbBytesNeeded, vbBytesAvail);
            if (vbBytesNeeded > vbBytesAvail) {
                static bool s_loggedOnce = false;
                if (!s_loggedOnce) {
                    s_loggedOnce = true;
                    spdlog::warn("VB: vertex buffer smaller than expected (needed={} avail={}); clamping VBV size",
                                 vbBytesNeeded, vbBytesAvail);
                }
            }
        }

        vbv.SizeInBytes = static_cast<UINT>(std::min<uint64_t>(vbBytes, static_cast<uint64_t>(UINT_MAX)));
        vbv.StrideInBytes = stride;
        cmdList->IASetVertexBuffers(0, 1, &vbv);

        // Set index buffer for this mesh
        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = drawInfo.indexBuffer->GetGPUVirtualAddress();
        const uint32_t indexStride = (drawInfo.indexFormat == 1u) ? 2u : 4u;
        const uint64_t ibBytesNeeded = static_cast<uint64_t>(drawInfo.indexCount) * static_cast<uint64_t>(indexStride);
        const uint64_t ibBytesAvail = drawInfo.indexBuffer->GetDesc().Width;
        const uint64_t ibBytes = std::min(ibBytesNeeded, ibBytesAvail);
        ibv.SizeInBytes = static_cast<UINT>(std::min<uint64_t>(ibBytes, static_cast<uint64_t>(UINT_MAX)));
        ibv.Format = (drawInfo.indexFormat == 1u) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        cmdList->IASetIndexBuffer(&ibv);

        // Important: use StartInstanceLocation=0 so SV_InstanceID is stable across
        // backends/drivers, and apply the draw's base instance explicitly via
        // root constants (g_BaseInstance). This keeps the visibility buffer's
        // instance IDs aligned with the packed global instance buffer.
        cmdList->DrawIndexedInstanced(
            drawInfo.indexCount,
            instanceCount,
            drawInfo.firstIndex,
            drawInfo.baseVertex,
            0
        );
    };

    // Draw each unique mesh with only the instances that reference it (opaque, single-sided).
    for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(meshDraws.size()); ++meshIdx) {
        const auto& drawInfo = meshDraws[meshIdx];
        drawMeshRange(meshIdx, drawInfo, drawInfo.instanceCount, drawInfo.startInstance);
    }

    // Opaque, double-sided (cull none).
    if (m_visibilityPipelineDoubleSided) {
        cmdList->SetPipelineState(m_visibilityPipelineDoubleSided.Get());
        cmdList->SetGraphicsRootSignature(m_visibilityRootSignature.Get());
        cmdList->SetGraphicsRootShaderResourceView(1, instanceBufferAddress);
    }
    for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(meshDraws.size()); ++meshIdx) {
        const auto& drawInfo = meshDraws[meshIdx];
        drawMeshRange(meshIdx, drawInfo, drawInfo.instanceCountDoubleSided, drawInfo.startInstanceDoubleSided);
    }

    // Alpha-tested visibility pass (cutout materials)
    if (m_visibilityAlphaPipeline && m_visibilityAlphaRootSignature && m_materialBuffer) {
        bool anyAlpha = false;
        for (const auto& drawInfo : meshDraws) {
            if (drawInfo.instanceCountAlpha > 0 || drawInfo.instanceCountAlphaDoubleSided > 0) {
                anyAlpha = true;
                break;
            }
        }

        if (anyAlpha) {
            cmdList->SetPipelineState(m_visibilityAlphaPipeline.Get());
            cmdList->SetGraphicsRootSignature(m_visibilityAlphaRootSignature.Get());

            cmdList->SetGraphicsRootShaderResourceView(1, instanceBufferAddress);
            cmdList->SetGraphicsRootShaderResourceView(2, m_materialBuffer[m_frameIndex]->GetGPUVirtualAddress());
            cmdList->SetGraphicsRootShaderResourceView(3, cullMaskAddress);

            for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(meshDraws.size()); ++meshIdx) {
                const auto& drawInfo = meshDraws[meshIdx];
                drawMeshRange(meshIdx, drawInfo, drawInfo.instanceCountAlpha, drawInfo.startInstanceAlpha);
            }

            // Alpha-tested, double-sided (cull none).
            if (m_visibilityAlphaPipelineDoubleSided) {
                cmdList->SetPipelineState(m_visibilityAlphaPipelineDoubleSided.Get());
                cmdList->SetGraphicsRootSignature(m_visibilityAlphaRootSignature.Get());
                cmdList->SetGraphicsRootShaderResourceView(1, instanceBufferAddress);
                cmdList->SetGraphicsRootShaderResourceView(2, m_materialBuffer[m_frameIndex]->GetGPUVirtualAddress());
                cmdList->SetGraphicsRootShaderResourceView(3, cullMaskAddress);
            }
            for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(meshDraws.size()); ++meshIdx) {
                const auto& drawInfo = meshDraws[meshIdx];
                drawMeshRange(meshIdx, drawInfo, drawInfo.instanceCountAlphaDoubleSided, drawInfo.startInstanceAlphaDoubleSided);
            }
        }
    }

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::RenderVisibilityPass(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDSV,
    const glm::mat4& viewProj,
    const std::vector<VBMeshDrawInfo>& meshDraws,
    D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress
) {
    if (meshDraws.empty() || m_instanceCount == 0) {
        return Result<void>::Ok();
    }

    auto clearResult = ClearVisibilityBuffer(cmdList);
    if (clearResult.IsErr()) {
        return clearResult;
    }

    return RasterizeVisibilityBuffer(cmdList, depthBuffer, depthDSV, viewProj, meshDraws, cullMaskAddress);
}

} // namespace Cortex::Graphics

