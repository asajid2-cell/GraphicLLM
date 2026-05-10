#include "Renderer.h"
#include "Core/Window.h"
#include "Passes/BloomPass.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace Cortex::Graphics {

Result<void> Renderer::CreateBloomResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for bloom target creation");
    }

    UINT fullWidth  = GetInternalRenderWidth();
    UINT fullHeight = GetInternalRenderHeight();
    if (m_mainTargets.hdrColor) {
        const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdrColor->GetDesc();
        fullWidth = static_cast<UINT>(hdrDesc.Width);
        fullHeight = hdrDesc.Height;
    }

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create bloom targets");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
        fullWidth,
        fullHeight);
    m_bloomResources.activeLevels = std::clamp<uint32_t>(budget.bloomLevels, 1u, kBloomLevels);

    // Reset existing bloom resources. Descriptor slots remain valid and are
    // rewritten below, which keeps adaptive render-scale changes from leaking
    // RTV/SRV descriptors.
    for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
        m_bloomResources.texA[level].Reset();
        m_bloomResources.texB[level].Reset();
        m_bloomResources.resourceState[level][0] = D3D12_RESOURCE_STATE_COMMON;
        m_bloomResources.resourceState[level][1] = D3D12_RESOURCE_STATE_COMMON;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // Build a small bloom pyramid: level 0 = 1/2, level 1 = 1/4, level 2 = 1/8, ...
    for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
        const UINT div = 1u << (level + 1); // 2, 4, 8, ...
        const UINT width  = std::max<UINT>(1, fullWidth  / div);
        const UINT height = std::max<UINT>(1, fullHeight / div);

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        // Bloom only needs HDR RGB; R11G11B10_FLOAT cuts memory and bandwidth
        // in half compared to RGBA16F while preserving sufficient range.
        desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = desc.Format;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        for (uint32_t ping = 0; ping < 2; ++ping) {
            ComPtr<ID3D12Resource>& tex = (ping == 0) ? m_bloomResources.texA[level] : m_bloomResources.texB[level];

            HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
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

            m_bloomResources.resourceState[level][ping] = D3D12_RESOURCE_STATE_RENDER_TARGET;

            // RTV for this bloom target
            if (!m_bloomResources.rtv[level][ping].IsValid()) {
                auto rtvResult = m_services.descriptorManager->AllocateRTV();
                if (rtvResult.IsErr()) {
                    return Result<void>::Err("Failed to allocate RTV for bloom target: " + rtvResult.Error());
                }
                m_bloomResources.rtv[level][ping] = rtvResult.Value();
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = desc.Format;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_services.device->GetDevice()->CreateRenderTargetView(
                tex.Get(),
                &rtvDesc,
                m_bloomResources.rtv[level][ping].cpu
            );

            // SRV for sampling this bloom target - use staging heap (copied in post-process)
            if (!m_bloomResources.srv[level][ping].IsValid()) {
                auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    return Result<void>::Err("Failed to allocate staging SRV for bloom target: " + srvResult.Error());
                }
                m_bloomResources.srv[level][ping] = srvResult.Value();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;

            m_services.device->GetDevice()->CreateShaderResourceView(
                tex.Get(),
                &srvDesc,
                m_bloomResources.srv[level][ping].cpu
            );
        }
    }

    // By convention, use the quarter-resolution level (level 1) A texture as the
    // final combined bloom SRV; if only one level is active, fall back to level 0.
    const uint32_t combinedLevel = (m_bloomResources.activeLevels > 1) ? 1u : 0u;
    m_bloomResources.combinedSrv = m_bloomResources.srv[combinedLevel][0];

    spdlog::info("Bloom pyramid created: base {}x{}, levels={}", fullWidth, fullHeight, m_bloomResources.activeLevels);
    return Result<void>::Ok();
}

