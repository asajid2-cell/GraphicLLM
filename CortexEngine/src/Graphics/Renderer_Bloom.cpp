#include "Renderer.h"
#include "Core/Window.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace Cortex::Graphics {

Result<void> Renderer::CreateBloomResources() {
    if (!m_device || !m_descriptorManager || !m_window) {
        return Result<void>::Err("Renderer not initialized for bloom target creation");
    }

    const UINT fullWidth  = m_window->GetWidth();
    const UINT fullHeight = m_window->GetHeight();

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create bloom targets");
    }

    // Reset existing bloom resources.
    for (uint32_t level = 0; level < kBloomLevels; ++level) {
        m_bloomTexA[level].Reset();
        m_bloomTexB[level].Reset();
        m_bloomRTV[level][0] = {};
        m_bloomRTV[level][1] = {};
        m_bloomSRV[level][0] = {};
        m_bloomSRV[level][1] = {};
        m_bloomState[level][0] = D3D12_RESOURCE_STATE_COMMON;
        m_bloomState[level][1] = D3D12_RESOURCE_STATE_COMMON;
    }
    m_bloomCombinedSRV = {};

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // Build a small bloom pyramid: level 0 = 1/2, level 1 = 1/4, level 2 = 1/8, ...
    for (uint32_t level = 0; level < kBloomLevels; ++level) {
        const UINT div = 1u << (level + 1); // 2, 4, 8, ...
        const UINT width  = std::max<UINT>(1, fullWidth  / div);
        const UINT height = std::max<UINT>(1, fullHeight / div);

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = desc.Format;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        for (uint32_t ping = 0; ping < 2; ++ping) {
            ComPtr<ID3D12Resource>& tex = (ping == 0) ? m_bloomTexA[level] : m_bloomTexB[level];

            HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                &clearValue,
                IID_PPV_ARGS(&tex)
            );

            if (FAILED(hr)) {
                return Result<void>::Err("Failed to create bloom render target");
            }

            m_bloomState[level][ping] = D3D12_RESOURCE_STATE_RENDER_TARGET;

            // RTV for this bloom target
            if (!m_bloomRTV[level][ping].IsValid()) {
                auto rtvResult = m_descriptorManager->AllocateRTV();
                if (rtvResult.IsErr()) {
                    return Result<void>::Err("Failed to allocate RTV for bloom target: " + rtvResult.Error());
                }
                m_bloomRTV[level][ping] = rtvResult.Value();
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = desc.Format;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_device->GetDevice()->CreateRenderTargetView(
                tex.Get(),
                &rtvDesc,
                m_bloomRTV[level][ping].cpu
            );

            // SRV for sampling this bloom target
            if (!m_bloomSRV[level][ping].IsValid()) {
                auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    return Result<void>::Err("Failed to allocate SRV for bloom target: " + srvResult.Error());
                }
                m_bloomSRV[level][ping] = srvResult.Value();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;

            m_device->GetDevice()->CreateShaderResourceView(
                tex.Get(),
                &srvDesc,
                m_bloomSRV[level][ping].cpu
            );
        }
    }

    // By convention, use the quarter-resolution level (level 1) A texture as the
    // final combined bloom SRV; if only one level exists, fall back to level 0.
    if (kBloomLevels > 1) {
        m_bloomCombinedSRV = m_bloomSRV[1][0];
    } else {
        m_bloomCombinedSRV = m_bloomSRV[0][0];
    }

    spdlog::info("Bloom pyramid created: base {}x{}, levels={}", fullWidth, fullHeight, kBloomLevels);
    return Result<void>::Ok();
}

