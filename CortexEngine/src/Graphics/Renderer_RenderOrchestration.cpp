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

bool Renderer::HasPendingGpuJobs() const {
    return m_assetRuntime.gpuJobs.HasPendingJobs();
}

uint32_t Renderer::GetPendingMeshJobs() const {
    return m_assetRuntime.gpuJobs.pendingMeshJobs;
}

uint32_t Renderer::GetPendingBLASJobs() const {
    return m_assetRuntime.gpuJobs.pendingBLASJobs;
}

void Renderer::ProcessGpuJobsPerFrame() {
    if (m_frameLifecycle.deviceRemoved) {
        return;
    }

    uint32_t meshCount = 0;
    uint32_t blasCount = 0;

    while (!m_assetRuntime.gpuJobs.pendingJobs.empty()) {
        GpuJob job = m_assetRuntime.gpuJobs.pendingJobs.front();

        if (job.type == GpuJobType::MeshUpload) {
            if (meshCount >= m_assetRuntime.gpuJobs.maxMeshJobsPerFrame) {
                break;
            }
            if (job.mesh) {
                auto res = UploadMesh(job.mesh);
                if (res.IsErr()) {
                    spdlog::warn("GpuJob MeshUpload '{}' failed: {}", job.label, res.Error());
                }
            }
            if (m_assetRuntime.gpuJobs.pendingMeshJobs > 0) {
                --m_assetRuntime.gpuJobs.pendingMeshJobs;
            }
            ++meshCount;
        } else if (job.type == GpuJobType::BuildBLAS) {
            if (blasCount >= m_assetRuntime.gpuJobs.maxBLASJobsPerFrame) {
                break;
            }
            if (m_services.rayTracingContext && job.blasMeshKey) {
                m_services.rayTracingContext->BuildSingleBLAS(job.blasMeshKey);
            }
            if (m_assetRuntime.gpuJobs.pendingBLASJobs > 0) {
                --m_assetRuntime.gpuJobs.pendingBLASJobs;
            }
            ++blasCount;
        }

        m_assetRuntime.gpuJobs.pendingJobs.pop_front();
    }
}

void Renderer::Render(Scene::ECS_Registry* registry, float deltaTime) {
    ResetFrameExecutionState();

    if (m_frameLifecycle.deviceRemoved) {
        if (!m_frameLifecycle.deviceRemovedLogged) {
            spdlog::error("Renderer::Render skipped because DX12 device was removed earlier (likely out of GPU memory). Restart is required.");
            m_frameLifecycle.deviceRemovedLogged = true;
        }
        return;
    }

    if (!m_services.window || !m_services.window->GetCurrentBackBuffer()) {
        spdlog::error("Renderer::Render called without a valid back buffer; skipping frame");
        return;
    }

    using clock = std::chrono::high_resolution_clock;

    FrameExecutionContext frameCtx = BuildFrameExecutionContext(registry, deltaTime);
    FrameFeaturePlan& featurePlan = frameCtx.features;

    m_ssrResources.frame.activeThisFrame = featurePlan.runSSR;

    RunPreFrameServices(frameCtx);
    if (!BeginFrameExecution(frameCtx)) {
        return;
    }

    if (TryRenderSpecialFramePath(frameCtx)) {
        return;
    }

    ExecuteRayTracingFramePhase(frameCtx);

    const auto tMainStart = clock::now();

    ExecuteShadowFramePhase(frameCtx);

    BeginMainSceneFramePhase(frameCtx);

    ExecuteGeometryFramePhase(frameCtx);

    const MainSceneEffectsResult mainEffects = ExecuteMainSceneEffectsFramePhase(frameCtx);

    const auto tMainEnd = clock::now();
    if (m_commandResources.graphicsList) {
        Debug::GPUProfiler::Get().EndScope(m_commandResources.graphicsList.Get()); // MainPass
    }
    m_frameDiagnostics.timings.mainPassMs =
        std::chrono::duration_cast<std::chrono::microseconds>(tMainEnd - tMainStart).count() / 1000.0f;

    ExecutePostProcessingFramePhase(frameCtx, mainEffects);

    CompleteFrameExecutionPhase(frameCtx);

    // If desired later, we can expose total render CPU time via
    // duration_cast here using (clock::now() - frameStart).
}

void Renderer::ResetTemporalHistoryForSceneChange() {
    // Reset TAA history so the first frame after a scene switch uses the
    // current HDR as the new history without blending in the previous scene.
    InvalidateTAAHistory("scene_change");
    m_temporalAAState.sampleIndex = 0;
    m_temporalAAState.jitterPrevPixels = glm::vec2(0.0f);
    m_temporalAAState.jitterCurrPixels = glm::vec2(0.0f);
    m_temporalAAState.hasPrevViewProj = false;

    // Reset RT temporal data so RT shadows / GI / reflections do not leave
    // ghosted silhouettes from the previous scene.
    InvalidateRTShadowHistory("scene_change");
    InvalidateRTGIHistory("scene_change");
    InvalidateRTReflectionHistory("scene_change");
    m_cameraState.ResetHistory();
    m_gpuCullingState.previousWorldByEntity.clear();
    m_gpuCullingState.previousCenterByEntity.clear();
    m_gpuCullingState.previousTransformHistoryResetPending = true;

    // Clear any pending debug-line state to avoid drawing lines that belonged
    // to the previous layout.
    m_debugLineState.lines.clear();
    m_debugLineState.disabled = false;
}
} // namespace Cortex::Graphics
