#include "Renderer.h"
#include "Core/Window.h"
#include "Passes/BloomPass.h"
#include "Passes/FullscreenPass.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace Cortex::Graphics {

Result<void> Renderer::CreateBloomResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for bloom target creation");
    }

    UINT fullWidth  = GetInternalRenderWidth();
    UINT fullHeight = GetInternalRenderHeight();
    if (m_mainTargets.hdr.resources.color) {
        const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
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
    auto result = m_bloomResources.resources.CreateTargets(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        fullWidth,
        fullHeight,
        budget.bloomLevels);
    if (result.IsErr()) {
        return result;
    }

    spdlog::info("Bloom pyramid created: base {}x{}, levels={}", fullWidth, fullHeight, m_bloomResources.resources.activeLevels);
    return Result<void>::Ok();
}

bool Renderer::PrepareBloomPassState() {
    if (!m_mainTargets.hdr.resources.color || !m_pipelineState.bloomDownsample || !m_pipelineState.bloomBlurH || !m_pipelineState.bloomBlurV ||
        !m_pipelineState.bloomComposite || !m_mainTargets.hdr.descriptors.srv.IsValid()) {
        return false;
    }

    if (m_bloomResources.controls.intensity <= 0.0f) {
        return false;
    }

    if (!m_bloomResources.resources.texA[0] || !m_bloomResources.resources.texB[0]) {
        return false;
    }

    BloomPass::FullscreenContext context{};
    context.commandList = m_commandResources.graphicsList.Get();
    context.descriptorManager = m_services.descriptorManager.get();
    context.rootSignature = m_pipelineState.rootSignature.get();
    context.frameConstants = m_constantBuffers.currentFrameGPU;
    return BloomPass::PrepareFullscreenState(context);
}

bool Renderer::BindBloomPassSRV(DescriptorHandle source, const char* label, uint32_t tableSlot) {
    if (!source.IsValid()) {
        spdlog::warn("RenderBloom: invalid source SRV for {}", label ? label : "pass");
        return false;
    }

    BloomPass::FullscreenContext context{};
    context.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    context.commandList = m_commandResources.graphicsList.Get();
    context.descriptorManager = m_services.descriptorManager.get();
    context.srvTable = m_bloomResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount].data();
    context.srvTableCount = kBloomDescriptorSlots;
    context.srvTableValid = m_bloomResources.descriptors.srvTableValid;
    return BloomPass::BindSrvDescriptor(context, source, label, tableSlot);
}

bool Renderer::BindBloomPassTexture(ID3D12Resource* source, DXGI_FORMAT format, const char* label, uint32_t tableSlot) {
    if (!source) {
        spdlog::warn("RenderBloom: invalid source texture for {}", label ? label : "pass");
        return false;
    }

    BloomPass::FullscreenContext context{};
    context.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    context.commandList = m_commandResources.graphicsList.Get();
    context.descriptorManager = m_services.descriptorManager.get();
    context.srvTable = m_bloomResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount].data();
    context.srvTableCount = kBloomDescriptorSlots;
    context.srvTableValid = m_bloomResources.descriptors.srvTableValid;
    return BloomPass::BindTexture(context, source, format, label, tableSlot);
}

bool Renderer::RenderBloomDownsampleBase(bool skipTransitions) {
    if (!m_mainTargets.hdr.resources.color || !m_bloomResources.resources.texA[0]) {
        return false;
    }
    if (!BloomPass::PrepareSourceToRenderTarget({
            m_commandResources.graphicsList.Get(),
            {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state},
            {m_bloomResources.resources.texA[0].Get(), &m_bloomResources.resources.resourceState[0][0]},
            skipTransitions,
        })) {
        return false;
    }

    if (!BloomPass::BindAndClearTarget({m_commandResources.graphicsList.Get(), m_bloomResources.resources.texA[0].Get(), m_bloomResources.resources.rtv[0][0]})) {
        return false;
    }

    if (!BloomPass::BindPipelineState(m_commandResources.graphicsList.Get(), m_pipelineState.bloomDownsample.get())) {
        return false;
    }

    if (!BindBloomPassTexture(m_mainTargets.hdr.resources.color.Get(), DXGI_FORMAT_UNKNOWN, "downsample hdr", BloomPass::BaseDownsampleSlot())) {
        return false;
    }

    FullscreenPass::DrawTriangle(m_commandResources.graphicsList.Get());
    return true;
}

