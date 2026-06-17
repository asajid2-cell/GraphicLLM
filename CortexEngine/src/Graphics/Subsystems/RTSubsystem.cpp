#include "Graphics/Subsystems/RTSubsystem.h"

#include "Graphics/BudgetPlanner.h"
#include "Graphics/RTScheduler.h"
#include "Graphics/RTDenoiser.h"
#include "Graphics/RTReflectionSignalStats.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Graphics/TemporalManager.h"
#include "Graphics/VisibilityBuffer.h"
#include "Graphics/RHI/DX12Device.h"
#include "Graphics/RHI/DX12Raytracing.h"
#include "Graphics/Passes/RTShadowsGIPass.h"
#include "Graphics/Passes/RTReflectionDispatchPass.h"
#include "Graphics/Passes/ReadbackBuffer.h"
#include "Core/Window.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

void RTSubsystem::ClearBLASCache(const RTContext& ctx) {
    // Clear all BLAS entries from the ray tracing context.
    // This MUST be called AFTER ResetCommandList() to ensure no GPU operations
    // are still referencing these resources.
    if (ctx.rayTracingContext) {
        ctx.rayTracingContext->ClearAllBLAS();
        spdlog::info("Renderer: BLAS cache cleared for scene switch");
    }

    // Also clear mesh asset keys so stale pointers don't get reused
    ctx.assetRuntime->meshAssetKeys.clear();
}

bool RTSubsystem::IsRTWarmingUp(DX12RaytracingContext* rayTracingContext,
                               uint32_t pendingRendererBLASJobs) const {
    if (!m_runtime.supported || !m_runtime.enabled || !rayTracingContext) {
        return false;
    }
    // Consider RT "warming up" while there are outstanding BLAS jobs either
    // in the renderer's queue or pending inside the DXR context.
    if (pendingRendererBLASJobs > 0) {
        return true;
    }
    return rayTracingContext->GetPendingBLASCount() > 0;
}

void RTSubsystem::InvalidateRTShadowHistory(TemporalManager& mgr, uint64_t frameCounter, const char* reason) {
    mgr.Invalidate(TemporalHistoryId::RTShadow, reason, frameCounter);
}

void RTSubsystem::MarkRTShadowHistoryValid(TemporalManager& mgr, uint64_t frameCounter) {
    TemporalMarkValidDesc desc{};
    desc.rejectionMode = m_denoise.usedDisocclusionRejectionThisFrame ? "disocclusion_depth_normal_velocity" :
        (m_denoise.usedDepthNormalRejectionThisFrame ? "depth_normal_velocity" : "copy_seed");
    desc.accumulationAlpha = m_denoise.shadowAlpha;
    desc.usedDepthNormalRejection = m_denoise.usedDepthNormalRejectionThisFrame;
    desc.usedVelocityReprojection = m_denoise.usedVelocityThisFrame;
    desc.usedDisocclusionRejection = m_denoise.usedDisocclusionRejectionThisFrame;
    mgr.MarkValid(TemporalHistoryId::RTShadow, frameCounter, desc);
}

void RTSubsystem::InvalidateRTReflectionHistory(TemporalManager& mgr, uint64_t frameCounter, const char* reason) {
    mgr.Invalidate(TemporalHistoryId::RTReflection, reason, frameCounter);
}

void RTSubsystem::MarkRTReflectionHistoryValid(TemporalManager& mgr, uint64_t frameCounter) {
    TemporalMarkValidDesc desc{};
    desc.rejectionMode = m_denoise.usedDisocclusionRejectionThisFrame ? "disocclusion_depth_normal_velocity" :
        (m_denoise.usedDepthNormalRejectionThisFrame ? "depth_normal_velocity" : "copy_seed");
    desc.accumulationAlpha = m_denoise.reflectionAlpha;
    desc.usedDepthNormalRejection = m_denoise.usedDepthNormalRejectionThisFrame;
    desc.usedVelocityReprojection = m_denoise.usedVelocityThisFrame;
    desc.usedDisocclusionRejection = m_denoise.usedDisocclusionRejectionThisFrame;
    mgr.MarkValid(TemporalHistoryId::RTReflection, frameCounter, desc);
}

void RTSubsystem::InvalidateRTGIHistory(TemporalManager& mgr, uint64_t frameCounter, const char* reason) {
    mgr.Invalidate(TemporalHistoryId::RTGI, reason, frameCounter);
}

void RTSubsystem::MarkRTGIHistoryValid(TemporalManager& mgr, uint64_t frameCounter) {
    TemporalMarkValidDesc desc{};
    desc.rejectionMode = m_denoise.usedDisocclusionRejectionThisFrame ? "disocclusion_depth_normal_velocity" :
        (m_denoise.usedDepthNormalRejectionThisFrame ? "depth_normal_velocity" : "copy_seed");
    desc.accumulationAlpha = m_denoise.giAlpha;
    desc.usedDepthNormalRejection = m_denoise.usedDepthNormalRejectionThisFrame;
    desc.usedVelocityReprojection = m_denoise.usedVelocityThisFrame;
    desc.usedDisocclusionRejection = m_denoise.usedDisocclusionRejectionThisFrame;
    mgr.MarkValid(TemporalHistoryId::RTGI, frameCounter, desc);
}

Result<void> RTSubsystem::CreateRTShadowMask(const RTContext& ctx) {
    if (!ctx.device || !ctx.descriptorManager || !ctx.window) {
        return Result<void>::Err("Renderer not initialized for RT shadow mask creation");
    }

    UINT width = ctx.internalRenderWidth;
    UINT height = ctx.internalRenderHeight;
    if (ctx.hdrColor) {
        const D3D12_RESOURCE_DESC hdrDesc = ctx.hdrColor->GetDesc();
        width = static_cast<UINT>(hdrDesc.Width);
        height = hdrDesc.Height;
    }

    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create RT shadow mask");
    }

    auto targetResult = m_shadowTargets.CreateResources(
        ctx.device->GetDevice(),
        ctx.descriptorManager,
        width,
        height);
    if (targetResult.IsErr()) {
        return targetResult;
    }

    ctx.updateEnvironmentTable();

    InvalidateRTShadowHistory(*ctx.historyManager, ctx.renderFrameCounter, "resource_recreated");

    return Result<void>::Ok();
}

