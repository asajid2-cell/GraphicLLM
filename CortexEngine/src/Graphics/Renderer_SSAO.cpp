#include "Renderer.h"
#include "Core/Window.h"
#include <algorithm>

namespace Cortex::Graphics {

Result<void> Renderer::CreateSSAOResources() {
    if (!m_device || !m_descriptorManager || !m_window) {
        return Result<void>::Err("Renderer not initialized for SSAO target creation");
    }

    // Render SSAO at half resolution for better performance; results are
    // bilinearly upsampled in post-process using depth-aware filtering.
    const UINT fullWidth = m_window->GetWidth();
    const UINT fullHeight = m_window->GetHeight();

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create SSAO target");
    }

    const UINT width = std::max<UINT>(1, fullWidth / 2);
    const UINT height = std::max<UINT>(1, fullHeight / 2);

    // Release existing target (descriptor handles remain valid and are reused)
    m_ssaoTex.Reset();

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8_UNORM;
    // AO of 1.0 means no occlusion
    clearValue.Color[0] = 1.0f;
    clearValue.Color[1] = 1.0f;
    clearValue.Color[2] = 1.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_ssaoTex)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create SSAO render target");
    }

    m_ssaoState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // RTV
    if (!m_ssaoRTV.IsValid()) {
        auto rtvResult = m_descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate RTV for SSAO target: " + rtvResult.Error());
        }
        m_ssaoRTV = rtvResult.Value();
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    m_device->GetDevice()->CreateRenderTargetView(
        m_ssaoTex.Get(),
        &rtvDesc,
        m_ssaoRTV.cpu
    );

    // SRV - use staging heap for persistent SSAO SRV (copied in post-process)
    if (!m_ssaoSRV.IsValid()) {
        auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate staging SRV for SSAO target: " + srvResult.Error());
        }
        m_ssaoSRV = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->GetDevice()->CreateShaderResourceView(
        m_ssaoTex.Get(),
        &srvDesc,
        m_ssaoSRV.cpu
    );

    spdlog::info("SSAO target created: {}x{}", width, height);
    return Result<void>::Ok();
}

void Renderer::RenderSSAO() {
    if (!m_ssaoEnabled || !m_ssaoPipeline || !m_ssaoTex || !m_depthBuffer || !m_depthSRV.IsValid()) {
        return;
    }

    // Transition depth to SRV for sampling
    if (m_depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_depthBuffer.Get();
        barrier.Transition.StateBefore = m_depthState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);
        m_depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Transition SSAO target to render target state
    if (m_ssaoState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_ssaoTex.Get();
        barrier.Transition.StateBefore = m_ssaoState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);
        m_ssaoState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    // Bind SSAO render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_ssaoRTV.cpu;
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_RESOURCE_DESC texDesc = m_ssaoTex->GetDesc();

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(texDesc.Width);
    viewport.Height = static_cast<float>(texDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(texDesc.Width);
    scissorRect.bottom = static_cast<LONG>(texDesc.Height);

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    // Clear to no occlusion
    const float clearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Bind pipeline and resources
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_ssaoPipeline->GetPipelineState());

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    // Frame constants
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    // Depth SRV as t0 via transient descriptor
    auto depthHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
    if (depthHandleResult.IsErr()) {
        spdlog::warn("RenderSSAO: failed to allocate transient depth SRV: {}", depthHandleResult.Error());
        return;
    }
    DescriptorHandle depthHandle = depthHandleResult.Value();

    m_device->GetDevice()->CopyDescriptorsSimple(
        1,
        depthHandle.cpu,
        m_depthSRV.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    // Bind SRV table at slot 3 (t0-t3)
    m_commandList->SetGraphicsRootDescriptorTable(3, depthHandle.gpu);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);
}

void Renderer::SetSSAOEnabled(bool enabled) {
    if (m_ssaoEnabled == enabled) {
        return;
    }
    m_ssaoEnabled = enabled;
    spdlog::info("SSAO {}", m_ssaoEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetSSAOParams(float radius, float bias, float intensity) {
    float r = glm::clamp(radius, 0.05f, 5.0f);
    float b = glm::clamp(bias, 0.0f, 0.1f);
    float i = glm::clamp(intensity, 0.0f, 4.0f);

    if (std::abs(r - m_ssaoRadius) < 1e-3f &&
        std::abs(b - m_ssaoBias) < 1e-4f &&
        std::abs(i - m_ssaoIntensity) < 1e-3f) {
        return;
    }

    m_ssaoRadius = r;
    m_ssaoBias = b;
    m_ssaoIntensity = i;
    spdlog::info("SSAO params set to radius={}, bias={}, intensity={}", m_ssaoRadius, m_ssaoBias, m_ssaoIntensity);
}

} // namespace Cortex::Graphics