void Renderer::RenderBloom() {
    // If any of the required pieces are missing, skip bloom.
    if (!m_hdrColor || !m_bloomDownsamplePipeline || !m_bloomBlurHPipeline || !m_bloomBlurVPipeline ||
        !m_bloomCompositePipeline || !m_hdrSRV.IsValid()) {
        return;
    }

    // Allow the user to disable bloom purely via intensity.
    if (m_bloomIntensity <= 0.0f) {
        return;
    }

    // Ensure we actually have textures for the pyramid.
    if (!m_bloomTexA[0] || !m_bloomTexB[0]) {
        return;
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // ---------------------------------------------------------------------
    // Pass 0: Downsample + bright-pass from HDR into bloom level 0 (1/2 res)
    // ---------------------------------------------------------------------
    {
        D3D12_RESOURCE_DESC desc = m_bloomTexA[0]->GetDesc();

        D3D12_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(desc.Width);
        viewport.Height = static_cast<float>(desc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<LONG>(desc.Width);
        scissor.bottom = static_cast<LONG>(desc.Height);

        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissor);

        D3D12_RESOURCE_BARRIER barriers[2] = {};
        UINT barrierCount = 0;

        if (m_hdrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_hdrColor.Get();
            barriers[barrierCount].Transition.StateBefore = m_hdrState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_bloomState[0][0] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_bloomTexA[0].Get();
            barriers[barrierCount].Transition.StateBefore = m_bloomState[0][0];
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_bloomState[0][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        if (barrierCount > 0) {
            m_commandList->ResourceBarrier(barrierCount, barriers);
        }

        m_commandList->OMSetRenderTargets(1, &m_bloomRTV[0][0].cpu, FALSE, nullptr);
        m_commandList->ClearRenderTargetView(m_bloomRTV[0][0].cpu, clearColor, 0, nullptr);

        m_commandList->SetPipelineState(m_bloomDownsamplePipeline->GetPipelineState());

        auto hdrHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (hdrHandleResult.IsErr()) {
            spdlog::warn("RenderBloom: failed to allocate transient HDR SRV: {}", hdrHandleResult.Error());
            return;
        }
        DescriptorHandle hdrHandle = hdrHandleResult.Value();

        m_device->GetDevice()->CopyDescriptorsSimple(
            1,
            hdrHandle.cpu,
            m_hdrSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

        // Bind g_SceneColor at t0 via root parameter 3.
        m_commandList->SetGraphicsRootDescriptorTable(3, hdrHandle.gpu);
        m_commandList->DrawInstanced(3, 1, 0, 0);
    }

    // ---------------------------------------------------------------------
    // Downsample chain: level i-1 (A) -> level i (A)
    // ---------------------------------------------------------------------
    for (uint32_t level = 1; level < kBloomLevels; ++level) {
        if (!m_bloomTexA[level] || !m_bloomTexA[level - 1]) {
            continue;
        }

        D3D12_RESOURCE_DESC desc = m_bloomTexA[level]->GetDesc();

        D3D12_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(desc.Width);
        viewport.Height = static_cast<float>(desc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<LONG>(desc.Width);
        scissor.bottom = static_cast<LONG>(desc.Height);

        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissor);

        D3D12_RESOURCE_BARRIER barriers[2] = {};
        UINT barrierCount = 0;

        if (m_bloomState[level - 1][0] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_bloomTexA[level - 1].Get();
            barriers[barrierCount].Transition.StateBefore = m_bloomState[level - 1][0];
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_bloomState[level - 1][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_bloomState[level][0] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_bloomTexA[level].Get();
            barriers[barrierCount].Transition.StateBefore = m_bloomState[level][0];
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_bloomState[level][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        if (barrierCount > 0) {
            m_commandList->ResourceBarrier(barrierCount, barriers);
        }

        m_commandList->OMSetRenderTargets(1, &m_bloomRTV[level][0].cpu, FALSE, nullptr);
        m_commandList->ClearRenderTargetView(m_bloomRTV[level][0].cpu, clearColor, 0, nullptr);

        m_commandList->SetPipelineState(m_bloomDownsamplePipeline->GetPipelineState());

        auto srcHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (srcHandleResult.IsErr()) {
            spdlog::warn("RenderBloom: failed to allocate transient bloom SRV (downsample level={}): {}", level, srcHandleResult.Error());
            return;
        }
        DescriptorHandle srcHandle = srcHandleResult.Value();

        m_device->GetDevice()->CopyDescriptorsSimple(
            1,
            srcHandle.cpu,
            m_bloomSRV[level - 1][0].cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

        m_commandList->SetGraphicsRootDescriptorTable(3, srcHandle.gpu);
        m_commandList->DrawInstanced(3, 1, 0, 0);
    }

    // ---------------------------------------------------------------------
    // Blur each level locally (A -> B -> A)
    // ---------------------------------------------------------------------
    for (uint32_t level = 0; level < kBloomLevels; ++level) {
        if (!m_bloomTexA[level] || !m_bloomTexB[level]) {
            continue;
        }

        D3D12_RESOURCE_DESC desc = m_bloomTexA[level]->GetDesc();

        D3D12_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(desc.Width);
        viewport.Height = static_cast<float>(desc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<LONG>(desc.Width);
        scissor.bottom = static_cast<LONG>(desc.Height);

        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissor);

        // Horizontal blur: A -> B
        {
            D3D12_RESOURCE_BARRIER barriers[2] = {};
            UINT barrierCount = 0;

            if (m_bloomState[level][0] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomTexA[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomState[level][0];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
                m_bloomState[level][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }

            if (m_bloomState[level][1] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomTexB[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomState[level][1];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
                m_bloomState[level][1] = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }

            if (barrierCount > 0) {
                m_commandList->ResourceBarrier(barrierCount, barriers);
            }

            m_commandList->OMSetRenderTargets(1, &m_bloomRTV[level][1].cpu, FALSE, nullptr);
            m_commandList->ClearRenderTargetView(m_bloomRTV[level][1].cpu, clearColor, 0, nullptr);

            m_commandList->SetPipelineState(m_bloomBlurHPipeline->GetPipelineState());

            auto srcHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
            if (srcHandleResult.IsErr()) {
                spdlog::warn("RenderBloom: failed to allocate transient bloom SRV (blur H level={}): {}", level, srcHandleResult.Error());
                return;
            }
            DescriptorHandle srcHandle = srcHandleResult.Value();

            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                srcHandle.cpu,
                m_bloomSRV[level][0].cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );

            m_commandList->SetGraphicsRootDescriptorTable(3, srcHandle.gpu);
            m_commandList->DrawInstanced(3, 1, 0, 0);
        }

        // Vertical blur: B -> A
        {
            D3D12_RESOURCE_BARRIER barriers[2] = {};
            UINT barrierCount = 0;

            if (m_bloomState[level][1] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomTexB[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomState[level][1];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
                m_bloomState[level][1] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }

            if (m_bloomState[level][0] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomTexA[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomState[level][0];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
                m_bloomState[level][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }

            if (barrierCount > 0) {
                m_commandList->ResourceBarrier(barrierCount, barriers);
            }

            m_commandList->OMSetRenderTargets(1, &m_bloomRTV[level][0].cpu, FALSE, nullptr);
            m_commandList->ClearRenderTargetView(m_bloomRTV[level][0].cpu, clearColor, 0, nullptr);

            m_commandList->SetPipelineState(m_bloomBlurVPipeline->GetPipelineState());

            auto srcHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
            if (srcHandleResult.IsErr()) {
                spdlog::warn("RenderBloom: failed to allocate transient bloom SRV (blur V level={}): {}", level, srcHandleResult.Error());
                return;
            }
            DescriptorHandle srcHandle = srcHandleResult.Value();

            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                srcHandle.cpu,
                m_bloomSRV[level][1].cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );

            m_commandList->SetGraphicsRootDescriptorTable(3, srcHandle.gpu);
            m_commandList->DrawInstanced(3, 1, 0, 0);

            // Final blurred result for this level resides in A.
            D3D12_RESOURCE_BARRIER finalBarrier = {};
            finalBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            finalBarrier.Transition.pResource = m_bloomTexA[level].Get();
            finalBarrier.Transition.StateBefore = m_bloomState[level][0];
            finalBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            finalBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &finalBarrier);
            m_bloomState[level][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

    // ---------------------------------------------------------------------
    // Upsample and accumulate into the quarter-res level (level 1 by default)
    // ---------------------------------------------------------------------
    const uint32_t baseLevel = (kBloomLevels > 1) ? 1u : 0u;
    if (!m_bloomTexA[baseLevel]) {
        return;
    }

    {
        D3D12_RESOURCE_DESC desc = m_bloomTexA[baseLevel]->GetDesc();

        D3D12_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(desc.Width);
        viewport.Height = static_cast<float>(desc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<LONG>(desc.Width);
        scissor.bottom = static_cast<LONG>(desc.Height);

        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissor);

        // Ensure base level is in render target state for accumulation.
        if (m_bloomState[baseLevel][0] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_bloomTexA[baseLevel].Get();
            barrier.Transition.StateBefore = m_bloomState[baseLevel][0];
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &barrier);
            m_bloomState[baseLevel][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        m_commandList->OMSetRenderTargets(1, &m_bloomRTV[baseLevel][0].cpu, FALSE, nullptr);
        m_commandList->ClearRenderTargetView(m_bloomRTV[baseLevel][0].cpu, clearColor, 0, nullptr);

        m_commandList->SetPipelineState(m_bloomCompositePipeline->GetPipelineState());

        // Accumulate from smallest to largest level into baseLevel.
        for (int level = static_cast<int>(kBloomLevels) - 1; level >= 0; --level) {
            if (!m_bloomTexA[level]) {
                continue;
            }

            auto srcHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
            if (srcHandleResult.IsErr()) {
                spdlog::warn("RenderBloom: failed to allocate transient bloom SRV (composite level={}): {}", level, srcHandleResult.Error());
                return;
            }
            DescriptorHandle srcHandle = srcHandleResult.Value();

            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                srcHandle.cpu,
                m_bloomSRV[level][0].cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );

            m_commandList->SetGraphicsRootDescriptorTable(3, srcHandle.gpu);
            m_commandList->DrawInstanced(3, 1, 0, 0);
        }

        // Final combined bloom result in base level should be in SRV state for post-process.
        D3D12_RESOURCE_BARRIER finalBarrier = {};
        finalBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        finalBarrier.Transition.pResource = m_bloomTexA[baseLevel].Get();
        finalBarrier.Transition.StateBefore = m_bloomState[baseLevel][0];
        finalBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        finalBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &finalBarrier);
        m_bloomState[baseLevel][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

} // namespace Cortex::Graphics