bool Renderer::RenderBloomDownsampleLevel(uint32_t level, bool skipTransitions) {
    if (level == 0 || level >= m_bloomResources.resources.activeLevels || !m_bloomResources.resources.texA[level] || !m_bloomResources.resources.texA[level - 1]) {
        return false;
    }
    if (!BloomPass::PrepareSourceToRenderTarget({
            m_commandResources.graphicsList.Get(),
            {m_bloomResources.resources.texA[level - 1].Get(), &m_bloomResources.resources.resourceState[level - 1][0]},
            {m_bloomResources.resources.texA[level].Get(), &m_bloomResources.resources.resourceState[level][0]},
            skipTransitions,
        })) {
        return false;
    }

    if (!BloomPass::BindAndClearTarget({m_commandResources.graphicsList.Get(), m_bloomResources.resources.texA[level].Get(), m_bloomResources.resources.rtv[level][0]})) {
        return false;
    }

    if (!BloomPass::BindPipelineState(m_commandResources.graphicsList.Get(), m_pipelineState.bloomDownsample.get())) {
        return false;
    }

    if (!BindBloomPassTexture(m_bloomResources.resources.texA[level - 1].Get(), DXGI_FORMAT_UNKNOWN, "downsample chain",
                              BloomPass::DownsampleChainSlot(level))) {
        return false;
    }

    FullscreenPass::DrawTriangle(m_commandResources.graphicsList.Get());
    return true;
}

bool Renderer::RenderBloomBlurHorizontal(uint32_t level, bool skipTransitions) {
    if (level >= m_bloomResources.resources.activeLevels || !m_bloomResources.resources.texA[level] || !m_bloomResources.resources.texB[level]) {
        return false;
    }
    if (!BloomPass::PrepareSourceToRenderTarget({
            m_commandResources.graphicsList.Get(),
            {m_bloomResources.resources.texA[level].Get(), &m_bloomResources.resources.resourceState[level][0]},
            {m_bloomResources.resources.texB[level].Get(), &m_bloomResources.resources.resourceState[level][1]},
            skipTransitions,
        })) {
        return false;
    }

    if (!BloomPass::BindAndClearTarget({m_commandResources.graphicsList.Get(), m_bloomResources.resources.texB[level].Get(), m_bloomResources.resources.rtv[level][1]})) {
        return false;
    }

    if (!BloomPass::BindPipelineState(m_commandResources.graphicsList.Get(), m_pipelineState.bloomBlurH.get())) {
        return false;
    }

    if (!BindBloomPassTexture(m_bloomResources.resources.texA[level].Get(), DXGI_FORMAT_UNKNOWN, "blur horizontal",
                              BloomPass::BlurHSlot(level, kBloomLevels))) {
        return false;
    }

    FullscreenPass::DrawTriangle(m_commandResources.graphicsList.Get());
    return true;
}