Result<void> RTSubsystem::CreateRTGIResources(const RTContext& ctx) {
    if (!ctx.device || !ctx.descriptorManager || !ctx.window) {
        return Result<void>::Err("Renderer not initialized for RT GI creation");
    }

    UINT fullWidth = ctx.internalRenderWidth;
    UINT fullHeight = ctx.internalRenderHeight;
    if (ctx.hdrColor) {
        const D3D12_RESOURCE_DESC hdrDesc = ctx.hdrColor->GetDesc();
        fullWidth = static_cast<UINT>(hdrDesc.Width);
        fullHeight = hdrDesc.Height;
    }

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create RT GI buffer");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        ctx.device ? ctx.device->GetDedicatedVideoMemoryBytes() : 0,
        fullWidth,
        fullHeight);
    const UINT halfWidth = std::max<UINT>(1, fullWidth / 2u);
    const UINT halfHeight = std::max<UINT>(1, fullHeight / 2u);
    const UINT budgetWidth = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(fullWidth) * budget.rtResolutionScale)));
    const UINT budgetHeight = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(fullHeight) * budget.rtResolutionScale)));
    const UINT width = std::min(halfWidth, budgetWidth);
    const UINT height = std::min(halfHeight, budgetHeight);

    InvalidateRTGIHistory(*ctx.historyManager, ctx.renderFrameCounter, "resource_recreated");

    auto targetResult = m_giTargets.CreateResources(
        ctx.device->GetDevice(),
        ctx.descriptorManager,
        width,
        height);
    if (targetResult.IsErr()) {
        return targetResult;
    }

    ctx.updateEnvironmentTable();

    return Result<void>::Ok();
}

Result<void> RTSubsystem::CreateRTReflectionResources(const RTContext& ctx) {
    if (!ctx.device || !ctx.descriptorManager || !ctx.window) {
        return Result<void>::Err("Renderer not initialized for RT reflection creation");
    }

    UINT baseWidth = ctx.internalRenderWidth;
    UINT baseHeight = ctx.internalRenderHeight;

    if (ctx.hdrColor) {
        D3D12_RESOURCE_DESC hdrDesc = ctx.hdrColor->GetDesc();
        baseWidth = static_cast<UINT>(hdrDesc.Width);
        baseHeight = static_cast<UINT>(hdrDesc.Height);
    }

    if (baseWidth == 0 || baseHeight == 0) {
        return Result<void>::Err("Render target size is zero; cannot create RT reflection buffer");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        ctx.device ? ctx.device->GetDedicatedVideoMemoryBytes() : 0,
        baseWidth,
        baseHeight);
    const UINT halfWidth = std::max<UINT>(1, baseWidth / 2u);
    const UINT halfHeight = std::max<UINT>(1, baseHeight / 2u);
    const UINT budgetWidth = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(baseWidth) * budget.rtResolutionScale)));
    const UINT budgetHeight = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(baseHeight) * budget.rtResolutionScale)));
    const UINT width = std::min(halfWidth, budgetWidth);
    const UINT height = std::min(halfHeight, budgetHeight);

    m_reflectionSignal.ResetResources();
    auto targetResult = m_reflectionTargets.CreateResources(
        ctx.device->GetDevice(),
        ctx.descriptorManager,
        width,
        height);
    if (targetResult.IsErr()) {
        return targetResult;
    }

    auto signalStatsResult = m_reflectionSignal.CreateStatsResources(
        ctx.device->GetDevice(),
        ctx.descriptorManager,
        RTReflectionSignalStats::kStatsBytes,
        RTReflectionSignalStats::kStatsWords);
    if (signalStatsResult.IsErr()) {
        m_reflectionTargets.ResetResources();
        return signalStatsResult;
    }

    InvalidateRTReflectionHistory(*ctx.historyManager, ctx.renderFrameCounter, "resource_recreated");

    return Result<void>::Ok();
}

void RTSubsystem::UpdateRTFramePlan(const FrameFeaturePlan& featurePlan, const RTContext& ctx) {
    uint32_t reflectionWidth = 0;
    uint32_t reflectionHeight = 0;
    uint32_t giWidth = 0;
    uint32_t giHeight = 0;
    GetTextureSize(m_reflectionTargets.color.Get(), reflectionWidth, reflectionHeight);
    GetTextureSize(m_giTargets.color.Get(), giWidth, giHeight);

    RTSchedulerInputs inputs{};
    inputs.frameNumber = ctx.renderFrameCounter;
    inputs.dedicatedVideoMemoryBytes = ctx.device ? ctx.device->GetDedicatedVideoMemoryBytes() : 0;
    inputs.renderWidth = ctx.internalRenderWidth;
    inputs.renderHeight = ctx.internalRenderHeight;
    inputs.currentReflectionWidth = reflectionWidth;
    inputs.currentReflectionHeight = reflectionHeight;
    inputs.currentGIWidth = giWidth;
    inputs.currentGIHeight = giHeight;
    inputs.tlasCandidateCount = ctx.framePlanning->sceneSnapshot.IsValidForFrame(ctx.renderFrameCounter)
        ? static_cast<uint32_t>(ctx.framePlanning->sceneSnapshot.rtCandidateIndices.size())
        : 0u;
    inputs.pendingRendererBLASJobs = ctx.assetRuntime->gpuJobs.pendingBLASJobs;
    inputs.pendingContextBLAS = ctx.rayTracingContext ? ctx.rayTracingContext->GetPendingBLASCount() : 0;
    ctx.framePlanning->budgetPlan = BudgetPlanner::BuildPlan(
        inputs.dedicatedVideoMemoryBytes,
        ctx.window ? std::max(1u, ctx.window->GetWidth()) : inputs.renderWidth,
        ctx.window ? std::max(1u, ctx.window->GetHeight()) : inputs.renderHeight);
    ctx.assetRuntime->registry.SetBudgets(ctx.framePlanning->budgetPlan.textureBudgetBytes,
                               ctx.framePlanning->budgetPlan.environmentBudgetBytes,
                               ctx.framePlanning->budgetPlan.geometryBudgetBytes,
                               ctx.framePlanning->budgetPlan.rtStructureBudgetBytes);
    inputs.rendererBudget = ctx.framePlanning->budgetPlan;
    inputs.rendererBudgetValid = true;
    inputs.requested =
        featurePlan.planned.rayTracingEnabled && !featurePlan.runMinimalFrame && !featurePlan.runVoxelBackend;
    inputs.supported = m_runtime.supported;
    inputs.contextReady = ctx.rayTracingContext != nullptr;
    inputs.warmingUp = IsRTWarmingUp(ctx.rayTracingContext, ctx.assetRuntime->gpuJobs.pendingBLASJobs);
    inputs.shadowPipelineReady = ctx.rayTracingContext && ctx.rayTracingContext->HasPipeline();
    inputs.reflectionFeatureRequested = m_runtime.reflectionsEnabled;
    inputs.reflectionPipelineReady = ctx.rayTracingContext && ctx.rayTracingContext->HasReflectionPipeline();
    inputs.reflectionResourceReady = m_reflectionTargets.color != nullptr && m_reflectionTargets.uav.IsValid();
    inputs.giFeatureRequested = m_runtime.giEnabled;
    inputs.giPipelineReady = ctx.rayTracingContext && ctx.rayTracingContext->HasGIPipeline();
    inputs.giResourceReady = m_giTargets.color != nullptr && m_giTargets.uav.IsValid();
    inputs.depthReady = ctx.depthBuffer != nullptr && ctx.depthSrv.IsValid();
    inputs.shadowMaskReady = m_shadowTargets.mask != nullptr && m_shadowTargets.maskUAV.IsValid();

    ctx.framePlanning->rtPlan = RTScheduler::BuildFramePlan(inputs);
    if (ctx.rayTracingContext) {
        ctx.rayTracingContext->SetAccelerationStructureBudgets(
            ctx.framePlanning->rtPlan.budget.maxBLASTotalBytes,
            ctx.framePlanning->rtPlan.budget.maxBLASBuildBytesPerFrame);
    }
}

