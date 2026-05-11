#include "Renderer.h"
#include "RenderGraph.h"
#include "Debug/GPUProfiler.h"
#include "Core/Window.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/MaterialState.h"
#include "Graphics/MeshBuffers.h"
#include "Graphics/SurfaceClassification.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/FrameContractValidation.h"
#include "Graphics/FrameContractResources.h"
#include "Graphics/RendererGeometryUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
Renderer::~Renderer() {
    // Ensure GPU is completely idle before any member destructors run
    WaitForGPU();
    Shutdown();
}

// Helper to tag the last successfully completed high-level render pass.
// This is used purely for diagnostics when the DX12 device reports a
// removed/hung state so logs can point at the most recent pass that ran.

void Renderer::Shutdown() {
    // CRITICAL FIX: Wait for GPU to finish all work before destroying resources
    // Otherwise we get OBJECT_DELETED_WHILE_STILL_IN_USE crash
    spdlog::info("Renderer shutdown: waiting for GPU idle...");
    WaitForGPU();
    spdlog::info("Renderer shutdown: GPU idle, releasing resources...");

    if (m_services.commandQueue) {
        m_services.commandQueue->Flush();
    }

    if (m_services.rayTracingContext) {
        m_services.rayTracingContext->Shutdown();
        m_services.rayTracingContext.reset();
    }

    if (m_services.bindlessManager) {
        m_services.bindlessManager->Shutdown();
        m_services.bindlessManager.reset();
    }

    if (m_services.gpuCulling) {
        m_services.gpuCulling->Shutdown();
        m_services.gpuCulling.reset();
    }

    if (m_services.renderGraph) {
        m_services.renderGraph->Shutdown();
        m_services.renderGraph.reset();
    }

    // Clean up async compute resources
    if (m_services.computeQueue) {
        m_services.computeQueue->Flush();
    }
    m_commandResources.computeList.Reset();
    for (auto& allocator : m_commandResources.computeAllocators) {
        allocator.Reset();
    }
    m_services.computeQueue.reset();
    m_frameRuntime.asyncComputeSupported = false;

    m_materialFallbacks.ResetResources();
    m_assetRuntime.textureCache.clear();
    m_depthResources.buffer.Reset();
    m_shadowResources.resources.map.Reset();
    m_mainTargets.hdrColor.Reset();
    m_ssaoResources.resources.texture.Reset();
    m_commandResources.graphicsList.Reset();
    for (auto& allocator : m_commandResources.graphicsAllocators) {
        allocator.Reset();
    }

    m_pipelineState.Reset();
    m_services.descriptorManager.reset();
    m_services.commandQueue.reset();

    spdlog::info("Renderer shut down");
}
} // namespace Cortex::Graphics
