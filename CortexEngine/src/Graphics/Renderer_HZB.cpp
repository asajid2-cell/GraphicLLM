#include "Renderer.h"

#include "Passes/HZBPass.h"
#include "RenderGraph.h"
#include <spdlog/spdlog.h>
#include <span>

namespace Cortex::Graphics {

Result<void> Renderer::CreateHZBResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_depthResources.resources.buffer) {
        return Result<void>::Err("CreateHZBResources: renderer not initialized or depth buffer missing");
    }

    return HZBPass::CreateResources({
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        m_depthResources.resources.buffer.Get(),
        m_depthResources.descriptors.srv,
        &m_hzbResources
    });
}

void Renderer::BuildHZBFromDepth() {
    if (!m_services.device || !m_commandResources.graphicsList || !m_services.descriptorManager) {
        return;
    }
    if (!m_pipelineState.computeRootSignature || !m_pipelineState.hzbInit || !m_pipelineState.hzbDownsample) {
        return;
    }
    if (!m_depthResources.resources.buffer || !m_depthResources.descriptors.srv.IsValid()) {
        return;
    }

    auto resResult = CreateHZBResources();
    if (resResult.IsErr()) {
        spdlog::warn("BuildHZBFromDepth: {}", resResult.Error());
        return;
    }
    if (!m_hzbResources.resources.texture || m_hzbResources.resources.mipCount == 0 || m_hzbResources.descriptors.mipSRVStaging.size() != m_hzbResources.resources.mipCount ||
        m_hzbResources.descriptors.mipUAVStaging.size() != m_hzbResources.resources.mipCount) {
        return;
    }

    const auto& srvTable = m_hzbResources.descriptors.dispatchSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    const auto& uavTable = m_hzbResources.descriptors.dispatchUavTables[m_frameRuntime.frameIndex % kFrameCount];
    if (!HZBPass::BuildFromDepth({
            m_commandResources.graphicsList.Get(),
            m_services.descriptorManager.get(),
            {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState},
            {m_hzbResources.resources.texture.Get(), &m_hzbResources.resources.resourceState},
            m_pipelineState.singleSrvUavComputeRootSignature.Get(),
            m_pipelineState.computeRootSignature.get(),
            m_pipelineState.hzbInit.get(),
            m_pipelineState.hzbDownsample.get(),
            m_constantBuffers.currentFrameGPU,
            m_depthResources.descriptors.srv,
            std::span<const DescriptorHandle>(m_hzbResources.descriptors.mipSRVStaging.data(), m_hzbResources.descriptors.mipSRVStaging.size()),
            std::span<const DescriptorHandle>(m_hzbResources.descriptors.mipUAVStaging.data(), m_hzbResources.descriptors.mipUAVStaging.size()),
            std::span<const DescriptorHandle>(srvTable.data(), srvTable.size()),
            std::span<const DescriptorHandle>(uavTable.data(), uavTable.size()),
            m_hzbResources.descriptors.dispatchTablesValid,
            m_hzbResources.resources.width,
            m_hzbResources.resources.height,
            m_hzbResources.resources.mipCount,
        })) {
        return;
    }
    m_hzbResources.resources.valid = true;

    m_hzbResources.capture.captureViewMatrix = m_constantBuffers.frameCPU.viewMatrix;
    m_hzbResources.capture.captureViewProjMatrix = m_constantBuffers.frameCPU.viewProjectionMatrix;
    m_hzbResources.capture.captureCameraPosWS = m_cameraState.positionWS;
    m_hzbResources.capture.captureCameraForwardWS = glm::normalize(m_cameraState.forwardWS);
    m_hzbResources.capture.captureNearPlane = m_cameraState.nearPlane;
    m_hzbResources.capture.captureFarPlane = m_cameraState.farPlane;
    m_hzbResources.capture.captureFrameCounter = m_frameLifecycle.renderFrameCounter;
    m_hzbResources.capture.captureValid = true;
}

void Renderer::AddHZBFromDepthPasses_RG(RenderGraph& graph, RGResourceHandle depthHandle, RGResourceHandle hzbHandle) {
    if (!m_services.device || !m_services.descriptorManager || !m_pipelineState.computeRootSignature ||
        !m_pipelineState.hzbInit || !m_pipelineState.hzbDownsample) {
        return;
    }

    const auto& srvTable = m_hzbResources.descriptors.dispatchSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    const auto& uavTable = m_hzbResources.descriptors.dispatchUavTables[m_frameRuntime.frameIndex % kFrameCount];
    HZBPass::AddFromDepth(
        graph,
        depthHandle,
        hzbHandle,
        {
            m_services.descriptorManager.get(),
            m_pipelineState.singleSrvUavComputeRootSignature.Get(),
            m_pipelineState.computeRootSignature.get(),
            m_pipelineState.hzbInit.get(),
            m_pipelineState.hzbDownsample.get(),
            m_constantBuffers.currentFrameGPU,
            std::span<const DescriptorHandle>(srvTable.data(), srvTable.size()),
            std::span<const DescriptorHandle>(uavTable.data(), uavTable.size()),
            m_hzbResources.resources.width,
            m_hzbResources.resources.height,
            m_hzbResources.resources.mipCount,
        });
}

} // namespace Cortex::Graphics