void RTSubsystem::RenderRayTracing(Scene::ECS_Registry* registry, const RTContext& ctx) {
    if (!ctx.framePlanning->rtPlan.enabled || !ctx.rayTracingContext || !registry) {
        return;
    }

    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    HRESULT hr = ctx.commandList->QueryInterface(IID_PPV_ARGS(&rtCmdList));
    if (FAILED(hr) || !rtCmdList) {
        return;
    }

    const bool rtShadowInputsReady = RTShadowsGIPass::PrepareShadowInputs({
        rtCmdList.Get(),
        {ctx.depthBuffer, ctx.depthState},
        {m_shadowTargets.mask.Get(), &m_shadowTargets.maskState},
    });

    // Set the current frame index so BLAS builds can track when they were
    // recorded. This is used by ReleaseScratchBuffers() to ensure scratch
    // buffers aren't freed until the GPU has finished using them.
    ctx.rayTracingContext->SetCurrentFrameIndex(ctx.absoluteFrameIndex);

    // Build TLAS from the renderer's per-frame scene snapshot when available.
    // The registry path remains as a fallback for editor/debug calls outside
    // normal render orchestration.
    if (ctx.framePlanning->rtPlan.buildTLAS && ctx.framePlanning->sceneSnapshot.IsValidForFrame(ctx.renderFrameCounter)) {
        std::vector<DX12RaytracingContext::TLASBuildInput> tlasInputs;
        tlasInputs.reserve(std::min<uint32_t>(
            static_cast<uint32_t>(ctx.framePlanning->sceneSnapshot.rtCandidateIndices.size()),
            ctx.framePlanning->rtPlan.budget.maxTLASInstances));
        for (uint32_t entryIndex : ctx.framePlanning->sceneSnapshot.rtCandidateIndices) {
            if (tlasInputs.size() >= ctx.framePlanning->rtPlan.budget.maxTLASInstances) {
                break;
            }
            if (entryIndex >= ctx.framePlanning->sceneSnapshot.entries.size()) {
                continue;
            }
            const RendererSceneRenderable& sceneEntry = ctx.framePlanning->sceneSnapshot.entries[entryIndex];
            if (!sceneEntry.renderable || !sceneEntry.hasTransform || !sceneEntry.hasMesh) {
                continue;
            }

            DX12RaytracingContext::TLASBuildInput input{};
            input.stableId = static_cast<uint32_t>(sceneEntry.entity);
            input.renderable = sceneEntry.renderable;
            input.worldMatrix = sceneEntry.worldMatrix;
            input.maxWorldScale = GetMaxWorldScale(sceneEntry.worldMatrix);
            tlasInputs.push_back(input);
        }
        ctx.rayTracingContext->BuildTLAS(tlasInputs, rtCmdList.Get());
    } else if (ctx.framePlanning->rtPlan.buildTLAS) {
        ctx.rayTracingContext->BuildTLAS(registry, rtCmdList.Get());
    }

    // Dispatch the DXR sun-shadow pass when depth and mask descriptors are ready.
    if (rtShadowInputsReady &&
        ctx.framePlanning->rtPlan.dispatchShadows &&
        ctx.depthSrv.IsValid() &&
        m_shadowTargets.maskUAV.IsValid()) {
        DescriptorHandle envTable = ctx.environment->shadowAndEnvDescriptors[0];
            ctx.rayTracingContext->DispatchRayTracing(
                rtCmdList.Get(),
                ctx.depthSrv,
                m_shadowTargets.maskUAV,
                ctx.frameConstants,
                envTable);
    }

    // Note: RT reflections are dispatched later (after the main pass has
    // written the current frame's normal/roughness target). Dispatching
    // reflections here would sample previous-frame G-buffer data and produce
    // severe temporal instability / edge artifacts.

    // Optional RT diffuse GI: writes a low-frequency indirect lighting buffer
    // that can be sampled by the main PBR shader. As with reflections, this
    // pass is optional and disabled by default; DispatchGI is a no-op if the
    // GI pipeline is not available.
    if (ctx.framePlanning->rtPlan.dispatchGI && m_giTargets.color && m_giTargets.uav.IsValid()) {
        const bool rtGIOutputReady = RTShadowsGIPass::PrepareGIOutput({
            rtCmdList.Get(),
            {m_giTargets.color.Get(), &m_giTargets.colorState},
        });

        if (rtGIOutputReady &&
            ctx.depthSrv.IsValid() &&
            ctx.rayTracingContext->HasGIPipeline()) {
            DescriptorHandle envTable = ctx.environment->shadowAndEnvDescriptors[0];
            D3D12_RESOURCE_DESC giDesc = m_giTargets.color->GetDesc();
            const uint32_t giW = ctx.framePlanning->rtPlan.budget.giWidth > 0
                ? std::min(ctx.framePlanning->rtPlan.budget.giWidth, static_cast<uint32_t>(giDesc.Width))
                : static_cast<uint32_t>(giDesc.Width);
            const uint32_t giH = ctx.framePlanning->rtPlan.budget.giHeight > 0
                ? std::min(ctx.framePlanning->rtPlan.budget.giHeight, static_cast<uint32_t>(giDesc.Height))
                : static_cast<uint32_t>(giDesc.Height);
            ctx.rayTracingContext->DispatchGI(
                rtCmdList.Get(),
                ctx.depthSrv,
                m_giTargets.uav,
                ctx.frameConstants,
                envTable,
                giW,
                giH);
        }
    }
}