bool Renderer::PrepareBloomPassState() {
    if (!m_mainTargets.hdrColor || !m_pipelineState.bloomDownsample || !m_pipelineState.bloomBlurH || !m_pipelineState.bloomBlurV ||
        !m_pipelineState.bloomComposite || !m_mainTargets.hdrSRV.IsValid()) {
        return false;
    }

    if (m_bloomResources.intensity <= 0.0f) {
        return false;
    }

    if (!m_bloomResources.texA[0] || !m_bloomResources.texB[0]) {
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return true;
}

bool Renderer::BindBloomPassSRV(DescriptorHandle source, const char* label, uint32_t tableSlot) {
    if (!source.IsValid()) {
        spdlog::warn("RenderBloom: invalid source SRV for {}", label ? label : "pass");
        return false;
    }

    DescriptorHandle dst{};
    if (m_bloomResources.srvTableValid && tableSlot < kBloomDescriptorSlots) {
        dst = m_bloomResources.srvTables[m_frameRuntime.frameIndex % kFrameCount][tableSlot];
    } else {
        auto handleResult = m_services.descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (handleResult.IsErr()) {
            spdlog::warn("RenderBloom: failed to allocate SRV for {}: {}",
                         label ? label : "pass",
                         handleResult.Error());
            return false;
        }
        dst = handleResult.Value();
    }

    m_services.device->GetDevice()->CopyDescriptorsSimple(
        1,
        dst.cpu,
        source.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, dst.gpu);
    return true;
}

bool Renderer::BindBloomPassTexture(ID3D12Resource* source, DXGI_FORMAT format, const char* label, uint32_t tableSlot) {
    if (!source) {
        spdlog::warn("RenderBloom: invalid source texture for {}", label ? label : "pass");
        return false;
    }

    DescriptorHandle dst{};
    if (m_bloomResources.srvTableValid && tableSlot < kBloomDescriptorSlots) {
        dst = m_bloomResources.srvTables[m_frameRuntime.frameIndex % kFrameCount][tableSlot];
    } else {
        auto handleResult = m_services.descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (handleResult.IsErr()) {
            spdlog::warn("RenderBloom: failed to allocate SRV for {}: {}",
                         label ? label : "pass",
                         handleResult.Error());
            return false;
        }
        dst = handleResult.Value();
    }

    const D3D12_RESOURCE_DESC resourceDesc = source->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = (format == DXGI_FORMAT_UNKNOWN) ? resourceDesc.Format : format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(source, &srvDesc, dst.cpu);
    m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, dst.gpu);
    return true;
}

bool Renderer::RenderBloomDownsampleBase(bool skipTransitions) {
    if (!m_mainTargets.hdrColor || !m_bloomResources.texA[0]) {
        return false;
    }
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    BloomPass::SetFullscreenViewport(m_commandResources.graphicsList.Get(), m_bloomResources.texA[0].Get());

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        UINT barrierCount = 0;

        if (m_mainTargets.hdrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_mainTargets.hdrColor.Get();
            barriers[barrierCount].Transition.StateBefore = m_mainTargets.hdrState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (m_bloomResources.resourceState[0][0] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_bloomResources.texA[0].Get();
            barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[0][0];
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (barrierCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
        }
    }

    m_mainTargets.hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_bloomResources.resourceState[0][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &m_bloomResources.rtv[0][0].cpu, FALSE, nullptr);
    m_commandResources.graphicsList->ClearRenderTargetView(m_bloomResources.rtv[0][0].cpu, clearColor, 0, nullptr);

    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.bloomDownsample->GetPipelineState());

    if (!BindBloomPassTexture(m_mainTargets.hdrColor.Get(), DXGI_FORMAT_UNKNOWN, "downsample hdr", BloomPass::BaseDownsampleSlot())) {
        return false;
    }

    m_commandResources.graphicsList->DrawInstanced(3, 1, 0, 0);
    return true;
}

