#include "Graphics/Subsystems/SSRSubsystem.h"

#include "Graphics/Passes/SSRPass.h"
#include "Graphics/RendererGeometryUtils.h" // kDepthSampleState

#include <spdlog/spdlog.h>

#include <span>

namespace Cortex::Graphics {

void SSRSubsystem::RenderImmediate(const SSRRenderContext& ctx) {
    if (!ctx.ssrPipeline || !m_state.resources.color || !ctx.hdrColor || !ctx.depthBuffer) {
        return;
    }

    ID3D12Resource* normalResource = ctx.normalRoughness;
    if (ctx.vbRenderedThisFrame && ctx.vbNormalRoughness) {
        normalResource = ctx.vbNormalRoughness;
    }
    if (!normalResource) {
        return;
    }

    SSRPass::PrepareContext prepareContext{};
    prepareContext.commandList = ctx.commandList;
    prepareContext.skipTransitions = ctx.skipTransitions;
    prepareContext.ssrTarget = {m_state.resources.color.Get(), &m_state.resources.resourceState, D3D12_RESOURCE_STATE_RENDER_TARGET};
    prepareContext.hdr = {ctx.hdrColor, ctx.hdrState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    prepareContext.normalRoughness = {
        ctx.vbRenderedThisFrame ? nullptr : ctx.normalRoughness,
        ctx.vbRenderedThisFrame ? nullptr : ctx.normalRoughnessState,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    prepareContext.depth = {ctx.depthBuffer, ctx.depthState, kDepthSampleState};
    if (!SSRPass::PrepareTargets(prepareContext)) {
        spdlog::error("RenderSSR: target transition failed");
        return;
    }

    if (!m_state.descriptors.srvTableValid) {
        spdlog::error("RenderSSR: persistent SRV table is invalid");
        return;
    }

    auto& persistentTable = m_state.descriptors.srvTables[ctx.frameIndex % kFrameCount];
    if (!SSRPass::Draw({
            ctx.device,
            ctx.commandList,
            ctx.descriptorManager,
            ctx.rootSignature,
            ctx.frameConstants,
            ctx.ssrPipeline,
            m_state.resources.color.Get(),
            m_state.resources.rtv,
            ctx.hdrColor,
            ctx.depthBuffer,
            normalResource,
            std::span<DescriptorHandle>(persistentTable.data(), persistentTable.size()),
            ctx.shadowAndEnvDescriptor,
        })) {
        spdlog::error("RenderSSR: pass execution failed");
    }
}

} // namespace Cortex::Graphics