void RTSubsystem::RenderRayTracedReflections(const RTContext& ctx) {
    auto setReflectionReadinessReason = [&](const char* reason) {
        if (m_reflectionReadiness.reason.empty() && reason) {
            m_reflectionReadiness.reason = reason;
        }
    };

    m_reflectionReadiness.hasPipeline =
        ctx.rayTracingContext && ctx.rayTracingContext->HasReflectionPipeline();
    m_reflectionReadiness.hasTLAS =
        ctx.rayTracingContext && ctx.rayTracingContext->HasTLAS();
    m_reflectionReadiness.hasMaterialBuffer =
        ctx.rayTracingContext && ctx.rayTracingContext->HasRTMaterialBuffer();
    m_reflectionReadiness.hasOutput =
        m_reflectionTargets.color != nullptr && m_reflectionTargets.uav.IsValid();
    m_reflectionReadiness.hasDepth =
        ctx.depthBuffer != nullptr && ctx.depthSrv.IsValid();
    m_reflectionReadiness.hasFrameConstants =
        ctx.frameConstants != 0;
    m_reflectionReadiness.hasDispatchDescriptors =
        ctx.rayTracingContext && ctx.rayTracingContext->HasDispatchDescriptorTables();

    if (!ctx.framePlanning->rtPlan.dispatchReflections || !ctx.rayTracingContext) {
        setReflectionReadinessReason(!ctx.framePlanning->rtPlan.dispatchReflections
            ? "not_scheduled_this_frame"
            : "ray_tracing_context_missing");
        return;
    }

    if (!m_runtime.reflectionsEnabled || !m_reflectionTargets.color || !m_reflectionTargets.uav.IsValid()) {
        setReflectionReadinessReason(!m_runtime.reflectionsEnabled
            ? "feature_disabled"
            : "reflection_output_missing");
        return;
    }

    if (!ctx.rayTracingContext->HasReflectionPipeline()) {
        setReflectionReadinessReason("reflection_pipeline_missing");
        return;
    }

    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    HRESULT hr = ctx.commandList->QueryInterface(IID_PPV_ARGS(&rtCmdList));
    if (FAILED(hr) || !rtCmdList) {
        setReflectionReadinessReason("dxr_command_list_missing");
        return;
    }

    DescriptorHandle normalSrv = ctx.normalRoughnessSrv;
    DescriptorHandle materialExt2Srv{};
    ID3D12Resource* normalResource = ctx.normalRoughness;
    D3D12_RESOURCE_STATES* normalState = ctx.normalRoughnessState;
    ID3D12Resource* materialExt2Resource = nullptr;
    if (ctx.visibilityBufferRenderedThisFrame && ctx.visibilityBuffer) {
        const DescriptorHandle& vbNormal = ctx.visibilityBuffer->GetNormalRoughnessSRVHandle();
        if (vbNormal.IsValid()) {
            normalSrv = vbNormal;
            normalResource = ctx.visibilityBuffer->GetNormalRoughnessBuffer();
            normalState = nullptr;
        }
        const DescriptorHandle& vbMaterialExt2 = ctx.visibilityBuffer->GetMaterialExt2SRVHandle();
        if (vbMaterialExt2.IsValid()) {
            materialExt2Srv = vbMaterialExt2;
            materialExt2Resource = ctx.visibilityBuffer->GetMaterialExt2Buffer();
        }
    }
    m_reflectionReadiness.hasNormalRoughness =
        normalResource != nullptr && normalSrv.IsValid();
    m_reflectionReadiness.hasMaterialExt2 =
        materialExt2Resource != nullptr && materialExt2Srv.IsValid();

    if (!ctx.depthSrv.IsValid() || !normalSrv.IsValid()) {
        setReflectionReadinessReason(!ctx.depthSrv.IsValid()
            ? "depth_srv_missing"
            : "normal_roughness_srv_missing");
        return;
    }

    RTReflectionDispatchPass::PrepareContext prepareContext{};
    prepareContext.commandList = rtCmdList.Get();
    prepareContext.depth = {ctx.depthBuffer, ctx.depthState};
    prepareContext.normalRoughness = {normalResource, normalState};
    prepareContext.reflectionOutput = {m_reflectionTargets.color.Get(), &m_reflectionTargets.colorState};
    prepareContext.transitionNormal = !ctx.visibilityBufferRenderedThisFrame;
    if (!RTReflectionDispatchPass::PrepareInputsAndOutput(prepareContext)) {
        setReflectionReadinessReason("reflection_resource_transition_failed");
        return;
    }

    static bool s_checkedRtReflDebug = false;
    static int  s_rtReflClearMode = 0;
    static bool s_rtReflSkipDispatch = false;
    if (!s_checkedRtReflDebug) {
        s_checkedRtReflDebug = true;
        if (const char* mode = std::getenv("CORTEX_RTREFL_CLEAR")) {
            s_rtReflClearMode = std::atoi(mode);
            if (s_rtReflClearMode != 0) {
                spdlog::warn("Renderer: CORTEX_RTREFL_CLEAR={} set; clearing RT reflection target each frame (0=off,1=black,2=magenta)",
                             s_rtReflClearMode);
            }
        }
        if (std::getenv("CORTEX_RTREFL_SKIP_DXR")) {
            s_rtReflSkipDispatch = true;
            spdlog::warn("Renderer: CORTEX_RTREFL_SKIP_DXR set; skipping DXR reflection dispatch (debug)");
        }
    }

    const bool rtReflDebugView =
        (ctx.debugViewMode == 20u || ctx.debugViewMode == 30u || ctx.debugViewMode == 31u);

    // Optional debug clear to eliminate stale-tile/rectangle artifacts. This also
    // lets debug view 20 validate that the post-process SRV binding (t8) is correct.
    if (rtReflDebugView && s_rtReflClearMode != 0 && ctx.descriptorManager && ctx.device && m_reflectionTargets.uav.IsValid()) {
        DescriptorHandle clearUav = m_reflectionTargets.dispatchClearUAVs[ctx.frameIndex % kFrameCount];
        if (clearUav.IsValid()) {
            RTReflectionDispatchPass::DebugClearContext clearContext{};
            clearContext.commandList = rtCmdList.Get();
            clearContext.device = ctx.device->GetDevice();
            clearContext.descriptorHeap = ctx.descriptorManager->GetCBV_SRV_UAV_Heap();
            clearContext.reflectionOutput = {m_reflectionTargets.color.Get(), &m_reflectionTargets.colorState};
            clearContext.shaderVisibleUav = clearUav;
            clearContext.cpuUav = m_reflectionTargets.uav;
            clearContext.clearMode = s_rtReflClearMode;
            if (!RTReflectionDispatchPass::ClearOutputForDebugView(clearContext)) {
                setReflectionReadinessReason("reflection_debug_clear_failed");
                return;
            }
        } else {
            spdlog::warn("Renderer: RT reflection dispatch debug clear requested before persistent UAV descriptors were initialized");
        }
    }

    if (ctx.environment->ShouldBindImageBasedLightingTextures()) {
        if (const EnvironmentMaps* env = ctx.environment->ActiveEnvironment()) {
            RTReflectionDispatchPass::EnsureTextureNonPixelReadable(rtCmdList.Get(), env->diffuseIrradiance);
            RTReflectionDispatchPass::EnsureTextureNonPixelReadable(rtCmdList.Get(), env->specularPrefiltered);
        }
    }

    // Ensure the descriptor table (space1, t0-t6) is up to date before DXR
    // dispatch. If environments are loaded/evicted asynchronously, the table
    // can otherwise temporarily point at null SRVs.
    ctx.updateEnvironmentTable();

    DescriptorHandle envTable = ctx.environment->shadowAndEnvDescriptors[0];
    D3D12_RESOURCE_DESC reflDesc = m_reflectionTargets.color->GetDesc();
    const uint32_t reflW = ctx.framePlanning->rtPlan.budget.reflectionWidth > 0
        ? std::min(ctx.framePlanning->rtPlan.budget.reflectionWidth, static_cast<uint32_t>(reflDesc.Width))
        : static_cast<uint32_t>(reflDesc.Width);
    const uint32_t reflH = ctx.framePlanning->rtPlan.budget.reflectionHeight > 0
        ? std::min(ctx.framePlanning->rtPlan.budget.reflectionHeight, static_cast<uint32_t>(reflDesc.Height))
        : static_cast<uint32_t>(reflDesc.Height);
    m_reflectionReadiness.hasEnvironmentTable = envTable.IsValid();
    m_reflectionReadiness.dispatchWidth = reflW;
    m_reflectionReadiness.dispatchHeight = reflH;
    m_reflectionReadiness.ready =
        m_reflectionReadiness.hasPipeline &&
        m_reflectionReadiness.hasTLAS &&
        m_reflectionReadiness.hasMaterialBuffer &&
        m_reflectionReadiness.hasOutput &&
        m_reflectionReadiness.hasDepth &&
        m_reflectionReadiness.hasNormalRoughness &&
        m_reflectionReadiness.hasEnvironmentTable &&
        m_reflectionReadiness.hasFrameConstants &&
        m_reflectionReadiness.hasDispatchDescriptors &&
        reflW > 0 &&
        reflH > 0;
    if (!m_reflectionReadiness.ready) {
        setReflectionReadinessReason("reflection_inputs_incomplete");
        return;
    }

    if (!(rtReflDebugView && s_rtReflSkipDispatch)) {
        ctx.rayTracingContext->DispatchReflections(
            rtCmdList.Get(),
            ctx.depthSrv,
            m_reflectionTargets.uav,
            ctx.frameConstants,
            envTable,
            normalSrv,
            materialExt2Srv,
            normalResource,
            materialExt2Resource,
            reflW,
            reflH);
    } else {
        setReflectionReadinessReason("debug_skip_dxr");
    }

    if (!RTReflectionDispatchPass::FinalizeOutputWrites(rtCmdList.Get(), m_reflectionTargets.color.Get())) {
        setReflectionReadinessReason("reflection_output_finalize_failed");
        return;
    }

    *ctx.rtReflectionWrittenThisFrame = true;
    if (m_reflectionReadiness.reason.empty()) {
        m_reflectionReadiness.reason = "ready";
    }
}

