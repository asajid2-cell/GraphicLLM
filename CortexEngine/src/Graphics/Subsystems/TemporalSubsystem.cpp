#include "Graphics/Subsystems/TemporalSubsystem.h"

#include "Graphics/Passes/TAAPass.h"
#include "Graphics/Passes/TAACopyPass.h"
#include "Graphics/Passes/MotionVectorPass.h"
#include "Graphics/Passes/MotionVectorTargetPass.h"
#include "Graphics/RendererGeometryUtils.h" // kDepthSampleState
#include "Graphics/RHI/DX12Device.h"
#include "Graphics/VisibilityBuffer.h"
#include "Graphics/RendererVisibilityBufferState.h"

#include <spdlog/spdlog.h>

#include <span>

namespace Cortex::Graphics {

void TemporalSubsystem::InvalidateTAAHistory(TemporalManager& mgr, uint64_t frameCounter, const char* reason) {
    mgr.Invalidate(TemporalHistoryId::TAAColor, reason, frameCounter);
}

void TemporalSubsystem::MarkTAAHistoryValid(TemporalManager& mgr, uint64_t frameCounter) {
    TemporalMarkValidDesc desc{};
    desc.rejectionMode = "taa_resolve";
    desc.accumulationAlpha = m_aa.blendFactor;
    desc.usedVelocityReprojection = true;
    desc.usedDisocclusionRejection = true;
    mgr.MarkValid(TemporalHistoryId::TAAColor, frameCounter, desc);
}

void TemporalSubsystem::RenderMotionVectors(const TemporalContext& ctx) {
    m_ctx = &ctx;
    if (!m_screen.velocityBuffer) {
        return;
    }

    if (ctx.vbState && ctx.vbState->enabled && ctx.visibilityBuffer &&
        !ctx.vbState->meshDraws.empty() && !ctx.vbState->instances.empty()) {
        MotionVectorTargetPass::VelocityUAVContext uavContext{};
        uavContext.commandList = ctx.commandList;
        uavContext.velocity = {m_screen.velocityBuffer.Get(), &m_screen.velocityState};
        if (!MotionVectorTargetPass::TransitionVelocityToUnorderedAccess(uavContext)) {
            spdlog::warn("VB motion vectors failed; falling back to camera-only: velocity target transition failed");
        } else {
            auto mvResult = ctx.visibilityBuffer->ComputeMotionVectors(
                ctx.commandList,
                m_screen.velocityBuffer.Get(),
                ctx.vbState->meshDraws,
                ctx.frameConstants);
            if (mvResult.IsErr()) {
                spdlog::warn("VB motion vectors failed; falling back to camera-only: {}", mvResult.Error());
            } else {
                return;
            }
        }
    }

    if (!ctx.motionVectorsPipeline || !ctx.depthBuffer) {
        return;
    }

    MotionVectorTargetPass::CameraTargetContext cameraTargetContext{};
    cameraTargetContext.commandList = ctx.commandList;
    cameraTargetContext.velocity = {m_screen.velocityBuffer.Get(), &m_screen.velocityState};
    cameraTargetContext.depth = {ctx.depthBuffer, ctx.depthState};
    cameraTargetContext.depthSampleState = kDepthSampleState;
    if (!MotionVectorTargetPass::TransitionCameraTargets(cameraTargetContext)) {
        spdlog::error("RenderMotionVectors: target transition failed");
        return;
    }

    if (!m_screen.motionVectorSrvTableValid) {
        spdlog::error("RenderMotionVectors: persistent SRV table is invalid");
        return;
    }
    auto& persistentTable = m_screen.motionVectorSrvTables[ctx.frameIndex % kFrameCount];

    if (!MotionVectorPass::Draw({
            ctx.device->GetDevice(),
            ctx.commandList,
            ctx.descriptorManager,
            ctx.rootSignature,
            ctx.frameConstants,
            ctx.motionVectorsPipeline,
            m_screen.velocityBuffer.Get(),
            m_screen.velocityRTV,
            ctx.depthBuffer,
            std::span<DescriptorHandle>(persistentTable.data(), persistentTable.size()),
        })) {
        spdlog::error("RenderMotionVectors: pass draw failed");
        return;
    }

    m_screen.velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

bool TemporalSubsystem::SeedTAAHistory(bool skipTransitions) {
    if (!m_ctx->hdrColor || !m_screen.historyColor) {
        return false;
    }

    TAACopyPass::HistoryCopyContext copyContext{};
    copyContext.commandList = m_ctx->commandList;
    copyContext.hdrColor = {m_ctx->hdrColor, m_ctx->hdrState};
    copyContext.historyColor = {m_screen.historyColor.Get(), &m_screen.historyState};
    copyContext.skipTransitions = skipTransitions;
    copyContext.returnHdrAndHistoryToShaderResource = !skipTransitions;
    if (!TAACopyPass::CopyHdrToHistory(copyContext)) {
        return false;
    }
    MarkTAAHistoryValid(*m_ctx->historyManager, m_ctx->renderFrameCounter);
    return true;
}

bool TemporalSubsystem::ResolveTAAIntermediate(bool skipTransitions) {
    if (!m_aa.enabled || !m_ctx->taaPipeline || !m_ctx->hdrColor || !m_screen.taaIntermediate || !m_ctx->hasWindow) {
        return false;
    }
    if (!m_ctx->device || !m_ctx->device->GetDevice() || !m_ctx->commandList) {
        return false;
    }
    if (!m_screen.taaResolveSrvTableValid) {
        spdlog::error("RenderTAA: persistent SRV table is invalid");
        return false;
    }

    if (!TAACopyPass::PrepareResolveInputs({
            m_ctx->commandList,
            {m_screen.taaIntermediate.Get(), &m_screen.taaIntermediateState},
            {m_ctx->hdrColor, m_ctx->hdrState},
            {m_ctx->depthBuffer, m_ctx->depthState},
            {m_ctx->normalRoughness, m_ctx->normalRoughnessState},
            {m_screen.velocityBuffer.Get(), &m_screen.velocityState},
            {m_screen.historyColor.Get(), &m_screen.historyState},
            {m_ctx->maskTexture, m_ctx->maskState},
            kDepthSampleState,
            skipTransitions,
        })) {
        return false;
    }

    if (m_ctx->updateResolveTable) {
        m_ctx->updateResolveTable();
    }
    auto& resolveTable = m_screen.taaResolveSrvTables[m_ctx->frameIndex % kFrameCount];
    if (!TAAPass::Resolve({
            m_ctx->commandList,
            m_ctx->descriptorManager,
            m_ctx->rootSignature,
            m_ctx->frameConstants,
            m_ctx->taaPipeline,
            m_ctx->hdrColor,
            m_screen.taaIntermediateRTV,
            std::span<DescriptorHandle>(resolveTable.data(), resolveTable.size()),
            m_ctx->shadowEnvironmentTable,
        })) {
        spdlog::error("RenderTAA: pass resolve failed");
        return false;
    }
    return true;
}

bool TemporalSubsystem::CopyTAAIntermediateToHDR(bool skipTransitions) {
    if (!m_screen.taaIntermediate || !m_ctx->hdrColor) {
        return false;
    }

    TAACopyPass::IntermediateCopyContext copyContext{};
    copyContext.commandList = m_ctx->commandList;
    copyContext.taaIntermediate = {m_screen.taaIntermediate.Get(), &m_screen.taaIntermediateState};
    copyContext.hdrColor = {m_ctx->hdrColor, m_ctx->hdrState};
    copyContext.skipTransitions = skipTransitions;
    return TAACopyPass::CopyIntermediateToHdr(copyContext);
}

bool TemporalSubsystem::CopyHDRToTAAHistory(bool skipTransitions) {
    if (!m_ctx->hdrColor || !m_screen.historyColor) {
        return false;
    }

    TAACopyPass::HistoryCopyContext copyContext{};
    copyContext.commandList = m_ctx->commandList;
    copyContext.hdrColor = {m_ctx->hdrColor, m_ctx->hdrState};
    copyContext.historyColor = {m_screen.historyColor.Get(), &m_screen.historyState};
    copyContext.taaIntermediate = {m_screen.taaIntermediate.Get(), &m_screen.taaIntermediateState};
    copyContext.transitionIntermediateToRenderTarget = true;
    copyContext.skipTransitions = skipTransitions;
    copyContext.returnHdrAndHistoryToShaderResource = !skipTransitions;
    if (!TAACopyPass::CopyHdrToHistory(copyContext)) {
        return false;
    }
    MarkTAAHistoryValid(*m_ctx->historyManager, m_ctx->renderFrameCounter);
    return true;
}

void TemporalSubsystem::RenderTAA(const TemporalContext& ctx) {
    m_ctx = &ctx;
    if (!m_aa.enabled || !ctx.taaPipeline || !ctx.hdrColor || !m_screen.taaIntermediate || !ctx.hasWindow) {
        if (ctx.hdrColor &&
            !TAACopyPass::TransitionToShaderResource(ctx.commandList, {ctx.hdrColor, ctx.hdrState})) {
            return;
        }
        InvalidateTAAHistory(*ctx.historyManager, ctx.renderFrameCounter, "feature_disabled");
        return;
    }

    if (!ctx.device || !ctx.device->GetDevice() || !ctx.commandList) {
        return;
    }

    if (!m_screen.historyColor || !m_screen.historySRV.IsValid() ||
        !ctx.historyManager->CanReproject(TemporalHistoryId::TAAColor)) {
        (void)SeedTAAHistory(false);
        return;
    }

    if (!ResolveTAAIntermediate(false)) {
        return;
    }
    if (!CopyTAAIntermediateToHDR(false)) {
        return;
    }
    (void)CopyHDRToTAAHistory(false);
}

} // namespace Cortex::Graphics
