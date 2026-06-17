#include "Graphics/Subsystems/HZBSubsystem.h"

#include "Graphics/Passes/HZBPass.h"
#include "Graphics/Renderer_ConstantBuffer.h"

#include <spdlog/spdlog.h>

#include <span>

namespace Cortex::Graphics {

Result<void> HZBSubsystem::CreateResources(const HZBContext& ctx) {
    if (!ctx.device || !ctx.descriptorManager || !ctx.depthBuffer) {
        return Result<void>::Err("CreateHZBResources: renderer not initialized or depth buffer missing");
    }

    return HZBPass::CreateResources({
        ctx.device,
        ctx.descriptorManager,
        ctx.depthBuffer,
        ctx.depthSrv,
        &m_state
    });
}

void HZBSubsystem::BuildFromDepth(const HZBContext& ctx) {
    if (!ctx.device || !ctx.commandList || !ctx.descriptorManager) {
        return;
    }
    if (!ctx.fallbackRootSignature || !ctx.initPipeline || !ctx.downsamplePipeline) {
        return;
    }
    if (!ctx.depthBuffer || !ctx.depthSrv.IsValid()) {
        return;
    }

    auto resResult = CreateResources(ctx);
    if (resResult.IsErr()) {
        spdlog::warn("BuildHZBFromDepth: {}", resResult.Error());
        return;
    }
    if (!m_state.resources.texture || m_state.resources.mipCount == 0 ||
        m_state.descriptors.mipSRVStaging.size() != m_state.resources.mipCount ||
        m_state.descriptors.mipUAVStaging.size() != m_state.resources.mipCount) {
        return;
    }

    const auto& srvTable = m_state.descriptors.dispatchSrvTables[ctx.frameIndex % kFrameCount];
    const auto& uavTable = m_state.descriptors.dispatchUavTables[ctx.frameIndex % kFrameCount];
    if (!HZBPass::BuildFromDepth({
            ctx.commandList,
            ctx.descriptorManager,
            {ctx.depthBuffer, ctx.depthState},
            {m_state.resources.texture.Get(), &m_state.resources.resourceState},
            ctx.compactRootSignature,
            ctx.fallbackRootSignature,
            ctx.initPipeline,
            ctx.downsamplePipeline,
            ctx.frameConstants,
            ctx.depthSrv,
            std::span<const DescriptorHandle>(m_state.descriptors.mipSRVStaging.data(), m_state.descriptors.mipSRVStaging.size()),
            std::span<const DescriptorHandle>(m_state.descriptors.mipUAVStaging.data(), m_state.descriptors.mipUAVStaging.size()),
            std::span<const DescriptorHandle>(srvTable.data(), srvTable.size()),
            std::span<const DescriptorHandle>(uavTable.data(), uavTable.size()),
            m_state.descriptors.dispatchTablesValid,
            m_state.resources.width,
            m_state.resources.height,
            m_state.resources.mipCount,
        })) {
        return;
    }
    m_state.resources.valid = true;

    m_state.capture.captureViewMatrix = ctx.captureViewMatrix;
    m_state.capture.captureViewProjMatrix = ctx.captureViewProjMatrix;
    m_state.capture.captureCameraPosWS = ctx.captureCameraPosWS;
    m_state.capture.captureCameraForwardWS = glm::normalize(ctx.captureCameraForwardWS);
    m_state.capture.captureNearPlane = ctx.captureNearPlane;
    m_state.capture.captureFarPlane = ctx.captureFarPlane;
    m_state.capture.captureFrameCounter = ctx.captureFrameCounter;
    m_state.capture.captureValid = true;
}

void HZBSubsystem::AddFromDepthPasses(RenderGraph& graph,
                                      RGResourceHandle depthHandle,
                                      RGResourceHandle hzbHandle,
                                      const HZBContext& ctx) {
    if (!ctx.device || !ctx.descriptorManager || !ctx.fallbackRootSignature ||
        !ctx.initPipeline || !ctx.downsamplePipeline) {
        return;
    }

    const auto& srvTable = m_state.descriptors.dispatchSrvTables[ctx.frameIndex % kFrameCount];
    const auto& uavTable = m_state.descriptors.dispatchUavTables[ctx.frameIndex % kFrameCount];
    HZBPass::AddFromDepth(
        graph,
        depthHandle,
        hzbHandle,
        {
            ctx.descriptorManager,
            ctx.compactRootSignature,
            ctx.fallbackRootSignature,
            ctx.initPipeline,
            ctx.downsamplePipeline,
            ctx.frameConstants,
            std::span<const DescriptorHandle>(srvTable.data(), srvTable.size()),
            std::span<const DescriptorHandle>(uavTable.data(), uavTable.size()),
            m_state.resources.width,
            m_state.resources.height,
            m_state.resources.mipCount,
        });
}

} // namespace Cortex::Graphics
