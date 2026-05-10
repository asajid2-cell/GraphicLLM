#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
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
uint32_t Renderer::GetVisibilityBufferDebugView() const {
    switch (m_debugViewState.mode) {
        case 33u: return kVBDebugVisibility;
        case 34u: return kVBDebugDepth;
        case 35u: return kVBDebugGBufferAlbedo;
        case 36u: return kVBDebugGBufferNormal;
        case 37u: return kVBDebugGBufferEmissive;
        case 38u: return kVBDebugGBufferExt0;
        case 39u: return kVBDebugGBufferExt1;
        case 40u: return kVBDebugGBufferExt2;
        default: return kVBDebugNone;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS Renderer::ResolveVisibilityBufferCullMask(uint32_t debugView) {
    D3D12_GPU_VIRTUAL_ADDRESS vbCullMaskAddress = 0;
    if (!m_gpuCullingState.enabled || !m_services.gpuCulling || IsVisibilityBufferUnculledDebugView(debugView)) {
        return 0;
    }

    const bool forceVisible = (std::getenv("CORTEX_GPUCULL_FORCE_VISIBLE") != nullptr);
    m_services.gpuCulling->SetForceVisible(forceVisible);

    const uint32_t maxInstances = m_services.gpuCulling->GetMaxInstances();
    if (m_visibilityBufferState.instances.size() > maxInstances) {
        spdlog::warn("VB: instance count {} exceeds GPU culling capacity {}; disabling VB cull mask this frame",
                     m_visibilityBufferState.instances.size(), maxInstances);
        return 0;
    }

    std::vector<GPUInstanceData> cullInstances;
    cullInstances.reserve(m_visibilityBufferState.instances.size());
    for (const auto& vbInst : m_visibilityBufferState.instances) {
        GPUInstanceData inst{};
        inst.modelMatrix = vbInst.worldMatrix;
        inst.boundingSphere = vbInst.boundingSphere;
        inst.prevCenterWS = vbInst.prevCenterWS;
        inst.meshIndex = vbInst.meshIndex;
        inst.materialIndex = vbInst.materialIndex;
        inst.flags = vbInst.flags;
        inst.cullingId = vbInst.cullingId;
        cullInstances.push_back(inst);
    }

    auto uploadResult = m_services.gpuCulling->UpdateInstances(m_commandResources.graphicsList.Get(), cullInstances);
    if (uploadResult.IsErr()) {
        spdlog::warn("VB: GPU culling upload failed: {}", uploadResult.Error());
        return 0;
    }

    const bool freezeCullingEnv = (std::getenv("CORTEX_GPUCULL_FREEZE") != nullptr);
    const bool freezeCulling = freezeCullingEnv || m_gpuCullingState.freeze;

    glm::mat4 viewProjForCulling = m_constantBuffers.frameCPU.viewProjectionNoJitter;
    glm::vec3 cameraPosForCulling = glm::vec3(m_constantBuffers.frameCPU.cameraPosition);
    if (!freezeCulling) {
        m_gpuCullingState.freezeCaptured = false;
    } else {
        if (!m_gpuCullingState.freezeCaptured) {
            m_gpuCullingState.freezeCaptured = true;
            m_gpuCullingState.frozenViewProj = viewProjForCulling;
            m_gpuCullingState.frozenCameraPos = cameraPosForCulling;
            spdlog::warn("GPU culling freeze enabled ({}): capturing view on frame {}",
                         freezeCullingEnv ? "env CORTEX_GPUCULL_FREEZE=1" : "K toggle",
                         m_frameLifecycle.renderFrameCounter);
        }
        viewProjForCulling = m_gpuCullingState.frozenViewProj;
        cameraPosForCulling = m_gpuCullingState.frozenCameraPos;
    }

    static bool s_checkedEnv = false;
    static bool s_disableHzb = false;
    if (!s_checkedEnv) {
        s_checkedEnv = true;
        s_disableHzb = (std::getenv("CORTEX_DISABLE_VB_HZB") != nullptr);
        if (s_disableHzb) {
            spdlog::info("VB: HZB occlusion disabled (CORTEX_DISABLE_VB_HZB=1)");
        }
    }

    bool useHzbOcclusion = false;
    if (!s_disableHzb &&
        m_hzbResources.valid && m_hzbResources.captureValid && m_hzbResources.texture && m_hzbResources.mipCount > 0 &&
        (m_hzbResources.captureFrameCounter + 1u == m_frameLifecycle.renderFrameCounter)) {
        useHzbOcclusion = true;
    }
    if (freezeCulling) {
        useHzbOcclusion = false;
    }
    m_visibilityBufferState.hzbOcclusionUsedThisFrame = useHzbOcclusion;

    m_services.gpuCulling->SetHZBForOcclusion(
        useHzbOcclusion ? m_hzbResources.texture.Get() : nullptr,
        m_hzbResources.width,
        m_hzbResources.height,
        m_hzbResources.mipCount,
        m_hzbResources.captureViewMatrix,
        m_hzbResources.captureViewProjMatrix,
        m_hzbResources.captureCameraPosWS,
        m_hzbResources.captureNearPlane,
        m_hzbResources.captureFarPlane,
        useHzbOcclusion);

    if (useHzbOcclusion &&
        m_hzbResources.texture &&
        (m_hzbResources.resourceState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_hzbResources.texture.Get();
        barrier.Transition.StateBefore = m_hzbResources.resourceState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        m_hzbResources.resourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    static bool s_debugCullingEnv = (std::getenv("CORTEX_DEBUG_CULLING") != nullptr);
    m_services.gpuCulling->SetDebugEnabled(s_debugCullingEnv);

    auto cullResult = m_services.gpuCulling->DispatchCulling(m_commandResources.graphicsList.Get(), viewProjForCulling, cameraPosForCulling);
    if (cullResult.IsErr()) {
        spdlog::warn("VB: GPU culling dispatch failed: {}", cullResult.Error());
    } else if (auto* mask = m_services.gpuCulling->GetVisibilityMaskBuffer()) {
        vbCullMaskAddress = mask->GetGPUVirtualAddress();
    }

    if (s_debugCullingEnv) {
        static uint32_t s_cullLogCounter = 0;
        if ((s_cullLogCounter++ % 60) == 0) {
            auto stats = m_services.gpuCulling->GetDebugStats();
            if (stats.valid) {
                spdlog::info("GPU Cull Stats: tested={} frustumCulled={} occluded={} visible={} (HZB: near={:.2f} hzb={:.2f} mip={} flags={})",
                    stats.tested, stats.frustumCulled, stats.occluded, stats.visible,
                    stats.sampleNearDepth, stats.sampleHzbDepth, stats.sampleMip, stats.sampleFlags);
            }
        }
    }

    if (vbCullMaskAddress != 0 && std::getenv("CORTEX_DISABLE_VB_CULL_MASK") != nullptr) {
        vbCullMaskAddress = 0;
    }
    return vbCullMaskAddress;
}

} // namespace Cortex::Graphics