bool Renderer::RenderBloomDownsampleLevel(uint32_t level, bool skipTransitions) {
    if (level == 0 || level >= m_bloomResources.activeLevels || !m_bloomResources.texA[level] || !m_bloomResources.texA[level - 1]) {
        return false;
    }
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    BloomPass::SetFullscreenViewport(m_commandResources.graphicsList.Get(), m_bloomResources.texA[level].Get());

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        UINT barrierCount = 0;

        if (m_bloomResources.resourceState[level - 1][0] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_bloomResources.texA[level - 1].Get();
            barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[level - 1][0];
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (m_bloomResources.resourceState[level][0] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_bloomResources.texA[level].Get();
            barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[level][0];
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (barrierCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
        }
    }

    m_bloomResources.resourceState[level - 1][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_bloomResources.resourceState[level][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &m_bloomResources.rtv[level][0].cpu, FALSE, nullptr);
    m_commandResources.graphicsList->ClearRenderTargetView(m_bloomResources.rtv[level][0].cpu, clearColor, 0, nullptr);

    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.bloomDownsample->GetPipelineState());

    if (!BindBloomPassTexture(m_bloomResources.texA[level - 1].Get(), DXGI_FORMAT_UNKNOWN, "downsample chain",
                              BloomPass::DownsampleChainSlot(level))) {
        return false;
    }

    m_commandResources.graphicsList->DrawInstanced(3, 1, 0, 0);
    return true;
}

bool Renderer::RenderBloomBlurHorizontal(uint32_t level, bool skipTransitions) {
    if (level >= m_bloomResources.activeLevels || !m_bloomResources.texA[level] || !m_bloomResources.texB[level]) {
        return false;
    }
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    BloomPass::SetFullscreenViewport(m_commandResources.graphicsList.Get(), m_bloomResources.texA[level].Get());

    if (!skipTransitions) {
            D3D12_RESOURCE_BARRIER barriers[2] = {};
            UINT barrierCount = 0;

            if (m_bloomResources.resourceState[level][0] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomResources.texA[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[level][0];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
            }

            if (m_bloomResources.resourceState[level][1] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomResources.texB[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[level][1];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
            }

            if (barrierCount > 0) {
                m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
            }
    }

    m_bloomResources.resourceState[level][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_bloomResources.resourceState[level][1] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &m_bloomResources.rtv[level][1].cpu, FALSE, nullptr);
    m_commandResources.graphicsList->ClearRenderTargetView(m_bloomResources.rtv[level][1].cpu, clearColor, 0, nullptr);

    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.bloomBlurH->GetPipelineState());

    if (!BindBloomPassTexture(m_bloomResources.texA[level].Get(), DXGI_FORMAT_UNKNOWN, "blur horizontal",
                              BloomPass::BlurHSlot(level, kBloomLevels))) {
        return false;
    }

    m_commandResources.graphicsList->DrawInstanced(3, 1, 0, 0);
    return true;
}

bool Renderer::RenderBloomBlurVertical(uint32_t level, bool skipTransitions) {
    if (level >= m_bloomResources.activeLevels || !m_bloomResources.texA[level] || !m_bloomResources.texB[level]) {
        return false;
    }
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    BloomPass::SetFullscreenViewport(m_commandResources.graphicsList.Get(), m_bloomResources.texA[level].Get());

    if (!skipTransitions) {
            D3D12_RESOURCE_BARRIER barriers[2] = {};
            UINT barrierCount = 0;

            if (m_bloomResources.resourceState[level][1] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomResources.texB[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[level][1];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
            }

            if (m_bloomResources.resourceState[level][0] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomResources.texA[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[level][0];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
            }

            if (barrierCount > 0) {
                m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
            }
    }

    m_bloomResources.resourceState[level][1] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_bloomResources.resourceState[level][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &m_bloomResources.rtv[level][0].cpu, FALSE, nullptr);
    m_commandResources.graphicsList->ClearRenderTargetView(m_bloomResources.rtv[level][0].cpu, clearColor, 0, nullptr);

    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.bloomBlurV->GetPipelineState());

    if (!BindBloomPassTexture(m_bloomResources.texB[level].Get(), DXGI_FORMAT_UNKNOWN, "blur vertical",
                              BloomPass::BlurVSlot(level, kBloomLevels))) {
        return false;
    }

    m_commandResources.graphicsList->DrawInstanced(3, 1, 0, 0);

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER finalBarrier = {};
        finalBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        finalBarrier.Transition.pResource = m_bloomResources.texA[level].Get();
        finalBarrier.Transition.StateBefore = m_bloomResources.resourceState[level][0];
        finalBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        finalBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &finalBarrier);
        m_bloomResources.resourceState[level][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    return true;
}

bool Renderer::RenderBloomComposite(bool skipTransitions) {
    const uint32_t baseLevel = (m_bloomResources.activeLevels > 1) ? 1u : 0u;
    if (!m_bloomResources.texA[baseLevel] || !m_bloomResources.texB[baseLevel]) {
        return false;
    }
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    BloomPass::SetFullscreenViewport(m_commandResources.graphicsList.Get(), m_bloomResources.texB[baseLevel].Get());

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER barriers[kBloomLevels + 1] = {};
        UINT barrierCount = 0;

        for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
            if (m_bloomResources.texA[level] && m_bloomResources.resourceState[level][0] != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
                barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[barrierCount].Transition.pResource = m_bloomResources.texA[level].Get();
                barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[level][0];
                barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++barrierCount;
            }
        }

        if (m_bloomResources.resourceState[baseLevel][1] != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_bloomResources.texB[baseLevel].Get();
            barriers[barrierCount].Transition.StateBefore = m_bloomResources.resourceState[baseLevel][1];
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }

        if (barrierCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
        }
    }

    for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
        if (m_bloomResources.texA[level]) {
            m_bloomResources.resourceState[level][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }
    m_bloomResources.resourceState[baseLevel][1] = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &m_bloomResources.rtv[baseLevel][1].cpu, FALSE, nullptr);
    m_commandResources.graphicsList->ClearRenderTargetView(m_bloomResources.rtv[baseLevel][1].cpu, clearColor, 0, nullptr);

    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.bloomComposite->GetPipelineState());

    for (int level = static_cast<int>(m_bloomResources.activeLevels) - 1; level >= 0; --level) {
        if (!m_bloomResources.texA[level]) {
            continue;
        }

        const uint32_t compositeIndex = static_cast<uint32_t>((m_bloomResources.activeLevels - 1) - level);
        if (!BindBloomPassTexture(m_bloomResources.texA[level].Get(), DXGI_FORMAT_UNKNOWN, "composite",
                                  BloomPass::CompositeSlot(compositeIndex, kBloomLevels))) {
            return false;
        }

        m_commandResources.graphicsList->DrawInstanced(3, 1, 0, 0);
    }

    return true;
}