bool Renderer::RenderBloomBlurVertical(uint32_t level, bool skipTransitions) {
    if (level >= m_bloomResources.resources.activeLevels || !m_bloomResources.resources.texA[level] || !m_bloomResources.resources.texB[level]) {
        return false;
    }
    if (!BloomPass::PrepareSourceToRenderTarget({
            m_commandResources.graphicsList.Get(),
            {m_bloomResources.resources.texB[level].Get(), &m_bloomResources.resources.resourceState[level][1]},
            {m_bloomResources.resources.texA[level].Get(), &m_bloomResources.resources.resourceState[level][0]},
            skipTransitions,
        })) {
        return false;
    }

    if (!BloomPass::BindAndClearTarget({m_commandResources.graphicsList.Get(), m_bloomResources.resources.texA[level].Get(), m_bloomResources.resources.rtv[level][0]})) {
        return false;
    }

    if (!BloomPass::BindPipelineState(m_commandResources.graphicsList.Get(), m_pipelineState.bloomBlurV.get())) {
        return false;
    }

    if (!BindBloomPassTexture(m_bloomResources.resources.texB[level].Get(), DXGI_FORMAT_UNKNOWN, "blur vertical",
                              BloomPass::BlurVSlot(level, kBloomLevels))) {
        return false;
    }

    FullscreenPass::DrawTriangle(m_commandResources.graphicsList.Get());

    if (!BloomPass::TransitionToShaderResource(m_commandResources.graphicsList.Get(),
                                               {m_bloomResources.resources.texA[level].Get(), &m_bloomResources.resources.resourceState[level][0]},
                                               skipTransitions)) {
        return false;
    }

    return true;
}

bool Renderer::RenderBloomComposite(bool skipTransitions) {
    const uint32_t baseLevel = (m_bloomResources.resources.activeLevels > 1) ? 1u : 0u;
    if (!m_bloomResources.resources.texA[baseLevel] || !m_bloomResources.resources.texB[baseLevel]) {
        return false;
    }

    BloomPass::ResourceStateRef sourceRefs[kBloomLevels] = {};
    for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
        sourceRefs[level] = {m_bloomResources.resources.texA[level].Get(), &m_bloomResources.resources.resourceState[level][0]};
    }
    if (!BloomPass::PrepareCompositeTargets({
            m_commandResources.graphicsList.Get(),
            std::span<BloomPass::ResourceStateRef>(sourceRefs, m_bloomResources.resources.activeLevels),
            {m_bloomResources.resources.texB[baseLevel].Get(), &m_bloomResources.resources.resourceState[baseLevel][1]},
            skipTransitions,
        })) {
        return false;
    }

    if (!BloomPass::BindAndClearTarget({m_commandResources.graphicsList.Get(), m_bloomResources.resources.texB[baseLevel].Get(), m_bloomResources.resources.rtv[baseLevel][1]})) {
        return false;
    }

    if (!BloomPass::BindPipelineState(m_commandResources.graphicsList.Get(), m_pipelineState.bloomComposite.get())) {
        return false;
    }

    for (int level = static_cast<int>(m_bloomResources.resources.activeLevels) - 1; level >= 0; --level) {
        if (!m_bloomResources.resources.texA[level]) {
            continue;
        }

        const uint32_t compositeIndex = static_cast<uint32_t>((m_bloomResources.resources.activeLevels - 1) - level);
        if (!BindBloomPassTexture(m_bloomResources.resources.texA[level].Get(), DXGI_FORMAT_UNKNOWN, "composite",
                                  BloomPass::CompositeSlot(compositeIndex, kBloomLevels))) {
            return false;
        }

        FullscreenPass::DrawTriangle(m_commandResources.graphicsList.Get());
    }

    return true;
}

bool Renderer::CopyBloomCompositeToCombined(bool skipTransitions) {
    const uint32_t baseLevel = (m_bloomResources.resources.activeLevels > 1) ? 1u : 0u;
    if (!m_bloomResources.resources.texA[baseLevel] || !m_bloomResources.resources.texB[baseLevel]) {
        return false;
    }

    return BloomPass::CopyCompositeToCombined({
        m_commandResources.graphicsList.Get(),
        {m_bloomResources.resources.texB[baseLevel].Get(), &m_bloomResources.resources.resourceState[baseLevel][1]},
        {m_bloomResources.resources.texA[baseLevel].Get(), &m_bloomResources.resources.resourceState[baseLevel][0]},
        skipTransitions,
    });
}

void Renderer::RenderBloom() {
    if (!PrepareBloomPassState()) {
        return;
    }

    if (!RenderBloomDownsampleBase(false)) {
        return;
    }
    for (uint32_t level = 1; level < m_bloomResources.resources.activeLevels; ++level) {
        if (!RenderBloomDownsampleLevel(level, false)) {
            return;
        }
    }
    for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
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

