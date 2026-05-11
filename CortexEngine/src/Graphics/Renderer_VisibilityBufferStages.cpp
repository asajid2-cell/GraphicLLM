#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/Passes/VisibilityBufferResourcePass.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Graphics/SurfaceClassification.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
namespace Cortex::Graphics {
namespace {
constexpr uint32_t kVBDebugNone = 0;
constexpr uint32_t kVBDebugVisibility = 1;
constexpr uint32_t kVBDebugDepth = 2;
constexpr uint32_t kVBDebugGBufferAlbedo = 3;
constexpr uint32_t kVBDebugGBufferNormal = 4;
constexpr uint32_t kVBDebugGBufferEmissive = 5;
constexpr uint32_t kVBDebugGBufferExt0 = 6;
constexpr uint32_t kVBDebugGBufferExt1 = 7;
constexpr uint32_t kVBDebugGBufferExt2 = 8;

bool IsVisibilityBufferDebugView(uint32_t debugView) {
    return debugView != kVBDebugNone;
}

bool IsVisibilityBufferUnculledDebugView(uint32_t debugView) {
    return debugView == kVBDebugVisibility || debugView == kVBDebugDepth;
}

bool IsVisibilityBufferGBufferDebugView(uint32_t debugView) {
    return debugView >= kVBDebugGBufferAlbedo && debugView <= kVBDebugGBufferExt2;
}
} // namespace
bool Renderer::RenderVisibilityBufferVisibilityStage(D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress,
                                                     uint32_t debugView,
                                                     bool& completedPath) {
    completedPath = false;
    const bool depthReady = VisibilityBufferResourcePass::PrepareDepthForVisibility(
        m_commandResources.graphicsList.Get(),
        {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState});
    if (!depthReady) {
        return false;
    }

    auto visResult = m_services.visibilityBuffer->RenderVisibilityPass(
        m_commandResources.graphicsList.Get(),
        m_depthResources.resources.buffer.Get(),
        m_depthResources.descriptors.dsv.cpu,
        m_constantBuffers.frameCPU.viewProjectionMatrix,
        m_visibilityBufferState.meshDraws,
        cullMaskAddress);

    if (visResult.IsErr()) {
        spdlog::error("Visibility pass failed: {}", visResult.Error());
        return false;
    }

    uint32_t vbDrawBatches = 0;
    for (const auto& draw : m_visibilityBufferState.meshDraws) {
        vbDrawBatches += (draw.instanceCount > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountDoubleSided > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountAlpha > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountAlphaDoubleSided > 0) ? 1u : 0u;
    }
    m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances = static_cast<uint32_t>(m_visibilityBufferState.instances.size());
    m_frameDiagnostics.contract.drawCounts.visibilityBufferMeshes = static_cast<uint32_t>(m_visibilityBufferState.meshDraws.size());
    m_frameDiagnostics.contract.drawCounts.visibilityBufferDrawBatches = vbDrawBatches;

    if (debugView == kVBDebugVisibility) {
        auto dbg = m_services.visibilityBuffer->DebugBlitVisibilityToHDR(m_commandResources.graphicsList.Get(), m_mainTargets.hdr.resources.color.Get(), m_mainTargets.hdr.descriptors.rtv.cpu);
        if (dbg.IsErr()) {
            spdlog::warn("VB debug blit (visibility) failed: {}", dbg.Error());
        }
        m_visibilityBufferState.renderedThisFrame = true;
        completedPath = true;
    } else if (debugView == kVBDebugDepth) {
        const bool debugDepthReady = VisibilityBufferResourcePass::PrepareDepthForSampling(
            m_commandResources.graphicsList.Get(),
            {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState});
        if (!debugDepthReady) {
            return false;
        }
        auto dbg = m_services.visibilityBuffer->DebugBlitDepthToHDR(
            m_commandResources.graphicsList.Get(), m_mainTargets.hdr.resources.color.Get(), m_mainTargets.hdr.descriptors.rtv.cpu, m_depthResources.resources.buffer.Get());
        if (dbg.IsErr()) {
            spdlog::warn("VB debug blit (depth) failed: {}", dbg.Error());
        }
        m_visibilityBufferState.renderedThisFrame = true;
        completedPath = true;
    }

    return true;
}

bool Renderer::RenderVisibilityBufferMaterialResolveStage(uint32_t debugView, bool& completedPath) {
    completedPath = false;
    const bool depthSampleReady = VisibilityBufferResourcePass::PrepareDepthForSampling(
        m_commandResources.graphicsList.Get(),
        {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState});
    if (!depthSampleReady) {
        return false;
    }

    auto resolveResult = m_services.visibilityBuffer->ResolveMaterials(
        m_commandResources.graphicsList.Get(),
        m_depthResources.resources.buffer.Get(),
        m_depthResources.descriptors.srv.cpu,
        m_visibilityBufferState.meshDraws,
        m_constantBuffers.frameCPU.viewProjectionMatrix,
        m_constantBuffers.biomeMaterialsValid ? m_constantBuffers.biomeMaterials.gpuAddress : 0);

    if (resolveResult.IsErr()) {
        spdlog::error("Material resolve failed: {}", resolveResult.Error());
        return false;
    }

    static bool firstResolve = true;
    if (firstResolve) {
        spdlog::info("VB: Material resolve completed successfully");
        firstResolve = false;
    }

    if (IsVisibilityBufferGBufferDebugView(debugView)) {
        VisibilityBufferRenderer::DebugBlitBuffer which = VisibilityBufferRenderer::DebugBlitBuffer::Albedo;
        if (debugView == kVBDebugGBufferNormal) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::NormalRoughness;
        } else if (debugView == kVBDebugGBufferEmissive) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::EmissiveMetallic;
        } else if (debugView == kVBDebugGBufferExt0) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt0;
        } else if (debugView == kVBDebugGBufferExt1) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt1;
        } else if (debugView == kVBDebugGBufferExt2) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt2;
        }

        auto dbg = m_services.visibilityBuffer->DebugBlitGBufferToHDR(m_commandResources.graphicsList.Get(), m_mainTargets.hdr.resources.color.Get(), m_mainTargets.hdr.descriptors.rtv.cpu, which);
        if (dbg.IsErr()) {
            spdlog::warn("VB debug blit (gbuffer) failed: {}", dbg.Error());
        }
        m_visibilityBufferState.renderedThisFrame = true;
        completedPath = true;
    }

    return true;
}

} // namespace Cortex::Graphics
