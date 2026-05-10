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

void Renderer::RenderRayTracedReflections() {
    auto setReflectionReadinessReason = [&](const char* reason) {
        if (m_rtReflectionReadiness.reason.empty() && reason) {
            m_rtReflectionReadiness.reason = reason;
        }
    };

    m_rtReflectionReadiness.hasPipeline =
        m_services.rayTracingContext && m_services.rayTracingContext->HasReflectionPipeline();
    m_rtReflectionReadiness.hasTLAS =
        m_services.rayTracingContext && m_services.rayTracingContext->HasTLAS();
    m_rtReflectionReadiness.hasMaterialBuffer =
        m_services.rayTracingContext && m_services.rayTracingContext->HasRTMaterialBuffer();
    m_rtReflectionReadiness.hasOutput =
        m_rtReflectionTargets.color != nullptr && m_rtReflectionTargets.uav.IsValid();
    m_rtReflectionReadiness.hasDepth =
        m_depthResources.buffer != nullptr && m_depthResources.srv.IsValid();
    m_rtReflectionReadiness.hasFrameConstants =
        m_constantBuffers.currentFrameGPU != 0;
    m_rtReflectionReadiness.hasDispatchDescriptors =
        m_services.rayTracingContext && m_services.rayTracingContext->HasDispatchDescriptorTables();

    if (!m_framePlanning.rtPlan.dispatchReflections || !m_services.rayTracingContext) {
        setReflectionReadinessReason(!m_framePlanning.rtPlan.dispatchReflections
            ? "not_scheduled_this_frame"
            : "ray_tracing_context_missing");
        return;
    }

    if (!m_rtRuntimeState.reflectionsEnabled || !m_rtReflectionTargets.color || !m_rtReflectionTargets.uav.IsValid()) {
        setReflectionReadinessReason(!m_rtRuntimeState.reflectionsEnabled
            ? "feature_disabled"
            : "reflection_output_missing");
        return;
    }

    if (!m_services.rayTracingContext->HasReflectionPipeline()) {
        setReflectionReadinessReason("reflection_pipeline_missing");
        return;
    }

    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    HRESULT hr = m_commandResources.graphicsList.As(&rtCmdList);
    if (FAILED(hr) || !rtCmdList) {
        setReflectionReadinessReason("dxr_command_list_missing");
        return;
    }

    // Ensure the depth buffer is in a readable state for the DXR pass.
    // Depth resources should include DEPTH_READ when sampled as SRVs.
    if (m_depthResources.buffer && m_depthResources.resourceState != kDepthSampleState) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthResources.buffer.Get();
        depthBarrier.Transition.StateBefore = m_depthResources.resourceState;
        depthBarrier.Transition.StateAfter = kDepthSampleState;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rtCmdList->ResourceBarrier(1, &depthBarrier);
        m_depthResources.resourceState = kDepthSampleState;
    }

    constexpr D3D12_RESOURCE_STATES kSrvNonPixel =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    DescriptorHandle normalSrv = m_mainTargets.gbufferNormalRoughnessSRV;
    DescriptorHandle materialExt2Srv{};
    ID3D12Resource* normalResource = m_mainTargets.gbufferNormalRoughness.Get();
    ID3D12Resource* materialExt2Resource = nullptr;
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer) {
        const DescriptorHandle& vbNormal = m_services.visibilityBuffer->GetNormalRoughnessSRVHandle();
        if (vbNormal.IsValid()) {
            normalSrv = vbNormal;
            normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
        }
        const DescriptorHandle& vbMaterialExt2 = m_services.visibilityBuffer->GetMaterialExt2SRVHandle();
        if (vbMaterialExt2.IsValid()) {
            materialExt2Srv = vbMaterialExt2;
            materialExt2Resource = m_services.visibilityBuffer->GetMaterialExt2Buffer();
        }
    }
    m_rtReflectionReadiness.hasNormalRoughness =
        normalResource != nullptr && normalSrv.IsValid();
    m_rtReflectionReadiness.hasMaterialExt2 =
        materialExt2Resource != nullptr && materialExt2Srv.IsValid();

    // Ensure the current frame's normal/roughness target is readable. The VB
    // path leaves its G-buffer in a combined SRV state after deferred lighting.
    if (!m_visibilityBufferState.renderedThisFrame) {
        if (m_mainTargets.gbufferNormalRoughness && m_mainTargets.gbufferNormalRoughnessState != kSrvNonPixel) {
            D3D12_RESOURCE_BARRIER gbufBarrier{};
            gbufBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            gbufBarrier.Transition.pResource = m_mainTargets.gbufferNormalRoughness.Get();
            gbufBarrier.Transition.StateBefore = m_mainTargets.gbufferNormalRoughnessState;
            gbufBarrier.Transition.StateAfter = kSrvNonPixel;
            gbufBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            rtCmdList->ResourceBarrier(1, &gbufBarrier);
            m_mainTargets.gbufferNormalRoughnessState = kSrvNonPixel;
        }
    }

    if (!m_depthResources.srv.IsValid() || !normalSrv.IsValid()) {
        setReflectionReadinessReason(!m_depthResources.srv.IsValid()
            ? "depth_srv_missing"
            : "normal_roughness_srv_missing");
        return;
    }

    if (m_rtReflectionTargets.colorState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER reflBarrier{};
        reflBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        reflBarrier.Transition.pResource = m_rtReflectionTargets.color.Get();
        reflBarrier.Transition.StateBefore = m_rtReflectionTargets.colorState;
        reflBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        reflBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rtCmdList->ResourceBarrier(1, &reflBarrier);
        m_rtReflectionTargets.colorState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
        (m_debugViewState.mode == 20u || m_debugViewState.mode == 30u || m_debugViewState.mode == 31u);

    // Optional debug clear to eliminate stale-tile/rectangle artifacts. This also
    // lets debug view 20 validate that the post-process SRV binding (t8) is correct.
    if (rtReflDebugView && s_rtReflClearMode != 0 && m_services.descriptorManager && m_services.device && m_rtReflectionTargets.uav.IsValid()) {
        DescriptorHandle clearUav = m_rtReflectionTargets.dispatchClearUAVs[m_frameRuntime.frameIndex % kFrameCount];
        if (clearUav.IsValid()) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            m_services.device->GetDevice()->CreateUnorderedAccessView(
                m_rtReflectionTargets.color.Get(),
                nullptr,
                &uavDesc,
                clearUav.cpu);

            ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
            rtCmdList->SetDescriptorHeaps(1, heaps);

            const float magenta[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
            const float black[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };
            const float* clear = (s_rtReflClearMode == 2) ? magenta : black;
            // ClearUnorderedAccessView requires a CPU-visible, CPU-readable descriptor handle.
            // Use the persistent staging UAV as the CPU handle and the persistent shader-visible
            // descriptor as the GPU handle.
            rtCmdList->ClearUnorderedAccessViewFloat(
                clearUav.gpu,
                m_rtReflectionTargets.uav.cpu,
                m_rtReflectionTargets.color.Get(),
                clear,
                0,
                nullptr);

            D3D12_RESOURCE_BARRIER clearBarrier{};
            clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            clearBarrier.UAV.pResource = m_rtReflectionTargets.color.Get();
            rtCmdList->ResourceBarrier(1, &clearBarrier);
        } else {
            spdlog::warn("Renderer: RT reflection dispatch debug clear requested before persistent UAV descriptors were initialized");
        }
    }

    // RT dispatch samples the environment textures via "compute" access, so
    // environment maps must be readable as NON_PIXEL shader resources. The
    // raster path typically leaves them in PIXEL_SHADER_RESOURCE only.
    auto ensureTextureNonPixelReadable = [&](const std::shared_ptr<DX12Texture>& tex) {
        if (!tex || !tex->GetResource()) {
            return;
        }
        constexpr D3D12_RESOURCE_STATES kDesired =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES current = tex->GetCurrentState();
        if ((current & kDesired) == kDesired) {
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = tex->GetResource();
        barrier.Transition.StateBefore = current;
        barrier.Transition.StateAfter = kDesired;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rtCmdList->ResourceBarrier(1, &barrier);
        tex->SetState(kDesired);
    };

    if (!m_environmentState.maps.empty()) {
        size_t envIndex = m_environmentState.currentIndex;
        if (envIndex >= m_environmentState.maps.size()) {
            envIndex = 0;
        }
        const EnvironmentMaps& env = m_environmentState.maps[envIndex];
        ensureTextureNonPixelReadable(env.diffuseIrradiance);
        ensureTextureNonPixelReadable(env.specularPrefiltered);
    }

    // Ensure the descriptor table (space1, t0-t6) is up to date before DXR
    // dispatch. If environments are loaded/evicted asynchronously, the table
    // can otherwise temporarily point at null SRVs.
    UpdateEnvironmentDescriptorTable();

    DescriptorHandle envTable = m_environmentState.shadowAndEnvDescriptors[0];
    D3D12_RESOURCE_DESC reflDesc = m_rtReflectionTargets.color->GetDesc();
    const uint32_t reflW = m_framePlanning.rtPlan.budget.reflectionWidth > 0
        ? std::min(m_framePlanning.rtPlan.budget.reflectionWidth, static_cast<uint32_t>(reflDesc.Width))
        : static_cast<uint32_t>(reflDesc.Width);
    const uint32_t reflH = m_framePlanning.rtPlan.budget.reflectionHeight > 0
        ? std::min(m_framePlanning.rtPlan.budget.reflectionHeight, static_cast<uint32_t>(reflDesc.Height))
        : static_cast<uint32_t>(reflDesc.Height);
    m_rtReflectionReadiness.hasEnvironmentTable = envTable.IsValid();
    m_rtReflectionReadiness.dispatchWidth = reflW;
    m_rtReflectionReadiness.dispatchHeight = reflH;
    m_rtReflectionReadiness.ready =
        m_rtReflectionReadiness.hasPipeline &&
        m_rtReflectionReadiness.hasTLAS &&
        m_rtReflectionReadiness.hasMaterialBuffer &&
        m_rtReflectionReadiness.hasOutput &&
        m_rtReflectionReadiness.hasDepth &&
        m_rtReflectionReadiness.hasNormalRoughness &&
        m_rtReflectionReadiness.hasEnvironmentTable &&
        m_rtReflectionReadiness.hasFrameConstants &&
        m_rtReflectionReadiness.hasDispatchDescriptors &&
        reflW > 0 &&
        reflH > 0;
    if (!m_rtReflectionReadiness.ready) {
        setReflectionReadinessReason("reflection_inputs_incomplete");
        return;
    }

    if (!(rtReflDebugView && s_rtReflSkipDispatch)) {
        m_services.rayTracingContext->DispatchReflections(
            rtCmdList.Get(),
            m_depthResources.srv,
            m_rtReflectionTargets.uav,
            m_constantBuffers.currentFrameGPU,
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

    // Ensure UAV writes are visible before post-process samples the SRV.
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_rtReflectionTargets.color.Get();
    rtCmdList->ResourceBarrier(1, &uavBarrier);

    m_frameLifecycle.rtReflectionWrittenThisFrame = true;
    if (m_rtReflectionReadiness.reason.empty()) {
        m_rtReflectionReadiness.reason = "ready";
    }
}

} // namespace Cortex::Graphics