void RTSubsystem::ExecuteRTDenoisePass(const char* frameNormalRoughnessResource, const RTContext& ctx) {
    const bool planned =
        ctx.framePlanning->rtPlan.denoiseShadows ||
        ctx.framePlanning->rtPlan.denoiseReflections ||
        ctx.framePlanning->rtPlan.denoiseGI;
    if (!planned) {
        return;
    }

    bool executed = false;
    std::string fallbackReason;

    auto markFallback = [&](const char* reason) {
        if (fallbackReason.empty() && reason) {
            fallbackReason = reason;
        }
    };

    if (!ctx.rtDenoiser || !ctx.rtDenoiser->IsReady() || !ctx.device || !ctx.descriptorManager || !ctx.commandList) {
        markFallback("rt_denoiser_not_ready");
    } else if (!m_denoise.descriptorTablesValid) {
        markFallback("rt_denoiser_descriptor_tables_unavailable");
    }

    DescriptorHandle normalSrv = ctx.normalRoughnessSrv;
    ID3D12Resource* normalResource = ctx.normalRoughness;
    D3D12_RESOURCE_STATES* normalState = ctx.normalRoughnessState;
    VisibilityBufferRenderer::ResourceStateSnapshot vbStates{};
    bool usingVBNormal = false;
    if (ctx.visibilityBufferRenderedThisFrame && ctx.visibilityBuffer && ctx.visibilityBuffer->GetNormalRoughnessBuffer()) {
        const DescriptorHandle& vbNormalSrv = ctx.visibilityBuffer->GetNormalRoughnessSRVHandle();
        if (vbNormalSrv.IsValid()) {
            vbStates = ctx.visibilityBuffer->GetResourceStateSnapshot();
            normalSrv = vbNormalSrv;
            normalResource = ctx.visibilityBuffer->GetNormalRoughnessBuffer();
            normalState = &vbStates.normalRoughness;
            usingVBNormal = true;
        }
    }

    if (!normalSrv.IsValid() || !ctx.depthSrv.IsValid() || !ctx.velocitySrv.IsValid() ||
        !ctx.maskBuiltThisFrame || !ctx.maskSrv.IsValid()) {
        markFallback("rt_denoiser_missing_inputs");
    }

    if (fallbackReason.empty()) {
        RTDenoiser::CommonResourceContext commonResources{};
        commonResources.commandList = ctx.commandList;
        commonResources.depth = {ctx.depthBuffer, ctx.depthState};
        commonResources.normalRoughness = {normalResource, normalState};
        commonResources.velocity = {ctx.velocityBuffer, ctx.velocityState};
        commonResources.temporalMask = {ctx.maskTexture, ctx.maskState};
        if (!RTDenoiser::PrepareCommonResources(commonResources)) {
            markFallback("rt_denoiser_resource_transition_failed");
        }
        if (usingVBNormal && ctx.visibilityBuffer) {
            ctx.visibilityBuffer->ApplyResourceStateSnapshot(vbStates);
        }

        if (!fallbackReason.empty()) {
            ctx.recordFramePass("RTDenoise",
                            planned,
                            executed,
                            m_denoise.passCountThisFrame,
                            {"depth",
                             frameNormalRoughnessResource ? frameNormalRoughnessResource : "gbuffer_normal_roughness",
                             "velocity",
                             "temporal_rejection_mask",
                             "rt_shadow_mask",
                             "rt_shadow_history",
                             "rt_reflection",
                             "rt_reflection_history",
                             "rt_gi",
                             "rt_gi_history"},
                            {"rt_shadow_history",
                             "rt_reflection_history",
                             "rt_gi_history"},
                            true,
                            fallbackReason.c_str());
            return;
        }

        auto dispatchSignal = [&](RTDenoiser::Signal signal,
                                  bool shouldRun,
                                  bool historyValid,
                                  ID3D12Resource* current,
                                  D3D12_RESOURCE_STATES& currentState,
                                  DescriptorHandle currentSRV,
                                  ID3D12Resource* history,
                                  D3D12_RESOURCE_STATES& historyState,
                                  DescriptorHandle historySRV,
                                  DescriptorHandle historyUAV) -> RTDenoiser::DispatchResult {
            RTDenoiser::DispatchResult result{};
            if (!shouldRun) {
                return result;
            }
            if (!current || !history || !currentSRV.IsValid() || !historySRV.IsValid() || !historyUAV.IsValid()) {
                markFallback("rt_denoiser_missing_signal_resource");
                return result;
            }

            uint32_t width = 0;
            uint32_t height = 0;
            GetTextureSize(history, width, height);
            if (width == 0 || height == 0) {
                markFallback("rt_denoiser_invalid_history_size");
                return result;
            }

            RTDenoiser::SignalResourceContext signalResources{};
            signalResources.commandList = ctx.commandList;
            signalResources.current = {current, &currentState};
            signalResources.history = {history, &historyState};
            if (!RTDenoiser::PrepareSignalResources(signalResources)) {
                markFallback("rt_denoiser_signal_transition_failed");
                return result;
            }

            RTDenoiser::DispatchDesc desc{};
            desc.signal = signal;
            desc.historyValid = historyValid;
            desc.width = width;
            desc.height = height;
            desc.frameConstants = ctx.frameConstants;
            desc.currentSRV = currentSRV;
            desc.historySRV = historySRV;
            desc.depthSRV = ctx.depthSrv;
            desc.normalRoughnessSRV = normalSrv;
            desc.velocitySRV = ctx.velocitySrv;
            desc.temporalMaskSRV = ctx.maskSrv;
            desc.historyUAV = historyUAV;
            desc.currentResource = current;
            desc.historyResource = history;
            desc.depthResource = ctx.depthBuffer;
            desc.normalRoughnessResource = normalResource;
            desc.velocityResource = ctx.velocityBuffer;
            desc.temporalMaskResource = ctx.maskTexture;
            desc.srvTable = m_denoise.srvTables[ctx.frameIndex % kFrameCount][0];
            desc.uavTable = m_denoise.uavTables[ctx.frameIndex % kFrameCount][0];
            if (signal == RTDenoiser::Signal::Reflection) {
                desc.accumulationAlpha = m_denoise.reflectionHistoryAlpha;
            }

            result = ctx.rtDenoiser->Dispatch(
                ctx.commandList,
                ctx.device->GetDevice(),
                ctx.descriptorManager,
                desc);

            if (result.executed) {
                if (!RTDenoiser::FinalizeSignalResources(signalResources)) {
                    markFallback("rt_denoiser_signal_finalize_failed");
                    return result;
                }

                executed = true;
                m_denoise.executedThisFrame = true;
                ++m_denoise.passCountThisFrame;
                m_denoise.usedDepthNormalRejectionThisFrame =
                    m_denoise.usedDepthNormalRejectionThisFrame || result.usedDepthNormalRejection;
                m_denoise.usedVelocityThisFrame =
                    m_denoise.usedVelocityThisFrame || result.usedVelocityReprojection;
                m_denoise.usedDisocclusionRejectionThisFrame =
                    m_denoise.usedDisocclusionRejectionThisFrame || result.usedDisocclusionRejection;
            }
            return result;
        };

        const RTDenoiser::DispatchResult shadowResult = dispatchSignal(
            RTDenoiser::Signal::Shadow,
            ctx.framePlanning->rtPlan.denoiseShadows,
            ctx.historyManager->CanReproject(TemporalHistoryId::RTShadow),
            m_shadowTargets.mask.Get(),
            m_shadowTargets.maskState,
            m_shadowTargets.maskSRV,
            m_shadowTargets.history.Get(),
            m_shadowTargets.historyState,
            m_shadowTargets.historySRV,
            m_shadowTargets.historyUAV);
        if (shadowResult.executed) {
            m_denoise.shadowDenoisedThisFrame = true;
            m_denoise.shadowAlpha = shadowResult.accumulationAlpha;
            MarkRTShadowHistoryValid(*ctx.historyManager, ctx.renderFrameCounter);
        }

        const RTDenoiser::DispatchResult reflectionResult = dispatchSignal(
            RTDenoiser::Signal::Reflection,
            ctx.framePlanning->rtPlan.denoiseReflections,
            ctx.historyManager->CanReproject(TemporalHistoryId::RTReflection),
            m_reflectionTargets.color.Get(),
            m_reflectionTargets.colorState,
            m_reflectionTargets.srv,
            m_reflectionTargets.history.Get(),
            m_reflectionTargets.historyState,
            m_reflectionTargets.historySRV,
            m_reflectionTargets.historyUAV);
        if (reflectionResult.executed) {
            m_denoise.reflectionDenoisedThisFrame = true;
            m_denoise.reflectionAlpha = reflectionResult.accumulationAlpha;
            MarkRTReflectionHistoryValid(*ctx.historyManager, ctx.renderFrameCounter);
        }

        const RTDenoiser::DispatchResult giResult = dispatchSignal(
            RTDenoiser::Signal::GI,
            ctx.framePlanning->rtPlan.denoiseGI,
            ctx.historyManager->CanReproject(TemporalHistoryId::RTGI),
            m_giTargets.color.Get(),
            m_giTargets.colorState,
            m_giTargets.srv,
            m_giTargets.history.Get(),
            m_giTargets.historyState,
            m_giTargets.historySRV,
            m_giTargets.historyUAV);
        if (giResult.executed) {
            m_denoise.giDenoisedThisFrame = true;
            m_denoise.giAlpha = giResult.accumulationAlpha;
            MarkRTGIHistoryValid(*ctx.historyManager, ctx.renderFrameCounter);
        }
    }

    ctx.recordFramePass("RTDenoise",
                    planned,
                    executed,
                    m_denoise.passCountThisFrame,
                    {"depth",
                     frameNormalRoughnessResource ? frameNormalRoughnessResource : "gbuffer_normal_roughness",
                     "velocity",
                     "temporal_rejection_mask",
                     "rt_shadow_mask",
                     "rt_shadow_history",
                     "rt_reflection",
                     "rt_reflection_history",
                     "rt_gi",
                     "rt_gi_history"},
                    {"rt_shadow_history",
                     "rt_reflection_history",
                     "rt_gi_history"},
                    !fallbackReason.empty(),
                    fallbackReason.empty() ? nullptr : fallbackReason.c_str());
}

