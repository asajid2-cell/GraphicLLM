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
    if (!m_rtRuntimeState.supported || !m_rtRuntimeState.enabled || !m_services.rayTracingContext) {
        return false;
    }
    // Consider RT "warming up" while there are outstanding BLAS jobs either
    // in the renderer's queue or pending inside the DXR context.
    if (m_assetRuntime.gpuJobs.pendingBLASJobs > 0) {
        return true;
    }
    return m_services.rayTracingContext->GetPendingBLASCount() > 0;
}

Renderer::VRAMBreakdown Renderer::GetEstimatedVRAMBreakdown() const {
    VRAMBreakdown breakdown{};

    auto addResource = [](uint64_t& bucket, ID3D12Resource* resource) {
        bucket += EstimateResourceBytes(resource);
    };

    addResource(breakdown.renderTargetBytes, m_depthResources.buffer.Get());
    addResource(breakdown.renderTargetBytes, m_hzbResources.texture.Get());
    addResource(breakdown.renderTargetBytes, m_shadowResources.map.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.hdrColor.Get());
    addResource(breakdown.renderTargetBytes, m_mainTargets.gbufferNormalRoughness.Get());
    addResource(breakdown.renderTargetBytes, m_temporalScreenState.velocityBuffer.Get());

    addResource(breakdown.postProcessBytes, m_rtShadowTargets.mask.Get());
    addResource(breakdown.postProcessBytes, m_rtShadowTargets.history.Get());
    addResource(breakdown.postProcessBytes, m_ssaoResources.texture.Get());
    addResource(breakdown.postProcessBytes, m_ssrResources.color.Get());
    addResource(breakdown.postProcessBytes, m_rtReflectionTargets.color.Get());
    addResource(breakdown.postProcessBytes, m_rtReflectionTargets.history.Get());
    addResource(breakdown.postProcessBytes, m_rtGITargets.color.Get());
    addResource(breakdown.postProcessBytes, m_rtGITargets.history.Get());
    addResource(breakdown.postProcessBytes, m_temporalScreenState.historyColor.Get());
    addResource(breakdown.postProcessBytes, m_temporalScreenState.taaIntermediate.Get());

    addResource(breakdown.debugBytes, m_debugLineState.vertexBuffer.Get());
    addResource(breakdown.voxelBytes, m_voxelState.gridBuffer.Get());

    for (uint32_t i = 0; i < m_bloomResources.activeLevels; ++i) {
        addResource(breakdown.postProcessBytes, m_bloomResources.texA[i].Get());
        addResource(breakdown.postProcessBytes, m_bloomResources.texB[i].Get());
    }

    const AssetRegistry::MemoryBreakdown assets = m_assetRuntime.registry.GetMemoryBreakdown();
    breakdown.textureBytes = assets.textureBytes;
    breakdown.environmentBytes = assets.environmentBytes;
    breakdown.geometryBytes = assets.geometryBytes;

    // Add acceleration-structure memory usage when DXR is active. This folds
    // BLAS/TLAS buffers into the on-screen VRAM estimate so heavy RT scenes
    // surface their additional footprint to the user.
    if (m_services.rayTracingContext && m_rtRuntimeState.supported) {
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
