#include "Renderer.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Passes/TAACopyPass.h"
#include "Passes/TAAPass.h"

#include <span>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

bool Renderer::SeedTAAHistory(bool skipTransitions) {
    if (!m_mainTargets.hdr.resources.color || !m_temporalScreenState.historyColor) {
        return false;
    }

    TAACopyPass::HistoryCopyContext copyContext{};
    copyContext.commandList = m_commandResources.graphicsList.Get();
    copyContext.hdrColor = {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state};
    copyContext.historyColor = {m_temporalScreenState.historyColor.Get(), &m_temporalScreenState.historyState};
    copyContext.skipTransitions = skipTransitions;
    copyContext.returnHdrAndHistoryToShaderResource = !skipTransitions;
    if (!TAACopyPass::CopyHdrToHistory(copyContext)) {
        return false;
    }
    MarkTAAHistoryValid();
    return true;
}

bool Renderer::ResolveTAAIntermediate(bool skipTransitions) {
    if (!m_temporalAAState.enabled || !m_pipelineState.taa || !m_mainTargets.hdr.resources.color || !m_temporalScreenState.taaIntermediate || !m_services.window) {
        return false;
    }
    if (!m_services.device || !m_services.device->GetDevice() || !m_commandResources.graphicsList) {
        return false;
    }
    if (!m_temporalScreenState.taaResolveSrvTableValid) {
        spdlog::error("RenderTAA: persistent SRV table is invalid");
        return false;
    }

    if (!TAACopyPass::PrepareResolveInputs({
            m_commandResources.graphicsList.Get(),
            {m_temporalScreenState.taaIntermediate.Get(), &m_temporalScreenState.taaIntermediateState},
            {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state},
            {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState},
            {m_mainTargets.normalRoughness.resources.texture.Get(), &m_mainTargets.normalRoughness.resources.state},
            {m_temporalScreenState.velocityBuffer.Get(), &m_temporalScreenState.velocityState},
            {m_temporalScreenState.historyColor.Get(), &m_temporalScreenState.historyState},
            {m_temporalMaskState.texture.Get(), &m_temporalMaskState.resourceState},
            kDepthSampleState,
            skipTransitions,
        })) {
        return false;
    }

    UpdateTAAResolveDescriptorTable();
    auto& resolveTable = m_temporalScreenState.taaResolveSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    if (!TAAPass::Resolve({
            m_commandResources.graphicsList.Get(),
            m_services.descriptorManager.get(),
            m_pipelineState.rootSignature.get(),
            m_constantBuffers.currentFrameGPU,
            m_pipelineState.taa.get(),
            m_mainTargets.hdr.resources.color.Get(),
            m_temporalScreenState.taaIntermediateRTV,
            std::span<DescriptorHandle>(resolveTable.data(), resolveTable.size()),
            m_environmentState.shadowAndEnvDescriptors[0],
        })) {
        spdlog::error("RenderTAA: pass resolve failed");
        return false;
    }
    return true;
}

bool Renderer::CopyTAAIntermediateToHDR(bool skipTransitions) {
    if (!m_temporalScreenState.taaIntermediate || !m_mainTargets.hdr.resources.color) {
        return false;
    }

    TAACopyPass::IntermediateCopyContext copyContext{};
    copyContext.commandList = m_commandResources.graphicsList.Get();
    copyContext.taaIntermediate = {m_temporalScreenState.taaIntermediate.Get(), &m_temporalScreenState.taaIntermediateState};
    copyContext.hdrColor = {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state};
    copyContext.skipTransitions = skipTransitions;
    return TAACopyPass::CopyIntermediateToHdr(copyContext);
}

bool Renderer::CopyHDRToTAAHistory(bool skipTransitions) {
    if (!m_mainTargets.hdr.resources.color || !m_temporalScreenState.historyColor) {
        return false;
    }

    TAACopyPass::HistoryCopyContext copyContext{};
    copyContext.commandList = m_commandResources.graphicsList.Get();
    copyContext.hdrColor = {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state};
    copyContext.historyColor = {m_temporalScreenState.historyColor.Get(), &m_temporalScreenState.historyState};
    copyContext.taaIntermediate = {m_temporalScreenState.taaIntermediate.Get(), &m_temporalScreenState.taaIntermediateState};
    copyContext.transitionIntermediateToRenderTarget = true;
    copyContext.skipTransitions = skipTransitions;
    copyContext.returnHdrAndHistoryToShaderResource = !skipTransitions;
    if (!TAACopyPass::CopyHdrToHistory(copyContext)) {
        return false;
    }
    MarkTAAHistoryValid();
    return true;
}

void Renderer::RenderTAA() {
    if (!m_temporalAAState.enabled || !m_pipelineState.taa || !m_mainTargets.hdr.resources.color || !m_temporalScreenState.taaIntermediate || !m_services.window) {
        // Ensure HDR is in a readable state for subsequent passes even when TAA
        // is disabled so SSR/post-process can still sample it.
        if (m_mainTargets.hdr.resources.color &&
            !TAACopyPass::TransitionToShaderResource(m_commandResources.graphicsList.Get(),
                                                     {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state})) {
            return;
        }
        // History is no longer meaningful once TAA has been disabled.
        InvalidateTAAHistory("feature_disabled");
        return;
    }

    if (!m_services.device || !m_services.device->GetDevice() || !m_commandResources.graphicsList) {
        return;
    }

    if (!m_temporalScreenState.historyColor || !m_temporalScreenState.historySRV.IsValid() ||
        !m_temporalHistory.manager.CanReproject(TemporalHistoryId::TAAColor)) {
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