void RTSubsystem::CaptureRTReflectionSignalStats(const RTContext& ctx) {
    if (!*ctx.rtReflectionWrittenThisFrame ||
        !ctx.rtReflectionSignalStats || !ctx.rtReflectionSignalStats->IsReady() ||
        !m_reflectionTargets.color || !m_reflectionTargets.srv.IsValid() ||
        !m_reflectionSignal.rawResources.statsBuffer ||
        !m_reflectionSignal.rawResources.statsUAV.IsValid() ||
        ctx.frameIndex >= kFrameCount ||
        !m_reflectionSignal.rawResources.readback[ctx.frameIndex] ||
        !ctx.device || !ctx.descriptorManager || !ctx.commandList) {
        return;
    }
    if (!m_reflectionSignal.descriptors.valid ||
        !m_reflectionSignal.descriptors.srvTables[ctx.frameIndex % kFrameCount][0].IsValid() ||
        !m_reflectionSignal.descriptors.uavTables[ctx.frameIndex % kFrameCount][0].IsValid()) {
        return;
    }

    const D3D12_RESOURCE_DESC reflectionDesc = m_reflectionTargets.color->GetDesc();
    const uint32_t width = static_cast<uint32_t>(reflectionDesc.Width);
    const uint32_t height = reflectionDesc.Height;
    if (width == 0 || height == 0) {
        return;
    }

    RTReflectionSignalStats::CaptureResources captureResources{};
    captureResources.commandList = ctx.commandList;
    captureResources.reflectionResource = m_reflectionTargets.color.Get();
    captureResources.reflectionState = &m_reflectionTargets.colorState;
    captureResources.statsResource = m_reflectionSignal.rawResources.statsBuffer.Get();
    captureResources.statsState = &m_reflectionSignal.rawResources.statsResourceState;
    captureResources.readbackResource =
        m_reflectionSignal.rawResources.readback[ctx.frameIndex].Get();
    if (!RTReflectionSignalStats::PrepareCaptureResources(captureResources)) {
        return;
    }

    RTReflectionSignalStats::DispatchDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.target = RTReflectionSignalStats::SignalTarget::Raw;
    desc.reflectionSRV = m_reflectionTargets.srv;
    desc.statsUAV = m_reflectionSignal.rawResources.statsUAV;
    desc.reflectionResource = m_reflectionTargets.color.Get();
    desc.statsResource = m_reflectionSignal.rawResources.statsBuffer.Get();
    desc.srvTable = m_reflectionSignal.descriptors.srvTables[ctx.frameIndex % kFrameCount][0];
    desc.uavTable = m_reflectionSignal.descriptors.uavTables[ctx.frameIndex % kFrameCount][0];

    const bool executed = ctx.rtReflectionSignalStats->Dispatch(
        ctx.commandList,
        ctx.device->GetDevice(),
        ctx.descriptorManager,
        desc);
    if (!executed) {
        return;
    }

    if (!RTReflectionSignalStats::FinalizeCaptureReadback(captureResources)) {
        return;
    }

    m_reflectionSignal.rawResources.readbackPending[ctx.frameIndex] = true;
    m_reflectionSignal.rawResources.sampleFrame[ctx.frameIndex] = ctx.renderFrameCounter;
    m_reflectionSignal.rawCapturedThisFrame = true;

    ctx.recordFramePass("RTReflectionSignalStats",
                    true,
                    true,
                    0,
                    {"rt_reflection"},
                    {"rt_reflection_signal_stats"},
                    false,
                    nullptr);
}

