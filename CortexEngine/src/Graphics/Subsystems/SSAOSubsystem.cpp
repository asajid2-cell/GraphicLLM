#include "Graphics/Subsystems/SSAOSubsystem.h"

#include "Graphics/Passes/SSAOPass.h"
#include "Graphics/RendererGeometryUtils.h" // kDepthSampleState

#include <spdlog/spdlog.h>

#include <span>

namespace Cortex::Graphics {

Result<void> SSAOSubsystem::CreateTarget(ID3D12Device* device,
                                         DescriptorHeapManager* descriptorManager,
                                         uint32_t width,
                                         uint32_t height) {
    auto result = m_state.resources.CreateTarget(device, descriptorManager, width, height);
    if (result.IsErr()) {
        return result;
    }
    spdlog::info("SSAO target created: {}x{}", width, height);
    return Result<void>::Ok();
}

void SSAOSubsystem::RenderImmediate(const SSAORenderContext& ctx) {
    if (!m_state.controls.enabled || !ctx.ssaoPipeline || !m_state.resources.texture ||
        !ctx.depthBuffer || !ctx.depthSrvValid) {
        return;
    }

    SSAOPass::PrepareContext prepareContext{};
    prepareContext.commandList = ctx.commandList;
    prepareContext.skipTransitions = ctx.skipTransitions;
    prepareContext.depth = {ctx.depthBuffer, ctx.depthState, kDepthSampleState};
    prepareContext.target = {m_state.resources.texture.Get(), &m_state.resources.resourceState, D3D12_RESOURCE_STATE_RENDER_TARGET};
    if (!SSAOPass::PrepareGraphicsTargets(prepareContext)) {
        spdlog::warn("RenderSSAO: target transition failed");
        return;
    }

    if (!m_state.descriptors.descriptorTablesValid) {
        spdlog::warn("RenderSSAO: persistent SSAO descriptor tables are unavailable");
        return;
    }
    auto& depthTable = m_state.descriptors.srvTables[ctx.frameIndex % kFrameCount];

    if (!SSAOPass::DrawGraphics({
            ctx.device,
            ctx.commandList,
            ctx.descriptorManager,
            ctx.graphicsRootSignature,
            ctx.frameConstants,
            ctx.ssaoPipeline,
            m_state.resources.texture.Get(),
            m_state.resources.rtv,
            ctx.depthBuffer,
            std::span<DescriptorHandle>(depthTable.data(), depthTable.size()),
        })) {
        spdlog::warn("RenderSSAO: pass execution failed");
    }
}

void SSAOSubsystem::RenderAsync(const SSAORenderContext& ctx) {
    if (!m_state.controls.enabled || !ctx.ssaoComputePipeline || !m_state.resources.texture ||
        !ctx.depthBuffer || !ctx.depthSrvValid || !m_state.resources.uav.IsValid()) {
        return;
    }

    SSAOPass::PrepareContext prepareContext{};
    prepareContext.commandList = ctx.commandList;
    prepareContext.skipTransitions = ctx.skipTransitions;
    prepareContext.depth = {ctx.depthBuffer, ctx.depthState, kDepthSampleState};
    prepareContext.target = {m_state.resources.texture.Get(), &m_state.resources.resourceState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    if (!SSAOPass::PrepareComputeTargets(prepareContext)) {
        spdlog::warn("RenderSSAOAsync: target transition failed");
        return;
    }

    const bool compactRoot = ctx.compactComputeRootSignature != nullptr;
    ID3D12RootSignature* ssaoRootSignature =
        compactRoot ? ctx.compactComputeRootSignature
                    : (ctx.computeRootSignature ? ctx.computeRootSignature->GetRootSignature() : nullptr);
    const UINT frameConstantsRoot = compactRoot ? 0u : 1u;
    const UINT srvTableRoot = compactRoot ? 1u : 3u;
    const UINT uavTableRoot = compactRoot ? 2u : 6u;
    const uint32_t srvTableSize = compactRoot ? 1u : 10u;
    const uint32_t uavTableSize = compactRoot ? 1u : 4u;

    if (!m_state.descriptors.descriptorTablesValid) {
        spdlog::warn("RenderSSAOAsync: persistent SSAO descriptor tables are unavailable");
        return;
    }
    auto& depthTable = m_state.descriptors.srvTables[ctx.frameIndex % kFrameCount];
    auto& uavTable = m_state.descriptors.uavTables[ctx.frameIndex % kFrameCount];

    if (!SSAOPass::DispatchCompute({
            ctx.device,
            ctx.commandList,
            ctx.descriptorManager,
            ssaoRootSignature,
            ctx.frameConstants,
            ctx.ssaoComputePipeline,
            frameConstantsRoot,
            srvTableRoot,
            uavTableRoot,
            m_state.resources.texture.Get(),
            ctx.depthBuffer,
            std::span<DescriptorHandle>(depthTable.data(), srvTableSize),
            std::span<DescriptorHandle>(uavTable.data(), uavTableSize),
        })) {
        spdlog::warn("RenderSSAOAsync: pass dispatch failed");
        return;
    }

    if (!SSAOPass::FinishComputeTarget(prepareContext)) {
        spdlog::warn("RenderSSAOAsync: target final transition failed");
    }
}

} // namespace Cortex::Graphics
