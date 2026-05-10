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

namespace {

void GetTextureSize(ID3D12Resource* resource, uint32_t& width, uint32_t& height) {
    width = 0;
    height = 0;
    if (!resource) {
        return;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        width = static_cast<uint32_t>(desc.Width);
        height = desc.Height;
    }
}

} // namespace
void Renderer::UpdateRTFramePlan(const FrameFeaturePlan& featurePlan) {
    uint32_t reflectionWidth = 0;
    uint32_t reflectionHeight = 0;
    uint32_t giWidth = 0;
    uint32_t giHeight = 0;
    GetTextureSize(m_rtReflectionTargets.color.Get(), reflectionWidth, reflectionHeight);
    GetTextureSize(m_rtGITargets.color.Get(), giWidth, giHeight);

    RTSchedulerInputs inputs{};
    inputs.frameNumber = m_frameLifecycle.renderFrameCounter;
    inputs.dedicatedVideoMemoryBytes = m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0;
    inputs.renderWidth = GetInternalRenderWidth();
    inputs.renderHeight = GetInternalRenderHeight();
    inputs.currentReflectionWidth = reflectionWidth;
    inputs.currentReflectionHeight = reflectionHeight;
    inputs.currentGIWidth = giWidth;
    inputs.currentGIHeight = giHeight;
    inputs.tlasCandidateCount = m_framePlanning.sceneSnapshot.IsValidForFrame(m_frameLifecycle.renderFrameCounter)
        ? static_cast<uint32_t>(m_framePlanning.sceneSnapshot.rtCandidateIndices.size())
        : 0u;
    inputs.pendingRendererBLASJobs = m_assetRuntime.gpuJobs.pendingBLASJobs;
    inputs.pendingContextBLAS = m_services.rayTracingContext ? m_services.rayTracingContext->GetPendingBLASCount() : 0;
    m_framePlanning.budgetPlan = BudgetPlanner::BuildPlan(
        inputs.dedicatedVideoMemoryBytes,
        m_services.window ? std::max(1u, m_services.window->GetWidth()) : inputs.renderWidth,
        m_services.window ? std::max(1u, m_services.window->GetHeight()) : inputs.renderHeight);
    m_assetRuntime.registry.SetBudgets(m_framePlanning.budgetPlan.textureBudgetBytes,
                               m_framePlanning.budgetPlan.environmentBudgetBytes,
                               m_framePlanning.budgetPlan.geometryBudgetBytes,
                               m_framePlanning.budgetPlan.rtStructureBudgetBytes);
    inputs.rendererBudget = m_framePlanning.budgetPlan;
    inputs.rendererBudgetValid = true;
    inputs.requested = featurePlan.runRayTracing;
    inputs.supported = m_rtRuntimeState.supported;
    inputs.contextReady = m_services.rayTracingContext != nullptr;
    inputs.warmingUp = IsRTWarmingUp();
    inputs.shadowPipelineReady = m_services.rayTracingContext && m_services.rayTracingContext->HasPipeline();
    inputs.reflectionFeatureRequested = m_rtRuntimeState.reflectionsEnabled;
    inputs.reflectionPipelineReady = m_services.rayTracingContext && m_services.rayTracingContext->HasReflectionPipeline();
    inputs.reflectionResourceReady = m_rtReflectionTargets.color != nullptr && m_rtReflectionTargets.uav.IsValid();
    inputs.giFeatureRequested = m_rtRuntimeState.giEnabled;
    inputs.giPipelineReady = m_services.rayTracingContext && m_services.rayTracingContext->HasGIPipeline();
    inputs.giResourceReady = m_rtGITargets.color != nullptr && m_rtGITargets.uav.IsValid();
    inputs.depthReady = m_depthResources.buffer != nullptr && m_depthResources.srv.IsValid();
    inputs.shadowMaskReady = m_rtShadowTargets.mask != nullptr && m_rtShadowTargets.maskUAV.IsValid();

    m_framePlanning.rtPlan = RTScheduler::BuildFramePlan(inputs);
    if (m_services.rayTracingContext) {
        m_services.rayTracingContext->SetAccelerationStructureBudgets(
            m_framePlanning.rtPlan.budget.maxBLASTotalBytes,
            m_framePlanning.rtPlan.budget.maxBLASBuildBytesPerFrame);
    }
}

} // namespace Cortex::Graphics