void RTSubsystem::CaptureRTReflectionHistorySignalStats(const RTContext& ctx) {
    if (!m_denoise.reflectionDenoisedThisFrame ||
        !ctx.rtReflectionSignalStats || !ctx.rtReflectionSignalStats->IsReady() ||
        !m_reflectionTargets.history || !m_reflectionTargets.historySRV.IsValid() ||
        !m_reflectionSignal.historyResources.statsBuffer ||
        !m_reflectionSignal.historyResources.statsUAV.IsValid() ||
        ctx.frameIndex >= kFrameCount ||
        !m_reflectionSignal.historyResources.readback[ctx.frameIndex] ||
        !ctx.device || !ctx.descriptorManager || !ctx.commandList) {
        return;
    }
    if (!m_reflectionSignal.descriptors.valid ||
        !m_reflectionSignal.descriptors.srvTables[ctx.frameIndex % kFrameCount][0].IsValid() ||
        !m_reflectionSignal.descriptors.uavTables[ctx.frameIndex % kFrameCount][0].IsValid()) {
        return;
    }

    const D3D12_RESOURCE_DESC reflectionDesc = m_reflectionTargets.history->GetDesc();
    const uint32_t width = static_cast<uint32_t>(reflectionDesc.Width);
    const uint32_t height = reflectionDesc.Height;
    if (width == 0 || height == 0) {
        return;
    }

    RTReflectionSignalStats::CaptureResources captureResources{};
    captureResources.commandList = ctx.commandList;
    captureResources.reflectionResource = m_reflectionTargets.history.Get();
    captureResources.reflectionState = &m_reflectionTargets.historyState;
    captureResources.statsResource = m_reflectionSignal.historyResources.statsBuffer.Get();
    captureResources.statsState = &m_reflectionSignal.historyResources.statsResourceState;
    captureResources.readbackResource =
        m_reflectionSignal.historyResources.readback[ctx.frameIndex].Get();
    if (!RTReflectionSignalStats::PrepareCaptureResources(captureResources)) {
        return;
    }

    RTReflectionSignalStats::DispatchDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.target = RTReflectionSignalStats::SignalTarget::History;
    desc.reflectionSRV = m_reflectionTargets.historySRV;
    desc.statsUAV = m_reflectionSignal.historyResources.statsUAV;
    desc.reflectionResource = m_reflectionTargets.history.Get();
    desc.statsResource = m_reflectionSignal.historyResources.statsBuffer.Get();
    desc.srvTable = m_reflectionSignal.descriptors.srvTables[ctx.frameIndex % kFrameCount][0];
    desc.uavTable = m_reflectionSignal.descriptors.uavTables[ctx.frameIndex % kFrameCount][0];

    const bool executed = ctx.rtReflectionSignalStats->Dispatch(
        ctx.commandList,
        ctx.device->GetDevice(),
        ctx.descriptorManager,
        desc);
    if (!executed) {
        return;
    }

    if (!RTReflectionSignalStats::FinalizeCaptureReadback(captureResources)) {
        return;
    }

    m_reflectionSignal.historyResources.readbackPending[ctx.frameIndex] = true;
    m_reflectionSignal.historyResources.sampleFrame[ctx.frameIndex] = ctx.renderFrameCounter;
    m_reflectionSignal.historyCapturedThisFrame = true;

    ctx.recordFramePass("RTReflectionHistorySignalStats",
                    true,
                    true,
                    0,
                    {"rt_reflection_history"},
                    {"rt_reflection_history_signal_stats"},
                    false,
                    nullptr);
}