bool Renderer::CopyBloomCompositeToCombined(bool skipTransitions) {
    const uint32_t baseLevel = (m_bloomResources.activeLevels > 1) ? 1u : 0u;
    if (!m_bloomResources.texA[baseLevel] || !m_bloomResources.texB[baseLevel]) {
        return false;
    }

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER copyBarriers[2] = {};
        UINT copyCount = 0;

        if (m_bloomResources.resourceState[baseLevel][1] != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            copyBarriers[copyCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            copyBarriers[copyCount].Transition.pResource = m_bloomResources.texB[baseLevel].Get();
            copyBarriers[copyCount].Transition.StateBefore = m_bloomResources.resourceState[baseLevel][1];
            copyBarriers[copyCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            copyBarriers[copyCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++copyCount;
        }

        if (m_bloomResources.resourceState[baseLevel][0] != D3D12_RESOURCE_STATE_COPY_DEST) {
            copyBarriers[copyCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            copyBarriers[copyCount].Transition.pResource = m_bloomResources.texA[baseLevel].Get();
            copyBarriers[copyCount].Transition.StateBefore = m_bloomResources.resourceState[baseLevel][0];
            copyBarriers[copyCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            copyBarriers[copyCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++copyCount;
        }

        if (copyCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(copyCount, copyBarriers);
        }
    }

    m_bloomResources.resourceState[baseLevel][1] = D3D12_RESOURCE_STATE_COPY_SOURCE;
    m_bloomResources.resourceState[baseLevel][0] = D3D12_RESOURCE_STATE_COPY_DEST;
    m_commandResources.graphicsList->CopyResource(m_bloomResources.texA[baseLevel].Get(), m_bloomResources.texB[baseLevel].Get());

    if (!skipTransitions) {
        D3D12_RESOURCE_BARRIER finalBarrier = {};
        finalBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        finalBarrier.Transition.pResource = m_bloomResources.texA[baseLevel].Get();
        finalBarrier.Transition.StateBefore = m_bloomResources.resourceState[baseLevel][0];
        finalBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        finalBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &finalBarrier);
        m_bloomResources.resourceState[baseLevel][0] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    return true;
}

void Renderer::RenderBloom() {
    if (!PrepareBloomPassState()) {
        return;
    }

    if (!RenderBloomDownsampleBase(false)) {
        return;
    }
    for (uint32_t level = 1; level < m_bloomResources.activeLevels; ++level) {
        if (!RenderBloomDownsampleLevel(level, false)) {
            return;
        }
    }
    for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
        if (!RenderBloomBlurHorizontal(level, false) ||
            !RenderBloomBlurVertical(level, false)) {
            return;
        }
    }
    if (!RenderBloomComposite(false)) {
        return;
    }
    (void)CopyBloomCompositeToCombined(false);
}

} // namespace Cortex::Graphics

