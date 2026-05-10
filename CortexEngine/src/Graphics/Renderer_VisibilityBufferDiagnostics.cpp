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

void Renderer::LogVisibilityBufferFirstFrame() {
    static bool firstFrame = true;
    if (!firstFrame) {
        return;
    }

    spdlog::info("VB: First frame - rendering {} instances across {} unique meshes",
                 m_visibilityBufferState.instances.size(), m_visibilityBufferState.meshDraws.size());
    std::unordered_map<uint32_t, uint32_t> meshIndexCounts;
    for (const auto& inst : m_visibilityBufferState.instances) {
        meshIndexCounts[inst.meshIndex]++;
    }
    for (const auto& [meshIdx, count] : meshIndexCounts) {
        spdlog::info("  Mesh {} has {} instances", meshIdx, count);
    }
    for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(m_visibilityBufferState.meshDraws.size()); ++meshIdx) {
        const auto& draw = m_visibilityBufferState.meshDraws[meshIdx];
        const uint64_t vbBytes = draw.vertexBuffer ? draw.vertexBuffer->GetDesc().Width : 0ull;
        const uint64_t ibBytes = draw.indexBuffer ? draw.indexBuffer->GetDesc().Width : 0ull;
        spdlog::info("  MeshDraw {}: vtxCount={} idxCount={} stride={} vbBytes={} ibBytes={} opaque={} ds={} alpha={} alphaDs={} start={}/{}/{}/{}",
                     meshIdx,
                     draw.vertexCount,
                     draw.indexCount,
                     draw.vertexStrideBytes,
                     vbBytes,
                     ibBytes,
                     draw.instanceCount,
                     draw.instanceCountDoubleSided,
                     draw.instanceCountAlpha,
                     draw.instanceCountAlphaDoubleSided,
                     draw.startInstance,
                     draw.startInstanceDoubleSided,
                     draw.startInstanceAlpha,
                     draw.startInstanceAlphaDoubleSided);
    }
    firstFrame = false;
}

} // namespace Cortex::Graphics
