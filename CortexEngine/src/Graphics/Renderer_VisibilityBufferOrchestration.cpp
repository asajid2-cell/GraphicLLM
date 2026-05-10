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
void Renderer::RenderVisibilityBufferPath(Scene::ECS_Registry* registry) {
    if (!m_services.visibilityBuffer || !m_visibilityBufferState.enabled) {
        spdlog::warn("VB: Disabled or not initialized");
        return;
    }

    // Collect and upload instance data + mesh draw info
    CollectInstancesForVisibilityBuffer(registry);

    if (m_visibilityBufferState.instances.empty() || m_visibilityBufferState.meshDraws.empty()) {
        spdlog::warn("VB: No instances collected (instances={}, meshDraws={})",
                     m_visibilityBufferState.instances.size(), m_visibilityBufferState.meshDraws.size());
        return;
    }

    const uint32_t vbDebugView = GetVisibilityBufferDebugView();
    if (IsVisibilityBufferDebugView(vbDebugView)) {
        m_visibilityBufferState.debugOverrideThisFrame = true;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS vbCullMaskAddress = ResolveVisibilityBufferCullMask(vbDebugView);
    LogVisibilityBufferFirstFrame();

    bool completedPath = false;
    if (!RenderVisibilityBufferVisibilityStage(vbCullMaskAddress, vbDebugView, completedPath) || completedPath) {
        return;
    }
    if (!RenderVisibilityBufferMaterialResolveStage(vbDebugView, completedPath) || completedPath) {
        return;
    }
    RenderVisibilityBufferDeferredLightingStage(registry);
}

} // namespace Cortex::Graphics
