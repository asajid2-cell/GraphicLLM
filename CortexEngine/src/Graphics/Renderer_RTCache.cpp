#include "Renderer.h"

#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
namespace Cortex::Graphics {

void Renderer::ClearBLASCache() {
    // Clear all BLAS entries from the ray tracing context.
    // This MUST be called AFTER ResetCommandList() to ensure no GPU operations
    // are still referencing these resources.
    if (m_services.rayTracingContext) {
        m_services.rayTracingContext->ClearAllBLAS();
        spdlog::info("Renderer: BLAS cache cleared for scene switch");
    }

    // Also clear mesh asset keys so stale pointers don't get reused
    m_assetRuntime.meshAssetKeys.clear();
}

} // namespace Cortex::Graphics
