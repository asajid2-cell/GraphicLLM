#include "Graphics/Subsystems/BloomSubsystem.h"

#include "Graphics/Passes/BloomPass.h"
#include "Graphics/Passes/FullscreenPass.h"
#include "Graphics/BudgetPlanner.h"
#include "Graphics/RHI/DX12Device.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <span>

namespace Cortex::Graphics {

Result<void> BloomSubsystem::CreateResources(const BloomContext& ctx) {
    if (!ctx.device || !ctx.descriptorManager) {
        return Result<void>::Err("Renderer not initialized for bloom target creation");
    }

    UINT fullWidth = ctx.internalWidth;
    UINT fullHeight = ctx.internalHeight;
    if (ctx.hdrColor) {
        const D3D12_RESOURCE_DESC hdrDesc = ctx.hdrColor->GetDesc();
        fullWidth = static_cast<UINT>(hdrDesc.Width);
        fullHeight = hdrDesc.Height;
    }

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create bloom targets");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        ctx.device->GetDedicatedVideoMemoryBytes(), fullWidth, fullHeight);
    auto result = m_state.resources.CreateTargets(
        ctx.device->GetDevice(), ctx.descriptorManager, fullWidth, fullHeight, budget.bloomLevels);
    if (result.IsErr()) {
        return result;
    }

    spdlog::info("Bloom pyramid created: base {}x{}, levels={}", fullWidth, fullHeight, m_state.resources.activeLevels);
    return Result<void>::Ok();
}

bool BloomSubsystem::PrepareState() {
    if (!m_ctx->hdrColor || !m_ctx->downsample || !m_ctx->blurH || !m_ctx->blurV ||
        !m_ctx->composite || !m_ctx->hdrSrvValid) {
        return false;
    }
    if (m_state.controls.intensity <= 0.0f) {
        return false;
    }
    if (!m_state.resources.texA[0] || !m_state.resources.texB[0]) {
        return false;
    }

    BloomPass::FullscreenContext context{};
    context.commandList = m_ctx->commandList;
    context.descriptorManager = m_ctx->descriptorManager;
    context.rootSignature = m_ctx->rootSignature;
    context.frameConstants = m_ctx->frameConstants;
    return BloomPass::PrepareFullscreenState(context);
}

bool BloomSubsystem::BindSRV(DescriptorHandle source, const char* label, uint32_t tableSlot) {
    if (!source.IsValid()) {
        spdlog::warn("RenderBloom: invalid source SRV for {}", label ? label : "pass");
        return false;
    }
    BloomPass::FullscreenContext context{};
    context.device = m_ctx->device ? m_ctx->device->GetDevice() : nullptr;
    context.commandList = m_ctx->commandList;
    context.descriptorManager = m_ctx->descriptorManager;
    context.srvTable = m_state.descriptors.srvTables[m_ctx->frameIndex % kFrameCount].data();
    context.srvTableCount = kBloomDescriptorSlots;
    context.srvTableValid = m_state.descriptors.srvTableValid;
    return BloomPass::BindSrvDescriptor(context, source, label, tableSlot);
}

bool BloomSubsystem::BindTexture(ID3D12Resource* source, DXGI_FORMAT format, const char* label, uint32_t tableSlot) {
    if (!source) {
        spdlog::warn("RenderBloom: invalid source texture for {}", label ? label : "pass");
        return false;
    }
    BloomPass::FullscreenContext context{};
    context.device = m_ctx->device ? m_ctx->device->GetDevice() : nullptr;
    context.commandList = m_ctx->commandList;
    context.descriptorManager = m_ctx->descriptorManager;
    context.srvTable = m_state.descriptors.srvTables[m_ctx->frameIndex % kFrameCount].data();
    context.srvTableCount = kBloomDescriptorSlots;
    context.srvTableValid = m_state.descriptors.srvTableValid;
    return BloomPass::BindTexture(context, source, format, label, tableSlot);
}

bool BloomSubsystem::DownsampleBase(bool skipTransitions) {
    if (!m_ctx->hdrColor || !m_state.resources.texA[0]) {
        return false;
    }
    if (!BloomPass::PrepareSourceToRenderTarget({
            m_ctx->commandList,
            {m_ctx->hdrColor, m_ctx->hdrState},
            {m_state.resources.texA[0].Get(), &m_state.resources.resourceState[0][0]},
            skipTransitions,
        })) {
        return false;
    }
    if (!BloomPass::BindAndClearTarget({m_ctx->commandList, m_state.resources.texA[0].Get(), m_state.resources.rtv[0][0]})) {
        return false;
    }
    if (!BloomPass::BindPipelineState(m_ctx->commandList, m_ctx->downsample)) {
        return false;
    }
    if (!BindTexture(m_ctx->hdrColor, DXGI_FORMAT_UNKNOWN, "downsample hdr", BloomPass::BaseDownsampleSlot())) {
        return false;
    }
    FullscreenPass::DrawTriangle(m_ctx->commandList);
    return true;
}

bool BloomSubsystem::DownsampleLevel(uint32_t level, bool skipTransitions) {
    if (level == 0 || level >= m_state.resources.activeLevels || !m_state.resources.texA[level] || !m_state.resources.texA[level - 1]) {
        return false;
    }
    if (!BloomPass::PrepareSourceToRenderTarget({
            m_ctx->commandList,
            {m_state.resources.texA[level - 1].Get(), &m_state.resources.resourceState[level - 1][0]},
            {m_state.resources.texA[level].Get(), &m_state.resources.resourceState[level][0]},
            skipTransitions,
        })) {
        return false;
    }
    if (!BloomPass::BindAndClearTarget({m_ctx->commandList, m_state.resources.texA[level].Get(), m_state.resources.rtv[level][0]})) {
        return false;
    }
    if (!BloomPass::BindPipelineState(m_ctx->commandList, m_ctx->downsample)) {
        return false;
    }
    if (!BindTexture(m_state.resources.texA[level - 1].Get(), DXGI_FORMAT_UNKNOWN, "downsample chain",
                     BloomPass::DownsampleChainSlot(level))) {
        return false;
    }
    FullscreenPass::DrawTriangle(m_ctx->commandList);
    return true;
}

bool BloomSubsystem::BlurHorizontal(uint32_t level, bool skipTransitions) {
    if (level >= m_state.resources.activeLevels || !m_state.resources.texA[level] || !m_state.resources.texB[level]) {
        return false;
    }
    if (!BloomPass::PrepareSourceToRenderTarget({
            m_ctx->commandList,
            {m_state.resources.texA[level].Get(), &m_state.resources.resourceState[level][0]},
            {m_state.resources.texB[level].Get(), &m_state.resources.resourceState[level][1]},
            skipTransitions,
        })) {
        return false;
    }
    if (!BloomPass::BindAndClearTarget({m_ctx->commandList, m_state.resources.texB[level].Get(), m_state.resources.rtv[level][1]})) {
        return false;
    }
    if (!BloomPass::BindPipelineState(m_ctx->commandList, m_ctx->blurH)) {
        return false;
    }
    if (!BindTexture(m_state.resources.texA[level].Get(), DXGI_FORMAT_UNKNOWN, "blur horizontal",
                     BloomPass::BlurHSlot(level, kBloomLevels))) {
        return false;
    }
    FullscreenPass::DrawTriangle(m_ctx->commandList);
    return true;
}

bool BloomSubsystem::BlurVertical(uint32_t level, bool skipTransitions) {
    if (level >= m_state.resources.activeLevels || !m_state.resources.texA[level] || !m_state.resources.texB[level]) {
        return false;
    }
    if (!BloomPass::PrepareSourceToRenderTarget({
            m_ctx->commandList,
            {m_state.resources.texB[level].Get(), &m_state.resources.resourceState[level][1]},
            {m_state.resources.texA[level].Get(), &m_state.resources.resourceState[level][0]},
            skipTransitions,
        })) {
        return false;
    }
    if (!BloomPass::BindAndClearTarget({m_ctx->commandList, m_state.resources.texA[level].Get(), m_state.resources.rtv[level][0]})) {
        return false;
    }
    if (!BloomPass::BindPipelineState(m_ctx->commandList, m_ctx->blurV)) {
        return false;
    }
    if (!BindTexture(m_state.resources.texB[level].Get(), DXGI_FORMAT_UNKNOWN, "blur vertical",
                     BloomPass::BlurVSlot(level, kBloomLevels))) {
        return false;
    }
    FullscreenPass::DrawTriangle(m_ctx->commandList);

    if (!BloomPass::TransitionToShaderResource(m_ctx->commandList,
                                               {m_state.resources.texA[level].Get(), &m_state.resources.resourceState[level][0]},
                                               skipTransitions)) {
        return false;
    }
    return true;
}

bool BloomSubsystem::Composite(bool skipTransitions) {
    const uint32_t baseLevel = (m_state.resources.activeLevels > 1) ? 1u : 0u;
    if (!m_state.resources.texA[baseLevel] || !m_state.resources.texB[baseLevel]) {
        return false;
    }

    BloomPass::ResourceStateRef sourceRefs[kBloomLevels] = {};
    for (uint32_t level = 0; level < m_state.resources.activeLevels; ++level) {
        sourceRefs[level] = {m_state.resources.texA[level].Get(), &m_state.resources.resourceState[level][0]};
    }
    if (!BloomPass::PrepareCompositeTargets({
            m_ctx->commandList,
            std::span<BloomPass::ResourceStateRef>(sourceRefs, m_state.resources.activeLevels),
            {m_state.resources.texB[baseLevel].Get(), &m_state.resources.resourceState[baseLevel][1]},
            skipTransitions,
        })) {
        return false;
    }
    if (!BloomPass::BindAndClearTarget({m_ctx->commandList, m_state.resources.texB[baseLevel].Get(), m_state.resources.rtv[baseLevel][1]})) {
        return false;
    }
    if (!BloomPass::BindPipelineState(m_ctx->commandList, m_ctx->composite)) {
        return false;
    }

    for (int level = static_cast<int>(m_state.resources.activeLevels) - 1; level >= 0; --level) {
        if (!m_state.resources.texA[level]) {
            continue;
        }
        const uint32_t compositeIndex = static_cast<uint32_t>((m_state.resources.activeLevels - 1) - level);
        if (!BindTexture(m_state.resources.texA[level].Get(), DXGI_FORMAT_UNKNOWN, "composite",
                         BloomPass::CompositeSlot(compositeIndex, kBloomLevels))) {
            return false;
        }
        FullscreenPass::DrawTriangle(m_ctx->commandList);
    }
    return true;
}

bool BloomSubsystem::CopyCompositeToCombined(bool skipTransitions) {
    const uint32_t baseLevel = (m_state.resources.activeLevels > 1) ? 1u : 0u;
    if (!m_state.resources.texA[baseLevel] || !m_state.resources.texB[baseLevel]) {
        return false;
    }
    return BloomPass::CopyCompositeToCombined({
        m_ctx->commandList,
        {m_state.resources.texB[baseLevel].Get(), &m_state.resources.resourceState[baseLevel][1]},
        {m_state.resources.texA[baseLevel].Get(), &m_state.resources.resourceState[baseLevel][0]},
        skipTransitions,
    });
}

void BloomSubsystem::Render(const BloomContext& ctx) {
    m_ctx = &ctx;

    if (!PrepareState()) {
        return;
    }
    if (!DownsampleBase(false)) {
        return;
    }
    for (uint32_t level = 1; level < m_state.resources.activeLevels; ++level) {
        if (!DownsampleLevel(level, false)) {
            return;
        }
    }
    for (uint32_t level = 0; level < m_state.resources.activeLevels; ++level) {
        if (!BlurHorizontal(level, false) || !BlurVertical(level, false)) {
            return;
        }
    }
    if (!Composite(false)) {
        return;
    }
    (void)CopyCompositeToCombined(false);
}

} // namespace Cortex::Graphics