void RTSubsystem::UpdateRTReflectionSignalStatsFromReadback(const RTContext& ctx) {
    if (ctx.frameIndex >= kFrameCount) {
        return;
    }

    constexpr float kStatsScale = 256.0f;
    const D3D12_RANGE readRange{0, RTReflectionSignalStats::kStatsBytes};
    auto readStats = [&](ID3D12Resource* readback,
                         bool& pending,
                         const char* label,
                         uint32_t& pixels,
                         float& avgLuma,
                         float& maxLuma,
                         float& nonZeroRatio,
                         float& brightRatio,
                         float& outlierRatio) -> bool {
        if (!pending || !readback) {
            return false;
        }

        auto mappedReadback = ReadbackBuffer::MapRange(readback, readRange, label);
        if (!mappedReadback.IsValid()) {
            pending = false;
            return false;
        }
        const uint32_t* mapped = mappedReadback.As<const uint32_t>();

        const uint32_t lumaSumQ = mapped[0];
        const uint32_t nonZero = mapped[1];
        const uint32_t bright = mapped[2];
        const uint32_t mappedPixels = mapped[3];
        const uint32_t maxLumaQ = mapped[4];
        const uint32_t outliers = mapped[5];
        mappedReadback.Reset();
        pending = false;

        pixels = mappedPixels;
        if (pixels == 0) {
            avgLuma = 0.0f;
            maxLuma = 0.0f;
            nonZeroRatio = 0.0f;
            brightRatio = 0.0f;
            outlierRatio = 0.0f;
            return false;
        }

        const float pixelDenom = static_cast<float>(pixels);
        avgLuma = std::max(0.0f, static_cast<float>(lumaSumQ) / (pixelDenom * kStatsScale));
        maxLuma = std::max(0.0f, static_cast<float>(maxLumaQ) / kStatsScale);
        nonZeroRatio = std::clamp(static_cast<float>(nonZero) / pixelDenom, 0.0f, 1.0f);
        brightRatio = std::clamp(static_cast<float>(bright) / pixelDenom, 0.0f, 1.0f);
        outlierRatio = std::clamp(static_cast<float>(outliers) / pixelDenom, 0.0f, 1.0f);
        return true;
    };

    uint32_t pixels = 0;
    float avgLuma = 0.0f;
    float maxLuma = 0.0f;
    float nonZeroRatio = 0.0f;
    float brightRatio = 0.0f;
    float outlierRatio = 0.0f;
    const bool rawHadPendingReadback =
        m_reflectionSignal.rawResources.readbackPending[ctx.frameIndex] &&
        m_reflectionSignal.rawResources.readback[ctx.frameIndex];
    const bool rawValid = readStats(
        m_reflectionSignal.rawResources.readback[ctx.frameIndex].Get(),
        m_reflectionSignal.rawResources.readbackPending[ctx.frameIndex],
        "RT reflection signal stats",
        pixels,
        avgLuma,
        maxLuma,
        nonZeroRatio,
        brightRatio,
        outlierRatio);
    if (rawValid) {
        m_reflectionSignal.raw.valid = true;
        m_reflectionSignal.raw.sampleFrame =
            m_reflectionSignal.rawResources.sampleFrame[ctx.frameIndex];
        m_reflectionSignal.raw.pixelCount = pixels;
        m_reflectionSignal.raw.avgLuma = avgLuma;
        m_reflectionSignal.raw.maxLuma = maxLuma;
        m_reflectionSignal.raw.nonZeroRatio = nonZeroRatio;
        m_reflectionSignal.raw.brightRatio = brightRatio;
        m_reflectionSignal.raw.outlierRatio = outlierRatio;
        m_reflectionSignal.raw.readbackLatencyFrames =
            (ctx.renderFrameCounter >= m_reflectionSignal.raw.sampleFrame)
                ? static_cast<uint32_t>(ctx.renderFrameCounter - m_reflectionSignal.raw.sampleFrame)
                : 0u;
    } else if (rawHadPendingReadback && pixels == 0) {
        m_reflectionSignal.raw.valid = false;
        m_reflectionSignal.raw.pixelCount = 0;
    }

    uint32_t historyPixels = 0;
    float historyAvgLuma = 0.0f;
    float historyMaxLuma = 0.0f;
    float historyNonZeroRatio = 0.0f;
    float historyBrightRatio = 0.0f;
    float historyOutlierRatio = 0.0f;
    const bool historyHadPendingReadback =
        m_reflectionSignal.historyResources.readbackPending[ctx.frameIndex] &&
        m_reflectionSignal.historyResources.readback[ctx.frameIndex];
    const bool historyValid = readStats(
        m_reflectionSignal.historyResources.readback[ctx.frameIndex].Get(),
        m_reflectionSignal.historyResources.readbackPending[ctx.frameIndex],
        "RT reflection history signal stats",
        historyPixels,
        historyAvgLuma,
        historyMaxLuma,
        historyNonZeroRatio,
        historyBrightRatio,
        historyOutlierRatio);
    if (historyValid) {
        m_reflectionSignal.history.valid = true;
        m_reflectionSignal.history.sampleFrame =
            m_reflectionSignal.historyResources.sampleFrame[ctx.frameIndex];
        m_reflectionSignal.history.pixelCount = historyPixels;
        m_reflectionSignal.history.avgLuma = historyAvgLuma;
        m_reflectionSignal.history.maxLuma = historyMaxLuma;
        m_reflectionSignal.history.nonZeroRatio = historyNonZeroRatio;
        m_reflectionSignal.history.brightRatio = historyBrightRatio;
        m_reflectionSignal.history.outlierRatio = historyOutlierRatio;
        m_reflectionSignal.history.readbackLatencyFrames =
            (ctx.renderFrameCounter >= m_reflectionSignal.history.sampleFrame)
                ? static_cast<uint32_t>(ctx.renderFrameCounter - m_reflectionSignal.history.sampleFrame)
                : 0u;
    } else if (historyHadPendingReadback && historyPixels == 0) {
        m_reflectionSignal.history.valid = false;
        m_reflectionSignal.history.pixelCount = 0;
    }

    if (m_reflectionSignal.raw.valid && m_reflectionSignal.history.valid) {
        m_reflectionSignal.history.avgLumaDelta =
            m_reflectionSignal.history.avgLuma - m_reflectionSignal.raw.avgLuma;
    }
}

} // namespace Cortex::Graphics
