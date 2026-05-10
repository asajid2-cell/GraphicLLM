#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {
D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetAlbedoSRV() const {
    return m_albedoSRV.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetNormalRoughnessSRV() const {
    return m_normalRoughnessSRV.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetEmissiveMetallicSRV() const {
    return m_emissiveMetallicSRV.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetMaterialExt2SRV() const {
    return m_materialExt2SRV.gpu;
}

Result<void> VisibilityBufferRenderer::DebugBlitAlbedoToHDR(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* hdrTarget,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV
) {
    return DebugBlitGBufferToHDR(cmdList, hdrTarget, hdrRTV, DebugBlitBuffer::Albedo);
}

Result<void> VisibilityBufferRenderer::DebugBlitGBufferToHDR(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* hdrTarget,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
    DebugBlitBuffer buffer
) {
    (void)hdrTarget;

    if (!cmdList) {
        return Result<void>::Err("Debug blit requires a valid command list");
    }
    if (!m_blitPipeline || !m_blitRootSignature || !m_blitSamplerHeap) {
        return Result<void>::Err("Debug blit pipeline not initialized");
    }

    constexpr D3D12_RESOURCE_STATES kSrvState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    ID3D12Resource* src = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE srv = {};

    switch (buffer) {
        case DebugBlitBuffer::Albedo:
            src = m_gbufferAlbedo.Get();
            state = &m_albedoState;
            srv = m_albedoSRV.gpu;
            break;
        case DebugBlitBuffer::NormalRoughness:
            src = m_gbufferNormalRoughness.Get();
            state = &m_normalRoughnessState;
            srv = m_normalRoughnessSRV.gpu;
            break;
        case DebugBlitBuffer::EmissiveMetallic:
            src = m_gbufferEmissiveMetallic.Get();
            state = &m_emissiveMetallicState;
            srv = m_emissiveMetallicSRV.gpu;
            break;
        case DebugBlitBuffer::MaterialExt0:
            src = m_gbufferMaterialExt0.Get();
            state = &m_materialExt0State;
            srv = m_materialExt0SRV.gpu;
            break;
        case DebugBlitBuffer::MaterialExt1:
            src = m_gbufferMaterialExt1.Get();
            state = &m_materialExt1State;
            srv = m_materialExt1SRV.gpu;
            break;
        case DebugBlitBuffer::MaterialExt2:
            src = m_gbufferMaterialExt2.Get();
            state = &m_materialExt2State;
            srv = m_materialExt2SRV.gpu;
            break;
        default:
            return Result<void>::Err("Unknown debug blit buffer");
    }

    if (!src || !state || srv.ptr == 0) {
        return Result<void>::Err("Debug blit source not available");
    }

    if (!m_transitionSkip.debugBlit && *state != kSrvState) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = src;
        barrier.Transition.StateBefore = *state;
        barrier.Transition.StateAfter = kSrvState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        *state = kSrvState;
    }

    cmdList->OMSetRenderTargets(1, &hdrRTV, FALSE, nullptr);

    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    cmdList->SetPipelineState(m_blitPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_blitRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = {
        m_descriptorManager->GetCBV_SRV_UAV_Heap(),
        m_blitSamplerHeap.Get()
    };
    cmdList->SetDescriptorHeaps(2, heaps);

    cmdList->SetGraphicsRootDescriptorTable(0, srv);
    cmdList->SetGraphicsRootDescriptorTable(1, m_blitSamplerHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::DebugBlitVisibilityToHDR(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* hdrTarget,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV
) {
    (void)hdrTarget;

    if (!cmdList) {
        return Result<void>::Err("Visibility debug blit requires a valid command list");
    }
    if (!m_blitVisibilityPipeline || !m_blitRootSignature || !m_blitSamplerHeap) {
        return Result<void>::Err("Visibility debug blit pipeline not initialized");
    }

    constexpr D3D12_RESOURCE_STATES kSrvState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    if (!m_transitionSkip.debugBlit && m_visibilityState != kSrvState) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityBuffer.Get();
        barrier.Transition.StateBefore = m_visibilityState;
        barrier.Transition.StateAfter = kSrvState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityState = kSrvState;
    }

    cmdList->OMSetRenderTargets(1, &hdrRTV, FALSE, nullptr);

    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    cmdList->SetPipelineState(m_blitVisibilityPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_blitRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = {
        m_descriptorManager->GetCBV_SRV_UAV_Heap(),
        m_blitSamplerHeap.Get()
    };
    cmdList->SetDescriptorHeaps(2, heaps);

    cmdList->SetGraphicsRootDescriptorTable(0, m_visibilitySRV.gpu);
    cmdList->SetGraphicsRootDescriptorTable(1, m_blitSamplerHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::DebugBlitDepthToHDR(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* hdrTarget,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
    ID3D12Resource* depthBuffer
) {
    (void)hdrTarget;

    if (!cmdList) {
        return Result<void>::Err("Depth debug blit requires a valid command list");
    }
    if (!depthBuffer) {
        return Result<void>::Err("Depth debug blit requires a valid depth buffer");
    }
    if (!m_blitDepthPipeline || !m_blitRootSignature || !m_blitSamplerHeap) {
        return Result<void>::Err("Depth debug blit pipeline not initialized");
    }

    DescriptorHandle srv = m_debugDepthSrvTables[m_frameIndex];
    if (!srv.IsValid()) {
        return Result<void>::Err("Depth debug blit persistent SRV table is not allocated");
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
    depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(depthBuffer, &depthSrvDesc, srv.cpu);

    cmdList->OMSetRenderTargets(1, &hdrRTV, FALSE, nullptr);

    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    cmdList->SetPipelineState(m_blitDepthPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_blitRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = {
        m_descriptorManager->GetCBV_SRV_UAV_Heap(),
        m_blitSamplerHeap.Get()
    };
    cmdList->SetDescriptorHeaps(2, heaps);

    cmdList->SetGraphicsRootDescriptorTable(0, srv.gpu);
    cmdList->SetGraphicsRootDescriptorTable(1, m_blitSamplerHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    return Result<void>::Ok();
}
D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetMaterialExt0SRV() const {
    return m_materialExt0SRV.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetMaterialExt1SRV() const {
    return m_materialExt1SRV.gpu;
}
} // namespace Cortex::Graphics

