#include "Renderer.h"
#include "Debug/GPUProfiler.h"

#include "Graphics/FrameContractResources.h"
#include "Graphics/FrameContractValidation.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/SurfaceClassification.h"
#include "Scene/Components.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

namespace Cortex::Graphics {

namespace {
uint32_t EstimateFormatBytesPerPixel(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 16;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
        return 8;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
        return 4;
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
        return 2;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
        return 1;
    default:
        return 4;
    }
}

uint64_t EstimateResourceBytes(ID3D12Resource* resource) {
    if (!resource) {
        return 0;
    }

    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        return static_cast<uint64_t>(desc.Width);
    }

    const uint64_t bytesPerPixel = EstimateFormatBytesPerPixel(desc.Format);
    uint64_t total = 0;
    uint64_t width = std::max<uint64_t>(1, desc.Width);
    uint64_t height = std::max<uint64_t>(1, desc.Height);
    const uint64_t depthOrArray = std::max<uint64_t>(1, desc.DepthOrArraySize);
    const uint32_t mipLevels = std::max<uint32_t>(1, desc.MipLevels);

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        total += width * height * depthOrArray * bytesPerPixel;
        width = std::max<uint64_t>(1, width / 2);
        height = std::max<uint64_t>(1, height / 2);
    }
    return total;
}

} // namespace

bool Renderer::IsRTWarmingUp() const {
    return m_rt.IsRTWarmingUp(m_services.rayTracingContext.get(), m_assetRuntime.gpuJobs.pendingBLASJobs);
}

Renderer::VRAMBreakdown Renderer::GetEstimatedVRAMBreakdown() const {
    VRAMBreakdown breakdown{};

    auto addResource = [](uint64_t& bucket, ID3D12Resource* resource) {
        bucket += EstimateResourceBytes(resource);
    };

    addResource(breakdown.renderTargetBytes, m_depthResources.resources.buffer.Get());
    addResource(breakdown.renderTargetBytes, m_hzb.State().resources.texture.Get());
    addResource(breakdown.renderTargetBytes, m_shadows.Resources().resources.map.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.hdr.resources.color.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.normalRoughness.resources.texture.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.radiance.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.confidence.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.sourceId.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.rejectedSourceMask.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.temporalDelta.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.ssrSourceSignal.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.rtSourceSignal.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.sourceSuppression.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.historyCurr.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.historyPrev.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.historyPrevSourceId.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.historyValidity.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.reflectionV3.resources.historyRejection.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.compositeV3.resources.hdrSceneColor.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.candidateBeautyV3.resources.ldrOutput.Get());
    addResource(breakdown.renderTargetBytes, m_temporal.ScreenState().velocityBuffer.Get());

    addResource(breakdown.postProcessBytes, m_rt.ShadowTargets().mask.Get());
    addResource(breakdown.postProcessBytes, m_rt.ShadowTargets().history.Get());
    addResource(breakdown.postProcessBytes, m_ssao.State().resources.texture.Get());
    addResource(breakdown.postProcessBytes, m_ssr.State().resources.color.Get());
    addResource(breakdown.postProcessBytes, m_rt.ReflectionTargets().color.Get());
    addResource(breakdown.postProcessBytes, m_rt.ReflectionTargets().history.Get());
    addResource(breakdown.postProcessBytes, m_rt.GITargets().color.Get());
    addResource(breakdown.postProcessBytes, m_rt.GITargets().history.Get());
    addResource(breakdown.postProcessBytes, m_temporal.ScreenState().historyColor.Get());
    addResource(breakdown.postProcessBytes, m_temporal.ScreenState().taaIntermediate.Get());

    addResource(breakdown.debugBytes, m_debugLines.State().vertexBuffer.Get());
    addResource(breakdown.voxelBytes, m_voxel.State().gridBuffer.Get());

    for (uint32_t i = 0; i < m_bloom.State().resources.activeLevels; ++i) {
        addResource(breakdown.postProcessBytes, m_bloom.State().resources.texA[i].Get());
        addResource(breakdown.postProcessBytes, m_bloom.State().resources.texB[i].Get());
    }

    const AssetRegistry::MemoryBreakdown assets = m_assetRuntime.registry.GetMemoryBreakdown();
    breakdown.textureBytes = assets.textureBytes;
    breakdown.environmentBytes = assets.environmentBytes;
    breakdown.geometryBytes = assets.geometryBytes;

    // Add acceleration-structure memory usage when DXR is active. This folds
    // BLAS/TLAS buffers into the on-screen VRAM estimate so heavy RT scenes
    // surface their additional footprint to the user.
    if (m_services.rayTracingContext && m_rt.RuntimeState().supported) {
        breakdown.rtStructureBytes = m_services.rayTracingContext->GetAccelerationStructureBytes();
        // Mirror RT structure usage into the asset registry so the memory
        // inspector can report it alongside textures/geometry.
        m_assetRuntime.registry.SetRTStructureBytes(breakdown.rtStructureBytes);
    } else {
        breakdown.rtStructureBytes = assets.rtStructureBytes;
    }

    return breakdown;
}

float Renderer::GetEstimatedVRAMMB() const {
    const VRAMBreakdown breakdown = GetEstimatedVRAMBreakdown();
    const double mb = static_cast<double>(breakdown.TotalBytes()) / (1024.0 * 1024.0);
    return static_cast<float>(mb);
}

Renderer::DescriptorStats Renderer::GetDescriptorStats() const {
    DescriptorStats stats{};
    if (m_services.descriptorManager) {
        stats.rtvUsed = m_services.descriptorManager->GetRTVUsedCount();
        stats.rtvCapacity = m_services.descriptorManager->GetRTVCapacity();
        stats.dsvUsed = m_services.descriptorManager->GetDSVUsedCount();
        stats.dsvCapacity = m_services.descriptorManager->GetDSVCapacity();
        stats.shaderVisibleUsed = m_services.descriptorManager->GetCBVSrvUavUsedCount();
        stats.shaderVisibleCapacity = m_services.descriptorManager->GetCBVSrvUavCapacity();
        stats.persistentUsed = m_services.descriptorManager->GetCBVSrvUavPersistentCount();
        stats.persistentReserve = m_services.descriptorManager->GetCBVSrvUavPersistentReserve();
        stats.transientStart = m_services.descriptorManager->GetCBVSrvUavTransientStart();
        stats.transientEnd = m_services.descriptorManager->GetCBVSrvUavTransientEnd();
        stats.stagingUsed = m_services.descriptorManager->GetStagingCBVSrvUavUsedCount();
        stats.stagingCapacity = m_services.descriptorManager->GetStagingCBVSrvUavCapacity();
    }
    if (m_services.bindlessManager) {
        stats.bindlessAllocated = m_services.bindlessManager->GetAllocatedCount();
        stats.bindlessCapacity = m_services.bindlessManager->GetCapacity();
    }
    return stats;
}

const Debug::GPUFrameProfile* Renderer::GetLastGPUProfile() const {
    return Debug::GPUProfiler::Get().GetLastResolvedFrame();
}

} // namespace Cortex::Graphics
